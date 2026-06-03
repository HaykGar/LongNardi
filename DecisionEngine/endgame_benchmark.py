"""Endgame benchmark: MCTS (2048 sims) vs 1-ply lookahead, both driven by the
same value network, with Black played by the random strategy.

Question: is MCTS meaningfully better than 1-ply lookahead *specifically in the
endgame* (where White already has all 15 checkers home and just needs to bear
off efficiently)?

Setup (reproducible from a master seed):
  * 50 "both-endgame" boards   : White all-home (row 1, cols 6-11), Black all-home
                                 (row 0, cols 6-11). Separate quadrants, and nardi
                                 has no hitting, so the two sides never interact ->
                                 a pure bear-off-efficiency test.
  * 50 "contested" boards      : White all-home, but Black still has checkers in
                                 White's home quadrant (row 1, cols 6-11), so Black
                                 occupies points White wants -> tests bearing off
                                 under blocking.
  For each of the 100 boards, 10 pre-determined dice trajectories of length 200
  (longer than any endgame can last) => 1000 unique (board, dice) pairings.

Each pairing is played out twice -- once with White=MCTS, once with White=1-ply --
on the SAME board and the SAME dice stream (White faces identical rolls on its
k-th turn in both runs; White=even turns, Black=odd). Black's random choices are
seeded per pairing (identical seed for both strategy runs).

Recorded per (pairing, strategy): did White win, White's turn count to bear off
(won games), and White checkers still on the board (lost games).

Both strategies use the loaded C++ target network: 1-ply via apply_lookahead_target,
MCTS via mcts_apply_move -- same value net, different search.

Example:
    venv/bin/python endgame_benchmark.py --weights weights/res2.pt --sims 2048 \
        --workers 8 --out endgame_bench_results.json
Pilot (fast):
    venv/bin/python endgame_benchmark.py --n-a 1 --n-b 1 --trajectories 1 \
        --sims 2048 --workers 1
"""

import argparse
import io
import json
import math
import multiprocessing as mp
import os
import time
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
import torch

from nardi_net import ResNardiNet

ROWS, COLS = 2, 12
HOME_COLS = list(range(6, 12))          # cols 6-11
# White home quadrant = row 1, cols 6-11; Black home quadrant = row 0, cols 6-11.
PIECES = 15
MASTER_SEED = 20260603


# --------------------------------------------------------------------------- #
# Board generation
# --------------------------------------------------------------------------- #

def _spread(rng, total, k):
    """Distribute `total` indistinguishable checkers into k bins (bins may be 0)."""
    if k <= 0 or total <= 0:
        return np.zeros(max(k, 0), dtype=int)
    return rng.multinomial(total, [1.0 / k] * k)


def gen_both_endgame(rng):
    """White all-home (row 1, cols 6-11), Black all-home (row 0, cols 6-11)."""
    b = np.zeros((ROWS, COLS), dtype=np.int8)
    for c, n in zip(HOME_COLS, _spread(rng, PIECES, 6)):
        b[1, c] = n            # white (+)
    for c, n in zip(HOME_COLS, _spread(rng, PIECES, 6)):
        b[0, c] = -n           # black (-) in its home
    return b


def gen_contested(rng):
    """White all-home; Black still has >=1 checker in White's home quadrant
    (row 1, cols 6-11), the rest in Black's home (row 0, cols 6-11)."""
    b = np.zeros((ROWS, COLS), dtype=np.int8)
    n_contest_cols = int(rng.integers(1, 3))                  # 1 or 2 cols reserved for Black
    contest_cols = list(rng.choice(HOME_COLS, size=n_contest_cols, replace=False))
    white_cols = [c for c in HOME_COLS if c not in contest_cols]
    for c, n in zip(white_cols, _spread(rng, PIECES, len(white_cols))):
        b[1, c] = n                                           # white (+) in row 1
    # Black: 1..min(8,14) checkers contesting row-1 home cols, rest in row-0 home.
    n_black_contest = int(rng.integers(1, min(9, PIECES)))    # >=1, leaves room for home pieces
    for c, n in zip(contest_cols, _spread(rng, n_black_contest, n_contest_cols)):
        b[1, c] = -n                                          # black (-) in WHITE's quadrant
    rem = PIECES - n_black_contest
    for c, n in zip(HOME_COLS, _spread(rng, rem, 6)):
        b[0, c] += -n                                         # black (-) in its home
    # A contesting col might have drawn 0; guarantee at least one black checker in
    # White's quadrant by force-placing one on the first contest col if needed.
    if not (b[1, contest_cols] < 0).any():
        b[1, contest_cols[0]] -= 1
        # remove one from black's home to keep the count at 15
        home_occupied = [c for c in HOME_COLS if b[0, c] < 0]
        if home_occupied:
            b[0, home_occupied[0]] += 1
    return b


def _assert_valid(b):
    white = int(b[b > 0].sum())
    black = int(-b[b < 0].sum())
    assert white == PIECES and black == PIECES, (white, black)
    # white only in its home quadrant (row 1, cols 6-11)
    assert (b[0, :] <= 0).all() and (b[1, :6] <= 0).all(), "white outside its quadrant"


def generate_boards(n_a, n_b, seed):
    rng = np.random.default_rng(seed)
    boards = []
    for _ in range(n_a):
        b = gen_both_endgame(rng)
        _assert_valid(b)
        boards.append(("both_endgame", b))
    for _ in range(n_b):
        b = gen_contested(rng)
        _assert_valid(b)
        boards.append(("contested", b))
    return boards


def generate_trajectory(rng, length):
    """A length-`length` sequence of dice rolls (each a uniform 1-6 pair)."""
    return rng.integers(1, 7, size=(length, 2), dtype=np.int8)


# --------------------------------------------------------------------------- #
# One game with a forced dice stream
# --------------------------------------------------------------------------- #

def play_game(eng, board, dice_stream, white_strategy, n_sims, c_uct, black_seed):
    """Play White(`white_strategy`) vs Black(random) from `board` with White to
    move, forcing the dice from `dice_stream`. Returns (white_won, white_turns,
    white_remaining)."""
    black_rng = np.random.default_rng(black_seed)
    eng.set_position(board, False)          # False => White (player 0) to move
    white_turns = 0

    for (d1, d2) in dice_stream:
        if eng.is_terminal():
            break
        options = eng.set_and_enumerate(int(d1), int(d2))   # FORCE these dice
        white = (eng.current_player() == 0)
        if white:
            white_turns += 1
        if len(options) == 0:
            eng.confirm_turn()                              # forced pass
            continue
        if white:
            if white_strategy == "mcts":
                # eval mode: most-visited move, no root noise.
                eng.mcts_apply_move(n_sims, 1.0, False, c_uct, 0.0, 0.3, 0)
            else:                                           # "lookahead" (1-ply)
                eng.apply_lookahead_target()
        else:                                               # Black: seeded random
            idx = int(black_rng.integers(len(options)))
            eng.apply_board(options[idx].raw_data)
        # whole-board applies auto-confirm; this is a no-op safety net.
        if not eng.is_terminal():
            eng.confirm_turn()

    board_now = np.asarray(eng.board_features().raw_data, dtype=np.int8)
    white_remaining = int(board_now[board_now > 0].sum())
    # On game over the loser is to move, so winner == !current_player; White = 0.
    white_won = bool(eng.is_terminal() and eng.current_player() == 1)
    return white_won, white_turns, white_remaining


# --------------------------------------------------------------------------- #
# Worker
# --------------------------------------------------------------------------- #

def _run_chunk(pairings, state_bytes, n_sims, c_uct):
    """Worker: for each pairing run both strategies; return per-pairing metrics."""
    torch.set_num_threads(1)
    os.environ.setdefault("OMP_NUM_THREADS", "1")
    import tempfile
    import nardi
    from nardi_net import export_target_network

    model = ResNardiNet()
    model.load_state_dict(torch.load(io.BytesIO(state_bytes), map_location="cpu"))
    model.eval()

    eng = nardi.Engine()
    blob = os.path.join(tempfile.gettempdir(), f"endgame_target_{os.getpid()}.pt")
    export_target_network(model, blob)
    eng.load_target_network(blob)           # both strategies use this C++ target net

    out = []
    with torch.inference_mode():
        for (scenario, board, board_idx, traj_idx, dice_stream, black_seed) in pairings:
            rec = {"scenario": scenario, "board_idx": board_idx, "traj_idx": traj_idx}
            for strat in ("lookahead", "mcts"):
                won, turns, remaining = play_game(
                    eng, board, dice_stream, strat, n_sims, c_uct, black_seed)
                rec[strat] = {"won": won, "turns": turns, "remaining": remaining}
            out.append(rec)
    return out


# --------------------------------------------------------------------------- #
# Aggregation
# --------------------------------------------------------------------------- #

def summarize(records):
    """Aggregate per scenario (and overall) per strategy."""
    scenarios = sorted({r["scenario"] for r in records})
    summary = {}
    for scope in scenarios + ["overall"]:
        rs = records if scope == "overall" else [r for r in records if r["scenario"] == scope]
        summary[scope] = {"n": len(rs)}
        for strat in ("lookahead", "mcts"):
            wins = [r[strat] for r in rs if r[strat]["won"]]
            losses = [r[strat] for r in rs if not r[strat]["won"]]
            win_turns = [w["turns"] for w in wins]
            loss_remaining = [l["remaining"] for l in losses]
            summary[scope][strat] = {
                "win_rate": len(wins) / len(rs) if rs else float("nan"),
                "n_wins": len(wins),
                "avg_turns_to_win": float(np.mean(win_turns)) if win_turns else None,
                "std_turns_to_win": float(np.std(win_turns)) if win_turns else None,
                "n_losses": len(losses),
                "avg_pieces_remaining_on_loss": float(np.mean(loss_remaining)) if loss_remaining else None,
            }
    return summary


def print_summary(summary, n_sims):
    for scope, s in summary.items():
        print(f"\n=== {scope}  (n={s['n']} pairings) ===")
        for strat in ("lookahead", "mcts"):
            d = s[strat]
            tag = f"MCTS-{n_sims}" if strat == "mcts" else "1-ply"
            turns = f"{d['avg_turns_to_win']:.2f}" if d["avg_turns_to_win"] is not None else "n/a"
            rem = f"{d['avg_pieces_remaining_on_loss']:.2f}" if d["avg_pieces_remaining_on_loss"] is not None else "n/a"
            print(f"  {tag:>10}: win {100*d['win_rate']:5.1f}%  "
                  f"avg White turns to win {turns:>6}  "
                  f"(losses={d['n_losses']}, avg White pieces left on loss {rem})")


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def build_pairings(n_a, n_b, trajectories, traj_len, seed):
    boards = generate_boards(n_a, n_b, seed)
    traj_rng = np.random.default_rng(seed + 1)
    pairings = []
    for board_idx, (scenario, board) in enumerate(boards):
        for traj_idx in range(trajectories):
            stream = generate_trajectory(traj_rng, traj_len)
            black_seed = int(seed + 7919 * board_idx + 31 * traj_idx + 1)
            pairings.append((scenario, board, board_idx, traj_idx, stream, black_seed))
    return pairings


def main():
    ap = argparse.ArgumentParser(description="Endgame MCTS vs 1-ply lookahead benchmark.")
    ap.add_argument("--weights", default="weights/res2.pt")
    ap.add_argument("--n-a", type=int, default=50, help="both-endgame boards")
    ap.add_argument("--n-b", type=int, default=50, help="contested boards")
    ap.add_argument("--trajectories", type=int, default=10, help="dice trajectories per board")
    ap.add_argument("--traj-len", type=int, default=200)
    ap.add_argument("--sims", type=int, default=2048, help="MCTS simulations per move")
    ap.add_argument("--c-uct", type=float, default=0.1)
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--seed", type=int, default=MASTER_SEED)
    ap.add_argument("--out", default=None, help="optional JSON path to save results")
    args = ap.parse_args()

    model = ResNardiNet()
    model.load_state_dict(torch.load(args.weights, map_location="cpu", weights_only=True))
    model.eval()
    buf = io.BytesIO()
    torch.save(model.state_dict(), buf)
    state_bytes = buf.getvalue()

    pairings = build_pairings(args.n_a, args.n_b, args.trajectories, args.traj_len, args.seed)
    print(f"Boards: {args.n_a} both-endgame + {args.n_b} contested; "
          f"{args.trajectories} trajectories x {args.traj_len} rolls => {len(pairings)} pairings.")
    print(f"MCTS sims/move={args.sims}, c_uct={args.c_uct}, workers={args.workers}, "
          f"model={args.weights}")

    workers = max(1, args.workers)
    t0 = time.time()
    records = []
    if workers == 1:
        records = _run_chunk(pairings, state_bytes, args.sims, args.c_uct)
    else:
        chunks = [pairings[i::workers] for i in range(workers)]
        chunks = [c for c in chunks if c]
        ctx = mp.get_context("spawn")
        with ProcessPoolExecutor(max_workers=workers, mp_context=ctx) as ex:
            futs = [ex.submit(_run_chunk, c, state_bytes, args.sims, args.c_uct) for c in chunks]
            for fut in as_completed(futs):
                records.extend(fut.result())
    elapsed = time.time() - t0

    summary = summarize(records)
    print_summary(summary, args.sims)
    print(f"\nTotal wall time: {elapsed:.1f}s for {len(records)} pairings "
          f"({2*len(records)} games).")

    if args.out:
        with open(args.out, "w") as fh:
            json.dump({"config": vars(args), "elapsed_sec": elapsed,
                       "summary": summary, "records": records}, fh, indent=2)
        print(f"Saved results to {args.out}")


if __name__ == "__main__":
    main()

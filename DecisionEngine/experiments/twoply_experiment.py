"""Two-ply vs one-ply lookahead: does the extra ply change/strengthen play, and
what top-K is feasible on an iPhone 15+?

Three experiments, all on one shared value network:
  (1) move-change : over realistic decision positions, how often does the move
                    two-ply picks differ from the one-ply move (per top-K)?
  (2) strength    : two-ply(K) vs one-ply head-to-head win rate.
  (3) cost        : model-evals per move (per K) + the hand-rolled C++ net's
                    throughput -> projected per-move time on an iPhone 15+, and
                    the largest K under a per-move time budget.

Run:  venv/bin/python twoply_experiment.py --weights weights/res2.pt
"""

import argparse
import io
import math
import multiprocessing as mp
import os
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
import torch

from nardi_net import ResNardiNet, export_target_network, export_weights

COLS = 12
SEED = 20260603


def board_of(eng):
    return np.asarray(eng.board_features().raw_data, dtype=np.int8).copy()


# --------------------------------------------------------------------------- #
# Generate realistic decision positions via 1-ply self-play
# --------------------------------------------------------------------------- #

def generate_positions(blob, n_games, cap, seed):
    import nardi
    eng = nardi.Engine()
    eng.load_target_network(blob)
    rng = np.random.default_rng(seed)
    positions = []   # (board, side, d1, d2, turn)
    for g in range(n_games):
        eng.reset()
        turn = 0
        while eng.should_continue_game() and turn < cap:
            d1, d2 = int(rng.integers(1, 7)), int(rng.integers(1, 7))
            opts = eng.set_and_enumerate(d1, d2)
            if len(opts) >= 2:                       # only real decisions
                positions.append((board_of(eng), eng.current_player(), d1, d2, turn))
            if len(opts) == 0:
                eng.confirm_turn()
            else:
                eng.apply_lookahead_target()         # progress with a decent player
            turn += 1
    return positions


# --------------------------------------------------------------------------- #
# (1) move-change rate  +  (3) eval counts / wall time, per top-K (multiprocessed)
# --------------------------------------------------------------------------- #

def _phase(turn):
    return "open" if turn < 6 else ("mid" if turn < 20 else "end")


def _mc_chunk(blob, positions, ks):
    import nardi
    torch.set_num_threads(1)
    eng = nardi.Engine()
    eng.load_target_network(blob)
    changed = {k: 0 for k in ks}
    evals2 = {k: [] for k in ks}
    wall2 = {k: [] for k in ks}
    evals1 = []
    wall1 = []
    phase_changed = {k: {"open": 0, "mid": 0, "end": 0} for k in ks}
    phase_n = {"open": 0, "mid": 0, "end": 0}

    for (board, side, d1, d2, turn) in positions:
        ph = _phase(turn); phase_n[ph] += 1
        # 1-ply move + timing
        eng.set_position(board, bool(side)); eng.set_and_enumerate(d1, d2)
        t = time.perf_counter(); eng.apply_lookahead_target(); wall1.append(time.perf_counter() - t)
        b1 = board_of(eng)
        eng.set_position(board, bool(side)); eng.set_and_enumerate(d1, d2)
        evals1.append(eng.make_lookahead_batch().num_eval_features)
        for k in ks:
            eng.set_position(board, bool(side)); eng.set_and_enumerate(d1, d2)
            t = time.perf_counter(); eng.apply_lookahead2_target(k); wall2[k].append(time.perf_counter() - t)
            if not np.array_equal(b1, board_of(eng)):
                changed[k] += 1; phase_changed[k][ph] += 1
            evals2[k].append(eng.last_lookahead2_evals())
    return {"n": len(positions), "changed": changed, "evals2": evals2, "wall2": wall2,
            "evals1": evals1, "wall1": wall1, "phase_changed": phase_changed, "phase_n": phase_n}


def move_change_and_cost(blob, positions, ks, workers):
    workers = max(1, min(workers, len(positions)))
    chunks = [positions[i::workers] for i in range(workers)]
    chunks = [c for c in chunks if c]
    results = []
    if workers == 1:
        results = [_mc_chunk(blob, positions, ks)]
    else:
        ctx = mp.get_context("spawn")
        with ProcessPoolExecutor(max_workers=workers, mp_context=ctx) as ex:
            futs = [ex.submit(_mc_chunk, blob, c, ks) for c in chunks]
            results = [f.result() for f in as_completed(futs)]

    n = sum(r["n"] for r in results)
    changed = {k: sum(r["changed"][k] for r in results) for k in ks}
    evals2 = {k: [x for r in results for x in r["evals2"][k]] for k in ks}
    wall2 = {k: [x for r in results for x in r["wall2"][k]] for k in ks}
    evals1 = [x for r in results for x in r["evals1"]]
    wall1 = [x for r in results for x in r["wall1"]]
    phase_n = {p: sum(r["phase_n"][p] for r in results) for p in ("open", "mid", "end")}
    phase_changed = {k: {p: sum(r["phase_changed"][k][p] for r in results)
                         for p in ("open", "mid", "end")} for k in ks}
    return {
        "n": n,
        "rate": {k: changed[k] / n if n else float("nan") for k in ks},
        "avg_evals2": {k: float(np.mean(evals2[k])) if evals2[k] else 0 for k in ks},
        "avg_wall2_ms": {k: 1000 * float(np.mean(wall2[k])) if wall2[k] else 0 for k in ks},
        "avg_evals1": float(np.mean(evals1)) if evals1 else 0,
        "avg_wall1_ms": 1000 * float(np.mean(wall1)) if wall1 else 0,
        "phase_n": phase_n,
        "phase_rate": {k: {p: (phase_changed[k][p] / phase_n[p] if phase_n[p] else float("nan"))
                           for p in ("open", "mid", "end")} for k in ks},
    }




# --------------------------------------------------------------------------- #
# (2) strength: two-ply(K) vs one-ply head-to-head
# --------------------------------------------------------------------------- #

def _play_game(eng, white_two_ply, k, rng):
    """White vs Black; one side is two-ply(k), the other one-ply. Returns +points
    for the two-ply side (>0 win), negative if it lost."""
    eng.reset()
    turn = 0
    while eng.should_continue_game() and turn < 1000:
        d1, d2 = int(rng.integers(1, 7)), int(rng.integers(1, 7))
        opts = eng.set_and_enumerate(d1, d2)
        if len(opts) == 0:
            eng.confirm_turn(); turn += 1; continue
        white = (eng.current_player() == 0)
        two_ply_to_move = (white == white_two_ply)
        if two_ply_to_move:
            eng.apply_lookahead2_target(k)
        else:
            eng.apply_lookahead_target()
        turn += 1
    if not eng.is_terminal():
        return 0
    white_won = (eng.current_player() == 1)
    pts = eng.winner_result()
    two_ply_won = (white_won == white_two_ply)
    return pts if two_ply_won else -pts


def _strength_chunk(blob, k, n_games, seed):
    import nardi
    torch.set_num_threads(1)
    eng = nardi.Engine()
    eng.load_target_network(blob)
    rng = np.random.default_rng(seed)
    wins = 0; pts = 0; played = 0
    for g in range(n_games):
        r = _play_game(eng, white_two_ply=bool(g % 2), k=k, rng=rng)  # alternate colors
        if r == 0:
            continue
        played += 1
        pts += r
        if r > 0:
            wins += 1
    return wins, played, pts


def strength(blob_path, k, n_games, workers, seed):
    workers = max(1, workers)
    per = math.ceil(n_games / workers)
    ctx = mp.get_context("spawn")
    wins = played = pts = 0
    if workers == 1:
        wins, played, pts = _strength_chunk(blob_path, k, per, seed)
    else:
        with ProcessPoolExecutor(max_workers=workers, mp_context=ctx) as ex:
            futs = [ex.submit(_strength_chunk, blob_path, k, per, seed + 100 * i)
                    for i in range(workers)]
            for fut in as_completed(futs):
                w, p, pt = fut.result(); wins += w; played += p; pts += pt
    return {"k": k, "games": played, "two_ply_wins": wins,
            "two_ply_win_rate": wins / played if played else float("nan"),
            "two_ply_point_diff": pts}


# --------------------------------------------------------------------------- #
# (3) hand-rolled net throughput (for the iPhone projection)
# --------------------------------------------------------------------------- #

def handrolled_evals_per_sec(model, n=20000):
    import nardi
    net = nardi.InferenceNet(export_weights(model, tempfile.mktemp(suffix=".nardiw")))
    eng = nardi.Engine(); eng.reset()
    feats = [eng.board_features() for _ in range(min(n, 2000))]
    feats = (feats * (n // len(feats) + 1))[:n]
    net.evaluate_batch(feats[:64])     # warm up
    t0 = time.time()
    net.evaluate_batch(feats)
    dt = time.time() - t0
    return n / dt if dt > 0 else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights", default="weights/res2.pt")
    ap.add_argument("--ks", type=int, nargs="+", default=[1, 2, 3, 4, 5])
    ap.add_argument("--positions-games", type=int, default=30)
    ap.add_argument("--cap", type=int, default=120)
    ap.add_argument("--max-positions", type=int, default=250)
    ap.add_argument("--strength-games", type=int, default=240)
    ap.add_argument("--strength-ks", type=int, nargs="+", default=[3])
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--iphone-factor", type=float, default=0.6,
                    help="assumed iPhone-15+ single-core throughput vs this Mac core")
    args = ap.parse_args()

    model = ResNardiNet()
    model.load_state_dict(torch.load(args.weights, map_location="cpu", weights_only=True))
    model.eval()
    blob = export_target_network(model, tempfile.mktemp(suffix=".pt"))   # C++ target (torch)

    print(f"model={args.weights}  Ks={args.ks}")
    print("Generating positions via 1-ply self-play...")
    positions = generate_positions(blob, args.positions_games, args.cap, SEED)
    if len(positions) > args.max_positions:
        rng = np.random.default_rng(SEED)
        positions = [positions[i] for i in rng.choice(len(positions), args.max_positions, replace=False)]
    print(f"  {len(positions)} decision positions (phase: open<6, mid<20, end>=20 turns)")

    print("\n(1) does K change the move, and (2) at what cost?")
    mc = move_change_and_cost(blob, positions, args.ks, args.workers)
    eps = handrolled_evals_per_sec(model)
    iphone_eps = eps * args.iphone_factor
    print(f"  baseline 1-ply: avg {mc['avg_evals1']:.0f} model-evals/move, {mc['avg_wall1_ms']:.1f} ms/move (this Mac, torch)")
    print(f"  hand-rolled net throughput: {eps:,.0f} evals/s (Mac) -> ~{iphone_eps:,.0f}/s assumed iPhone15+")
    print(f"  {'K':>3} {'move-changed':>12} {'evals/move':>11} {'~iPhone ms*':>11} {'Mac ms':>8}   {'changed by phase o/m/e':>22}")
    for k in args.ks:
        ev = mc["avg_evals2"][k]
        ims = 1000.0 * ev / iphone_eps if iphone_eps > 0 else float("nan")
        pr = mc["phase_rate"][k]
        print(f"  {k:>3} {100*mc['rate'][k]:>11.1f}% {ev:>11.0f} {ims:>10.1f} {mc['avg_wall2_ms'][k]:>8.1f}   "
              f"{100*pr['open']:>4.0f}/{100*pr['mid']:>3.0f}/{100*pr['end']:>3.0f}")
    print("  *iPhone ms = model-eval time only (eval-count / throughput); excludes move-enumeration overhead.")

    print(f"\n(3) is it actually stronger? two-ply(K) vs one-ply, {args.strength_games} games:")
    for k in args.strength_ks:
        s = strength(blob, k, args.strength_games, args.workers, SEED + k)
        se = (s['two_ply_win_rate'] * (1 - s['two_ply_win_rate']) / s['games']) ** 0.5 if s['games'] else 0
        print(f"  K={k}: two-ply win rate {100*s['two_ply_win_rate']:5.1f}% +/- {100*se:.1f}%  "
              f"({s['two_ply_wins']}/{s['games']}, point diff {s['two_ply_point_diff']:+d})")

    print(f"\n(2) strength: two-ply(K) vs one-ply, {args.strength_games} games each:")
    for k in args.strength_ks:
        s = strength(blob, k, args.strength_games, args.workers, SEED + k)
        klabel = "full" if k == 0 else str(k)
        print(f"  K={klabel:>4}: two-ply win rate {100*s['two_ply_win_rate']:5.1f}%  "
              f"({s['two_ply_wins']}/{s['games']}, point diff {s['two_ply_point_diff']:+d})")


if __name__ == "__main__":
    main()

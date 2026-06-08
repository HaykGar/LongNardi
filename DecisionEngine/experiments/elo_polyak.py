"""Round-robin greedy ELO for the Polyak-averaged Lookahead models vs chkpt45.

Reuses get_elos.run_parallel_round_robin_elos / EloTracker (the existing harness,
which calls Simulator.benchmark under the hood) but forces greedy play. Also fits
an order-independent Bradley-Terry MLE rating for a more reliable ranking, since
the contenders are close.

Usage:
    python elo_polyak.py [num_games_per_orientation] [output.md]
"""

import os
import sys
import math
import datetime

import torch

from nardi_net import ResNardiNet
from get_elos import run_parallel_round_robin_elos

WEIGHTS = os.path.join(os.path.dirname(__file__), "weights")

# name -> weight file (relative to weights/)
SPECS = {
    "avg10_ema9":  "pol_avg_lookahead10_ema9.pt",
    "avg20_ema9":  "pol_avg_lookahead20_ema9.pt",
    "avg10_ema99": "pol_avg_lookahead10_ema99.pt",
    "avg20_ema99": "pol_avg_lookahead20_ema99.pt",
    "chkpt45":     "Checkpoints/Lookahead/chkpt45lookahead_res2.pt",
}


def load_model(rel_path):
    m = ResNardiNet()
    sd = torch.load(os.path.join(WEIGHTS, rel_path), map_location="cpu", weights_only=True)
    m.load_state_dict(sd)
    m.eval()
    return m


def bradley_terry_elo(totals, anchor_mean=1500.0, iters=10000, tol=1e-9):
    """Order-independent MLE rating from pairwise point totals.

    totals: dict (a, b) -> [points_a, points_b]. Treats points as BT win weights.
    Returns dict name -> elo, normalized so the mean rating == anchor_mean.
    """
    names = sorted({n for pair in totals for n in pair})
    idx = {n: i for i, n in enumerate(names)}
    W = [0.0] * len(names)                     # total points won by each player
    N = [[0.0] * len(names) for _ in names]    # games (points) between i and j
    for (a, b), (wa, wb) in totals.items():
        ia, ib = idx[a], idx[b]
        W[ia] += wa
        W[ib] += wb
        N[ia][ib] += wa + wb
        N[ib][ia] += wa + wb

    s = [1.0] * len(names)                      # BT strengths
    for _ in range(iters):
        new = s[:]
        for i in range(len(names)):
            denom = sum(N[i][j] / (s[i] + s[j]) for j in range(len(names)) if j != i)
            if denom > 0 and W[i] > 0:
                new[i] = W[i] / denom
        # normalize (geometric mean = 1) to keep it from drifting
        gm = math.exp(sum(math.log(max(v, 1e-12)) for v in new) / len(new))
        new = [v / gm for v in new]
        if max(abs(new[i] - s[i]) for i in range(len(names))) < tol:
            s = new
            break
        s = new

    elos = {names[i]: 400.0 * math.log10(s[i]) for i in range(len(names))}
    shift = anchor_mean - sum(elos.values()) / len(elos)
    return {n: r + shift for n, r in elos.items()}


def main():
    num_games = int(sys.argv[1]) if len(sys.argv) > 1 else 2500
    out_path = sys.argv[2] if len(sys.argv) > 2 else "polyak_elo_results.md"

    players = {name: load_model(p) for name, p in SPECS.items()}
    ctor_keys = {name: "res_v2" for name in players}  # all ResNardiNet

    elo, totals = run_parallel_round_robin_elos(
        players,
        ctor_keys,
        num_games=num_games,
        chunk_size=64,
        strat="greedy",
        max_workers=None,
        elo_k=64,
        score_power=1.5,
        confidence_scale=True,
    )

    bt = bradley_terry_elo(totals)

    # per-player aggregate points + win share (point-based, gammon=2)
    pts = {n: 0.0 for n in players}
    opp_pts = {n: 0.0 for n in players}
    for (a, b), (wa, wb) in totals.items():
        pts[a] += wa; opp_pts[a] += wb
        pts[b] += wb; opp_pts[b] += wa
    win_share = {n: pts[n] / (pts[n] + opp_pts[n]) if (pts[n] + opp_pts[n]) else 0.0
                 for n in players}

    names_by_bt = sorted(players, key=lambda n: -bt[n])

    lines = []
    w = lines.append
    w(f"# Polyak-averaged Lookahead models — greedy round-robin ELO\n")
    w(f"_Generated {datetime.datetime.now():%Y-%m-%d %H:%M}_\n")
    w("## Setup\n")
    w(f"- Play: **greedy** (1-ply, no lookahead), CPU.")
    w(f"- Games per matchup: **{2*num_games}** ({num_games} with each side moving first).")
    w(f"- Round robin over {len(players)} models, {len(totals)} matchups.")
    w(f"- Scores are **points** (gammon/mars count 2), as Simulator.benchmark reports them.\n")
    w("## Models\n")
    for n, p in SPECS.items():
        w(f"- `{n}` ← `{p}`")
    w("\n  Averaging recipe (training order = chkpt0..chkpt49):")
    w("  - `avg10_*` = forward EMA over chkpt40..49; `avg20_*` = over chkpt30..49.")
    w("  - `ema9` = beta 0.9, `ema99` = beta 0.99. Forward EMA weights the *oldest*")
    w("    checkpoint in the window most (e.g. beta=0.99 over 10 ⇒ chkpt40 ≈ 0.91,")
    w("    chkpt49 ≈ 0.01).\n")

    w("## Ranking (Bradley-Terry MLE, order-independent, mean=1500)\n")
    w("| rank | model | BT-Elo | EloTracker | win share | total pts |")
    w("|---|---|---:|---:|---:|---:|")
    for i, n in enumerate(names_by_bt, 1):
        w(f"| {i} | {n} | {bt[n]:.0f} | {elo.rating[n]:.0f} | "
          f"{100*win_share[n]:.1f}% | {pts[n]:.0f} |")
    w("")

    w("## Head-to-head matrix (row's point win share vs column)\n")
    order = names_by_bt
    w("| | " + " | ".join(order) + " |")
    w("|" + "---|" * (len(order) + 1))
    share = {}
    for (a, b), (wa, wb) in totals.items():
        tot = wa + wb
        share[(a, b)] = wa / tot if tot else 0.0
        share[(b, a)] = wb / tot if tot else 0.0
    for r in order:
        cells = []
        for c in order:
            if r == c:
                cells.append("—")
            else:
                cells.append(f"{100*share[(r, c)]:.1f}%")
        w(f"| **{r}** | " + " | ".join(cells) + " |")
    w("")

    w("## Raw matchup point totals\n")
    w("| A | B | pts A | pts B | A win share |")
    w("|---|---|---:|---:|---:|")
    for (a, b), (wa, wb) in sorted(totals.items()):
        tot = wa + wb
        w(f"| {a} | {b} | {wa} | {wb} | {100*wa/tot:.1f}% |")
    w("")

    report = "\n".join(lines)
    with open(out_path, "w") as f:
        f.write(report)
    print(report)
    print(f"\n[written to {out_path}]")


if __name__ == "__main__":
    main()

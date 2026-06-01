"""Multi-processed head-to-head benchmark of move strategies for one shared model.

Default: MCTS (eval mode, model-informed UCT -> most-visited move) vs 1-ply
lookahead, both driven by the same value network (e.g. weights/res2.pt). This
isolates the *search method*: same value net, different move selection.

Example:
    venv/bin/python mcts_benchmark.py --weights weights/res2.pt \
        --games 400 --sims 100 --workers 8
"""

import argparse
import io
import math
import multiprocessing as mp
import os
from concurrent.futures import ProcessPoolExecutor, as_completed

import torch

from nardi_net import NardiNet, ConvNardiNet, ResNardiNet


def build_model(architecture):
    if architecture == "Conv":
        return ConvNardiNet()
    if architecture == "DeepConv":
        return ConvNardiNet(extra_conv=True)
    if architecture == "ResNet":
        return ResNardiNet()
    if architecture == "MLP":
        return NardiNet(64, 16)
    raise ValueError(f"Unknown architecture: {architecture}")


def _play_chunk(n_games, architecture, state_bytes, n_sims, strat_a, strat_b, c_uct):
    """Worker: play n_games of strat_a vs strat_b (same model), alternating colors.

    Returns (score_a, score_b) in points.
    """
    torch.set_num_threads(1)
    os.environ.setdefault("OMP_NUM_THREADS", "1")

    # Imported here so each spawned worker initializes its own engine/module.
    from sim_play import Simulator

    model = build_model(architecture)
    model.load_state_dict(torch.load(io.BytesIO(state_bytes), map_location="cpu"))
    model.eval()

    sim = Simulator()
    sim.mcts_n_sims = n_sims
    sim.mcts_mode = "eval"            # competitive play: most-visited move, no noise
    sim.mcts_c_uct = c_uct
    sim.mcts_rollouts_per_leaf = 0
    sim.mcts_dirichlet_eps = 0.0      # irrelevant in eval mode, set 0 for clarity

    move_a = sim.strat_to_func(model, strat_a)
    move_b = sim.strat_to_func(model, strat_b)

    score_a = score_b = 0
    with torch.inference_mode():
        for g in range(n_games):
            # alternate which strategy plays white to remove first-move bias
            r = sim.simulate_game(move_a, move_b, swap_order=bool(g % 2))
            score_a += r[0]
            score_b += r[1]
    return score_a, score_b


def benchmark(weights, architecture="ResNet", strat_a="mcts", strat_b="lookahead",
              num_games=400, n_sims=100, workers=8, c_uct=0.1):
    model = build_model(architecture)
    if weights:
        model.load_state_dict(torch.load(weights, map_location="cpu", weights_only=True))
    model.eval()

    buf = io.BytesIO()
    torch.save(model.state_dict(), buf)
    state_bytes = buf.getvalue()

    workers = max(1, workers)
    per = math.ceil(num_games / workers)
    chunks = [per] * workers

    ctx = mp.get_context("spawn")
    total_a = total_b = 0
    if workers == 1:
        total_a, total_b = _play_chunk(chunks[0], architecture, state_bytes, n_sims,
                                       strat_a, strat_b, c_uct)
    else:
        with ProcessPoolExecutor(max_workers=workers, mp_context=ctx) as ex:
            futs = [ex.submit(_play_chunk, c, architecture, state_bytes, n_sims,
                              strat_a, strat_b, c_uct) for c in chunks]
            for fut in as_completed(futs):
                a, b = fut.result()
                total_a += a
                total_b += b

    total_points = total_a + total_b
    wr_a = total_a / total_points if total_points else float("nan")
    print(f"\n{strat_a} vs {strat_b}  (same {architecture} model: {weights})")
    print(f"  sims/move={n_sims}, games~={sum(chunks)}, workers={workers}")
    print(f"  points: {strat_a}={total_a}  {strat_b}={total_b}")
    print(f"  {strat_a} win rate (by points): {wr_a*100:.2f}%")
    return total_a, total_b


def main():
    ap = argparse.ArgumentParser(description="Head-to-head strategy benchmark (multiprocessed).")
    ap.add_argument("--weights", default="weights/res2.pt")
    ap.add_argument("--architecture", "--arch", default="ResNet",
                    choices=["Conv", "DeepConv", "ResNet", "MLP"])
    ap.add_argument("--strat-a", default="mcts", choices=["mcts", "lookahead", "greedy"])
    ap.add_argument("--strat-b", default="lookahead", choices=["mcts", "lookahead", "greedy"])
    ap.add_argument("--games", type=int, default=400, help="games per worker chunk * workers (approx total).")
    ap.add_argument("--sims", type=int, default=100, help="MCTS simulations per move.")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--c-uct", type=float, default=0.1)
    args = ap.parse_args()

    benchmark(args.weights, args.architecture, args.strat_a, args.strat_b,
              args.games, args.sims, args.workers, args.c_uct)


if __name__ == "__main__":
    main()

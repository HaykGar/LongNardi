import argparse
import io
import math
import multiprocessing as mp
import os
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader, Dataset
from tqdm import tqdm

import nardi
from nardi_net import ConvNardiNet, ResNardiNet, export_target_network


class MCTSDataset(Dataset):
    def __init__(self, features: np.ndarray, targets: np.ndarray):
        self.features = torch.from_numpy(features).float()
        self.targets = torch.from_numpy(targets).float()

    def __len__(self):
        return int(self.targets.shape[0])

    def __getitem__(self, idx):
        return self.features[idx], self.targets[idx]


def temperature_at(iteration, n_iterations, tau0=1.0, tau_min=0.1, half_life=None):
    if half_life is None:
        half_life = max(1.0, n_iterations / 3.0)
    return tau_min + (tau0 - tau_min) * 2.0 ** (-iteration / half_life)


def collect_games_worker(n_games, n_sims, temperature, target_path, max_turns):
    eng = nardi.Engine()
    eng.load_target_network(target_path)

    feature_chunks = []
    target_chunks = []
    game_lengths = []

    for _ in range(n_games):
        pairs = eng.run_mcts_game(n_sims, temperature, max_turns)
        game_lengths.append(len(pairs))
        if not pairs:
            continue

        features = [p[0] for p in pairs]
        targets = np.asarray([p[1] for p in pairs], dtype=np.float32)
        feature_chunks.append(nardi.feature_batch_to_tensor(features, "conv", False))
        target_chunks.append(targets)

    if feature_chunks:
        x = np.concatenate(feature_chunks, axis=0).astype(np.float32, copy=False)
        y = np.concatenate(target_chunks, axis=0).astype(np.float32, copy=False)
    else:
        x = np.empty((0, 6, 25), dtype=np.float32)
        y = np.empty((0,), dtype=np.float32)

    return x, y, game_lengths


def _split_games(total_games, n_workers):
    base = total_games // n_workers
    rem = total_games % n_workers
    return [base + (1 if i < rem else 0) for i in range(n_workers) if base + (1 if i < rem else 0) > 0]


def collect_dataset(total_games, n_sims, temperature, target_path, n_workers, max_turns):
    counts = _split_games(total_games, n_workers)
    if not counts:
        raise ValueError("total_games must be positive.")

    if len(counts) == 1:
        x, y, lengths = collect_games_worker(counts[0], n_sims, temperature, target_path, max_turns)
        return x, y, lengths

    feature_chunks = []
    target_chunks = []
    all_lengths = []

    with ProcessPoolExecutor(max_workers=len(counts)) as executor:
        futures = [
            executor.submit(collect_games_worker, count, n_sims, temperature, target_path, max_turns)
            for count in counts
        ]
        for future in tqdm(as_completed(futures), total=len(futures), desc="collect", leave=False):
            x, y, lengths = future.result()
            if len(y) > 0:
                feature_chunks.append(x)
                target_chunks.append(y)
            all_lengths.extend(lengths)

    if not feature_chunks:
        return np.empty((0, 6, 25), dtype=np.float32), np.empty((0,), dtype=np.float32), all_lengths

    return (
        np.concatenate(feature_chunks, axis=0).astype(np.float32, copy=False),
        np.concatenate(target_chunks, axis=0).astype(np.float32, copy=False),
        all_lengths,
    )


def require_conv_model(model):
    pipeline = getattr(model, "pipeline", None)
    if pipeline is None or pipeline.kind != "conv" or pipeline.flatten:
        raise TypeError("MCTS training currently requires a conv-pipeline model such as ConvNardiNet or ResNardiNet.")


def train_on_dataset(model, features, targets, epochs, batch_size, lr, device):
    dataset = MCTSDataset(features, targets)
    loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)
    optimizer = torch.optim.Adam(model.parameters(), lr=lr)

    model.train()
    losses = []
    for _ in range(epochs):
        total_loss = 0.0
        total_count = 0
        for x, y in loader:
            x = x.to(device)
            y = y.to(device)
            pred = model.value_from_tensor(x).view(-1)
            loss = F.mse_loss(pred, y)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()

            total_loss += float(loss.item()) * int(y.numel())
            total_count += int(y.numel())

        losses.append(total_loss / max(1, total_count))

    return losses


def _benchmark_worker(n_games, architecture, current_state_bytes, baseline_state_bytes, n_sims, c_uct):
    torch.set_num_threads(1)
    os.environ.setdefault("OMP_NUM_THREADS", "1")

    from sim_play import Simulator

    current = build_model(architecture, dropout=0.0)
    current.load_state_dict(torch.load(io.BytesIO(current_state_bytes), map_location="cpu", weights_only=True))
    current.eval()

    baseline = ResNardiNet()
    baseline.load_state_dict(torch.load(io.BytesIO(baseline_state_bytes), map_location="cpu", weights_only=True))
    baseline.eval()

    sim = Simulator()
    sim.mcts_n_sims = n_sims
    sim.mcts_mode = "eval"
    sim.mcts_c_uct = c_uct
    sim.mcts_dirichlet_eps = 0.0
    sim.mcts_rollouts_per_leaf = 0

    current_move = sim.strat_to_func(current, "mcts")
    baseline_move = sim.strat_to_func(baseline, "greedy")

    current_score = 0
    baseline_score = 0
    with torch.inference_mode():
        for game in range(n_games):
            result = sim.simulate_game(current_move, baseline_move, swap_order=bool(game % 2))
            current_score += result[0]
            baseline_score += result[1]

    return current_score, baseline_score


def run_baseline_benchmark(
    model,
    architecture,
    baseline_weights,
    *,
    games_per_side,
    n_sims,
    workers,
    c_uct,
):
    if games_per_side <= 0:
        return None
    if not baseline_weights:
        return None
    if not os.path.exists(baseline_weights):
        raise FileNotFoundError(f"Benchmark baseline weights not found: {baseline_weights}")

    model_was_training = model.training
    model.eval()

    current_buf = io.BytesIO()
    torch.save(model.state_dict(), current_buf)
    current_state_bytes = current_buf.getvalue()

    baseline_model = ResNardiNet()
    baseline_model.load_state_dict(torch.load(baseline_weights, map_location="cpu", weights_only=True))
    baseline_model.eval()
    baseline_buf = io.BytesIO()
    torch.save(baseline_model.state_dict(), baseline_buf)
    baseline_state_bytes = baseline_buf.getvalue()

    total_games = games_per_side * 2
    counts = _split_games(total_games, max(1, workers))
    current_score = 0
    baseline_score = 0

    if len(counts) == 1:
        current_score, baseline_score = _benchmark_worker(
            counts[0], architecture, current_state_bytes, baseline_state_bytes, n_sims, c_uct
        )
    else:
        ctx = mp.get_context("spawn")
        with ProcessPoolExecutor(max_workers=len(counts), mp_context=ctx) as executor:
            futures = [
                executor.submit(
                    _benchmark_worker,
                    count,
                    architecture,
                    current_state_bytes,
                    baseline_state_bytes,
                    n_sims,
                    c_uct,
                )
                for count in counts
            ]
            for future in tqdm(as_completed(futures), total=len(futures), desc="benchmark", leave=False):
                score_a, score_b = future.result()
                current_score += score_a
                baseline_score += score_b

    if model_was_training:
        model.train()

    total_points = current_score + baseline_score
    win_rate = current_score / total_points if total_points else float("nan")
    return {
        "games": int(sum(counts)),
        "current_score": int(current_score),
        "baseline_score": int(baseline_score),
        "win_rate": float(win_rate),
    }


def mcts_train(
    model,
    *,
    architecture,
    n_iterations,
    games_per_iteration,
    n_sims=100,
    n_workers=1,
    epochs=3,
    batch_size=256,
    lr=1e-3,
    output_dir="mcts_runs",
    weights_file=None,
    tau0=1.0,
    tau_min=0.1,
    tau_half_life=None,
    max_turns=1000,
    device="cpu",
    benchmark_every=10,
    benchmark_games_per_side=100,
    benchmark_workers=1,
    benchmark_baseline="weights/res2.pt",
    benchmark_sims=None,
    benchmark_c_uct=0.1,
):
    require_conv_model(model)
    os.makedirs(output_dir, exist_ok=True)

    device = torch.device(device)
    model.to(device)
    history = []

    def maybe_benchmark(label):
        result = run_baseline_benchmark(
            model,
            architecture,
            benchmark_baseline,
            games_per_side=benchmark_games_per_side,
            n_sims=n_sims if benchmark_sims is None else benchmark_sims,
            workers=benchmark_workers,
            c_uct=benchmark_c_uct,
        )
        if result is None:
            return None
        print(
            f"benchmark {label}: current MCTS(eval) {result['current_score']} - "
            f"res2 greedy {result['baseline_score']} "
            f"({100.0 * result['win_rate']:.2f}% by points, games={result['games']})"
        )
        return result

    initial_benchmark = maybe_benchmark("initial") if benchmark_every != 0 else None
    if initial_benchmark is not None:
        history.append({"iteration": -1, "benchmark": initial_benchmark})

    for iteration in tqdm(range(n_iterations), desc="mcts iterations"):
        temperature = temperature_at(iteration, n_iterations, tau0, tau_min, tau_half_life)
        target_path = os.path.join(output_dir, f"target_iter_{iteration:04d}.pt")
        export_target_network(model, target_path, device=device)

        features, targets, lengths = collect_dataset(
            games_per_iteration,
            n_sims,
            temperature,
            target_path,
            max(1, n_workers),
            max_turns,
        )
        if len(targets) == 0:
            raise RuntimeError("MCTS collection produced no training samples.")

        losses = train_on_dataset(model, features, targets, epochs, batch_size, lr, device)
        record = {
            "iteration": iteration,
            "temperature": float(temperature),
            "games": int(games_per_iteration),
            "samples": int(len(targets)),
            "avg_game_samples": float(np.mean(lengths)) if lengths else 0.0,
            "target_mean": float(np.mean(targets)),
            "target_std": float(np.std(targets)),
            "losses": losses,
        }
        history.append(record)

        print(
            f"iter={iteration} temp={temperature:.3f} samples={len(targets)} "
            f"target={record['target_mean']:.3f}+/-{record['target_std']:.3f} "
            f"loss={losses[-1]:.5f}"
        )

        if weights_file:
            torch.save(model.state_dict(), weights_file)

        if benchmark_every > 0 and (iteration + 1) % benchmark_every == 0:
            benchmark = maybe_benchmark(f"after_iter_{iteration + 1}")
            if benchmark is not None:
                record["benchmark"] = benchmark

    return history


def build_model(architecture, dropout):
    if architecture == "Conv":
        return ConvNardiNet(dropout=dropout)
    if architecture == "DeepConv":
        return ConvNardiNet(dropout=dropout, extra_conv=True)
    if architecture == "ResNet":
        return ResNardiNet(dropout=dropout)
    raise ValueError(f"Unknown architecture: {architecture}")


def main():
    parser = argparse.ArgumentParser(description="MCTS fitted value-iteration training for Nardi.")
    parser.add_argument("--architecture", "--arch", choices=["Conv", "DeepConv", "ResNet"], required=True)
    parser.add_argument("--file", "-f", default=None, help="Optional model state_dict path to load/save.")
    parser.add_argument("--output-dir", default="mcts_runs", help="Directory for TorchScript targets and logs.")
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--games", type=int, default=500, help="Self-play games per iteration.")
    parser.add_argument("--sims", type=int, default=100, help="MCTS simulations per move.")
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--dropout", type=float, default=0.0)
    parser.add_argument("--tau0", type=float, default=1.0)
    parser.add_argument("--tau-min", type=float, default=0.1)
    parser.add_argument("--tau-half-life", type=float, default=None)
    parser.add_argument("--max-turns", type=int, default=1000)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--benchmark-every", type=int, default=10,
                        help="Run baseline benchmark every N completed iterations. Use 0 to disable all benchmarks.")
    parser.add_argument("--benchmark-games-per-side", type=int, default=100,
                        help="Benchmark games per color/order. Total benchmark games are twice this value.")
    parser.add_argument("--benchmark-workers", type=int, default=1)
    parser.add_argument("--benchmark-baseline", default="weights/res2.pt",
                        help="Baseline ResNet state_dict. Baseline uses greedy strategy.")
    parser.add_argument("--benchmark-sims", type=int, default=None,
                        help="MCTS simulations per benchmark move. Defaults to --sims.")
    parser.add_argument("--benchmark-c-uct", type=float, default=0.1)
    args = parser.parse_args()

    model = build_model(args.architecture, args.dropout)
    if args.file and os.path.exists(args.file) and os.path.getsize(args.file) > 0:
        model.load_state_dict(torch.load(args.file, map_location=torch.device(args.device), weights_only=True))

    mcts_train(
        model,
        architecture=args.architecture,
        n_iterations=args.iterations,
        games_per_iteration=args.games,
        n_sims=args.sims,
        n_workers=args.workers,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
        output_dir=args.output_dir,
        weights_file=args.file,
        tau0=args.tau0,
        tau_min=args.tau_min,
        tau_half_life=args.tau_half_life,
        max_turns=args.max_turns,
        device=args.device,
        benchmark_every=args.benchmark_every,
        benchmark_games_per_side=args.benchmark_games_per_side,
        benchmark_workers=args.benchmark_workers,
        benchmark_baseline=args.benchmark_baseline,
        benchmark_sims=args.benchmark_sims,
        benchmark_c_uct=args.benchmark_c_uct,
    )


if __name__ == "__main__":
    main()

# worker_globals.py (or top of your module)
import io, torch
from nardi_net import NardiNet

_MODEL1 = None
_MODEL2 = None

def _init_worker(model1_bytes, model1_cfg, model2_bytes, model2_cfg):
    global _MODEL1, _MODEL2
    torch.set_num_threads(1)  # keep BLAS threads per proc = 1

    if model1_bytes is not None:
        _MODEL1 = NardiNet(*model1_cfg)
        _MODEL1.load_state_dict(torch.load(io.BytesIO(model1_bytes), map_location="cpu"))
        _MODEL1.eval()

    if model2_bytes is not None:
        _MODEL2 = NardiNet(*model2_cfg)
        _MODEL2.load_state_dict(torch.load(io.BytesIO(model2_bytes), map_location="cpu"))
        _MODEL2.eval()

from dataclasses import dataclass

@dataclass(frozen=True)
class Job:
    n_games: int
    # who is "first arg" in Simulator.simulate_game: 1 -> model1, 2 -> model2, 0 -> heuristic/random
    first_id: int   # 1 or 2
    second_id: int  # 0, 1, or 2
    strat_first: str
    strat_second: str
    primary_is_model1: bool

from sim_play import Simulator

def _resolve_model(mid):
    if mid == 1: return _MODEL1
    if mid == 2: return _MODEL2
    return None   # heuristic/random/human

def _play_many(job: Job):
    wins_primary = 0
    wins_secondary = 0
    sim = Simulator()

    with torch.inference_mode():
        for _ in range(job.n_games):
            m_first  = _resolve_model(job.first_id)
            m_second = _resolve_model(job.second_id)
            d1, d2 = sim.simulate_game(m_first, job.strat_first, m_second, job.strat_second)
            wins_primary  += d1
            wins_secondary += d2
            sim.reset()

    return (wins_primary, wins_secondary, job.primary_is_model1)

import io, torch, os, math, multiprocessing as mp
from tqdm.auto import tqdm
from concurrent.futures import ProcessPoolExecutor, as_completed

def _dump_state(model):
    if model is None: return None
    buf = io.BytesIO()
    torch.save(model.state_dict(), buf)
    return buf.getvalue()

def benchmark_parallel(model1, model1_cfg, model_strat="greedy",
                       model2=None, model2_cfg=None, model2_strat="heuristic",
                       num_games=10000, chunk_size=64, max_workers=None):

    if num_games <= 0: num_games = 100
    if chunk_size <= 0: chunk_size = 64

    # Reduce oversubscription
    os.environ.setdefault("OMP_NUM_THREADS", "1")
    os.environ.setdefault("MKL_NUM_THREADS", "1")

    # Serialize weights once
    m1_bytes = _dump_state(model1)
    m2_bytes = _dump_state(model2)

    # Build jobs
    jobs = []
    def add_jobs(total, primary_is_model1):
        batches = math.ceil(total / chunk_size)
        for b in range(batches):
            n = chunk_size if (b+1)*chunk_size <= total else (total - b*chunk_size)
            if primary_is_model1:
                jobs.append(Job(n_games=n, first_id=1, second_id=2 if model2 else 0,
                                strat_first=model_strat, strat_second=model2_strat,
                                primary_is_model1=True))
            else:
                jobs.append(Job(n_games=n, first_id=2 if model2 else 0, second_id=1,
                                strat_first=model2_strat, strat_second=model_strat,
                                primary_is_model1=False))

    add_jobs(num_games, True)
    add_jobs(num_games, False)

    ctx = mp.get_context("spawn")
    if max_workers is None:
        max_workers = min(len(jobs), ctx.cpu_count())

    score_model1 = 0
    score_model2 = 0

    with ProcessPoolExecutor(max_workers=max_workers,
                             mp_context=ctx,
                             initializer=_init_worker,
                             initargs=(m1_bytes, model1_cfg, m2_bytes, model2_cfg)) as ex:
        futures = [ex.submit(_play_many, job) for job in jobs]
        total = len(futures)
        with tqdm(total=total, desc="Sim batches", smoothing=0.05) as pbar:
            for fut in as_completed(futures):
                d_primary, d_secondary, primary_is_model1 = fut.result()
                if primary_is_model1:
                    score_model1 += d_primary
                    score_model2 += d_secondary
                else:
                    score_model1 += d_secondary
                    score_model2 += d_primary
                pbar.update(1)

    return score_model1, score_model2

import torch

if __name__ == "__main__":
    # build models on CPU
    m1 = NardiNet(64, 16)
    m1.load_state_dict(torch.load("mw64_16.pt", map_location="cpu"))
    m1.eval()

    m2 = NardiNet(128, 32)
    m2.load_state_dict(torch.load("mw128_32.pt", map_location="cpu"))
    m2.eval()

    score_64 = score_128 = score_h = 0

    print("64-16 vs heuristic:")
    d64, dh = benchmark_parallel(m1, (64,16), model_strat="lookahead",
                                 model2=None, model2_cfg=None, model2_strat="heuristic",
                                 num_games=10_000)
    print(f"64-16 achieved a win rate of {100 * d64 / (d64 + dh):.2f}%")
    score_64 += d64; score_h += dh

    print("128-32 vs heuristic:")
    d128, dh2 = benchmark_parallel(m2, (128,32), model_strat="lookahead",
                                   model2=None, model2_cfg=None, model2_strat="heuristic",
                                   num_games=10_000)
    print(f"128-32 achieved a win rate of {100 * d128 / (d128 + dh2):.2f}%")
    score_128 += d128; score_h += dh2

    print("64-16 vs 128-32:")
    d_64, d_128 = benchmark_parallel(m1, (64,16), model_strat="lookahead",
                                     model2=m2, model2_cfg=(128,32), model2_strat="lookahead",
                                     num_games=10_000)
    print(f"64-16 achieved a win rate of {100 * d_64 / (d_64 + d_128):.2f}% "
          f"while 128-32 achieved {100 * d_128 / (d_128 + d_64):.2f}%")
    score_64 += d_64; score_128 += d_128

    print(f"total final scores: 64: {score_64}, 128: {score_128}, heuristic: {score_h}")

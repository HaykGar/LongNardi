"""Polyak-average a directory of checkpoint state_dicts into a single model.

Usage:
    python polyak_average.py <checkpoint_dir> <output_file> [--last N] [--ema BETA] [--glob PATTERN]

Each checkpoint is a plain state_dict saved by train.py
(`torch.save(model.state_dict(), chkpt_file)`), so the output is also a plain
state_dict you can load with `model.load_state_dict(torch.load(output_file))`.

By default every matching checkpoint is averaged uniformly (classic
Polyak-Ruppert mean of the parameter iterates). Options:
    --last N     average only the N highest-numbered checkpoints (SWA-style tail)
    --ema BETA   exponential moving average instead of uniform mean, in
                 checkpoint order: avg = BETA*avg + (1-BETA)*next
"""

import argparse
import os
import re
import sys

import torch


def find_checkpoints(directory, pattern):
    """Return checkpoint paths sorted by the integer embedded in the filename."""
    if not os.path.isdir(directory):
        # convenience fallback: <arg> relative to weights/Checkpoints
        alt = os.path.join("weights", "Checkpoints", directory)
        if os.path.isdir(alt):
            directory = alt
        else:
            sys.exit(f"Not a directory: {directory}")

    import glob
    paths = glob.glob(os.path.join(directory, pattern))
    paths = [p for p in paths if os.path.isfile(p)]
    if not paths:
        sys.exit(f"No checkpoints matching {pattern!r} in {directory}")

    def order_key(p):
        m = re.search(r"(\d+)", os.path.basename(p))
        # files without a number sort last, then alphabetically
        return (m is None, int(m.group(1)) if m else 0, os.path.basename(p))

    return sorted(paths, key=order_key)


def average(paths, ema_beta=None):
    """Uniform mean (ema_beta is None) or EMA of a list of state_dicts.

    Float tensors are accumulated in float64 for precision and cast back to
    each tensor's original dtype. Non-float tensors (e.g. integer buffers) are
    not averaged — the value from the last checkpoint is kept.
    """
    ref = torch.load(paths[0], map_location="cpu", weights_only=True)
    ref_keys = set(ref.keys())

    acc = {k: v.to(torch.float64).clone() for k, v in ref.items()
           if v.is_floating_point()}
    non_float = [k for k, v in ref.items() if not v.is_floating_point()]
    if non_float:
        print(f"  not averaging non-float keys (keeping last): {non_float}")

    last = ref
    n = 1
    for p in paths[1:]:
        sd = torch.load(p, map_location="cpu", weights_only=True)
        if set(sd.keys()) != ref_keys:
            sys.exit(f"Key mismatch in {p}:\n  extra:   {set(sd.keys()) - ref_keys}"
                     f"\n  missing: {ref_keys - set(sd.keys())}")
        for k in acc:
            if sd[k].shape != ref[k].shape:
                sys.exit(f"Shape mismatch for {k} in {p}: "
                         f"{tuple(sd[k].shape)} vs {tuple(ref[k].shape)}")
            t = sd[k].to(torch.float64)
            if ema_beta is None:
                acc[k] += t
            else:
                acc[k].mul_(ema_beta).add_(t, alpha=1.0 - ema_beta)
        last = sd
        n += 1

    out = dict(last)  # carries non-float keys from the last checkpoint
    for k, v in acc.items():
        if ema_beta is None:
            v = v / n
        out[k] = v.to(ref[k].dtype)
    return out, n


def main():
    ap = argparse.ArgumentParser(description="Polyak-average checkpoint state_dicts.")
    ap.add_argument("checkpoint_dir", help="directory of .pt checkpoint state_dicts")
    ap.add_argument("output_file", help="path to write the averaged state_dict")
    ap.add_argument("--last", type=int, default=None,
                    help="average only the N highest-numbered checkpoints")
    ap.add_argument("--ema", type=float, default=None, metavar="BETA",
                    help="EMA decay in [0,1) instead of uniform mean")
    ap.add_argument("--glob", default="*.pt", help="filename pattern (default *.pt)")
    args = ap.parse_args()

    if args.ema is not None and not (0.0 <= args.ema < 1.0):
        sys.exit("--ema must be in [0, 1)")

    paths = find_checkpoints(args.checkpoint_dir, args.glob)
    if args.last is not None:
        paths = paths[-args.last:]

    print(f"Averaging {len(paths)} checkpoint(s)"
          + (f" via EMA(beta={args.ema})" if args.ema is not None else " uniformly")
          + ":")
    for p in paths:
        print(f"  {os.path.basename(p)}")

    avg, n = average(paths, ema_beta=args.ema)

    out_dir = os.path.dirname(os.path.abspath(args.output_file))
    os.makedirs(out_dir, exist_ok=True)
    torch.save(avg, args.output_file)
    print(f"Saved average of {n} checkpoint(s) -> {args.output_file}")


if __name__ == "__main__":
    main()

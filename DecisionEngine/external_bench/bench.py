"""Benchmark our engine (as the iOS app's Hard bot) against another Nardi app.

usage (from DecisionEngine/):  venv/bin/python external_bench/bench.py <app> [--games N]
apps: see apps/ (one profile per app). Logs go to external_bench/runs/<app>_<time>/.
"""
import argparse, importlib, os, pkgutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import core
import apps

if __name__ == "__main__":
    names = [m.name for m in pkgutil.iter_modules(apps.__path__)]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("app", choices=names)
    ap.add_argument("--games", type=int, default=1)
    ap.add_argument("--weights", default=core.WEIGHTS, help="value net (default: the iOS Hard bot's, weights/vzg0mk2.pt)")
    ap.add_argument("--stop-at-match-end", action="store_true", help="stop once the app's own match is decided")
    ap.add_argument("--only-opener", choices=["us", "opp"], help="play only games this side opens; resign the rest (not counted)")
    ap.add_argument("--strict", action="store_true", help="stop on anything not seen directly; screenshot every position we move from")
    a = ap.parse_args()
    try:
        core.run(importlib.import_module(f"apps.{a.app}"), a.games, strict=a.strict, weights=os.path.abspath(a.weights),
                 stop_at_match_end=a.stop_at_match_end, only_opener=a.only_opener)
    except core.Abort as e:
        print("ABORT:", e)
        sys.exit(2)

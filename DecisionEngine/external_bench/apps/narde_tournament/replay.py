"""Reconstruct a recorded Narde Tournament game through the engine, turn by turn.

Frames are grouped into turns by the settled dice (colour, values, position). A turn's
result is the last steady board while its dice are showing, or failing that the first
steady board of the next turn. A turn whose dice were never captured (same colour twice
in a row) is bridged by trying every roll for it.
"""
import os, sys, glob
import cv2, numpy as np
from core import ROLLS, children, matches, from_engine
from .reader import read_board

ME = "white"   # app colour we play; also the colour of our dice
LABELS = []     # (frame idx, engine board) for every view a turn was resolved from


def steady_frames(paths):
    """Yield (frame idx, turn key or None, dice values, obs, steady) — steady = identical to the previous frame."""
    prev = None
    for i, p in enumerate(paths):
        obs, dice = read_board(cv2.imread(p))
        kinds = {k for k, _, _ in dice}
        tkey, vals = None, None
        if len(dice) == 2 and len(kinds) == 1 and all(v for _, _, v in dice):
            vals = tuple(sorted(v for _, _, v in dice))
            boxes = tuple(sorted((int(b[0]) // 8, int(b[1]) // 8) for _, b, _ in dice))
            tkey = (kinds.pop(), vals, boxes)
        cur = (tkey, tuple(sorted(obs.items())))
        yield i, tkey, vals, obs, cur == prev, prev
        prev = cur


def turns(paths):
    """List of dicts: kind, dice, seen = first frame showing the settled dice, first/last steady (frame idx, obs).

    A turn starts once the same dice are read in two consecutive frames (mid-roll reads are noise)."""
    out, prev_i = [], None
    for i, tkey, vals, obs, steady, prev in steady_frames(paths):
        if tkey is not None and prev is not None and prev[0] == tkey:
            if not out or out[-1]["key"] != tkey:
                out.append({"key": tkey, "kind": tkey[0], "dice": vals, "seen": (prev_i, dict(prev[1])),
                            "first": None, "last": None})
            if steady:
                out[-1]["first"] = out[-1]["first"] or (i, obs)
                out[-1]["last"] = (i, obs)
        prev_i = i
    return out


def resolve(board, plies, obs, black_first):
    """All distinct end boards reachable by `plies` [(seat, dice|None)] that fit `obs`."""
    frontier = [((), board)]
    for seat, dice in plies:
        frontier = [(hist + (r,), b) for hist, prev in frontier
                    for r, b in children(prev, seat == 1, [dice] if dice else ROLLS)]
    return list({b.tobytes(): (h, b) for h, b in frontier if matches(obs, from_engine(b, black_first))}.values())


def main(frame_dir, black_first=True):
    paths = sorted(glob.glob(f"{frame_dir}/*.png"))
    T = turns(paths)
    print(f"{len(T)} turns with settled dice")
    board = np.zeros((2, 12), np.int8); board[0, 0] = 15; board[1, 0] = -15
    me_seat = 1 if black_first == (ME == "white") else 0
    who = lambda s: "me" if s == me_seat else "opp"
    pending, prev_turn, bad = [(0, None)], None, 0   # seat 0's opening dice were never on screen
    for t in T:
        seat = me_seat if t["kind"] == ME else 1 - me_seat
        if pending[-1][0] == seat:                    # same colour twice: the other seat's turn was missed
            pending.append((1 - seat, None))
        views = ([prev_turn["last"]] if prev_turn and len(pending) == 1 else []) + [t["seen"], t["first"]]
        views = [v for v in views if v]
        hits = []
        for i, obs in views:
            hits = resolve(board, pending, obs, black_first)
            if len(hits) == 1:
                break
        plies = ", ".join(who(s) + ("?" if d is None else str(d)) for s, d in pending)
        if len(hits) == 1:
            hist, board = hits[0]
            LABELS.append((i, board.copy()))
            print(f"frame {i:4d}  [{plies}] -> rolls {list(hist)}   then {who(seat)} rolls {t['dice']}")
        else:
            bad += 1
            print(f"frame {i:4d}  [{plies}] {len(hits)} matching results   then {who(seat)} rolls {t['dice']}")
            if not hits:
                break
            board = hits[0][1]
        pending, prev_turn = [(seat, t["dice"])], t
    print("unresolved:", bad)
    print(board)
    return LABELS


if __name__ == "__main__":
    # usage (from external_bench/): python -m apps.narde_tournament.replay <frames dir> [--white-first]
    main(sys.argv[1], black_first="--white-first" not in sys.argv)

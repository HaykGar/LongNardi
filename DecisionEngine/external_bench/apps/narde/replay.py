"""Rebuild a recorded Narde game (tools/record_frames.sh Narde) through the engine, to check the reader.

A turn is over once the mover's dice leave the board; the settled board (the same read on two frames
in a row) must then be a legal result of that turn. Pip counts (which animate, so can lag) only
break ties. Prints one line per turn and every
settled board that fits nothing.

usage (from external_bench/): python -m apps.narde.replay <frames dir>
"""
import glob, os, sys
import cv2
from core import ROLLS, START, children, matches, from_engine, app_pips, parse_ax
from . import parse, size, dice_owner, read_dice, read_opp_dice


def frames(d):
    for png in sorted(glob.glob(os.path.join(d, "*.png"))):
        im = cv2.imread(png)
        if im.shape[1] != size[0]:
            im = cv2.resize(im, size, interpolation=cv2.INTER_AREA)
        ours = read_dice(im)
        dice = {"w": tuple(sorted(v for v, _ in ours)) if all(f for _, f in ours) and all(v for v, _ in ours) else None,
                "b": read_opp_dice(im)}
        yield os.path.basename(png)[:5], parse(im, parse_ax(open(png[:-4] + ".ax").read())), dice_owner(im), dice


def pips_ok(st, b, black_first):
    want = app_pips(b, black_first)
    return all(v is None or v == want[k] for k, v in st.pips.items())


def main(d):
    states, prev, turn, loose = None, None, 0, []            # states: [(board, side to move, black_first)]
    seen, last, bad = {"w": None, "b": None}, {}, {"w": 0, "b": 0}           # dice read twice in a row, per colour
    for name, st, owner, dice in frames(d):
        for k, v in dice.items():
            if v and last.get(k) == v:
                seen[k] = v
        last = dice
        key = tuple(sorted(st.obs.items()))
        steady, prev = key == prev, key
        if not steady or st.over:
            continue
        if states is None:                                   # wait for the starting position
            if matches(st.obs, from_engine(START, False)):
                states = [(START, False, False), (START, False, True)]   # opener unknown yet
                print(f"{name} start")
            continue
        if any(matches(st.obs, from_engine(b, bf)) for b, _, bf in states):
            continue
        nxt = {}
        for b, side, bf in states:
            if owner and (side != bf) == (owner == "b"):
                continue                                     # this mover's dice are still up: mid-turn
            for r, c in children(b, side, ROLLS):
                if matches(st.obs, from_engine(c, bf)):
                    nxt.setdefault((c.tobytes(), not side, bf), (c, not side, bf, set()))[3].add(r)
        if len(nxt) > 1:                                     # counts read as ranges: the pip counters decide
            nxt = {k: v for k, v in nxt.items() if pips_ok(st, v[0], v[2])} or nxt
        if not nxt:
            loose.append(name)                               # half-played turn, or a misread
            continue
        turn += 1
        mover = {(s, bf) for _, s, bf, _ in nxt.values()}
        who = ", ".join(("black" if s == bf else "white") for s, bf in mover)
        rolls = sorted(set().union(*(r for *_, r in nxt.values())))
        note = ""
        col = "b" if len(mover) == 1 and next(iter(mover))[0] == next(iter(mover))[1] else "w"
        if turn > 1 and seen[col] and seen[col] not in rolls:
            bad[col] += 1
            note = f"  dice read {seen[col]}"
        seen[col] = None
        print(f"{name} turn {turn}: {who} {rolls if len(rolls) <= 4 else str(len(rolls)) + ' rolls'}"
              + (f"  ({len(nxt)} candidates)" if len(nxt) > 1 else "")
              + (f"  [{len(loose)} unexplained: {loose[0]}..{loose[-1]}]" if loose else "") + note)
        states, loose = [(c, s, bf) for c, s, bf, _ in nxt.values()], []
    print(f"dice settled on a wrong value: ours {bad['w']} turns, hers {bad['b']} (hers are only a hint)")
    print(f"end: {turn} turns" + (f", {len(loose)} unexplained settled boards after the last turn: {loose}" if loose else ""))


if __name__ == "__main__":
    main(sys.argv[1])

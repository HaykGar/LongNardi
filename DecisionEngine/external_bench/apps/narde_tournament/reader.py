"""Narde Tournament board reader (1051x816 window). A reading is {("T"|"B", c): Obs} plus the dice
found on the board; see core.py for the coordinates."""
import cv2, numpy as np
from core import Obs, MANY
from .dice import find_dice, read_die

TOP_X = [788.3, 743.4, 697.3, 652.5, 607.5, 564.4, 483.3, 437.6, 392.3, 348.0, 303.7, 258.5]
BOT_X = [236.4, 285.3, 335.1, 384.5, 432.5, 481.6, 567.4, 614.4, 662.8, 711.1, 759.4, 809.0]
SLOT_R = 8          # sampling disk radius (checker radius is ~20)

# Stack layout, measured from a replayed game: the base checker's centre row, the direction the
# stack grows, and per checker count n the stack's extent (base centre -> far edge, px) and the
# repeat distance between checkers. Up to 5 checkers sit a full diameter apart; from 6 on the
# app squeezes them, so extent barely grows and the repeat distance carries the count.
ROWS = {
    "T": dict(base=174, dir=1, X=TOP_X,
              ext={1: 19, 2: 57, 3: 96, 4: 135, 5: 174, 6: 179, 7: 184, 8: 187, 9: 190, 10: 192,
                   11: 194, 12: 195, 13: 197, 14: 198, 15: 199},
              lag={3: 38, 4: 38, 5: 38, 6: 32, 7: 27, 8: 24, 9: 21, 10: 19, 11: 18, 12: 16,
                   13: 15, 14: 14, 15: 13}),
    "B": dict(base=680, dir=-1, X=BOT_X,
              ext={1: 21, 2: 63, 3: 104, 4: 145, 5: 183, 6: 210, 7: 215, 8: 219, 9: 222, 10: 224,
                   11: 226, 12: 228, 13: 229, 14: 230, 15: 231},
              lag={3: 42, 4: 42, 5: 42, 6: 39, 7: 34, 8: 30, 9: 26, 10: 24, 11: 22, 12: 20,
                   13: 18, 14: 17, 15: 16}),
}
SIG_EXT, SIG_LAG, SLACK = 4.0, 1.5, 6.0   # read noise (px) and how far from the best fit a count may be


def classify(hsv, x, y):
    x, y = int(round(x)), int(round(y))
    patch = hsv[y - SLOT_R:y + SLOT_R + 1, x - SLOT_R:x + SLOT_R + 1].reshape(-1, 3)
    s, v = np.median(patch[:, 1]), np.median(patch[:, 2])
    if v < 110:
        return "b"
    if s < 95 and v > 140:
        return "w"
    return None


def stack_extent(hsv, x, P, limit):
    """Distance from the base centre to the far edge of the stack (0 if the point is empty)."""
    end, gap_start = 0, None
    for t in range(0, limit):
        y = P["base"] + P["dir"] * t
        s, v = np.median(hsv[y, x - 4:x + 5, 1]), np.median(hsv[y, x - 4:x + 5, 2])
        wood = s > 110 and 115 < v < 205
        if not wood:
            end = t
        elif t - end > 6:
            break
    return end


def stack_lag(gray, x, P, extent, lo, hi):
    """Repeat distance between checkers along the stack, searched in [lo, hi]."""
    ys = [P["base"] + P["dir"] * t for t in range(0, max(extent - 15, 30))]
    q = np.array([gray[y, x - 4:x + 5].mean() for y in ys])
    q = q - q.mean()
    def corr(L):
        a, b = q[:-L], q[L:]
        return (a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-9)
    return max((L for L in range(lo, hi + 1) if L < len(q) - 5), key=corr, default=None)


def count_range(P, extent, lag):
    """Checker counts consistent with the measured extent (and repeat distance, for squeezed stacks)."""
    score = {}
    for n, e in P["ext"].items():
        sc = ((extent - e) / SIG_EXT) ** 2
        if lag is not None and n >= 3:
            sc += ((lag - P["lag"][n]) / SIG_LAG) ** 2
        score[n] = sc
    best = min(score.values())
    ok = [n for n, sc in score.items() if sc <= best + SLACK]
    if max(ok) >= 6:                                 # tall stacks pack tighter than calibrated: a lower bound;
        return min(ok), MANY                         # the other points and the pip counters fix the count
    return min(ok), max(ok)


def read_board(im):
    """Return (obs per point, dice list [(kind, box, value)])."""
    hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
    gray = cv2.cvtColor(im, cv2.COLOR_BGR2GRAY).astype(float)
    dice = [(k, b, read_die(im, k, b)[0]) for k, b in find_dice(im)]
    boxes = [(x - 6, y - 6, x + w + 6, y + h + 6) for _, (x, y, w, h), _ in dice]

    obs = {}
    for row, P in ROWS.items():
        limit = P["ext"][15] + 8
        for c in range(12):
            x = int(round(P["X"][c]))
            # Distance along the stack at which a die first covers this column, if any.
            cover = min((min(abs(y0 - P["base"]), abs(y1 - P["base"])) if not (y0 <= P["base"] <= y1) else 0
                         for x0, y0, x1, y1 in boxes if x0 - 10 <= x <= x1 + 10), default=None)
            if cover is not None and cover <= 2 * SLOT_R:
                obs[(row, c)] = Obs(None, 0, MANY)          # base hidden under a die
                continue
            color = classify(hsv, x, P["base"])
            if color is None:
                obs[(row, c)] = Obs(None, 0, 0)
                continue
            ext = stack_extent(hsv, x, P, min(limit, cover) if cover is not None else limit)
            if cover is not None and ext >= cover - 6:      # stack runs under a die: only a lower bound
                lo = max([n for n, e in P["ext"].items() if e <= ext + SIG_EXT] or [1])
                obs[(row, c)] = Obs(color, lo, MANY)
                continue
            squeezed = ext >= P["ext"][5] - 6
            lag = stack_lag(gray, x, P, ext, 10, P["lag"][6] + 3 if ext >= P["ext"][6] - 3 else 46) if squeezed else None
            lo, hi = count_range(P, ext, lag)
            obs[(row, c)] = Obs(color, lo, hi)
    return obs, dice

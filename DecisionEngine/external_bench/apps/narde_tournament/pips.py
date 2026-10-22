"""Read the app's two pip counters (top = app-black, bottom = app-white).

Digits are matched against glyph templates in pip_digits.npz, built from a recorded game with
known boards (from external_bench/: python -m apps.narde_tournament.pips <frames dir> <labels.pkl>)."""
import os
import cv2, numpy as np
from core import app_pips

BOXES = {"black": (832, 334, 892, 364), "white": (838, 449, 898, 479)}
GLYPH = (12, 18)                                       # template size (w, h)
TEMPLATES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pip_digits.npz")
_tmpl = None


def glyphs(im, box):
    """Normalised digit glyphs in a counter, left to right."""
    x0, y0, x1, y1 = box
    hsv = cv2.cvtColor(im[y0:y1, x0:x1], cv2.COLOR_BGR2HSV)
    m = ((hsv[..., 2] > 175) & (hsv[..., 1] < 90)).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(m)
    parts = sorted((st[i][0], i) for i in range(1, n) if st[i][3] >= 10 and st[i][4] >= 25)
    out = []
    for _, i in parts:
        x, y, w, h, _ = st[i]
        g = (lab[y:y + h, x:x + w] == i).astype(np.float32)
        out.append(cv2.resize(g, GLYPH, interpolation=cv2.INTER_AREA))
    return out


def read_pips(im):
    """{"black": n, "white": n}; a counter that can't be read is None."""
    global _tmpl
    if _tmpl is None:
        _tmpl = dict(np.load(TEMPLATES)) if os.path.exists(TEMPLATES) else {}
    res = {}
    for who, box in BOXES.items():
        digits = []
        for g in glyphs(im, box):
            scores = {d: float((g * t).sum() / (np.sqrt((g * g).sum() * (t * t).sum()) + 1e-9)) for d, t in _tmpl.items()}
            best = max(scores, key=scores.get, default=None)
            if best is None or scores[best] < 0.85:
                digits = None
                break
            digits.append(best)
        res[who] = int("".join(digits)) if digits else None
    return res


if __name__ == "__main__":
    import sys, glob, pickle, collections
    frames = sorted(glob.glob(os.path.join(sys.argv[1], "*.png")))
    labels = pickle.load(open(sys.argv[2], "rb"))       # [(frame idx, engine board)], app-black opened
    samples = collections.defaultdict(list)
    for f, b in labels:
        im = cv2.imread(frames[f])
        for who, n in app_pips(b, True).items():
            gs = glyphs(im, BOXES[who])
            if len(gs) == len(str(n)):
                for d, g in zip(str(n), gs):
                    samples[d].append(g)
    np.savez(TEMPLATES, **{d: np.mean(v, axis=0) for d, v in samples.items()})
    print({d: len(v) for d, v in sorted(samples.items())})
    _tmpl = None
    ok = sum(read_pips(cv2.imread(frames[f])) == app_pips(b, True) for f, b in labels)
    print(f"labelled frames read correctly: {ok}/{len(labels)}")

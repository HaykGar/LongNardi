import cv2, numpy as np
BOARD = (225, 150, 825, 705)  # x0,y0,x1,y1 playing area incl. both halves
def find_dice(im):
    """Return [(kind, (x,y,w,h))] for white and green dice blobs on the board."""
    x0, y0, x1, y1 = BOARD
    hsv = cv2.cvtColor(im[y0:y1, x0:x1], cv2.COLOR_BGR2HSV)
    masks = {"white": ((hsv[..., 1] < 18) & (hsv[..., 2] > 185)),
             "green": ((hsv[..., 0] > 45) & (hsv[..., 0] < 95) & (hsv[..., 1] > 70) & (hsv[..., 2] > 40))}
    out = []
    for kind, m in masks.items():
        m = cv2.morphologyEx(m.astype(np.uint8) * 255, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
        n, lab, st, _ = cv2.connectedComponentsWithStats(m)
        for i in range(1, n):
            x, y, w, h, a = st[i]
            if a > 350 and 18 < w < 60 and 18 < h < 60:
                out.append((kind, (x + x0, y + y0, w, h)))
    return out

def read_die(im, kind, box):
    """Count round pips on the die's top face; returns (value or None, debug list of pip blobs)."""
    x, y, w, h = box
    c = im[y:y + h, x:x + w]
    hsv = cv2.cvtColor(c, cv2.COLOR_BGR2HSV)
    pip = (hsv[..., 2] < 90) if kind == "white" else ((hsv[..., 1] < 60) & (hsv[..., 2] > 170))
    pip = pip.astype(np.uint8) * 255
    n, lab, st, cen = cv2.connectedComponentsWithStats(pip)
    blobs = []
    for i in range(1, n):
        bx, by, bw, bh, a = st[i]
        if a < 6 or bx == 0 or by == 0 or bx + bw >= w or by + bh >= h:
            continue
        ys, xs = np.nonzero(lab == i)
        cov = np.cov(np.vstack([xs, ys])) if a > 2 else np.eye(2)
        ev = np.sort(np.linalg.eigvalsh(cov))
        aspect = float(np.sqrt(ev[1] / max(ev[0], 1e-3)))
        blobs.append((a, aspect, float(cen[i][0]), float(cen[i][1])))
    if not blobs:
        return None, blobs
    big = max(b[0] for b in blobs)
    top = [b for b in blobs if b[1] < 1.6 and b[0] > 0.45 * big]
    return (len(top) if 1 <= len(top) <= 6 else None), blobs

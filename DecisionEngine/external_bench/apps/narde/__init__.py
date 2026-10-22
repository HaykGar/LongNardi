"""Narde (App Store, SwiftUI; macOS build, 717x1055 window). SOLO mode; one AI level.

Flat 2D board; we are white (head top-right). The accessibility tree gives the Roll / Done / Undo
buttons (pressed without the mouse), both pip counts, and a count badge on squeezed stacks; the
board and dice are read from pixels. Stacks sit a checker apart (54.5 pt); five fill the point
(218 pt) and from six on the fifth checker carries a count badge. Forced moves are played by the app itself.
"""
import time
import cv2, numpy as np
from core import State, Obs, Abort, MANY

name = "narde"
owner = "Narde"
size = (717, 1055)
auto_forced = True
opening_throw = True                                 # the opener's first roll is the one-die-each throw: no doubles
_X = [42, 96, 150, 204, 258, 312, 403, 457, 511, 565, 619, 673]
ROWS = {"T": dict(X=_X[::-1], base=262, dir=1), "B": dict(X=_X, base=878, dir=-1)}
STEP, SQUEEZE, R = 54.5, 218, 27                     # checker spacing, squeezed stack length, checker radius
DICE = [(491, 567), (585, 567)]                     # our dice: white faces, grey once used
OPP_DICE = [(128, 567), (223, 567)]                  # hers: dark faces, blue pips
tray_xy = (662, 970)                                 # our bear-off tray: right end of the bottom pip bar
ROLL, DONE, UNDO = "skin1 btn roll", "skin1 btn done", "skin1 btn undo"


def classify(hsv, x, y):
    p = hsv[y - 6:y + 7, x - 6:x + 7].reshape(-1, 3)
    s, v = np.median(p[:, 1]), np.median(p[:, 2])
    if v > 200 and s < 50:
        return "w"
    if v < 60 and s < 60:
        return "b"
    return None


def checker_px(hsv, x, y):
    """Any part of a checker: white body/rim, black body, or the blue ring (triangles are excluded)."""
    p = hsv[y, x - 4:x + 5]
    s, v = np.median(p[:, 1]), np.median(p[:, 2])
    return v > 185 or (s < 60 and v < 50) or (s > 120 and v > 75)


def stack_count(hsv, x, P):
    """Count range of a stack without a badge, from how far it reaches along its point."""
    end = 0
    for t in range(0, SQUEEZE + R + 10):
        if checker_px(hsv, x, P["base"] + P["dir"] * t):
            end = t
        elif t - end > 6:
            break
    est = (end - R) / STEP + 1
    if est >= 4.7:                                   # five fill the point; six and up get a count badge
        return 5, 5
    lo, hi = int(np.floor(est + 0.25)), int(np.ceil(est - 0.25))
    return max(1, min(lo, hi)), max(1, max(lo, hi))


def die_at(im, x, y):
    """(face brightness, number of blue pips) of the die square centred at x, y."""
    hsv = cv2.cvtColor(im[y - 17:y + 18, x - 17:x + 18], cv2.COLOR_BGR2HSV)
    m = ((hsv[..., 1] > 90) & (hsv[..., 2] > 60)).astype(np.uint8)
    n, _, st, _ = cv2.connectedComponentsWithStats(m)
    return np.median(hsv[..., 2]), sum(1 for i in range(1, n) if 10 <= st[i][4] <= 120)


def read_dice(im):
    """[(value or None, fresh)] for our two dice: white = unused (fresh), grey = used."""
    out = []
    for x, y in DICE:
        face, dots = die_at(im, x, y)
        out.append((dots if 1 <= dots <= 6 and face > 120 else None, face > 215))
    return out


def read_opp_dice(im):
    """Her dice (sorted values) while they're showing, else None."""
    vals = []
    for x, y in OPP_DICE:
        face, dots = die_at(im, x, y)
        if face > 60 or not 1 <= dots <= 6:
            return None
        vals.append(dots)
    return tuple(sorted(vals))


def dice_owner(im):
    """Whose dice are on the board: "w" (ours), "b" (hers) or None (between turns)."""
    if all(die_at(im, x, y)[0] > 140 for x, y in DICE):
        return "w"
    if all(die_at(im, x, y)[0] < 60 for x, y in OPP_DICE):
        return "b"
    return None


def is_ad(els):
    """Full-screen ads (Unity video, store pages) cover the app: its own SOLO button is gone."""
    return not any(r == "AXButton" and l == "SOLO" for r, l, *_ in els)


def wait_out_ad(win, els):
    """Wait for an ad's Close (a button, or a plain label on some ads) and use only that: Skip /
    Get / Install open the App Store. Returns fresh elements."""
    deadline = time.time() + 180
    while is_ad(els):
        if time.time() > deadline:
            raise Abort(f"an ad never offered Close; screenshot {win.save(win.grab(), 'ad')}")
        close = next(((r, x, y) for r, l, x, y, w, h in els if l == "Close"), None)
        if close and close[0] == "AXButton":
            win.press("Close")
        elif close:
            win.click(close[1:])
        time.sleep(2)
        els = win.ax()
    return els


def read(win):
    els = wait_out_ad(win, win.ax())
    if any(l == "Accept" for r, l, *_ in els):      # seen once ("Opponent doubles to 2"); not understood yet
        raise Abort(f"an Accept / Reject offer is on screen; screenshot {win.save(win.grab(), 'offer')}")
    return parse(win.grab(), els)


def parse(im, els):
    """State from a window image (in points) and its accessibility elements."""
    hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV)
    labels = {l for r, l, *_ in els if r == "AXButton"}
    nums = [(int(l), x, y) for r, l, x, y, w, h in els if r == "AXStaticText" and l.isdigit()]
    badges = [(n, x, y) for n, x, y in nums if 225 < y < 915]
    pips = {"black": next((n for n, x, y in nums if y < 225), None), "white": next((n for n, x, y in nums if y > 915), None)}
    obs = {}
    for row, P in ROWS.items():
        for c, x in enumerate(P["X"]):
            color = classify(hsv, x, P["base"])
            if color is None:
                obs[(row, c)] = Obs(None, 0, 0)
                continue
            badge = next((n for n, bx, by in badges if abs(bx - x) < 20 and (by < 527) == (row == "T")), None)
            lo, hi = (badge, badge) if badge else stack_count(hsv, x, P)
            obs[(row, c)] = Obs(color, lo, hi)
    fresh = all(f for _, f in read_dice(im)) and not labels & {UNDO, DONE}
    ready = ROLL in labels or (fresh and dice_owner(im) == "w")
    over = any(l == "Game Over" for r, l, *_ in els)
    return State(im=im, obs=obs, ready=ready, opp_dice=read_opp_dice(im), over=over, pips=pips)


def roll(win, st):
    """Our dice: press Roll if the app shows it, then return them once they've stopped tumbling
    (the same read for 0.8 s), fresh or already greyed by a forced move the app played itself."""
    if ROLL in {l for r, l, *_ in win.ax()}:
        win.press(ROLL)
    deadline, last, since = time.time() + 6, None, 0
    while time.time() < deadline:
        if is_ad(win.ax()):
            wait_out_ad(win, win.ax())
            deadline = time.time() + 6
        dice = read_dice(win.grab())
        vals = tuple(sorted(v for v, _ in dice)) if all(v for v, _ in dice) else None
        if vals != last:
            last, since = vals, time.time()
        elif vals and time.time() - since >= 0.8:
            return vals
        time.sleep(0.15)
    raise Abort(f"could not read our roll; screenshot {win.save(win.grab(), 'roll_unread')}")


def point_xy(app_point, n):
    """Screen position of the n-th checker (1-based) on a point: where to grab or drop."""
    row, c = app_point
    P = ROWS[row]
    off = (n - 1) * STEP if n <= 5 else SQUEEZE
    return P["X"][c], int(P["base"] + P["dir"] * min(off, SQUEEZE))


def track(point):
    """Our (white) distance from the head: T0..T11 then B0..B11; None (off the board) = 24."""
    return 24 if point is None else (point[1] if point[0] == "T" else 12 + point[1])


def play_step(win, src, dst, before):
    """One step by tapping: tap the dice to swap them if the left die (the one a tap uses) isn't the
    one needed, then tap the checker. Bearing off with no exact die uses the larger one."""
    need = track(dst) - track(src)
    (left, _), (right, _) = read_dice(win.grab())
    die = need if need in (left, right) else max(v or 0 for v in (left, right))
    if left != die:
        win.click(DICE[1])
        time.sleep(0.5)
    win.click(point_xy(src, before[src]))


def undo(win):
    win.press(UNDO)


def done(win):
    """Press Done; none comes when the game is over or the app ended the turn itself (the rest of
    the roll can't be played): Undo greys out and our dice leave."""
    for _ in range(20):
        els = win.ax()
        labels = {l for r, l, *_ in els}
        if "Game Over" in labels:
            return
        if DONE in labels:
            win.press(DONE)
            return
        if UNDO not in labels and dice_owner(win.grab()) != "w":
            return
        time.sleep(0.3)
    raise Abort(f"no Done button after our move; screenshot {win.save(win.grab(), 'no_done')}")


def after_turn(win, passed):
    """Wait until our Done / Undo are gone (the turn has passed to her)."""
    deadline = time.time() + 10
    while time.time() < deadline:
        if not any(l in (DONE, UNDO) for r, l, *_ in win.ax()):
            return
        time.sleep(0.3)
    raise Abort("the turn never passed after Done")


def resign(win):
    """Resign from the in-game menu; stop (listing the screen) if anything but a result appears."""
    win.press("skin1 btn menu")
    time.sleep(1.2)
    win.press("skin1 btn ingame resign")
    deadline = time.time() + 6
    while time.time() < deadline:
        time.sleep(0.5)
        els = win.ax()
        if any(l == "Game Over" for r, l, *_ in els):
            return
    raise Abort("resign: no result screen; on-screen elements: "
                + "; ".join(f"{r} '{l}' @{x},{y}" for r, l, x, y, w, h in els if l)
                + f"; screenshot {win.save(win.grab(), 'resign')}")


def match_over(win):
    """The app's match ("n /N" scores) has been won: stop instead of starting a new one."""
    scores = [tuple(map(int, l.replace(" ", "").split("/"))) for r, l, *_ in win.ax()
              if r == "AXStaticText" and l.replace(" ", "").replace("/", "").isdigit() and "/" in l]
    return any(n >= of for n, of in scores)


def new_game(win):
    """Replay (the next game of the app's match to 5, or a new match), then wait for the dialog to go."""
    deadline, pressed = time.time() + 30, False
    while time.time() < deadline:
        els = wait_out_ad(win, win.ax())
        labels = {l for r, l, *_ in els}
        if "Game Over" not in labels or (pressed and "Replay" not in labels):   # the text can linger after a match
            return
        if "Replay" in labels:
            win.press("Replay")
            pressed = True
        time.sleep(1)
    raise Abort(f"no fresh game after Replay; screenshot {win.save(win.grab(), 'replay_failed')}")

"""Narde Tournament (App Store, Unity; macOS build, 1051x816 window). Pick white checkers.

No accessibility tree (Unity), so everything is read from pixels: the board (reader.py), the 3D
dice thrown anywhere on the board (dice.py) and the pip counters (pips.py). Our dice roll by
themselves and come to rest, ringed, in two fixed slots; a Done button with a 5 s countdown ends
the turn; Undo sits on the bar; the result screen has Rematch.
"""
import time
import cv2, numpy as np
from core import State, Abort, wait_board
from .reader import read_board, ROWS
from .pips import read_pips

name = "narde_tournament"
owner = "Narde Tournament"
size = (1051, 816)
auto_forced = False                                  # forced moves are ours to play
opening_throw = False                                # the opener rolls afresh after the throw
MY_DICE_SLOTS = [(634, 381), (697, 381)]             # where our dice rest, ringed, when it's our move
tray_xy = (875, 600)                                 # we (white) bear off into the bottom-right tray
DONE_BOX = (600, 340, 880, 460)                      # region searched for the green Done button
REMATCH = (592, 566)
UNDO = (523, 405)                                    # undo button on the bar during our move


def green_blob(im, box, min_area):
    x0, y0, x1, y1 = box
    hsv = cv2.cvtColor(im[y0:y1, x0:x1], cv2.COLOR_BGR2HSV)
    m = ((hsv[..., 0] > 40) & (hsv[..., 0] < 90) & (hsv[..., 1] > 120) & (hsv[..., 2] > 90)).astype(np.uint8)
    n, _, st, cen = cv2.connectedComponentsWithStats(m)
    best = max(range(1, n), key=lambda i: st[i][4], default=None)
    if best is None or st[best][4] < min_area:
        return None
    return int(cen[best][0]) + x0, int(cen[best][1]) + y0


def my_dice_ready(dice):
    """Our two (white) dice resting in their ringed slots, both read."""
    mine = [(b, v) for k, b, v in dice if k == "white"]
    if len(mine) != 2 or len(dice) != 2 or any(v is None for _, v in mine):
        return None
    for (x, y, w, h), _ in mine:
        if not any(abs(x - sx) <= 8 and abs(y - sy) <= 8 for sx, sy in MY_DICE_SLOTS):
            return None
    return tuple(sorted(v for _, v in mine))


def opp_dice(dice):
    vals = [v for k, b, v in dice if k != "white"]
    return tuple(sorted(vals)) if len(vals) == 2 and None not in vals else None


def game_over(im):
    """Result screen: the Rematch button's green block."""
    x, y = REMATCH
    return green_blob(im, (x - 60, y - 22, x + 60, y + 22), 3000) is not None


def read(win):
    im = win.grab()
    obs, dice = read_board(im)
    mine = my_dice_ready(dice)
    return State(im=im, obs=obs, ready=mine is not None, my_dice=mine, opp_dice=opp_dice(dice),
                 over=game_over(im), pips=read_pips(im))


def roll(win, st):
    return st.my_dice                                # the app rolls for us


def point_xy(app_point, n):
    """Screen position of the n-th checker (1-based) on a point: where to grab or drop."""
    row, c = app_point
    P = ROWS[row]
    t = P["ext"][max(1, min(n, 15))] - (19 if row == "T" else 21)   # centre of the outermost checker
    return int(round(P["X"][c])), int(P["base"] + P["dir"] * t)


def undo(win):
    win.click(UNDO)


def done(win):
    btn = green_blob(win.grab(), DONE_BOX, 2500)
    if btn:
        win.click(btn)


def after_turn(win, passed):
    """Wait until our dice leave the board, so one roll is never played twice."""
    deadline = time.time() + 15
    while time.time() < deadline:
        st = read(win)
        if st.over or not st.ready:
            return
        time.sleep(0.2)
    raise Abort("our dice never left the board after our turn")


def new_game(win):
    """Rematch, then wait until the result screen has been gone for two reads."""
    win.click(REMATCH)
    deadline, clear = time.time() + 30, 0
    while time.time() < deadline:
        clear = 0 if game_over(win.grab()) else clear + 1
        if clear >= 2:
            return
        time.sleep(0.3)
    raise Abort(f"no fresh game after Rematch; screenshot {win.save(win.grab(), 'rematch_failed')}")

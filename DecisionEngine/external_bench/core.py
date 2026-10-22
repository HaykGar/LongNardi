"""Shared core for benchmarking our engine against other Nardi apps (macOS builds of iOS apps).

Our side always plays as the iOS app's Hard bot (weights/vzg0mk2.pt + one-ply lookahead). A
parallel engine game is kept, and every screen read is only *checked* against boards the engine
predicts: her result must be exactly one of her legal results, and each of our drags must give
exactly the board the engine expects. Anything else stops the run with a screenshot.

App-native coordinates (every profile uses them): T[c] = top row counted from the RIGHT, B[c] =
bottom row counted from the LEFT. We always play the colour whose head is T0 and count it as +;
her head is B0 and her checkers count as -. Everything app-specific (reading the screen, rolling,
Done/Undo/new game, where checkers are) lives in a profile under apps/.
"""
from dataclasses import dataclass
import json, os, subprocess, sys, time
import cv2, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DE = os.path.dirname(HERE)                                     # DecisionEngine/
sys.path.insert(0, DE)
import nardi

BUILD = os.path.join(HERE, "build")                            # compiled helpers + exported model (git-ignored)
RUNS = os.path.join(HERE, "runs")                              # logs and screenshots (git-ignored)
WEIGHTS = os.path.join(DE, "weights", "vzg0mk2.pt")             # what ios/scripts/make_model.sh ships as vzg0
ROLLS = [(a, b) for a in range(1, 7) for b in range(a, 7)]
START = np.array([[15] + [0] * 11, [-15] + [0] * 11], np.int8)
MANY = 15
PATH = [("T", c) for c in range(12)] + [("B", c) for c in range(12)]   # our checkers' route; then off


class Abort(Exception):
    pass


# --- board observations ---------------------------------------------------------------

@dataclass(frozen=True)
class Obs:
    """What the screen shows on one point: colour ("w" ours, "b" hers, None) and a count range."""
    color: str | None
    lo: int
    hi: int

    def allows(self, n: int) -> bool:
        """n > 0 our checkers, n < 0 hers, 0 empty."""
        if n == 0:
            return self.lo == 0
        c = "w" if n > 0 else "b"
        return (self.color in (None, c)) and self.lo <= abs(n) <= self.hi


def matches(obs, app):
    return all(obs[p].allows(app[p]) for p in obs)


@dataclass
class State:
    """One read of the app. `ready`: our turn, and her move (if any) is complete."""
    im: np.ndarray
    obs: dict
    ready: bool = False
    my_dice: tuple | None = None        # our roll, when already on screen
    opp_dice: tuple | None = None       # her last roll, when visible
    over: bool = False                  # result screen
    pips: dict | None = None            # {"black": n, "white": n} as the app shows them, values may be None


# --- engine <-> app coordinates -----------------------------------------------------------
# Engine: [2,12] int8, + = seat 0 (moved first), head at [0][0]; - = seat 1, head at [1][0].
# If she (app-black, head B0) moved first she is seat 0, else we are.

def _rows(black_first):
    return (("B", "T"), -1) if black_first else (("T", "B"), 1)


def to_engine(app, black_first):
    (first, second), sgn = _rows(black_first)
    b = np.zeros((2, 12), np.int8)
    for c in range(12):
        b[0, c] = sgn * app[(first, c)]
        b[1, c] = sgn * app[(second, c)]
    return b


def from_engine(b, black_first):
    (first, second), sgn = _rows(black_first)
    app = {}
    for c in range(12):
        app[(first, c)] = sgn * int(b[0, c])
        app[(second, c)] = sgn * int(b[1, c])
    return app


def engine_point_to_app(row, col, black_first):
    (first, second), _ = _rows(black_first)
    return (first if row == 0 else second, col)


def app_pips(board, black_first):
    """Engine board -> {"black": pips, "white": pips} (black = her colour, white = ours)."""
    s0 = sum(int(board[r, c]) * (24 - (12 * r + c)) for r in range(2) for c in range(12) if board[r, c] > 0)
    s1 = sum(-int(board[r, c]) * (24 - (12 * (1 - r) + c)) for r in range(2) for c in range(12) if board[r, c] < 0)
    return {"black": s0, "white": s1} if black_first else {"black": s1, "white": s0}


# --- engine ---------------------------------------------------------------------------------

def children(board, side, rolls):
    """[(roll, end board)] of every legal turn for `side` (False = seat 0) with each roll."""
    e = nardi.Engine()
    out = []
    for d1, d2 in rolls:
        e.set_position(board, side)
        kids = [((d1, d2), np.array(f.raw_data, dtype=np.int8)) for f in e.set_and_enumerate(d1, d2)]
        out += kids or [((d1, d2), board.copy())]               # no legal move: the turn passes
        if (d1, d2) == (3, 3):
            out += [((3, 3), b) for b in first_33(board, side) if not any(np.array_equal(b, k) for _, k in kids)]
    return out


def unseen_plies(board, side, black_first, seat_me, obs, depth, no_double_first=False):
    """The shortest histories (up to `depth` plies) from `board` (`side` to move) ending on her ply
    with the board on screen: her plies any legal turn, ours only forced turns (any other would
    have been ours to choose). [[(who, roll, board after), ...]], one per distinct sequence of
    boards. no_double_first: the first ply is a game's opening, thrown one die each (never a double)."""
    for d in range(1, depth + 1):
        found = _histories(board, side, black_first, seat_me, obs, d, no_double_first)
        if found:
            return found
    return []


def _histories(board, side, black_first, seat_me, obs, depth, no_double_first):
    out, seen = [], set()

    def walk(b, sd, path):
        if len(path) == depth:
            return
        me = (sd == 1) == (seat_me == 1)
        for r in ROLLS:
            if no_double_first and not path and r[0] == r[1]:
                continue
            kids = {k.tobytes(): k for _, k in children(b, sd, [r])}
            if me and len(kids) != 1:
                continue
            for k in kids.values():
                p = path + [("me" if me else "opp", r, k)]
                if not me and len(p) == depth and matches(obs, from_engine(k, black_first)):
                    key = (k.tobytes(), tuple(x[2].tobytes() for x in p))
                    if key not in seen:
                        seen.add(key)
                        out.append(p)
                walk(k, not sd, p)

    walk(board, side, [])
    ends = {}
    for p in out:
        ends.setdefault(tuple(x[2].tobytes() for x in p), p)   # same boards, other rolls: one history
    return list(ends.values())


STRICT = False                                                  # set by run(strict=...)


def screen_board(obs, black_first):
    """The engine board shown on screen, if every count is exact and both sides have 15; else None."""
    if any(o.color and o.lo != o.hi for o in obs.values()):
        return None
    app = {k: (o.lo if o.color == "w" else -o.lo if o.color == "b" else 0) for k, o in obs.items()}   # ours (white) +
    if sum(v for v in app.values() if v > 0) != 15 or -sum(v for v in app.values() if v < 0) != 15:
        return None
    return to_engine(app, black_first)


def only_passes(board, paths):
    """Every unseen ply of ours in these histories is a pass (no legal move): nothing was ours to
    choose, so histories that differ only in her moves' order still pin down the position."""
    for p in paths:
        b = board
        for who, _, k in p:
            if who == "me" and not np.array_equal(k, b):
                return False
            b = k
    return True


def log_unseen(log, game_no, turn, paths):
    """Log plies played before we looked (one history, or how many fit) and return the end board."""
    if len(paths) == 1:
        for who, r, b in paths[0]:
            log({"game": game_no, "after_turn": turn, "who": who, "dice": list(r), "unseen": True, "board": b.tolist()})
    else:
        log({"game": game_no, "after_turn": turn, "unseen_histories": len(paths), "board": paths[0][-1][2].tolist()})
    return paths[0][-1][2]


def first_33(board, side):
    """3-3 on a first move, two checkers off the head (3 + 9 or 6 + 6): allowed by apps whose rules
    extend the 6-6 / 4-4 head exception to 3-3; our engine doesn't."""
    sgn, me = (1, 0) if not side else (-1, 1)
    flat = lambda b: [int(b[(me + k // 12) % 2, k % 12]) * sgn for k in range(24)]   # mover's track
    track = flat(board)
    if track[0] != 15:
        return []
    out = []
    for a, b in ((3, 9), (6, 6)):
        if all(track[p] >= 0 for p in range(3, b + 1, 3)):
            t = track[:]
            t[0] -= 2; t[a] += 1; t[b] += 1
            nb = board.copy()
            for k in range(24):
                nb[(me + k // 12) % 2, k % 12] = t[k] * sgn
            out.append(nb)
    return out


def export_model(src=WEIGHTS):
    """Export the Hard bot's net in the format this nardi build loads; cached by mtime."""
    import torch
    from nardi_net import ResNardiNet, export_for_engine
    os.makedirs(BUILD, exist_ok=True)
    out = os.path.join(BUILD, os.path.basename(src) + ".engine")
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        m = ResNardiNet()
        m.load_state_dict(torch.load(src, map_location="cpu", weights_only=True))
        m.eval()
        export_for_engine(m, out)
    return out


def choose(eng, board, seat, dice):
    """The Hard bot's move for `dice`, as (index into analyze_dice's ranking, that ranking).

    lookahead_choice_target is the app's Hard strategy itself (including its tie-breaking and
    'always take a win'); analyze_dice ranks the same one-ply values and lets us play the chosen
    board with sub-moves (apply_analyzed_move -> recent_moves)."""
    eng.set_position(board, seat == 1)
    ranked = eng.analyze_dice(*dice)
    if len(ranked) <= 1:
        return 0, ranked
    kids = [np.array(f.raw_data, dtype=np.int8) for f in eng.get_children(nardi.status_codes.SUCCESS)]
    pick = kids[eng.lookahead_choice_target()]                  # indexes the same enumeration as get_children
    idx = next(i for i, (b, _) in enumerate(ranked) if np.array_equal(np.array(b, dtype=np.int8), pick))
    return idx, ranked


def step_orders(start, target, dice, first):
    """Every order of single-die steps (src point, dst point or None = bear off) taking our checkers
    from `start` to `target` (app-native dicts) without landing on hers; `first` (the engine's own
    order) comes first. Apps differ in what they allow mid-turn, so the app has the final word."""
    pool = [dice[0]] * 4 if dice[0] == dice[1] else list(dice)
    n, out, seen = len(first), [first], {tuple(first)}

    def dfs(cur, left, steps):
        if len(steps) == n:
            if cur == target and tuple(steps) not in seen:
                seen.add(tuple(steps)); out.append(list(steps))
            return
        ours = [i for i, p in enumerate(PATH) if cur[p] > 0]
        for d in sorted(set(left)):
            rest = list(left); rest.remove(d)
            for i in ours:
                j, src = i + d, PATH[i]
                if j < 24:
                    dst = PATH[j]
                    if cur[dst] < 0:
                        continue
                elif min(ours) < 18 or (j > 24 and min(ours) < i):
                    continue                                    # bear off only from home, overshoot only from the back
                else:
                    dst = None
                nxt = dict(cur); nxt[src] -= 1
                if dst:
                    nxt[dst] += 1
                dfs(nxt, rest, steps + [(src, dst)])

    dfs(dict(start), pool, [])
    return out


def result_record(board, seat_me):
    """Win/loss and oin/mars from the last board the engine knows. We win only on our own move,
    so that board is final; on a loss our checkers can't change on her last move, so mars is exact."""
    if board is None:
        return {"result": "unknown"}
    sgn = 1 if seat_me == 0 else -1
    ours = int((board * sgn).clip(min=0).sum())
    theirs = int((-board * sgn).clip(min=0).sum())
    if ours == 0:
        return {"result": "win", "mars": theirs == 15, "their_checkers_left": theirs}
    return {"result": "loss", "mars": ours == 15, "our_checkers_left": ours}


def parse_ax(text):
    """axlist output (also saved as .ax by tools/record_frames.sh) -> [(role, label, x, y, w, h)]."""
    els = []
    for line in text.splitlines():
        parts = [p.strip() for p in line.split("|")]
        if len(parts) == 3:
            x, y, w, h = map(int, parts[2].split())
            els.append((parts[0], parts[1], x, y, w, h))
    return els


# --- the app's window --------------------------------------------------------------------------

def build_tools():
    """Compile the Swift helpers (window list, mouse, accessibility) once."""
    os.makedirs(BUILD, exist_ok=True)
    for name in ("click", "listwin", "axlist", "axpress"):
        out, src = os.path.join(BUILD, name), os.path.join(HERE, "tools", f"{name}.swift")
        if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
            subprocess.run(["swiftc", "-O", src, "-o", out], check=True, capture_output=True)


class Window:
    """Capture and input for one app's window, in window points (captures are scaled to points,
    so the window may sit on a Retina or a normal display)."""

    def __init__(self, owner, size, logdir):
        self.owner, self.size, self.logdir, self.n = owner, size, logdir, 0

    def window_id(self):
        out = subprocess.run([os.path.join(BUILD, "listwin")], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split(" | ")[0].split(" ", 1)
            if len(parts) == 2 and parts[1] == self.owner:
                return parts[0]
        raise Abort(f"{self.owner} window not found")

    def grab(self):
        path = os.path.join(self.logdir, "cur.png")
        subprocess.run(["screencapture", "-x", "-o", "-l", self.window_id(), path], check=True)
        im = cv2.imread(path)
        if im is None:
            raise Abort("window capture failed")
        h, w = im.shape[:2]
        W, H = self.size
        if (w, h) != (W, H):
            if abs(w / W - h / H) > 0.01 or round(w / W) != w / W:
                raise Abort(f"window is {w}x{h} px, expected {W}x{H} points (or a multiple); resize it back")
            im = cv2.resize(im, (W, H), interpolation=cv2.INTER_AREA)
        return im

    def save(self, im, tag):
        self.n += 1
        p = os.path.join(self.logdir, f"{self.n:04d}_{tag}.png")
        cv2.imwrite(p, im)
        return p

    def drag(self, a, b):
        subprocess.run([os.path.join(BUILD, "click"), self.owner, *map(str, (*a, *b))], check=True)

    def click(self, a):
        subprocess.run([os.path.join(BUILD, "click"), self.owner, *map(str, a)], check=True)

    def ax(self):
        """Accessibility elements: [(role, label, x, y, w, h)] with centres in window points."""
        return parse_ax(subprocess.run([os.path.join(BUILD, "axlist"), self.owner], capture_output=True, text=True).stdout)

    def press(self, label):
        r = subprocess.run([os.path.join(BUILD, "axpress"), self.owner, label], capture_output=True, text=True)
        if "press: 0" not in r.stdout:
            raise Abort(f"could not press '{label}': {r.stdout.strip()}")


# --- playing -----------------------------------------------------------------------------------

def wait_board(prof, win, expect, timeout):
    """Poll until two consecutive reads fit `expect` (app-native dict). Returns (ok, last State)."""
    deadline, hits, st = time.time() + timeout, 0, None
    while time.time() < deadline:
        st = prof.read(win)
        if st.over and (min(expect.values()) >= 0 or max(expect.values()) <= 0):
            return True, st                                     # our last checker off: a result dialog may cover the board
        hits = hits + 1 if matches(st.obs, expect) else 0
        if hits >= 2:
            return True, st
        time.sleep(0.15)
    return False, st


def mismatch(win, st, expect, tag):
    p = win.save(st.im, f"{tag}_mismatch")
    bad = {f"{r}{c}": (expect[(r, c)], str(st.obs[(r, c)])) for (r, c) in st.obs if not st.obs[(r, c)].allows(expect[(r, c)])}
    return Abort(f"{tag}: board never matched the engine's expectation; mismatches {bad}; screenshot {p}")


def play_steps(prof, win, start, steps, tag):
    """Drag `steps` in order, checking each board. True when all landed. If the app ignores a step
    (board unchanged after a retry), undo back to `start` and return False."""
    cur = dict(start)
    for k, (src, dst) in enumerate(steps):
        before = dict(cur)
        grab = prof.point_xy(src, cur[src])
        cur[src] -= 1
        if dst:
            cur[dst] += 1
        drop = prof.point_xy(dst, cur[dst]) if dst else prof.tray_xy
        for attempt in range(2):
            if hasattr(prof, "play_step"):
                prof.play_step(win, src, dst, before)
            else:
                win.drag(grab, drop)
            ok, st = wait_board(prof, win, cur, 3.0)
            if ok:
                break
            if not matches(st.obs, before):
                raise mismatch(win, st, cur, f"{tag}s{k}")
        else:
            win.save(st.im, f"{tag}s{k}_refused")
            for _ in range(k):                                  # take back the steps that did land
                prof.undo(win)
                time.sleep(0.6)
            ok, st = wait_board(prof, win, start, 4.0)
            if not ok:
                raise mismatch(win, st, start, f"{tag}_undo")
            return False
    return True


def play_game(prof, win, eng, log, game_no, resume=None, only_opener=None):
    """Play one game to its result screen and return its result record.

    resume = (board, black_first, turn[, next]) picks a game up from its last logged board: with
    our turn up (a half-played turn on screen is undone first); next=True: her move comes next;
    next="ours": ours does, and the app may already have played it (forced)."""
    black_first, board, seat_me = None, None, None
    last_opp_dice, prev_key, turn, ours_pending = None, None, 0, False
    if resume:
        board, black_first, turn = np.array(resume[0], np.int8), resume[1], resume[2]
        seat_me = 1 if black_first else 0
        if resume[3:] and resume[3]:
            ours_pending, resume = resume[3] == "ours", None    # "ours": our turn may have been played unseen
    while True:
        st = prof.read(win)
        if st.over and board is None:                          # a result screen left over from before: next game
            prof.new_game(win)
            continue
        if st.over:
            win.save(st.im, f"g{game_no}_result")
            return result_record(board, seat_me) | {"we_open": not black_first}
        if st.opp_dice:
            last_opp_dice = st.opp_dice
        key = (st.ready, st.my_dice, tuple(sorted(st.obs.items())))
        steady = st.ready and key == prev_key
        prev_key = key
        if not steady:
            time.sleep(0.15)
            continue

        # --- our turn: first account for her turn since our last move ---
        obs = st.obs
        her_seen = False
        if black_first is None:
            board = START
            if matches(obs, from_engine(START, False)):            # untouched start: we open
                black_first = False
            else:                                               # plies played before we looked
                fits = [(bf, p) for bf in (False, True)
                        for p in unseen_plies(START, False, bf, 1 if bf else 0, obs, 3, prof.opening_throw)]
                ends = {(bf, p[-1][2].tobytes()) for bf, p in fits}
                if len(ends) == 1 and not (STRICT and len(fits) != 1):
                    black_first, path = fits[0]
                    board, her_seen = log_unseen(log, game_no, turn, [p for _, p in fits]), True
                    turn += sum(w == "me" for w, _, _ in path)
                else:                                           # history unclear: play on from the screen
                    if STRICT:
                        raise Abort(f"start: {len(fits)} histories fit the screen; screenshot {win.save(st.im, 'start_unresolved')}")
                    black_first, board = True, screen_board(obs, True)
                    if board is None:
                        raise Abort(f"start: screen not readable exactly; screenshot {win.save(st.im, 'start_unresolved')}")
                    her_seen, turn = True, 1
                    log({"game": game_no, "event": "adopted screen position", "board": board.tolist()})
            seat_me = 1 if black_first else 0
            log({"game": game_no, "event": "start", "we_open": not black_first})
            if only_opener and (only_opener == "us") == black_first:   # the other side opens: resign, not counted
                prof.resign(win)
                return {"result": "skipped", "we_open": not black_first}
        if resume:
            for _ in range(4):                                  # a half-played turn: undo back to its start
                if matches(obs, from_engine(board, black_first)):
                    break
                prof.undo(win)
                time.sleep(0.8)
                st = prof.read(win)
                obs = st.obs
            else:
                raise Abort(f"resume: screen does not show the logged board; screenshot {win.save(st.im, 'resume_mismatch')}")
            resume = None
        elif ours_pending and matches(obs, from_engine(board, black_first)):
            pass                                                # our pending turn wasn't played after all
        elif not her_seen and (turn > 0 or black_first or ours_pending):
            opp_seat = 1 - seat_me
            fit = lambda frm, rolls: {b.tobytes(): (r, b) for r, b in children(frm, opp_seat == 1, rolls)
                                      if matches(obs, from_engine(b, black_first))}
            uniq = fit(board, [last_opp_dice]) if last_opp_dice and not ours_pending else {}
            if not uniq and not ours_pending:                   # dice unseen, misread or stale: try every roll
                uniq = fit(board, ROLLS)
            if not uniq and prof.auto_forced:                   # forced turns of ours the app played unseen
                paths = unseen_plies(board, (seat_me if ours_pending else opp_seat) == 1, black_first, seat_me, obs, 3)
                if len({p[-1][2].tobytes() for p in paths}) == 1 and not (STRICT and len(paths) != 1 and not only_passes(board, paths)):
                    b = log_unseen(log, game_no, turn, paths)
                    turn += sum(w == "me" for w, _, _ in paths[0])
                    uniq = {b.tobytes(): ("unseen", b)}
            if len(uniq) > 1 and st.pips:                       # counts read as ranges: the pip counters decide
                uniq = {k: (r, b) for k, (r, b) in uniq.items()
                        if all(v is None or v == app_pips(b, black_first)[w] for w, v in st.pips.items())}
            if len(uniq) != 1:                                  # history unclear: play on from the screen
                b = None if STRICT else screen_board(obs, black_first)
                if b is None:
                    p = win.save(st.im, f"g{game_no}_opp_unresolved")
                    raise Abort(f"opponent move: {len(uniq)} legal results fit the screen "
                                f"(her dice read {last_opp_dice}); screenshot {p}")
                log({"game": game_no, "after_turn": turn, "event": "adopted screen position", "board": b.tolist()})
                uniq = {b.tobytes(): ("unseen", b)}
            r, board = next(iter(uniq.values()))
            if r != "unseen":
                log({"game": game_no, "turn": turn, "who": "opp", "dice": list(r), "board": board.tolist()})
            last_opp_dice = None
        st = prof.read(win)
        if not matches(st.obs, from_engine(board, black_first)):   # the app moved on while we worked
            if STRICT:
                raise mismatch(win, st, from_engine(board, black_first), f"g{game_no}t{turn}_moved_on")
            log({"game": game_no, "turn": turn, "event": "screen moved on; re-reading"})
            prev_key, ours_pending = None, True
            continue
        ours_pending = False
        turn += 1
        if STRICT:
            win.save(st.im, f"g{game_no}t{turn}_before")             # every position we move from, for auditing

        # --- our move ---
        mine = prof.roll(win, st)
        idx, ranked = choose(eng, board, seat_me, mine)
        if not ranked:
            log({"game": game_no, "turn": turn, "who": "me", "dice": list(mine), "move": "pass"})
            prof.after_turn(win, passed=True)
            prev_key = None
            continue
        eng.apply_analyzed_move(idx)
        target = np.array(ranked[idx][0], dtype=np.int8)
        subs = eng.recent_moves()
        start, goal = from_engine(board, black_first), from_engine(target, black_first)
        played = False
        if len(ranked) == 1 and prof.auto_forced:               # the app may play a forced move itself
            played, s2 = wait_board(prof, win, goal, 4.0)
            if not played and not matches(s2.obs, start):       # the game ran on (our dice read late)
                if STRICT:
                    raise mismatch(win, s2, goal, f"g{game_no}t{turn}_forced")
                log({"game": game_no, "turn": turn, "event": "screen moved on; re-reading"})
                turn -= 1
                prev_key, ours_pending = None, True
                continue
        if not played:
            first = [(engine_point_to_app(fr, fc, black_first),
                      engine_point_to_app(tr, tc, black_first) if tr >= 0 else None) for fr, fc, tr, tc in subs]
            orders = step_orders(start, goal, mine, first)
            if not any(play_steps(prof, win, start, o, f"g{game_no}t{turn}o{i}") for i, o in enumerate(orders)):
                raise Abort(f"g{game_no}t{turn}: the app refused all {len(orders)} orders of the engine's move")
            prof.done(win)
        board = target
        log({"game": game_no, "turn": turn, "who": "me", "dice": list(mine), "subs": subs,
             "rank_1ply": idx, "value_1ply": float(ranked[idx][1]), "board": board.tolist()})
        prof.after_turn(win, passed=False)
        prev_key = None


def run(prof, games, resume=None, strict=False, weights=WEIGHTS, stop_at_match_end=False, only_opener=None):
    """Play `games` games against `prof`'s app, logging to runs/<app>_<time>/. strict: stop on
    anything not seen directly (only a single unambiguous history of unseen plies is accepted, a
    board is never taken from the screen as is) and save the screen before each of our moves.
    stop_at_match_end: stop once the app's own match is decided. only_opener ("us" / "opp"): play
    only games that side opens; resign the others, which don't count towards `games`."""
    global STRICT
    STRICT = strict
    build_tools()
    logdir = os.path.join(RUNS, f"{prof.name}_{time.strftime('%Y%m%d_%H%M%S')}")
    os.makedirs(logdir)
    logf = open(os.path.join(logdir, "log.jsonl"), "a")
    def log(rec):
        logf.write(json.dumps(rec) + "\n"); logf.flush(); print(rec, flush=True)
    eng = nardi.Engine()
    eng.load_target_network(export_model(weights))
    log({"event": "config", "app": prof.name, "games": games, "strict": strict, "weights": os.path.basename(weights),
         "only_opener": only_opener})
    win = Window(prof.owner, prof.size, logdir)
    results = []
    try:
        g = 0
        while sum(r["result"] != "skipped" for r in results) < games:
            if stop_at_match_end and (g or prof.read(win).over) and prof.match_over(win):
                log({"event": "match over"})                # the app's match is decided: stop here
                break
            if not (g == 0 and resume) and prof.read(win).over:   # a resigned game may leave no result screen
                prof.new_game(win)
            res = {"game": g, **play_game(prof, win, eng, log, g, resume if g == 0 else None, only_opener)}
            results.append(res)
            log({"event": "end", **res})
            g += 1
    except Abort as e:
        log({"event": "abort", "game": len(results), "reason": str(e)})
        raise
    finally:
        results = [r for r in results if r["result"] != "skipped"]
        wins = [r for r in results if r["result"] == "win"]
        losses = [r for r in results if r["result"] == "loss"]
        summary = {"app": prof.name, "games": len(results), "wins": len(wins), "losses": len(losses),
                   "mars_won": sum(r["mars"] for r in wins), "mars_lost": sum(r["mars"] for r in losses),
                   "points_for": sum(2 if r["mars"] else 1 for r in wins),
                   "points_against": sum(2 if r["mars"] else 1 for r in losses)}
        json.dump(summary, open(os.path.join(logdir, "summary.json"), "w"), indent=2)
        print("summary:", summary, "\nlogs:", logdir)

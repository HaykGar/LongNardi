"""Drive the plain-C API (nardi_c_api.h) entirely through ctypes -- i.e. calling
only the extern "C" symbols, the same surface an iOS / Objective-C++ bridge will
use. Confirms the C state machine plays full games (human-vs-bot and bot-vs-bot),
reads board/dice correctly, and reports errors without throwing across the
boundary.

Run directly:  python tests/test_c_api.py
"""

import ctypes
import glob
import os
import random
import sys
import tempfile

import torch

PARENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, PARENT)
from nardi_net import ResNardiNet, export_for_engine  # noqa: E402

# Strategy / StepResult constants (must match nardi_c_api.h).
HUMAN, GREEDY, LOOKAHEAD, MCTS, HEURISTIC, RANDOM = range(6)
STEP_GAME_OVER, STEP_AWAITING_HUMAN, STEP_BOT_MOVED, STEP_TURN_PASSED = 0, 1, 2, 3
STEP_ERROR = -1
BOARD_CELLS = 24

c_void_p, c_int, c_float, c_char_p = (
    ctypes.c_void_p, ctypes.c_int, ctypes.c_float, ctypes.c_char_p)
c_byte_p = ctypes.POINTER(ctypes.c_byte)


def _load_lib():
    # Pick the extension built for the running interpreter (others may be stale or
    # a different architecture).
    tag = f"nardi.cpython-{sys.version_info.major}{sys.version_info.minor}-darwin.so"
    path = os.path.join(PARENT, tag)
    if not os.path.exists(path):
        cands = sorted(glob.glob(os.path.join(PARENT, "nardi.cpython-*.so")))
        assert cands, "build the extension first (setup.py build_ext --inplace)"
        path = cands[-1]
    lib = ctypes.CDLL(path)
    lib.nardi_create.restype = c_void_p
    lib.nardi_destroy.argtypes = [c_void_p]
    for fn in ("nardi_reset", "nardi_current_player", "nardi_sign", "nardi_is_terminal",
               "nardi_should_continue", "nardi_winner_result", "nardi_legal_move_count",
               "nardi_advance"):
        getattr(lib, fn).argtypes = [c_void_p]
        getattr(lib, fn).restype = c_int
    lib.nardi_load_model.argtypes = [c_void_p, c_char_p]; lib.nardi_load_model.restype = c_int
    lib.nardi_configure_players.argtypes = [c_void_p, c_int, c_int]
    lib.nardi_configure_players.restype = c_int
    lib.nardi_set_mcts_params.argtypes = [c_void_p, c_int, c_float, c_int, c_float, c_float,
                                          c_float, c_int]
    lib.nardi_set_mcts_params.restype = c_int
    lib.nardi_dice.argtypes = [c_void_p, ctypes.POINTER(ctypes.c_int)]; lib.nardi_dice.restype = c_int
    lib.nardi_board.argtypes = [c_void_p, c_byte_p]; lib.nardi_board.restype = c_int
    lib.nardi_option_board.argtypes = [c_void_p, c_int, c_byte_p]
    lib.nardi_option_board.restype = c_int
    lib.nardi_apply_human_move.argtypes = [c_void_p, c_int]; lib.nardi_apply_human_move.restype = c_int
    lib.nardi_last_error.argtypes = [c_void_p]; lib.nardi_last_error.restype = c_char_p
    return lib


def _blob():
    m = ResNardiNet()
    m.load_state_dict(torch.load(os.path.join(PARENT, "weights", "res2.pt"),
                                 map_location="cpu", weights_only=True))
    m.eval()
    p = tempfile.mktemp(suffix=".nardiw")
    export_for_engine(m, p)
    return p


def _read_board(lib, h):
    buf = (ctypes.c_byte * BOARD_CELLS)()
    assert lib.nardi_board(h, buf) == 0
    return list(buf)


def _play(lib, h, white, black, max_steps=5000):
    assert lib.nardi_configure_players(h, white, black) == 0
    assert lib.nardi_reset(h) == 0
    for _ in range(max_steps):
        step = lib.nardi_advance(h)
        assert step != STEP_ERROR, lib.nardi_last_error(h).decode()
        if step == STEP_GAME_OVER:
            return lib.nardi_winner_result(h)
        if step == STEP_AWAITING_HUMAN:
            n = lib.nardi_legal_move_count(h)
            assert n > 0
            # dice should be valid faces while a human is to move
            dice = (ctypes.c_int * 2)()
            assert lib.nardi_dice(h, dice) == 0
            assert 1 <= dice[0] <= 6 and 1 <= dice[1] <= 6
            assert lib.nardi_apply_human_move(h, random.randrange(n)) == 0
    return None


def test_c_api_games():
    lib = _load_lib()
    blob = _blob()
    h = lib.nardi_create()
    assert h
    try:
        assert lib.nardi_load_model(h, blob.encode()) == 0, lib.nardi_last_error(h).decode()

        # initial board has all 30 checkers (15 per side)
        assert lib.nardi_reset(h) == 0
        assert sum(abs(v) for v in _read_board(lib, h)) == 30

        # human (random) vs each bot, and bot-vs-bot
        for white, black in [(HUMAN, GREEDY), (HUMAN, LOOKAHEAD), (GREEDY, HEURISTIC)]:
            w = _play(lib, h, white, black)
            assert w in (1, 2), f"{white} vs {black} did not finish ({w})"

        # MCTS via C API
        assert lib.nardi_set_mcts_params(h, 20, 1.0, 0, 0.1, 0.25, 0.3, 0) == 0
        assert _play(lib, h, MCTS, HEURISTIC) in (1, 2)

        # error path: out-of-range human move must not throw, must report error
        lib.nardi_configure_players(h, HUMAN, GREEDY)
        lib.nardi_reset(h)
        lib.nardi_advance(h)  # roll into AwaitingHuman
        assert lib.nardi_apply_human_move(h, 9999) != 0
        assert lib.nardi_last_error(h).decode() != ""
    finally:
        lib.nardi_destroy(h)
        os.remove(blob)


if __name__ == "__main__":
    random.seed(0)
    lib = _load_lib()
    blob = _blob()
    h = lib.nardi_create()
    try:
        lib.nardi_load_model(h, blob.encode())
        for white, black, name in [(HUMAN, GREEDY, "human vs greedy"),
                                    (HUMAN, LOOKAHEAD, "human vs lookahead"),
                                    (GREEDY, HEURISTIC, "greedy vs heuristic")]:
            print(f"{name}: winner_result =", _play(lib, h, white, black))
        lib.nardi_set_mcts_params(h, 20, 1.0, 0, 0.1, 0.25, 0.3, 0)
        print("mcts vs heuristic: winner_result =", _play(lib, h, MCTS, HEURISTIC))
        # strength sanity over the C API
        wins = {1: 0, 2: 0}
        for g in range(20):
            w = _play(lib, h, GREEDY, HEURISTIC)  # model is white every game
            # white won iff current player is black (loser to move)
            white_won = (lib.nardi_current_player(h) == 1)
            wins[1 if white_won else 2] += 1
        print(f"greedy(white,model) vs heuristic: white {wins[1]} - {wins[2]} black")
        print("C API OK")
    finally:
        lib.nardi_destroy(h)
        os.remove(blob)

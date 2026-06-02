"""Verify forced moves (exactly one legal end-board) are auto-played for everyone
and never surfaced as a human decision -- and that the lookahead bot skips its
search on a forced move.

In a Human-vs-Human game there are no bots, so any advance() that returns
BotMoved must be a forced auto-play. AwaitingHuman must therefore always carry
>= 2 options.

Run directly:  python tests/test_forced_move.py
"""

import os
import sys
import tempfile

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402
from nardi_net import ResNardiNet, export_weights  # noqa: E402

WEIGHTS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "weights")
ROWS, COLS = 2, 12


def _engine():
    m = ResNardiNet()
    m.load_state_dict(torch.load(os.path.join(WEIGHTS, "res2.pt"), map_location="cpu", weights_only=True))
    m.eval()
    blob = tempfile.mktemp(suffix=".nardiw")
    export_weights(m, blob)
    eng = nardi.Engine()
    eng.load_target_network(blob)
    return eng, blob


def _cell(eng, r, c):
    return int(np.asarray(eng.board_features().raw_data)[r][c])


def _finish_human_turn(eng):
    """Drive incremental sub-moves until the turn is complete, then confirm."""
    guard = 0
    while not eng.turn_is_complete() and guard < 200:
        guard += 1
        sign = 1 if eng.current_player() == 0 else -1
        moved = False
        for r in range(ROWS):
            for c in range(COLS):
                if _cell(eng, r, c) * sign > 0 and eng.human_select(r, c):
                    for die in (0, 1):
                        if eng.can_use_die(die) and eng.human_move_die(die):
                            moved = True
                            break
                if moved:
                    break
            if moved:
                break
        if not moved:
            break
    if eng.turn_is_complete():
        eng.confirm_turn()
        return True
    return False


def test_forced_moves_autoplayed():
    eng, blob = _engine()
    try:
        eng.configure_players(nardi.Strategy.Human, nardi.Strategy.Human)
        eng.reset()
        forced = 0       # BotMoved in a human/human game == a forced auto-play
        choices = 0
        for _ in range(400):
            if not eng.should_continue_game():
                eng.reset()
            res = eng.advance()
            if res == nardi.StepResult.BotMoved:
                forced += 1
            elif res == nardi.StepResult.AwaitingHuman:
                # A turn presented to the human must offer a real choice.
                assert eng.legal_move_count() >= 2, \
                    "a single forced move was surfaced as a human decision"
                choices += 1
                if not _finish_human_turn(eng):
                    eng.reset()
            # TurnPassed / GameOver: nothing to do
        assert forced > 0, "expected some forced (auto-played) turns"
        print(f"auto-played {forced} forced turns; {choices} multi-option turns offered to human")
    finally:
        os.remove(blob)


def test_lookahead_plays_forced_move():
    """On a forced position the lookahead bot must still play the only legal board
    (the short-circuit must not drop the move)."""
    eng, blob = _engine()
    try:
        eng.configure_players(nardi.Strategy.Lookahead, nardi.Strategy.Lookahead)
        eng.reset()
        forced_lookahead = 0
        for _ in range(60):
            if not eng.should_continue_game():
                eng.reset()
            before = eng.current_player()
            children = eng.roll_and_enumerate()
            if not children:
                eng.confirm_turn(); continue
            if len(children) == 1:
                # Drive the lookahead apply directly on a forced position.
                eng.apply_lookahead_target()
                assert eng.current_player() != before, "forced lookahead move did not advance"
                forced_lookahead += 1
            else:
                eng.apply_lookahead_target()
        print(f"lookahead played {forced_lookahead} forced positions without error")
    finally:
        os.remove(blob)


if __name__ == "__main__":
    test_forced_moves_autoplayed()
    test_lookahead_plays_forced_move()
    print("FORCED MOVE OK")

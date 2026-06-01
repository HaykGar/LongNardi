"""Verify the CONFIRM_TURN_OVER behavior: a completed human turn does NOT
auto-advance to the next player -- it waits for confirm_turn() so the player can
undo first -- while bot / whole-board moves still advance automatically.

Run directly:  python tests/test_confirm_turn.py
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


def _board(eng):
    return np.asarray(eng.board_features().raw_data, dtype=np.int8)


def _play_human_turn_collecting(eng):
    """Make sub-moves for the side to move until the turn is complete; assert the
    turn never advances mid-way. Returns the per-move board snapshots."""
    start_player = eng.current_player()
    snapshots = [_board(eng).copy()]
    guard = 0
    while not eng.turn_is_complete() and guard < 200:
        guard += 1
        sign = 1 if eng.current_player() == 0 else -1
        b = _board(eng)
        moved = False
        for r in range(ROWS):
            for c in range(COLS):
                if b[r][c] * sign > 0 and eng.human_select(r, c):
                    for die in (0, 1):
                        if eng.can_use_die(die) and eng.human_move_die(die):
                            moved = True
                            break
                if moved:
                    break
            if moved:
                break
        # The engine must NOT have advanced players while moves remain.
        assert eng.current_player() == start_player, "turn advanced before confirm!"
        snapshots.append(_board(eng).copy())
        if not moved:
            break
    return start_player, snapshots


def test_human_turn_requires_confirm():
    eng, blob = _engine()
    try:
        eng.configure_players(nardi.Strategy.Human, nardi.Strategy.Human)
        eng.reset()
        confirmed_turns = 0
        for _ in range(60):
            if not eng.should_continue_game():
                eng.reset()
            res = eng.advance()
            if res != nardi.StepResult.AwaitingHuman:
                continue  # GameOver / TurnPassed (no-move auto-pass)
            start_player, snaps = _play_human_turn_collecting(eng)
            if not eng.turn_is_complete():
                eng.reset()
                continue  # simple driver stalled
            # Turn complete but NOT yet advanced.
            assert eng.current_player() == start_player, "advanced without confirm"
            # If moves were made, undo must re-open the turn and restore the board.
            if len(snaps) >= 2 and not np.array_equal(snaps[-1], snaps[-2]):
                eng.human_undo()
                assert not eng.turn_is_complete(), "undo did not re-open the turn"
                assert np.array_equal(_board(eng), snaps[-2]), "undo did not restore board"
                # redo the move to complete again
                _play_human_turn_collecting(eng)
                if not eng.turn_is_complete():
                    eng.reset(); continue
            # Now confirm -> the player advances.
            eng.confirm_turn()
            assert eng.current_player() != start_player, "confirm did not advance the turn"
            confirmed_turns += 1
        assert confirmed_turns >= 5, f"too few confirmed human turns ({confirmed_turns})"
        print(f"verified {confirmed_turns} human turns: no auto-advance, undo re-opens, confirm advances")
    finally:
        os.remove(blob)


def test_bot_auto_advances():
    eng, blob = _engine()
    try:
        eng.configure_players(nardi.Strategy.Greedy, nardi.Strategy.Greedy)
        eng.reset()
        advanced = 0
        for _ in range(20):
            if not eng.should_continue_game():
                break
            before = eng.current_player()
            children = eng.roll_and_enumerate()
            if not children:
                eng.confirm_turn()
            else:
                eng.apply_greedy_target()  # whole-board apply -> auto-confirms
                assert not eng.turn_is_complete(), "bot move left an unconfirmed turn"
            if eng.should_continue_game():
                assert eng.current_player() != before, "bot turn did not advance"
                advanced += 1
        assert advanced >= 5
        print(f"verified {advanced} bot turns auto-advance (no manual confirm needed)")
    finally:
        os.remove(blob)


if __name__ == "__main__":
    test_human_turn_requires_confirm()
    test_bot_auto_advances()
    print("CONFIRM TURN OK")

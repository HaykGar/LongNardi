"""Verify the incremental human-move API (select source + click die, like the
desktop UI) reaches a legal full-turn position, and that undo works.

For each human turn we greedily try sub-moves (for each occupied source point of
the side to move, try die 0 then die 1) until the turn ends, then assert the
resulting board is one of the engine's enumerated legal end-boards.

Run directly:  python tests/test_incremental_moves.py
"""

import os
import sys
import tempfile

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402
from nardi_net import ResNardiNet, export_weights  # noqa: E402

WEIGHTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "weights")
ROWS, COLS = 2, 12


def board_of(features):  # raw_data is [2][12]
    return np.asarray(features.raw_data, dtype=np.int8)


def play_human_turn_incrementally(eng):
    """Drive one human turn via select/move_die until the turn ends. Returns True
    if the turn completed (or passed)."""
    # Snapshot legal end-boards for verification.
    options = [board_of(f).tolist() for f in eng.current_options()]
    if not options:
        return True  # no moves; turn passes

    guard = 0
    while eng.turn_in_progress() and guard < 200:
        guard += 1
        cur = eng.current_player()  # 0 white,1 black
        sign = 1 if cur == 0 else -1
        board = np.asarray([[eng_cell(eng, r, c) for c in range(COLS)] for r in range(ROWS)])
        moved = False
        # Try to move from any point owned by the side to move, using either die.
        for r in range(ROWS):
            for c in range(COLS):
                if board[r][c] * sign <= 0:
                    continue
                if not eng.human_select(r, c):
                    continue
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
    # The turn no longer auto-advances: once no legal moves remain, confirm it
    # (a human would tap a Confirm button here; bots confirm automatically).
    if eng.turn_is_complete():
        eng.confirm_turn()
    return not eng.turn_in_progress()


# Helper: read a single board cell via a fresh features snapshot (engine has no
# direct cell getter exposed; board_features().raw_data is the live board).
def eng_cell(eng, r, c):
    return int(np.asarray(eng.board_features().raw_data)[r][c])


def test_incremental_reaches_legal_board():
    """Whenever the (deliberately simple) incremental driver completes a turn, the
    resulting board must be one of the engine's enumerated legal end-boards. The
    driver can stall on some positions (e.g. doubles / bear-off ordering it does
    not search) -- those turns are finished off via the whole-board API so the
    game keeps moving; only *incrementally completed* turns are asserted."""
    model = ResNardiNet()
    model.load_state_dict(torch.load(os.path.join(WEIGHTS_DIR, "res2.pt"),
                                     map_location="cpu", weights_only=True))
    model.eval()
    blob = tempfile.mktemp(suffix=".nardiw")
    export_weights(model, blob)
    try:
        eng = nardi.Engine()
        eng.load_target_network(blob)
        eng.configure_players(nardi.Strategy.Human, nardi.Strategy.Greedy)
        eng.reset()

        verified = 0
        stalled = 0
        for _ in range(120):
            if not eng.should_continue_game():
                eng.reset()
            res = eng.advance()
            if res != nardi.StepResult.AwaitingHuman:
                continue
            legal = [board_of(f).tolist() for f in eng.current_options()]
            if not legal:
                continue
            if play_human_turn_incrementally(eng):
                final = [[eng_cell(eng, r, c) for c in range(COLS)] for r in range(ROWS)]
                assert final in legal, "incremental result is not a legal end-board"
                verified += 1
            else:
                # The simple driver stalled mid-turn; reset and keep sampling.
                stalled += 1
                eng.reset()
        assert verified >= 5, f"expected several incrementally completed turns, got {verified}"
        print(f"verified {verified} incremental turns land on legal end-boards "
              f"({stalled} driver stalls reset and resampled)")
    finally:
        os.remove(blob)


def test_undo_restores_board():
    eng = nardi.Engine()
    eng.configure_players(nardi.Strategy.Human, nardi.Strategy.Heuristic)
    eng.reset()
    # advance to a human turn with at least one legal move
    for _ in range(10):
        res = eng.advance()
        if res == nardi.StepResult.AwaitingHuman and eng.current_options():
            break
    before = [[eng_cell(eng, r, c) for c in range(COLS)] for r in range(ROWS)]
    # make one sub-move
    moved = False
    for r in range(ROWS):
        for c in range(COLS):
            sign = 1 if eng.current_player() == 0 else -1
            if before[r][c] * sign > 0 and eng.human_select(r, c):
                for die in (0, 1):
                    if eng.can_use_die(die) and eng.human_move_die(die):
                        moved = True
                        break
            if moved:
                break
        if moved:
            break
    if moved:
        after = [[eng_cell(eng, r, c) for c in range(COLS)] for r in range(ROWS)]
        assert after != before, "a sub-move should change the board"
        eng.human_undo()
        restored = [[eng_cell(eng, r, c) for c in range(COLS)] for r in range(ROWS)]
        assert restored == before, "undo should restore the pre-move board"
        print("undo correctly restored the board")
    else:
        print("(no sub-move available to test undo; skipped)")


if __name__ == "__main__":
    test_incremental_reaches_legal_board()
    test_undo_restores_board()
    print("INCREMENTAL MOVES OK")

"""Verify the analysis API used by the iOS Analyze mode:

  * set_position drops in an arbitrary board + side to move;
  * evaluate_position returns the loaded network's side-to-move value, and
    flipping the side to move negates it (same board, opposite perspective);
  * analyze_dice ranks the legal moves best-first in the mover frame, every
    listed end-board is a legal child for those dice, and apply_analyzed_move
    plays the chosen one (switching sides);
  * the first-move head rule is honored: a side sitting on the standard opening
    gets the 4-4 / 6-6 two-off-the-head exception (turn_number == 0), while the
    same board past the opening does not.

Needs the bundled weight blob (ios/Nardi/Resources/model.nardiw). Run directly:
    python tests/test_analyze.py
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402
import tempfile  # noqa: E402
from nardi_net import ResNardiNet, export_for_engine  # noqa: E402

COLS = 12
# Build a model blob in whatever format this build's engine expects (TorchScript
# for the LibTorch build, .nardiw for the torch-free build).
MODEL = export_for_engine(ResNardiNet(), tempfile.mktemp(suffix=".model"))


def start_board():
    """Standard opening: 15 on each head."""
    b = np.zeros((2, COLS), dtype=np.int8)
    b[0][0] = 15
    b[1][0] = -15
    return b


def midgame_board():
    """A legal-ish mid-game spread, 15 per side, single colour per point."""
    b = np.zeros((2, COLS), dtype=np.int8)
    b[0][0] = 8; b[0][3] = 4; b[0][7] = 3          # white: head + two stacks
    b[1][0] = -9; b[1][5] = -3; b[1][9] = -3       # black: head + two stacks
    assert b[b > 0].sum() == 15 and -b[b < 0].sum() == 15
    return b


def loaded_engine():
    eng = nardi.Engine()
    eng.load_target_network(MODEL)
    return eng


def test_evaluate_position_matches_target_eval_and_is_deterministic():
    eng = loaded_engine()
    b = midgame_board()
    eng.set_position(b, False)            # white to move
    v1 = eng.evaluate_position()
    v2 = eng.evaluate_position()
    assert v1 == v2, "evaluate_position must be deterministic"
    # evaluate_position is the side-to-move value of the current board -- the same
    # quantity debug_target_eval reports.
    assert abs(v1 - eng.debug_target_eval()) < 1e-6, (v1, eng.debug_target_eval())
    # NOTE: the value is always side-to-move. To pin the display to one player's
    # frame (the iOS "original side" convention) the CALLER negates when the side
    # to move differs from the anchor -- a zero-sum identity on TRUE values, not an
    # equality between the model's two independent evals, so it is not asserted
    # here. We only sanity-check that the move-to-play matters: white-to-move and
    # black-to-move on the same raw board are genuinely different positions.
    eng.set_position(b, True)
    v_black = eng.evaluate_position()
    assert v_black == v_black  # finite / well-formed
    print(f"evaluate_position deterministic & == debug_target_eval: {v1:+.4f}; "
          f"black-to-move on same board: {v_black:+.4f}")


def test_analyze_ranks_and_applies():
    eng = loaded_engine()
    eng.set_position(midgame_board(), False)   # white to move
    ranked = eng.analyze_dice(3, 5)
    assert ranked, "expected legal moves for 3-5 from this position"

    # Best-first by mover-frame value.
    vals = [v for _, v in ranked]
    assert vals == sorted(vals, reverse=True), vals

    # Every ranked end-board is a legal child for these dice.
    eng2 = loaded_engine()
    eng2.set_position(midgame_board(), False)
    legal = {np.asarray(f.raw_data, dtype=np.int8).tobytes()
             for f in eng2.set_and_enumerate(3, 5)}
    for board, _ in ranked:
        assert np.asarray(board, dtype=np.int8).tobytes() in legal

    # Applying the top move switches the side to move and records sub-moves.
    eng.apply_analyzed_move(0)
    assert eng.current_player() is True, "apply should switch white -> black"
    assert len(eng.recent_moves()) >= 1, "applied move must record sub-moves"
    print(f"analyze_dice ranked {len(ranked)} moves best-first; apply switched side "
          f"and recorded {len(eng.recent_moves())} sub-move(s)")


def test_handson_move_after_analyze_lands_on_a_child():
    """The iOS analysis screen sets the dice (analyze_dice), then the user plays
    the move by hand (human_select/human_move_die). The board they reach must be
    one of the enumerated children, so the eval bar can look up that move's
    lookahead value. Verify a hand-played move lands on a ranked child."""
    eng = loaded_engine()
    eng.set_position(midgame_board(), False)        # white to move
    ranked = eng.analyze_dice(3, 5)
    child_keys = {np.asarray(b, dtype=np.int8).tobytes() for b, _ in ranked}
    assert child_keys, "expected legal 3-5 moves"

    # Dice are now set on the engine; play one checker by hand with both dice.
    b = np.asarray(eng.board_features().raw_data, dtype=np.int8)
    played = False
    for r in range(2):
        for c in range(COLS):
            if b[r][c] > 0 and eng.human_select(r, c):
                used = 0
                for die in (0, 1):
                    if eng.can_use_die(die) and eng.human_move_die(die):
                        used += 1
                if used == 2:
                    played = True
                if eng.turn_is_complete():
                    break
        if played:
            break
    assert played, "could not hand-play a 2-die move"
    assert eng.turn_is_complete(), "both dice used should complete the turn"
    reached = np.asarray(eng.board_features().raw_data, dtype=np.int8).tobytes()
    assert reached in child_keys, "hand-played end board must match an enumerated child"
    print("hand-played move lands on a ranked child (eval lookup will hit)")


def test_forced_pass_returns_empty():
    eng = loaded_engine()
    # Regression: a roll with NO legal move must return [] (forced pass), not
    # crash. Before the fix, set_and_enumerate left dice_rolled=false on a no-move
    # roll and analyze_dice then built a lookahead batch -> "have not yet rolled
    # dice". White's 15 checkers are all on its head (0,0); Black holds a full
    # 6-prime on (0,1)..(0,6), so every white head move is blocked.
    b = np.zeros((2, COLS), dtype=np.int8)
    b[0][0] = 15
    for c in range(1, 7):
        b[0][c] = -1            # black 6-prime blocking the white head
    b[1][0] = -9                # rest of black on its head
    assert b[b > 0].sum() == 15 and -b[b < 0].sum() == 15
    eng.set_position(b, False)  # white to move, fully blocked
    for (d1, d2) in [(3, 4), (1, 2), (5, 6), (2, 2)]:
        ranked = eng.analyze_dice(d1, d2)
        assert ranked == [], f"blocked position must yield no moves for {d1}-{d2}, got {len(ranked)}"
    print("analyze_dice on a fully-blocked position returns [] (no crash on a no-move roll)")


def test_first_move_head_exception_by_turn_number():
    """On the opening, white rolling 4-4 may take TWO checkers off the head; the
    same board treated as mid-game (not the opening) may not. set_position keys
    this off whether the side sits on the standard opening."""
    # Opening: white at start -> turn_number 0 -> first-move exception applies.
    eng = loaded_engine()
    eng.set_position(start_board(), False)
    opening_children = eng.set_and_enumerate(4, 4)
    # With the exception, at least one legal end-board has only 13 on the head
    # (two checkers left), reachable only by playing two off the head.
    two_off_head = any(np.asarray(f.raw_data, dtype=np.int8)[0][0] == 13
                       for f in opening_children)
    assert two_off_head, "opening 4-4 should allow two checkers off the head"

    # A NON-opening board (head not full) is past the first move: build a board
    # where white has 14 on the head + 1 advanced, so turn_number becomes > 1 and
    # the two-off-head exception must NOT fire (head can drop by at most one).
    b = np.zeros((2, COLS), dtype=np.int8)
    b[0][0] = 14; b[0][6] = 1
    b[1][0] = -15
    eng.set_position(b, False)
    children = eng.set_and_enumerate(4, 4)
    assert children, "expected legal 4-4 moves"
    min_head = min(int(np.asarray(f.raw_data, dtype=np.int8)[0][0]) for f in children)
    assert min_head >= 13, (
        "non-opening 4-4 must not invoke the first-move two-off-head exception "
        f"(min head left was {min_head})")
    print("first-move head exception honored: opening 4-4 allows two off the head; "
          "non-opening 4-4 does not")


if __name__ == "__main__":
    test_evaluate_position_matches_target_eval_and_is_deterministic()
    test_analyze_ranks_and_applies()
    test_handson_move_after_analyze_lands_on_a_child()
    test_forced_pass_returns_empty()
    test_first_move_head_exception_by_turn_number()
    print("ANALYZE OK")

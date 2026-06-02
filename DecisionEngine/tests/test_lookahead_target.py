"""Guard the separation between lookahead-as-move-selection and lookahead-as-
training-target:

  * As a MOVE STRATEGY, a single legal end-board must short-circuit (no search) --
    verified in test_forced_move.py (apply_lookahead_target plays forced moves).
  * As a TD TRAINING TARGET, make_lookahead_batch() must STILL build the full
    one-ply expansion even for a single legal move, because train.py bootstraps
    the value target from those replies (that's value estimation, not move
    choice). This test asserts the target builder is NOT short-circuited.

Run directly:  python tests/test_lookahead_target.py
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402


def test_training_lookahead_not_shortcircuited():
    eng = nardi.Engine()
    eng.reset()
    forced = 0          # positions with exactly one legal end-board
    forced_expanded = 0 # of those, how many still expanded the opponent replies
    steps = 0
    while forced < 12 and steps < 6000:
        steps += 1
        # Endgame scenarios produce forced positions far more often than the open.
        if steps % 11 == 0:
            eng.config().withRandomEndgame(bool(steps % 2))
        elif not eng.should_continue_game():
            eng.reset()

        children = eng.roll_and_enumerate()
        if len(children) == 0:
            eng.confirm_turn()
            continue
        if len(children) == 1:
            forced += 1
            # The TRAINING target builder. Must still build the full tree.
            batch = eng.make_lookahead_batch()
            assert batch.num_children == 1, "batch should hold the single forced child"
            # A non-terminal forced move still expands all 21 dice replies, so the
            # lookahead value target is computed in full (num_eval_features > 0).
            # An immediate winning move is correctly reduced to its terminal value
            # (num_eval_features == 0); both are valid full-lookahead targets.
            if batch.num_eval_features > 0:
                forced_expanded += 1
            eng.apply_greedy_board(np.zeros(1, dtype=np.float32))  # play the only move
        else:
            eng.apply_random_board()

    assert forced >= 3, f"did not encounter enough forced positions ({forced})"
    assert forced_expanded >= 1, (
        "make_lookahead_batch() short-circuited a forced move -- training targets "
        "would be broken")
    print(f"forced positions: {forced}; with full one-ply expansion: {forced_expanded} "
          f"(training lookahead targets intact)")


if __name__ == "__main__":
    test_training_lookahead_not_shortcircuited()
    print("LOOKAHEAD TARGET OK")

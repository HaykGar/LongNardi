"""Verify the per-sub-move log (recent_moves) that drives the iOS sequential
animation:

  * a whole-board (bot/forced) apply records its sub-moves IN ORDER, and replaying
    them on the pre-move board reproduces the post-move board;
  * a checker played with both dice is recorded as two chained hops
    (move[1].from == move[0].to) -- i.e. the intermediate landing is captured;
  * a single incremental human move records exactly one sub-move.

Run directly:  python tests/test_move_log.py
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402

COLS = 12


def board_arr(eng):
    return np.asarray(eng.board_features().raw_data, dtype=np.int8).copy()


def replay(old, moves, sign):
    """Apply the recorded sub-moves to `old` and return the resulting board."""
    b = old.copy()
    for fr, fc, tr, tc in moves:
        if fr >= 0:
            b[fr][fc] -= sign          # lift off source (sign = mover's sign)
        if tr >= 0:
            b[tr][tc] += sign          # land on dest (off-board => bear off)
    return b


def test_whole_board_submoves_reproduce_transition():
    eng = nardi.Engine()
    eng.reset()
    checked = 0
    chained = 0
    for _ in range(150):
        if not eng.should_continue_game():
            eng.reset()
        children = eng.roll_and_enumerate()
        if not children:
            eng.confirm_turn()
            continue
        old = board_arr(eng)
        eng.apply_random_board()                  # whole-board apply records sub-moves
        moves = eng.recent_moves()
        assert moves, "a whole-board apply must record at least one sub-move"
        # mover sign from the first real source cell on the OLD board
        sign = 0
        for fr, fc, _, _ in moves:
            if fr >= 0 and old[fr][fc] != 0:
                sign = 1 if old[fr][fc] > 0 else -1
                break
        assert sign != 0
        new = board_arr(eng)
        assert np.array_equal(replay(old, moves, sign), new), \
            "replaying recorded sub-moves must reproduce the board transition"
        # chained hops: a checker that used both dice lands then moves again
        for i in range(1, len(moves)):
            if (moves[i][0], moves[i][1]) == (moves[i - 1][2], moves[i - 1][3]) and moves[i][0] >= 0:
                chained += 1
                break
        checked += 1
    assert checked > 0
    assert chained > 0, "expected some turns where one checker is played with both dice"
    print(f"verified {checked} whole-board transitions via sub-move replay; "
          f"{chained} had chained two-hop moves")


def test_single_human_move_is_one_submove():
    eng = nardi.Engine()
    eng.configure_players(nardi.Strategy.Human, nardi.Strategy.Human)
    eng.reset()
    seen = 0
    for _ in range(40):
        res = eng.advance()
        if res != nardi.StepResult.AwaitingHuman:
            continue
        # make one sub-move and check the log has exactly one entry
        sign = 1 if eng.current_player() == 0 else -1
        b = board_arr(eng)
        done = False
        for r in range(2):
            for c in range(COLS):
                if b[r][c] * sign > 0 and eng.human_select(r, c):
                    for die in (0, 1):
                        if eng.can_use_die(die) and eng.human_move_die(die):
                            assert len(eng.recent_moves()) == 1, \
                                "one human die move must record exactly one sub-move"
                            seen += 1
                            done = True
                            break
                if done:
                    break
            if done:
                break
        # finish/confirm the turn to move on
        if eng.turn_is_complete():
            eng.confirm_turn()
        elif not done:
            eng.reset()
    assert seen >= 3, f"did not exercise enough human sub-moves ({seen})"
    print(f"verified {seen} single human moves each record one sub-move")


if __name__ == "__main__":
    test_whole_board_submoves_reproduce_transition()
    test_single_human_move_is_one_submove()
    print("MOVE LOG OK")

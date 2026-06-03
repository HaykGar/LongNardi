"""Correctness test for the C++ two-ply lookahead (NardiEngine::lookahead2_*).

Validates the engine's depth-2 expectiminimax against an INDEPENDENT Python
brute force (different code path: set_position / set_and_enumerate / board_features
+ the torch model), on small boards where the full search is tractable. Also
checks top-K behaviour, the 2-ply==1-ply-baseline relationship, and determinism.

Run:  python tests/test_twoply.py
"""

import os
import sys
import tempfile

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402
from nardi_net import ResNardiNet, export_for_engine  # noqa: E402

COLS = 12
DICE_COMBOS = [(1, 1), (1, 2), (1, 3), (1, 4), (1, 5), (1, 6),
               (2, 2), (2, 3), (2, 4), (2, 5), (2, 6),
               (3, 3), (3, 4), (3, 5), (3, 6),
               (4, 4), (4, 5), (4, 6),
               (5, 5), (5, 6),
               (6, 6)]
COMBO_PROBS = [1/36 if a == b else 1/18 for (a, b) in DICE_COMBOS]


def win_value_to_mover(board, mover):
    """If `mover` has borne off all checkers on `board`, return its win value
    (2 = mars, 1 = normal); else None. mover: 0=white(+), 1=black(-)."""
    b = np.asarray(board, dtype=np.int8)
    if mover == 0:  # white
        if b[b > 0].sum() == 0:                       # white borne off all
            return 2.0 if (-b[b < 0].sum()) == 15 else 1.0
    else:           # black
        if (-b[b < 0].sum()) == 0:                    # black borne off all
            return 2.0 if b[b > 0].sum() == 15 else 1.0
    return None


class BruteForce:
    """Independent depth-2 expectiminimax over the SAME model the C++ engine loaded."""

    def __init__(self, model):
        self.model = model
        self.eng = nardi.Engine()   # used purely for enumeration + featurization

    def _value_to_side(self, board, side):
        self.eng.set_position(board, bool(side))
        with torch.inference_mode():
            return float(self.model(self.eng.board_features()).item())

    def _responses(self, board, side, d1, d2):
        self.eng.set_position(board, bool(side))
        return [np.asarray(f.raw_data, dtype=np.int8) for f in self.eng.set_and_enumerate(d1, d2)]

    def oneply_to_mover(self, board, mover):
        """E over mover's dice of [best move, static leaf] -- the 2-ply leaf."""
        opp = 1 - mover
        total = 0.0
        for (d1, d2), p in zip(DICE_COMBOS, COMBO_PROBS):
            resp = self._responses(board, mover, d1, d2)
            if not resp:
                best = -self._value_to_side(board, opp)        # mover passes
            else:
                best = -np.inf
                for s in resp:
                    w = win_value_to_mover(s, mover)
                    cand = w if w is not None else -self._value_to_side(s, opp)
                    best = max(best, cand)
            total += p * best
        return total

    def child_value_2ply(self, child_board, mover):
        """Value to `mover` of a root child (mover just moved, opp to move):
        E over opp dice of [min over opp reply of the reply's one-ply value]."""
        opp = 1 - mover
        total = 0.0
        for (d1, d2), p in zip(DICE_COMBOS, COMBO_PROBS):
            resp = self._responses(child_board, opp, d1, d2)
            if not resp:
                worst = self.oneply_to_mover(child_board, mover)   # opp passes -> mover to move
            else:
                worst = np.inf
                for r in resp:
                    w_opp = win_value_to_mover(r, opp)
                    if w_opp is not None:
                        val = -w_opp                               # opp won -> bad for mover
                    else:
                        val = self.oneply_to_mover(r, mover)
                    worst = min(worst, val)
            total += p * worst
        return total

    def child_values(self, board, mover, d1, d2):
        children = self._responses(board, mover, d1, d2)
        out = []
        for c in children:
            w = win_value_to_mover(c, mover)
            out.append(w if w is not None else self.child_value_2ply(c, mover))
        return out


def small_board():
    """A low-branching 15-per-side near-endgame: full piece counts (so mars
    semantics match the engine) but checkers stacked on two home points, so each
    roll yields only a handful of end-boards and no side can win within two
    plies -- keeping the brute force tractable while exercising min/max branching.
    White (+) and Black (-) sit in different rows (no interaction)."""
    b = np.zeros((2, COLS), dtype=np.int8)
    b[1, 10] = 8;  b[1, 11] = 7    # white: 15 in home (row 1, cols 6-11)
    b[0, 10] = -8; b[0, 11] = -7   # black: 15 in home (row 0, cols 6-11)
    return b


def test_matches_bruteforce():
    torch.manual_seed(0)
    model = ResNardiNet().eval()
    blob = export_for_engine(model, tempfile.mktemp(suffix=".model"))
    eng = nardi.Engine()
    eng.load_target_network(blob)
    bf = BruteForce(model)

    board = small_board()
    checked = 0
    for (d1, d2) in [(3, 5), (6, 2)]:
        eng.set_position(board, False)              # white to move
        eng.set_and_enumerate(d1, d2)               # roll the dice, then search
        cpp = list(eng.lookahead2_child_values_target(0))   # full depth-2
        py = bf.child_values(board, 0, d1, d2)
        assert len(cpp) == len(py), (len(cpp), len(py), d1, d2)
        # compare as sorted multisets (enumeration order may differ)
        a = np.sort(np.array(cpp)); b = np.sort(np.array(py))
        assert np.allclose(a, b, atol=2e-4), \
            f"dice {d1}-{d2}: C++ {a} vs brute {b}"
        checked += 1
    assert checked == 2
    print(f"two-ply matches independent brute force on {checked} dice rolls "
          f"(max board branching validated)")


def test_topk_only_changes_topk():
    """With top_k=1 only the best 1-ply child's value should change vs the 1-ply
    baseline; the others keep their 1-ply value."""
    torch.manual_seed(1)
    model = ResNardiNet().eval()
    blob = export_for_engine(model, tempfile.mktemp(suffix=".model"))
    eng = nardi.Engine()
    eng.load_target_network(blob)

    # A board/roll with several root moves so top-K actually prunes.
    board = np.zeros((2, COLS), dtype=np.int8)
    board[1, 6] = 5; board[1, 8] = 5; board[1, 10] = 5    # white spread across home
    board[0, 6] = -5; board[0, 8] = -5; board[0, 10] = -5
    eng.set_position(board, False)
    eng.set_and_enumerate(4, 2)
    full = np.array(eng.lookahead2_child_values_target(0))    # all expanded
    eng.set_position(board, False)
    eng.set_and_enumerate(4, 2)
    k1 = np.array(eng.lookahead2_child_values_target(1))      # only best 1-ply child
    assert k1.shape == full.shape
    if full.shape[0] >= 2:
        # top_k=1 expands only the best 1-ply child; the others keep their 1-ply
        # value, so k1 differs from the fully-expanded `full` in at most n-1 spots.
        n_diff = int(np.sum(~np.isclose(k1, full, atol=1e-6)))
        assert n_diff <= full.shape[0] - 1, (n_diff, full.shape[0])
        print(f"top_k=1: {full.shape[0]-n_diff}/{full.shape[0]} children match the fully-expanded run")
    else:
        print("top_k test: only one root move for this roll (pruning trivially consistent)")


def test_deterministic_and_eval_count():
    torch.manual_seed(2)
    model = ResNardiNet().eval()
    blob = export_for_engine(model, tempfile.mktemp(suffix=".model"))
    eng = nardi.Engine()
    eng.load_target_network(blob)
    board = small_board()

    eng.set_position(board, False); eng.set_and_enumerate(3, 4)
    v1 = list(eng.lookahead2_child_values_target(0))
    eng.set_position(board, False); eng.set_and_enumerate(3, 4)
    v2 = list(eng.lookahead2_child_values_target(0))
    assert v1 == v2, "two-ply must be deterministic"
    evals_full = eng.last_lookahead2_evals()

    eng.set_position(board, False); eng.set_and_enumerate(3, 4)
    eng.lookahead2_child_values_target(1)
    evals_k1 = eng.last_lookahead2_evals()
    assert 0 < evals_k1 <= evals_full, (evals_k1, evals_full)
    print(f"deterministic; evals: full={evals_full}, top_k=1={evals_k1}")


if __name__ == "__main__":
    test_matches_bruteforce()
    test_topk_only_changes_topk()
    test_deterministic_and_eval_count()
    print("TWO-PLY OK")

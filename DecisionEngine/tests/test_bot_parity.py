"""Bot-decision parity: the in-C++ greedy / 1-ply-lookahead bots (which evaluate
the model with the hand-rolled InferenceNet) must choose the same move as the
PyTorch model.

Selection (terminal-win shortcut + argmax / min-over-replies aggregation) is the
same C++ code regardless of where the model runs, so this reduces to: does
selection on net-produced values match selection on model-produced values for the
*same* enumerated options?  We therefore always compare on a single shared
enumeration/batch. (Note: the engine re-enumerates per call and the child order
is not stable across separate builds, so cross-build index comparison is invalid
by construction -- not a correctness issue.)

Run directly:  python tests/test_bot_parity.py
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
VALUE_TOL = 1e-4


def _terminal_child_index(children):
    # Mirrors C++ terminal_value_for_side_to_move: terminal when the side-to-move
    # has no pieces left on the board.
    for i, f in enumerate(children):
        if int(np.asarray(f.player.occ).sum()) == 0:
            return i
    return None


def _setup():
    model = ResNardiNet()
    model.load_state_dict(torch.load(os.path.join(WEIGHTS_DIR, "res2.pt"),
                                     map_location="cpu", weights_only=True))
    model.eval()
    blob = tempfile.mktemp(suffix=".nardiw")
    export_weights(model, blob)
    net = nardi.InferenceNet(blob)
    eng = nardi.Engine()
    eng.reset()
    eng.load_target_network(blob)
    return model, net, eng, blob


def check_greedy(n_turns=800):
    """The engine's greedy_choice_target reuses the just-rolled _last_children
    list, so its index lines up with the Python model's argmax over that same
    list -- a direct, valid comparison."""
    model, net, eng, blob = _setup()
    try:
        exact = total = 0
        for _ in range(n_turns):
            if not eng.should_continue_game():
                eng.reset()
            children = eng.roll_and_enumerate()
            if not children:
                eng.confirm_turn()   # no moves: pass to the next player
                continue
            t = _terminal_child_index(children)
            if t is not None:
                py_idx, vals = t, None
            else:
                vals = model(children).detach().cpu().numpy().astype(np.float64)
                py_idx = int(np.argmax(vals))
            cpp_idx = eng.greedy_choice_target()
            total += 1
            exact += (py_idx == cpp_idx)
            if vals is not None:
                assert abs(vals[cpp_idx] - vals[py_idx]) < VALUE_TOL, (
                    f"greedy chose a worse move: cpp={vals[cpp_idx]} py={vals[py_idx]}")
            eng.apply_random_board()
        return exact, total
    finally:
        os.remove(blob)


def check_lookahead(n_turns=500):
    """Compare model-valued vs net-valued selection on a SINGLE shared batch."""
    model, net, eng, blob = _setup()
    try:
        exact = total = 0
        for _ in range(n_turns):
            if not eng.should_continue_game():
                eng.reset()
            children = eng.roll_and_enumerate()
            if not children:
                eng.confirm_turn()   # no moves: pass to the next player
                continue
            batch = eng.make_lookahead_batch()
            if batch.num_children == 0:
                eng.apply_random_board()
                continue

            x = torch.from_numpy(batch.tensor("conv", False)).float()
            with torch.inference_mode():
                v_model = model.value_from_tensor(x).detach().cpu().numpy().astype(np.float32)
            v_net = np.asarray(net.evaluate_batch(batch.eval_features), dtype=np.float32)

            idx_model = batch.best_index(v_model)
            idx_net = batch.best_index(v_net)
            cv = np.asarray(batch.child_values(v_model), dtype=np.float64)

            total += 1
            exact += (idx_model == idx_net)
            assert abs(cv[idx_net] - cv[idx_model]) < VALUE_TOL, (
                f"lookahead chose a worse move: net={cv[idx_net]} model={cv[idx_model]}")
            eng.apply_random_board()
        return exact, total
    finally:
        os.remove(blob)


def smoke_full_games(n_games=4):
    """Drive complete games purely through the engine's in-C++ bot apply paths
    (greedy and lookahead vs heuristic) to confirm they run end to end."""
    _, _, eng, blob = _setup()
    try:
        finished = 0
        for g in range(n_games):
            eng.reset()
            use_lookahead = bool(g % 2)
            turns = 0
            while eng.should_continue_game() and turns < 2000:
                children = eng.roll_and_enumerate()
                if children:
                    if eng.sign() == 1:
                        eng.apply_lookahead_target() if use_lookahead else eng.apply_greedy_target()
                    else:
                        eng.apply_heuristic_board()
                turns += 1
            finished += eng.is_terminal()
        return finished, n_games
    finally:
        os.remove(blob)


def test_greedy_parity():
    exact, total = check_greedy()
    assert total > 0 and exact == total  # greedy index match must be exact


def test_lookahead_parity():
    exact, total = check_lookahead()
    assert total > 0


def test_full_games_run():
    finished, n = smoke_full_games()
    assert finished == n


if __name__ == "__main__":
    ge, gt = check_greedy()
    le, lt = check_lookahead()
    fin, n = smoke_full_games()
    print(f"greedy   : {gt} turns, exact index match {ge}/{gt} ({100*ge/gt:.1f}%)")
    print(f"lookahead: {lt} turns, exact index match {le}/{lt} ({100*le/lt:.1f}%), "
          f"chosen-move value within {VALUE_TOL} always")
    print(f"full games via in-C++ bots: {fin}/{n} finished")
    print("BOT PARITY OK")

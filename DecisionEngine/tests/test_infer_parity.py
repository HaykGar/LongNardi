"""Parity test: hand-rolled C++ inference (nardi_infer.cpp, exposed as
nardi.InferenceNet) must match the PyTorch models in nardi_net.py to within
float tolerance.

This guards against architecture drift between training (PyTorch) and the
torch-free C++ inference shipped to other platforms. Run directly

    python tests/test_infer_parity.py

or under pytest.
"""

import glob
import os
import sys
import tempfile

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nardi  # noqa: E402
from nardi_net import NardiNet, ConvNardiNet, ResNardiNet, export_weights  # noqa: E402

WEIGHTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "weights")

# Map each tracked weight file to the architecture that produced it. Anything in
# weights/ not listed here is reported as skipped so new files get noticed.
ARCH_FOR_FILE = {
    "mw64_16.pt": lambda: NardiNet(64, 16),
    "conv_v1.pt": lambda: ConvNardiNet(),
    "deepconv_v1.pt": lambda: ConvNardiNet(extra_conv=True),
    "deepconv_v2.pt": lambda: ConvNardiNet(extra_conv=True),
    "res.pt": lambda: ResNardiNet(),
    "res2.pt": lambda: ResNardiNet(),
    "res2_2.pt": lambda: ResNardiNet(),
    "res2_3.pt": lambda: ResNardiNet(),
    "res2_4.pt": lambda: ResNardiNet(),
    "res2prime.pt": lambda: ResNardiNet(),
    "res_greedy.pt": lambda: ResNardiNet(),
    "res_lookahead.pt": lambda: ResNardiNet(),
}

ATOL = 1e-4
RTOL = 1e-4


def _load_model(factory, weight_file):
    model = factory()
    path = os.path.join(WEIGHTS_DIR, weight_file)
    model.load_state_dict(torch.load(path, map_location="cpu", weights_only=True))
    model.eval()
    return model


def _collect_positions(n_positions=1500, seed=0):
    """Build a diverse Features set: random midgame play (the common case) mixed
    with random endgame scenarios (high stacks, large pieces_off / pip_count /
    pieces_not_reached, saturated occupancy planes) so the per-row scalars and
    the third occupancy plane are exercised near their range."""
    import random
    random.seed(seed)
    np.random.seed(seed)

    eng = nardi.Engine()
    eng.reset()
    features = []
    steps = 0
    while len(features) < n_positions:
        # ~ every 25 steps, jump to a fresh random endgame position
        if steps % 25 == 0:
            eng.config().withRandomEndgame(bool(steps % 2))
        elif not eng.should_continue_game():
            eng.reset()

        options = eng.roll_and_enumerate()
        features.append(eng.board_features())
        features.extend(options)
        if options:
            eng.apply_random_board()
        steps += 1
    return features[:n_positions]


def _compare(model, features):
    """Return max abs diff between torch and C++ batch eval, and assert the C++
    single-position path matches its own batch path over ALL positions."""
    with tempfile.NamedTemporaryFile(suffix=".nardiw", delete=False) as tmp:
        blob_path = tmp.name
    try:
        export_weights(model, blob_path)
        cpp_net = nardi.InferenceNet(blob_path)

        with torch.inference_mode():
            torch_vals = model(features).detach().cpu().numpy().astype(np.float64)
        cpp_batch = np.asarray(cpp_net.evaluate_batch(features), dtype=np.float64)
        cpp_single = np.asarray([cpp_net.evaluate(f) for f in features], dtype=np.float64)

        # single vs batch must be bit-identical (same code path per row)
        np.testing.assert_array_equal(cpp_single, cpp_batch)

        diff = np.abs(torch_vals - cpp_batch)
        return diff.max(), torch_vals, cpp_batch
    finally:
        os.remove(blob_path)


def _all_weight_files():
    return sorted(os.path.basename(p) for p in glob.glob(os.path.join(WEIGHTS_DIR, "*.pt")))


def test_inference_parity():
    # multiple seeds -> different random position distributions
    feature_sets = [_collect_positions(seed=s) for s in (0, 1)]
    for weight_file in _all_weight_files():
        factory = ARCH_FOR_FILE.get(weight_file)
        assert factory is not None, (
            f"{weight_file} has no architecture mapping in ARCH_FOR_FILE; "
            "add it so it is parity-checked.")
        model = _load_model(factory, weight_file)
        for features in feature_sets:
            _, torch_vals, cpp_vals = _compare(model, features)
            np.testing.assert_allclose(cpp_vals, torch_vals, atol=ATOL, rtol=RTOL,
                                       err_msg=f"{weight_file}: C++/torch mismatch")


if __name__ == "__main__":
    feature_sets = [_collect_positions(seed=s) for s in (0, 1)]
    n_pos = sum(len(f) for f in feature_sets)
    print(f"Comparing all weights over {n_pos} positions "
          f"(midgame+endgame, atol={ATOL}, rtol={RTOL}):")
    known = set(ARCH_FOR_FILE)
    found = set(_all_weight_files())
    for extra in sorted(found - known):
        print(f"  [SKIP] {extra:38s} no architecture mapping")

    failures = 0
    for weight_file in _all_weight_files():
        factory = ARCH_FOR_FILE.get(weight_file)
        if factory is None:
            continue
        try:
            model = _load_model(factory, weight_file)
            worst = 0.0
            for features in feature_sets:
                d, _, _ = _compare(model, features)
                worst = max(worst, d)
            status = "OK" if worst <= ATOL else "WARN"
            print(f"  [{status:4s}] {weight_file:38s} max|diff| = {worst:.2e}")
            if worst > ATOL:
                failures += 1
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"  [FAIL] {weight_file:38s} {type(exc).__name__}: {exc}")
    print("ALL PASSED" if not failures else f"{failures} FAILURE(S)")
    sys.exit(1 if failures else 0)

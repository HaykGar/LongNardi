#!/usr/bin/env bash
# Build and run the standalone native C-API test in the same configuration the
# iOS static lib will use: NO pybind11 / Python AND NO SFML (NARDI_ENABLE_SFML
# left undefined, SFMLRW.cpp excluded, no -lsfml). If any engine-core header
# pulls in pybind or SFML, this fails -- exactly what we want to catch.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DE="$(cd "$HERE/../.." && pwd)"          # DecisionEngine/
CE="$(cd "$DE/../CoreEngine" && pwd)"    # CoreEngine/
PY="$DE/venv/bin/python"
BLOB="${TMPDIR:-/tmp}/native_model.nardiw"
BIN="${TMPDIR:-/tmp}/nardi_native_test"

echo "[1/3] exporting model blob via Python ..."
cd "$DE"
"$PY" - "$BLOB" <<'PYEOF'
import sys, torch
from nardi_net import ResNardiNet, export_weights
m = ResNardiNet()
m.load_state_dict(torch.load("weights/res2.pt", map_location="cpu", weights_only=True))
m.eval()
export_weights(m, sys.argv[1])
print("   wrote", sys.argv[1])
PYEOF

echo "[2/3] compiling native test (no pybind / no Python / no SFML) ..."
clang++ -std=c++23 -O2 -Wall -mmacosx-version-min=10.15 \
    -I"$DE" \
    "$HERE/test_c_api_native.cpp" \
    "$DE/nardi_c_api.cpp" \
    "$DE/nardi_engine.cpp" \
    "$DE/nardi_infer.cpp" \
    "$DE/nardi_core.cpp" \
    "$DE/lookahead_batch.cpp" \
    "$DE/mcts_node.cpp" \
    "$DE/scenario_config.cpp" \
    "$DE/target_model.cpp" \
    "$CE/Auxilaries.cpp" \
    "$CE/Board.cpp" \
    "$CE/Controller.cpp" \
    "$CE/Game.cpp" \
    "$CE/Monitors.cpp" \
    "$CE/ReaderWriter.cpp" \
    "$CE/TerminalRW.cpp" \
    "$CE/ScenarioBuilder.cpp" \
    -o "$BIN"

echo "[3/3] running ..."
"$BIN" "$BLOB"

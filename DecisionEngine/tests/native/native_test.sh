#!/usr/bin/env bash
# Build and run the standalone native C-API test. Compiles the engine core + C
# API with NO pybind11 / Python include path: if any engine-core header pulls in
# pybind, this fails -- which is exactly what we want to catch. SFML is linked
# only because nardi_engine still references the desktop SFMLRW factory; the iOS
# build will compile that out, but it is harmless here and does not involve Python.
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

echo "[2/3] compiling native test (no pybind / no Python) ..."
clang++ -std=c++23 -O2 -Wall -mmacosx-version-min=10.15 \
    -I"$DE" -I/opt/homebrew/include \
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
    "$CE/SFMLRW.cpp" \
    "$CE/ScenarioBuilder.cpp" \
    -L/opt/homebrew/lib -lsfml-graphics -lsfml-window -lsfml-system \
    -Wl,-rpath,/opt/homebrew/lib \
    -o "$BIN"

echo "[3/3] running ..."
"$BIN" "$BLOB"

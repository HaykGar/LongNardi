#!/usr/bin/env bash
# Compile the engine core (no pybind / no Python / no SFML) into a static lib and
# drive it from Swift via a Clang module map over the plain-C API. Demonstrates
# the iOS integration seam on macOS (full Xcode / iOS SDK not required here):
# Swift imports the C header directly -- no Objective-C++ shim needed.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DE="$(cd "$HERE/../../.." && pwd)"        # DecisionEngine/
CE="$(cd "$DE/../CoreEngine" && pwd)"     # CoreEngine/
PY="$DE/venv/bin/python"
WORK="${TMPDIR:-/tmp}/nardi_swift"
BLOB="$WORK/model.nardiw"
mkdir -p "$WORK"

echo "[1/4] exporting model blob ..."
cd "$DE"
"$PY" - "$BLOB" <<'PYEOF'
import sys, torch
from nardi_net import ResNardiNet, export_weights
m = ResNardiNet()
m.load_state_dict(torch.load("weights/res2.pt", map_location="cpu", weights_only=True))
m.eval(); export_weights(m, sys.argv[1])
PYEOF

echo "[2/4] compiling engine core -> libnardicore.a (no pybind/Python/SFML) ..."
SRCS=(
    "$DE/nardi_c_api.cpp" "$DE/nardi_engine.cpp" "$DE/nardi_infer.cpp" "$DE/nardi_core.cpp"
    "$DE/lookahead_batch.cpp" "$DE/mcts_node.cpp" "$DE/scenario_config.cpp" "$DE/target_model.cpp"
    "$CE/Auxilaries.cpp" "$CE/Board.cpp" "$CE/Controller.cpp" "$CE/Game.cpp" "$CE/Monitors.cpp"
    "$CE/ReaderWriter.cpp" "$CE/TerminalRW.cpp" "$CE/ScenarioBuilder.cpp"
)
OBJS=()
for src in "${SRCS[@]}"; do
    obj="$WORK/$(basename "${src%.cpp}").o"
    clang++ -std=c++23 -O2 -mmacosx-version-min=10.15 -I"$DE" -c "$src" -o "$obj" 2>/dev/null
    OBJS+=("$obj")
done
ar rcs "$WORK/libnardicore.a" "${OBJS[@]}"

echo "[3/4] generating module map + building Swift binary ..."
cat > "$WORK/module.modulemap" <<EOF
module NardiC {
    header "$DE/nardi_c_api.h"
    export *
}
EOF
swiftc -O -I "$WORK" "$HERE/main.swift" \
    -L "$WORK" -lnardicore -lc++ \
    -o "$WORK/nardi_swift_test"

echo "[4/4] running ..."
"$WORK/nardi_swift_test" "$BLOB"

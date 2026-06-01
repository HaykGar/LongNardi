#!/usr/bin/env bash
# Export a trained PyTorch model to the flat .nardiw weight blob the app bundles
# (DecisionEngine/nardi_net.export_weights). Regenerate this whenever you retrain.
#
# Usage: ./scripts/make_model.sh [weight_file]   (default: weights/res2.pt)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
IOS="$(cd "$HERE/.." && pwd)"
DE="$(cd "$IOS/../DecisionEngine" && pwd)"
WEIGHT="${1:-res2.pt}"
OUT="$IOS/Nardi/Resources/model.nardiw"
mkdir -p "$IOS/Nardi/Resources"

"$DE/venv/bin/python" - "$DE/weights/$WEIGHT" "$OUT" <<'PY'
import sys, torch
from nardi_net import ResNardiNet, export_weights
m = ResNardiNet()
m.load_state_dict(torch.load(sys.argv[1], map_location="cpu", weights_only=True))
m.eval()
export_weights(m, sys.argv[2])
print("wrote", sys.argv[2])
PY

#!/usr/bin/env bash
# Export the trained PyTorch value networks to the flat .nardiw blobs the app
# bundles (DecisionEngine/nardi_net.export_weights). Regenerate whenever you
# retrain, then run `xcodegen generate` so any new/renamed blob is picked up by
# the Xcode project (blobs are referenced individually and are gitignored).
#
# Produces two blobs in Nardi/Resources:
#   mlp.nardiw       <- weights/mlp.pt               (NardiNet / MLP) -> Medium (greedy)
#   polavg10.nardiw  <- weights/polAvg10_lookahead.pt (ResNardiNet)   -> Hard (1-ply
#                                                       lookahead) + all analysis
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
IOS="$(cd "$HERE/.." && pwd)"
DE="$(cd "$IOS/../DecisionEngine" && pwd)"
RES="$IOS/Nardi/Resources"
mkdir -p "$RES"

"$DE/venv/bin/python" - "$DE" "$RES" <<'PY'
import sys, os, torch
de, res = sys.argv[1], sys.argv[2]
sys.path.insert(0, de)
from nardi_net import NardiNet, ResNardiNet, export_weights

def export(model, pt, out):
    model.load_state_dict(torch.load(os.path.join(de, "weights", pt),
                                     map_location="cpu", weights_only=True))
    model.eval()
    export_weights(model, os.path.join(res, out))
    print("wrote", os.path.join(res, out))

# Medium = greedy over the small MLP; Hard / analysis = 1-ply lookahead over the
# Polyak-averaged ResNardiNet.
export(NardiNet(64, 16), "mlp.pt",                "mlp.nardiw")
export(ResNardiNet(),    "polAvg10_lookahead.pt", "polavg10.nardiw")
PY

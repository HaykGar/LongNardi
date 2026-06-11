#!/usr/bin/env bash
# Export the trained PyTorch value networks to the flat .nardiw blobs the app
# bundles (DecisionEngine/nardi_net.export_weights). Regenerate whenever you
# retrain. The three blobs below keep the same names, so they overwrite in place
# and the committed Xcode project picks them up automatically. If you add a
# new/renamed blob, drag it into the Nardi > Resources group in Xcode once.
#
# Produces three blobs in Nardi/Resources:
#   mlp.nardiw   <- weights/mlp.pt   (NardiNet / MLP)   -> Medium (greedy)
#   vzg0.nardiw  <- weights/vzg0.pt  (ResNardiNet)      -> Hard (1-ply lookahead)
#                                                          + analyzer/review MOVE SELECTION
#   res2.nardiw  <- weights/res2.pt  (ResNardiNet)      -> analyzer/review DISPLAY eval
#                  (well-calibrated: ~0 at the symmetric start, near-antisymmetric,
#                   so the eval graph is smooth; vzg0 is biased and swings)
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
export(NardiNet(64, 16), "mlp.pt",  "mlp.nardiw")
export(ResNardiNet(),    "vzg0.pt", "vzg0.nardiw")
export(ResNardiNet(),    "res2.pt", "res2.nardiw")
PY

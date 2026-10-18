# Nardi — iOS app

A SwiftUI front-end over the shared C++ engine core (game rules, search, and the
torch-free hand-rolled inference in `../DecisionEngine` + `../CoreEngine`). Swift
talks to the engine through the plain-C API (`nardi_c_api.h`) via a bridging
header — no Objective-C++ shim, no Python, no SFML.

## Build (command line)

Requires full Xcode and the iOS **simulator runtime** (Xcode ▸ Settings ▸
Components). `Nardi.xcodeproj` is checked in and edited directly; there is no
xcodegen spec.

```sh
ios/scripts/test.sh                  # unit tests (NardiTests); SIM="iPhone 17 Pro" to pick
ios/scripts/check_release_hooks.sh   # Release build has no dev launch-arg hooks
ios/scripts/screenshots.sh game="--game-mid 28"   # App Store screenshots; see its header
```

Dev launch-arg hooks (jump to a screen or state) live in the `#if DEBUG` blocks of
`ContentView.swift` and `GameReviewView.swift`.

Both scripts point at `/Applications/Xcode.app` when the active developer dir
is Command Line Tools; for other `xcodebuild`/`simctl` calls prefix
`DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`. CI runs both
(`.github/workflows/ios.yml`).

The `.nardiw` value networks in `Nardi/Resources/` are checked in. After
retraining, regenerate them with `./scripts/make_model.sh` (needs the
`DecisionEngine/venv` and the `.pt` weights in `../DecisionEngine/weights/`).

Supabase credentials for uploads live in the gitignored
`Config/Supabase.xcconfig`: run `scripts/supabase_setup_wizard.sh --app-only`
from the repo root. Without it the app builds and queues games but never sends.

## Cloud game history (Supabase)

Finished games upload anonymously to a Supabase project. One-time setup (create
the project, enable anonymous sign-ins, apply `supabase/migrations/`, store the
URL + anon key in `.env`, set the keep-alive GitHub secrets):

```bash
scripts/supabase_setup_wizard.sh
```

`.github/workflows/supabase-keepalive.yml` pings the project every 3 days so the
free tier doesn't pause. App Store privacy answers and the policy draft live in
`docs/privacy/app-store-privacy.md`. Remaining manual steps, upkeep
(pauses, GitHub's 60-day schedule rule) and example queries:
`docs/cloud-game-history-runbook.md`.

## Layout

- `Nardi.xcodeproj` — the app target compiles the pybind/SFML-free subset of
  the C++ core directly (no separate library to manage).
- `Nardi/Sources/` — SwiftUI app (`NardiApp`, `ContentView`) and `NardiGame`,
  the `@MainActor` wrapper over the C engine.
- `Nardi/Bridge/` — bridging header exposing `nardi_c_api.h` to Swift.
- `Nardi/Resources/*.nardiw` — bundled value networks (generated; gitignored):
  `mlp.nardiw` (Medium / greedy) and `vzg0.nardiw` (Hard / 1-ply lookahead,
  and the network used for all analysis: game review + analyzer).

## Opponents & models

vs-Computer offers three difficulties (`Opponent` in `NardiGame.swift`), each a
strategy over a value network bundled as a `.nardiw` blob:

| Difficulty | Strategy        | Network                  | Source `.pt`            |
|------------|-----------------|--------------------------|-------------------------|
| Easy       | heuristic       | none (hand-crafted)      | —                       |
| Medium     | greedy (1-ply)  | `mlp.nardiw` (MLP)       | `mlp.pt`                |
| Hard       | 1-ply lookahead | `vzg0.nardiw` (ResNet) | `vzg0mk2.pt` |

All analysis (game review + the analyzer) uses the strongest network,
`vzg0.nardiw`. The blobs are produced by `scripts/make_model.sh` from
`../DecisionEngine/weights/`; see `DecisionEngine/nardi_net.export_weights` for
the torch-free format and `nardi_infer.cpp` for the C++ inference that reads it.
The engine still carries an (unused) MCTS bot; it's deprecated and not exposed in
the app.

## Status

The full stack compiles and links for the iOS simulator SDK (verified with
`swiftc` against the iPhoneSimulator SDK). Running requires the simulator
runtime to be installed. UI is an MVP board; visual polish is the next step.

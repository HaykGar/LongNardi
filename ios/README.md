# Nardi — iOS app

A SwiftUI front-end over the shared C++ engine core (game rules, search, and the
torch-free hand-rolled inference in `../DecisionEngine` + `../CoreEngine`). Swift
talks to the engine through the plain-C API (`nardi_c_api.h`) via a bridging
header — no Objective-C++ shim, no Python, no SFML.

## Build (command line)

Requires full Xcode (not just Command Line Tools), `xcodegen`, and the iOS
**simulator runtime** (Xcode ▸ Settings ▸ Components, or `xcodebuild
-downloadPlatform iOS`).

```sh
cd ios
./scripts/make_model.sh          # exports the bundled .nardiw value networks
xcodegen generate                # writes Nardi.xcodeproj (picks up the new blobs)
open Nardi.xcodeproj             # or build from CLI:
xcodebuild -project Nardi.xcodeproj -scheme Nardi \
  -destination 'platform=iOS Simulator,name=iPhone 16' build
```

**Order matters on a fresh checkout.** The `.nardiw` value networks are
generated artifacts and are **gitignored** (`*.nardiw`), so they don't exist
after a clone. `xcodegen` only adds the blobs that are present on disk when it
runs, so you must run `make_model.sh` *before* `xcodegen generate` — otherwise
the project builds without the networks and every bot/analysis falls back to the
"model blob missing" state. Re-run both (make_model, then xcodegen) whenever you
retrain or add/rename a blob. `make_model.sh` needs the `DecisionEngine/venv`
(PyTorch) and reads the `.pt` weights from `../DecisionEngine/weights/`.

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

- `project.yml` — xcodegen spec; the app target compiles the pybind/SFML-free
  subset of the C++ core directly (no separate library to manage).
- `Nardi/Sources/` — SwiftUI app (`NardiApp`, `ContentView`) and `NardiGame`,
  the `@MainActor` wrapper over the C engine.
- `Nardi/Bridge/` — bridging header exposing `nardi_c_api.h` to Swift.
- `Nardi/Resources/*.nardiw` — bundled value networks (generated; gitignored):
  `mlp.nardiw` (Medium / greedy) and `vzg0.nardiw` (Hard / 1-ply lookahead,
  and the network used for all analysis: game review + analyzer).

`Nardi.xcodeproj` is generated and gitignored — regenerate with `xcodegen`.

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

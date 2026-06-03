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
./scripts/make_model.sh          # exports Nardi/Resources/model.nardiw from a .pt
xcodegen generate                # writes Nardi.xcodeproj from project.yml
open Nardi.xcodeproj             # or build from CLI:
xcodebuild -project Nardi.xcodeproj -scheme Nardi \
  -destination 'platform=iOS Simulator,name=iPhone 16' build
```

## Layout

- `project.yml` — xcodegen spec; the app target compiles the pybind/SFML-free
  subset of the C++ core directly (no separate library to manage).
- `Nardi/Sources/` — SwiftUI app (`NardiApp`, `ContentView`) and `NardiGame`,
  the `@MainActor` wrapper over the C engine.
- `Nardi/Bridge/` — bridging header exposing `nardi_c_api.h` to Swift.
- `Nardi/Resources/model.nardiw` — bundled value network (generated; gitignored).

`Nardi.xcodeproj` is generated and gitignored — regenerate with `xcodegen`.

## Status

The full stack compiles and links for the iOS simulator SDK (verified with
`swiftc` against the iPhoneSimulator SDK). Running requires the simulator
runtime to be installed. UI is an MVP board; visual polish is the next step.

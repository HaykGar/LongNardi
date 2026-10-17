# Coding standards

Read during review. These are the judgement calls; mechanical rules live in
`ios/scripts/` and CI (`.github/workflows/ios.yml`).

## iOS (SwiftUI)

- **No overlap.** Controls never overlap each other or the board, at any
  supported size. Size panels from `GeometryReader` with one flexible element
  taking the remainder rather than fixed heights. UI changes come with a
  simulator screenshot.
- **Theme tokens.** Colours, fonts, gradients and button styles come from
  `Theme.swift` / `ThemeComponents.swift` (`Palette`, `ThemeFont`, `Grad`,
  `*ButtonStyle`). A new magic number or `.system(size:)` font needs a reason.
- **Existing design is authoritative.** Design mockups and handoffs inform, but
  the shipped design and the C++ engine win where they disagree.
- **One control, one place.** A setting shown on more than one screen is one
  shared view bound to one storage key (see `AnimationSpeedSetting`,
  `ShareGameDataSetting`).
- **Dev hooks.** Launch-argument hooks sit inside `#if DEBUG`
  (`check_release_hooks.sh` catches the long-named ones).

## Vocabulary

- Names follow `GLOSSARY.md`: a **game** is one contest; **match** is reserved
  for a future multi-game series (`SavedMatch`/`MatchStore` are legacy names).

## Stored keys

- Never rename a persisted `UserDefaults` key value; rename the Swift constant
  only, or existing installs lose state (e.g. re-see a one-time notice).

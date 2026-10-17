#!/usr/bin/env bash
# Builds Release and fails if dev launch-arg hooks leaked into it. Dev hooks in
# ContentView/GameView/NardiApp must sit inside #if DEBUG.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${DEVELOPER_DIR:-}" && "$(xcode-select -p)" != *Xcode*.app* && -d /Applications/Xcode.app ]]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

DD="${DERIVED_DATA:-$(mktemp -d)}"
xcodebuild build -project Nardi.xcodeproj -scheme Nardi -configuration Release \
  -destination 'generic/platform=iOS Simulator' -derivedDataPath "$DD" \
  CODE_SIGNING_ALLOWED=NO -quiet
BIN="$DD/Build/Products/Release-iphonesimulator/Nardi.app/Nardi"
# Swift inlines strings of 15 bytes or fewer into code, so only the longer hook
# names show up in `strings`; they stand in for the whole set.
leaked=$(strings "$BIN" | grep -E '^--(seed-history-review|review-analyze|reset-consent-notice|first-launch|game-settings)$' || true)
if [[ -n "$leaked" ]]; then
  echo "Dev hooks compiled into Release (wrap them in #if DEBUG):"; echo "$leaked"; exit 1
fi
echo "Release build has no dev hooks."

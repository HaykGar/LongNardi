#!/usr/bin/env bash
# Fails if dev launch-arg hooks leak into Release. Two passes:
#   1. Source: every "--flag" string literal in Nardi/Sources must sit inside #if DEBUG.
#   2. Binary: builds Release and greps it for the longer hook names.
# --source-only skips the build.
set -euo pipefail
cd "$(dirname "$0")/.."

# Pass 1. Tracks #if/#else/#endif nesting; a line is Debug-only when an enclosing
# `#if DEBUG` branch is active. Comments are stripped before matching.
leaked=$(awk '
  FNR == 1 { depth = 0 }
  /^[[:space:]]*#if[[:space:]]+DEBUG[[:space:]]*$/ { kind[++depth] = "D"; next }
  /^[[:space:]]*#if/                               { kind[++depth] = "N"; next }
  /^[[:space:]]*#elseif/                           { kind[depth] = "N"; next }
  /^[[:space:]]*#else/                             { kind[depth] = (kind[depth] == "D") ? "N" : kind[depth]; next }
  /^[[:space:]]*#endif/                            { depth--; next }
  {
    code = $0; sub(/\/\/.*/, "", code)
    if (code !~ /"--[a-z]/) next
    for (i = 1; i <= depth; i++) if (kind[i] == "D") next
    printf "%s:%d: %s\n", FILENAME, FNR, $0
  }
' Nardi/Sources/*.swift)
if [[ -n "$leaked" ]]; then
  echo "Dev hook flags outside #if DEBUG:"; echo "$leaked"; exit 1
fi
echo "Sources: every dev hook flag is inside #if DEBUG."
[[ "${1:-}" == "--source-only" ]] && exit 0

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

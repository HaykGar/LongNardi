#!/usr/bin/env bash
# Runs the iOS unit tests (NardiTests). Works on a Mac whose active developer
# dir is Command Line Tools: it points xcodebuild/simctl at full Xcode.
# Simulator: $SIM if set, else the first available iPhone.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${DEVELOPER_DIR:-}" && "$(xcode-select -p)" != *Xcode*.app* && -d /Applications/Xcode.app ]]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

SIM="${SIM:-$(xcrun simctl list devices available | grep -m1 -oE 'iPhone [^(]+' | sed 's/ *$//')}"
echo "Testing on: $SIM"
xcodebuild test -project Nardi.xcodeproj -scheme Nardi \
  -destination "platform=iOS Simulator,name=$SIM" "$@"

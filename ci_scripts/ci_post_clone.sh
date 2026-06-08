#!/bin/sh
# Xcode Cloud runs this right after cloning. The Xcode project is committed (so
# the build always finds ios/Nardi.xcodeproj), but it's generated from
# project.yml by XcodeGen — so refresh it here to match the source of truth.
#
# This is BEST-EFFORT: the committed project is the guaranteed fallback, so a
# Homebrew/XcodeGen hiccup must never fail the build. Hence the `|| true`s and
# `exit 0`.

cd "$CI_PRIMARY_REPOSITORY_PATH/ios" || exit 0

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "Installing XcodeGen…"
  brew install xcodegen >/dev/null 2>&1 || true
fi

if command -v xcodegen >/dev/null 2>&1; then
  echo "Regenerating Nardi.xcodeproj from project.yml…"
  xcodegen generate || true
else
  echo "XcodeGen unavailable; using the committed Nardi.xcodeproj."
fi

ls -d Nardi.xcodeproj 2>/dev/null || echo "WARNING: Nardi.xcodeproj missing!"
exit 0

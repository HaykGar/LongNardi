#!/usr/bin/env bash
# App Store screenshots from the simulator, one per NAME="hook args" pair:
#
#   ios/scripts/screenshots.sh [-d DEVICE] [-o DIR] NAME="--hook args" ...
#   ios/scripts/screenshots.sh game="--game-mid 28" analyze="--analyze-game 40" \
#       review="--review --review-at 24"
#   ios/scripts/screenshots.sh -d "iPad Pro 13-inch (M5)" game="--game-mid 28"
#
# DEVICE is a simulator name or UDID (default "iPhone 17 Pro Max"; 6.9" iPhone and
# 13" iPad are the two sizes App Store Connect requires). Use one nobody else is
# looking at: the script boots it, takes it over and relaunches the app per shot.
# DIR defaults to ~/Desktop/Nardi-AppStore-Screenshots/<device>.
#
# Builds Debug (dev hooks live in the #if DEBUG blocks of ContentView.swift and
# GameReviewView.swift), sets a 9:41 status bar, then for each shot waits for the
# app's NARDI_READY line (see devReady in NardiGame.swift), rotates the capture to
# landscape and paints out the Dynamic Island the simulator draws into it.
# Env: TIMEOUT (seconds per shot, default 300), SETTLE (default 1.5), DERIVED_DATA.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${DEVELOPER_DIR:-}" && "$(xcode-select -p)" != *Xcode*.app* && -d /Applications/Xcode.app ]]; then
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

DEVICE="iPhone 17 Pro Max"; OUT=""
while getopts "d:o:" opt; do
  case $opt in d) DEVICE=$OPTARG ;; o) OUT=$OPTARG ;; *) exit 2 ;; esac
done
shift $((OPTIND - 1))
[[ $# -gt 0 ]] || { sed -n '2,8p' "$0"; exit 2; }
TIMEOUT=${TIMEOUT:-300}; SETTLE=${SETTLE:-1.5}
BUNDLE=com.haykg.nardi

line=$(xcrun simctl list devices available | grep -F "$DEVICE (" | head -1 || true)
[[ -n "$line" ]] || line=$(xcrun simctl list devices available | grep -F "($DEVICE)" | head -1 || true)
[[ -n "$line" ]] || { echo "No available simulator matches: $DEVICE"; exit 1; }
UDID=$(sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/' <<<"$line")
NAME=$(sed -E 's/^ *(.*) \([0-9A-F-]{36}\).*/\1/' <<<"$line")
OUT=${OUT:-$HOME/Desktop/Nardi-AppStore-Screenshots/$NAME}
mkdir -p "$OUT"
[[ "$line" == *"(Booted)"* ]] && echo "Note: $NAME was already booted; taking it over."
echo "Device: $NAME ($UDID) → $OUT"

xcrun simctl boot "$UDID" 2>/dev/null || true
xcrun simctl bootstatus "$UDID" -b >/dev/null
DD="${DERIVED_DATA:-$(mktemp -d)}"
xcodebuild build -project Nardi.xcodeproj -scheme Nardi -configuration Debug \
  -destination "id=$UDID" -derivedDataPath "$DD" -quiet
xcrun simctl install "$UDID" "$DD/Build/Products/Debug-iphonesimulator/Nardi.app"
xcrun simctl status_bar "$UDID" override --time 9:41 --batteryState charged \
  --batteryLevel 100 --wifiBars 3 --cellularBars 4

# An upright iPad shows this landscape-only app as a letterboxed window, and simctl
# can't rotate a device, so turn its Simulator.app window via the Device menu.
# Needs Accessibility permission for the terminal (System Settings ▸ Privacy).
if [[ "$NAME" == iPad* ]]; then
  probe="$(mktemp).png"
  xcrun simctl io "$UDID" screenshot "$probe" >/dev/null 2>&1
  if python3 -c 'import sys; from PIL import Image; w, h = Image.open(sys.argv[1]).size; sys.exit(0 if h > w else 1)' "$probe"; then
    echo "Rotating $NAME to landscape"
    open -a Simulator --args -CurrentDeviceUDID "$UDID"; sleep 5
    osascript - "$NAME" >/dev/null <<'EOF' || { echo "Couldn't rotate it: in Simulator.app, select $NAME, press ⌘← and rerun."; exit 1; }
on run argv
  tell application "System Events" to tell process "Simulator"
    set frontmost to true
    perform action "AXRaise" of (first window whose name contains (item 1 of argv))
    delay 0.5
    click menu item "Rotate Left" of menu "Device" of menu bar 1
  end tell
end run
EOF
    sleep 2
  fi
  rm -f "$probe"
fi

LOG=$(mktemp)
for spec in "$@"; do
  shot=${spec%%=*}; read -r -a hook <<<"${spec#*=}"
  # --console-pty, not --stderr=<file>: the file redirect silently writes nothing.
  xcrun simctl launch --console-pty --terminate-running-process "$UDID" "$BUNDLE" "${hook[@]}" >"$LOG" 2>&1 &
  app=$!
  waited=0
  until grep -q NARDI_READY "$LOG"; do
    if (( waited >= TIMEOUT )); then echo "  $shot: no NARDI_READY after ${TIMEOUT}s, capturing anyway"; break; fi
    sleep 1; waited=$((waited + 1))
  done
  sleep "$SETTLE"
  xcrun simctl io "$UDID" screenshot "$OUT/$shot.png" >/dev/null
  kill "$app" 2>/dev/null || true; wait "$app" 2>/dev/null || true
  python3 - "$OUT/$shot.png" "$NAME" <<'EOF'
import sys
from PIL import Image
path, name = sys.argv[1], sys.argv[2]
im = Image.open(path).convert("RGB")
if im.height > im.width:                 # landscape UI on a portrait simulator
    im = im.transpose(Image.Transpose.ROTATE_90)
if "iPad" not in name:                   # the island lands on the left edge, mid-height
    px, W, H = im.load(), im.width, im.height
    pts = [(x, y) for x in range(0, 250) for y in range(H // 4, 3 * H // 4, 2) if sum(px[x, y]) < 20]
    if pts:
        x1 = max(p[0] for p in pts)
        y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
        for y in range(y0 - 6, y1 + 7):
            fill = px[min(W - 1, x1 + 12), y]
            for x in range(0, x1 + 10):
                px[x, y] = fill
im.save(path)
print(f"  {path.rsplit('/', 1)[-1]}: {im.width}x{im.height}")
EOF
done
rm -f "$LOG"

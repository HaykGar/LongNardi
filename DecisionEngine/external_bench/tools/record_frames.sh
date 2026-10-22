#!/bin/zsh
# Save a frame of an app's window whenever it (or its accessibility tree) changes, into
# ../runs/frames_<time>/: NNNNN_<hhmmss>.png plus NNNNN_<hhmmss>.ax (role | label | x y w h).
# usage: record_frames.sh [window owner, default "Narde Tournament"]
# Stops after 10 min with no window. Needs ../build/listwin and axlist (built by bench.py).
HERE="${0:a:h}"; OWNER="${1:-Narde Tournament}"
OUT="$HERE/../runs/frames_$(date +%Y%m%d_%H%M%S)"; mkdir -p "$OUT"; cd "$OUT"
last=""; misses=0; i=0
while (( misses < 1200 )); do
  id=$("$HERE/../build/listwin" | awk -v o="$OWNER" -F' \\| ' '{split($1, a, " "); n=substr($1, length(a[1]) + 2)} n == o {print a[1]; exit}')
  if [[ -z $id ]]; then (( misses++ )); sleep 0.5; continue; fi
  misses=0
  screencapture -x -o -l $id ../cur.png 2>/dev/null || { sleep 0.5; continue; }
  "$HERE/../build/axlist" "$OWNER" > ../cur.ax 2>/dev/null
  h=$(cat ../cur.png ../cur.ax | md5 -q)
  if [[ $h != $last ]]; then
    printf -v name "%05d_%s" $i $(date +%H%M%S)
    mv ../cur.png $name.png; mv ../cur.ax $name.ax; last=$h; (( i++ ))
  fi
  sleep 0.3
done

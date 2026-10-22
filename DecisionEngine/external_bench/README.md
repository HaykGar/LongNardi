# External app benchmark

Plays our engine against the AI of other Nardi apps (App Store apps run natively on an Apple
Silicon Mac) and logs every game. Our side plays exactly like the iOS app's **Hard** bot: the
same net (`weights/vzg0mk2.pt`, shipped as `vzg0.nardiw`) and the same one-ply lookahead choice.
Results so far: `RESULTS.md`.

## How it works

The bot keeps its own engine game in parallel and never trusts a screen read on its own:

- **Her move**: when it's our turn, the board is read and matched against every legal result of
  her turn. Exactly one must fit, or the run stops. Tall stacks read as a count range; when that
  leaves more than one result, the app's pip counters decide.
- **Our move**: the engine's move is split into sub-moves and each one is *dragged* (dragging
  names source and destination, so the app never has to guess which die). After each drag the
  board must equal the engine's expected in-between board, or the run stops. Then Done.
- If the app refuses a step (some apps check rules such as six points in a row after every
  single step, while our engine checks at the end of the turn), the bot undoes and tries every
  other order of single-die steps that reaches the same final board.
- Apps that play forced moves themselves (`auto_forced`) are checked against the engine's move.
- Any stop saves a screenshot in the run folder. `core.play_game(..., resume=(board, black_first,
  turn[, her_move_next]))` picks a stopped game up from its last logged board.
- These apps only take real mouse input while frontmost, so each click/drag briefly brings the
  app forward and then restores your cursor and front app. Buttons the accessibility tree exposes
  are pressed without the mouse.

| file | what |
|---|---|
| `bench.py` | command line: `bench.py <app> --games N` |
| `core.py` | the app-independent loop: engine, move verification, step orders, window I/O, logging |
| `apps/<app>/` | one profile per app (screen reading and controls); see below |
| `tools/` | Swift helpers (window list, mouse, accessibility list/press; compiled into `build/` on first run), frame recorder |
| `runs/` | logs and screenshots (git-ignored) |

## Running

1. System Settings > Privacy & Security: give your terminal **Screen Recording** and
   **Accessibility**, then restart it.
2. Set the app up as its profile's docstring says (opponent, colour, default window size) and
   either start a game or leave a finished game's result screen up.
3. From `DecisionEngine/`:

   ```
   venv/bin/python external_bench/bench.py narde_tournament --games 10
   venv/bin/python external_bench/bench.py narde --games 5
   ```

   Logs go to `runs/<app>_<time>/`: `log.jsonl` (every turn's dice, board and move),
   `summary.json` (wins, losses, mars, points) and any failure screenshots.

## Apps

- **`narde_tournament`** (Unity, no accessibility tree): board, 3D dice and pip counters all
  read from pixels (`reader.py`, `dice.py`, `pips.py`). `replay.py` rebuilds a game recorded with
  `tools/record_frames.sh` through the engine, to calibrate the reader after a layout change:
  `python -m apps.narde_tournament.replay <frames dir> [--white-first]` from `external_bench/`.
- **`narde`** (SwiftUI): buttons, pip counts and stack badges from the accessibility tree; board
  and dice from pixels. `python -m apps.narde.replay <frames dir>` replays a game recorded with
  `tools/record_frames.sh Narde` through the engine (every turn must resolve to one legal move).
  Calibrated on a recorded game vs Adikus; not yet run live with these settings.

## Adding an app

Add `apps/<name>/__init__.py` with:

| name | what |
|---|---|
| `name`, `owner`, `size` | profile name, window owner name, window size in points |
| `auto_forced` | the app plays forced moves itself |
| `tray_xy` | where to drop a checker to bear it off |
| `read(win)` | a `core.State`: per-point `Obs` (colour, count range), whether our turn is up, her dice, game over, pips |
| `roll(win, st)` | roll (if the app needs it) and return our dice |
| `point_xy(point, n)` | screen position of the n-th checker on a point (`("T", c)` top row from the right, `("B", c)` bottom row from the left) |
| `undo(win)`, `done(win)` | take back a step; end our turn |
| `after_turn(win, passed)` | wait until the turn has passed to her |
| `new_game(win)` | from the result screen to a fresh game |

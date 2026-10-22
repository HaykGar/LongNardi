# External benchmark results

Our side always plays as the iOS app's **Hard** bot: `weights/vzg0mk2.pt` (shipped as
`vzg0.nardiw`) with the engine's one-ply lookahead choice (`lookahead_choice_target`).
Points: oin = 1, mars = 2.

## Narde Tournament — Svetlana (Expert, rating 5010)

2026-10-18 · 10 games · macOS build of the App Store app · we play white

| | |
|---|---|
| Result | **8–2** (80% games won) |
| Points | **9–2** |
| Mars | 1 for us, 0 against |
| Openers | she opened 8 games, we opened 2 |

| game | opener | result | checkers loser had left |
|---|---|---|---|
| 1 | Svetlana | win, oin | 10 |
| 2 | us | win, **mars** | 15 |
| 3 | Svetlana | loss, oin | 3 |
| 4 | Svetlana | win, oin | 7 |
| 5 | us | win, oin | 6 |
| 6 | Svetlana | win, oin | 2 |
| 7 | Svetlana | win, oin | 5 |
| 8 | Svetlana | win, oin | 13 |
| 9 | Svetlana | win, oin | 1 |
| 10 | Svetlana | loss, oin | 4 |

Notes:
- Games 5, 6 and 8 each stopped once on a bot-side issue (an order of sub-moves the app refuses
  mid-turn; a one-point drag the app took as a tap; a tall-stack count tie). Each was fixed in
  the bot and the game resumed from its last logged board, so every move in every game is still
  the Hard bot's choice. Logs: `runs/batch_20261018_svetlana.json` and the per-game folders
  (git-ignored).
- The app checks some rules (e.g. six points in a row) after every single step, while our engine
  checks them at the end of the turn; the bot plays the same final board in an order the app accepts.

## Narde — Adikus (SOLO AI, AI level High)

2026-10-21 · 10 games · macOS build of the App Store app · we play white · strict mode

| | |
|---|---|
| Result | **8–2** (80% games won) |
| Points | **14–2** |
| Mars | 6 for us, 0 against |
| Openers | 5 each (we 4–1, Adikus 4–1) |

| game | opener | result | checkers loser had left |
|---|---|---|---|
| 1 | Adikus | win, **mars** | 15 |
| 2 | us | win, **mars** | 15 |
| 3 | us | win, oin | 2 |
| 4 | us | loss, oin | 2 |
| 5 | Adikus | win, **mars** | 15 |
| 6 | us | win, **mars** | 15 |
| 7 | us | win, **mars** | 15 |
| 8 | Adikus | loss, oin | 9 |
| 9 | Adikus | win, oin | 4 |
| 10 | Adikus | win, **mars** | 15 |

Notes:
- In this app the winner of a game opens the next, so openers were balanced by playing games 8–10
  as 1-game matches and resigning (not counting) any game we would have opened.
- Strict mode (`--strict`): the bot stops on anything it can't verify against the screen and never
  takes a board from the screen as is; it saves the screen before each of our moves. Stops were
  bot-side (input and timing) and each game resumed from its last logged board. Moves are played
  by tapping (the app treats drags as taps), with the app's "Autoplay forced moves" off from game 2.
- Game 1's last turns were not logged: the app left the game during a settings change, with
  Adikus still on 15 checkers and our win certain; the app scored it as our mars.
- Earlier runs against this app, before the bot was made strict, are discarded. Logs:
  `runs/narde_20261021_21*` – `_2321*` (git-ignored).


# 2. Cloud history stores moves, not board snapshots

**Status:** Accepted (2026-10-16)

## Context
Locally, each turn is saved as board-before/board-after plus dice. For the cloud copy we had to choose a turn format; it is hard to change once data accumulates.

## Decision
Each turn is stored as its two dice plus an ordered list of moves in the engine's native start-and-die form: `[start, die_idx]`, where `start` is the flat point index `row*12 + col` (0–23) and `die_idx` (0/1) selects which die was used. A pass is an empty list. Example: `{"d":[5,3],"m":[[0,0],[5,1]]}`. Moves are logged as turns are confirmed during play, not reconstructed from snapshots. The moving seat is implied: turns alternate from the first seat.

## Alternatives
- **Board snapshots** — matches local storage, but bulkier and can't answer "which move order was played" when several orders reach the same board.
- **From/to pairs** — readable in SQL, but needs a sentinel for bear-off and isn't what the engine plays/undoes with.
- **Both** — redundant; snapshots can be regenerated from moves cheaply and in parallel.

## Consequences
- Games saved before this feature (snapshot-only) can't be uploaded as-is.
- Analytics needing boards must replay moves through the engine.

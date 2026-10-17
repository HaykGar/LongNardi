# Glossary

**Game** — One complete contest from the first turn until one player wins (by bearing off or by resignation). Not a series.

**Match** — *Reserved.* A possible future series of games played to a target score. Code currently calls a single game a "match" (`SavedMatch`, `MatchStore`); that is legacy naming.

**Turn** — One player's roll of two dice and the moves played with it. A turn with no legal moves is a **pass**.

**Move** — Moving one checker from one point to another (or off the board) using one die value. A turn contains zero or more moves.

**Seat** — A player's position in a recorded game. Seat 1 is always the player using the device; seat 2 is the opponent.

**Opponent kind** — Who sits in seat 2: an easy, medium, or hard bot, or a local human (pass & play).

**First seat** — The seat that took the first turn of a game. There is no opening roll; checker colour is irrelevant beyond this.

**Oin** — A single-value win. **Mars** — A double-value win, when the loser has borne off no checkers.

**Install ID** — An anonymous identifier for one installation of the app. Contains no personal information; a reinstall produces a new one.

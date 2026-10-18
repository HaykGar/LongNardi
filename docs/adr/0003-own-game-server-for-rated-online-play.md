# 3. Own game server for rated online play

**Status:** Proposed (2026-10-18)

## Context
We want online play with ratings from day one. Ratings make cheating worth the effort, so clients can't be trusted with dice or move legality. ADR-0001 already puts game history in Supabase and notes that online play needs server-side dice.

## Decision
Run one small game server process next to Supabase. It links the C++ engine and is the referee for every online game.

- **Authoritative server.** The server rolls the dice with a secure RNG, reveals each roll only when that turn starts, and validates every submitted turn with the engine before applying it. Each game records its RNG seed and engine version.
- **Supabase stays the database and identity provider.** Sign in with Apple goes through Supabase Auth, and the existing anonymous users are linked to their Apple identity so their history carries over. The game server checks Supabase session tokens (JWTs) and writes to the same Postgres database. Turns use the ADR-0002 format, so online games work with game review as they are.
- **One process, in-memory live games.** Clients connect over WebSockets. Each confirmed turn is written to Postgres, so a restart reloads unfinished games. We split into several processes only when load requires it.
- **Language:** Swift (Hummingbird or Vapor) if the engine's Swift wrapper builds on Linux, so protocol types are shared with the app. Otherwise Go or Rust behind an `extern "C"` engine wrapper.
- **Ratings:** Glicko-2 (Elo plus a confidence value, so new players' ratings settle quickly). Only win/loss counts for rating at first, so a mars counts the same as an oin. The game result and both players' new ratings are written in one transaction.
- **Rules that protect ratings:**
  - Abandoning or running out of time is a loss, after a short disconnect grace period (~60 s).
  - Repeat games between the same two players count for less.
  - New players stay off the leaderboard until they've played about 10 games.
- **Matchmaking:** friend invites by code first. Then an in-memory queue that pairs players within a rating window and widens it the longer someone waits.

## Alternatives
- **Game Center only (GameKit turn-based):** no server, with free identity, invites and notifications. Neither client can be trusted, though: whoever's turn it is writes the match data, and a jailbroken client can fake the dice. Deterministic dice (`hash(seed, turn)`) plus validation on both clients would cover casual unrated play, but not ratings.
- **Supabase Edge Functions as the referee:** no extra host, but they run on Deno, so the engine would have to be compiled to WASM. They also don't suit long-lived WebSocket sessions or turn clocks.
- **Firebase or another BaaS:** can't run the C++ engine, and would mean leaving Supabase (ADR-0001).

## Costs (approximate, check before buying)
| Item | ~Monthly |
|---|---|
| Game server: Hetzner VPS (2 vCPU / 4 GB) + Caddy for TLS, or one small Fly.io machine | $5–8 |
| Supabase: free tier at first; Pro (no pausing, backups) once there's real traffic | $0 → $25 |
| Domain (unless we reuse one) | ~$1 |
| Sentry, uptime checks, GitHub Actions, APNs | $0 |

Total: about $5–35 a month, and it should stay under ~$50 for a few thousand active players. Bandwidth is negligible, because a turn is a few hundred bytes.

## Consequences
- We own a server: OS updates, TLS, deploys, monitoring and backups. ADR-0001 called this the main downside of running our own server.
- The engine must build on Linux, and client and server must agree on the engine version. The connection handshake carries a protocol and engine version, and the server rejects clients that are too old.
- App Store rules: in-app account deletion, which must also revoke the Apple token. Display names count as user-generated content, so we need report, block and a name filter. No free-text chat at first.
- The client needs a remote-opponent seam: the opponent's turns and the dice come from the network, and local and bot play stay unchanged.
- The privacy policy has to cover accounts, game history and ratings.
- The glossary's **Seat** ("seat 1 is always the player using the device") doesn't hold for games stored on the server. We need a device-independent definition before online games are recorded.
- Once online play is live, real traffic keeps the Supabase project from pausing, so the keep-alive workflow can be retired.

## Build order
1. An `extern "C"` engine wrapper that builds on Linux, plus a protocol document covering messages, errors and versioning.
2. Server: Supabase JWT auth, friend games by invite code, server-rolled dice, turn validation, saving to Postgres.
3. Client: Apple sign-in (linked to the anonymous user), the remote-opponent seam, a lobby, reconnect handling.
4. Random matchmaking, Glicko-2 ratings, a leaderboard.
5. Push notifications for invites and "your turn", account deletion, report and block.

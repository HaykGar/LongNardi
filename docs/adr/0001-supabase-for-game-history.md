# 1. Supabase for cloud game history

**Status:** Accepted (2026-10-16)

## Context
We want every finished game uploaded for analytics, with room for online multiplayer later. Storage needs are tiny; we want SQL and no server to maintain.

## Decision
Use Supabase (hosted Postgres, free tier) with anonymous auth. The app writes directly through the Swift SDK; row-level security limits each install to inserting its own games. A scheduled GitHub Actions workflow pings the database every 3 days to avoid the free tier's 7-day inactivity pause.

## Alternatives
- **Firestore** — never pauses, great realtime, but no SQL.
- **CloudKit** — free and private, but poor cross-user querying and Apple-only.
- **VPS + own API** — full control, but we would own maintenance and security.

## Consequences
- Plain Postgres: we can leave via `pg_dump`.
- GitHub disables scheduled workflows after 60 days without repo activity; re-enable if that happens (real uploads keep the project alive once live).
- Online play will require server-side dice (Edge Function or own server).
- Uploads run in the background and must never block or interrupt gameplay.

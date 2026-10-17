# Cloud game history — runbook

The human-only steps and upkeep for the anonymous game upload (issue #1). The
code lives in the repo; this file covers everything someone has to do by hand.

| Piece | Where |
| --- | --- |
| Table, RLS, `keepalive()` | `supabase/migrations/20261016000000_games.sql` |
| Guided setup | `scripts/supabase_setup_wizard.sh` |
| Keep-alive | `.github/workflows/supabase-keepalive.yml` |
| Engine turn record | `Game::TurnRecord()` → `nardi_turn_record_*` → `NardiGame.turnRecord()` |
| App Store privacy answers + policy draft | `docs/privacy/app-store-privacy.md` |

## Status checklist

Tick these as they happen; the matching issue closes when its lines are done.

- [ ] **#2** Wizard run: project created, anonymous sign-ins on, migration applied.
- [ ] **#2** RLS probe results were insert-own 201; other-seat insert, select,
      update and delete all 401/403. Test row deleted.
- [ ] **#2** `SUPABASE_URL` and `SUPABASE_ANON_KEY` repo secrets set.
- [ ] **#2** Keep-alive manually dispatched on `main` and green.
- [ ] **#6** Policy published (date + contact filled in), URL set in App Store Connect.
- [ ] **#6** App Privacy label submitted per the checklist.

#6 only has to be done before the first release that uploads games (after #4/#5).
Nothing in the app talks to Supabase until #4 lands, so merging before the
setup is safe.

## 1. One-time setup

Run from the repo root on a Mac with `gh` signed in:

```bash
scripts/supabase_setup_wizard.sh
```

It's safe to re-run: values already in `.env` are offered as defaults. It
walks six stages:

1. Create the Supabase project (free tier).
2. Enable **Allow anonymous sign-ins** (Authentication → Sign In / Providers).
3. Capture the project URL and **anon / publishable** key into `.env`
   (`SUPABASE_URL`, `SUPABASE_ANON_KEY`). `.env` is gitignored.
4. Apply the migration in the SQL Editor (the SQL is put on your clipboard).
5. Probe RLS with a real anonymous session and print each status code.
6. Set the two GitHub secrets and optionally dispatch the keep-alive.

**Never copy the `service_role` / secret key** out of the dashboard. The app and
CI only ever need the anon key, which is public by design: RLS is what protects
the table.

## 2. Verify

- **RLS**, from the stage 5 output: insert own row → `201`; insert with another
  `seat1_user`, select, update and delete → `401`/`403`. Then delete the probe
  row in the SQL Editor:
  ```sql
  delete from games where app_version = 'wizard';
  ```
- **Keep-alive**: the workflow can only be dispatched once it is on `main`.
  ```bash
  gh workflow run supabase-keepalive.yml
  gh run watch "$(gh run list --workflow supabase-keepalive.yml -L1 --json databaseId -q '.[0].databaseId')"
  ```
  A green run whose log shows a timestamp means it reached Postgres. Before the
  secrets exist, the run skips with a notice and still passes, so check the log.

## 3. Ongoing upkeep

- **Free-tier pause.** Supabase pauses a free project after about a week without
  activity. The keep-alive calls `keepalive()` every 3 days to prevent that. If
  the project pauses anyway, restore it from the dashboard. Uploads queued on
  devices retry and land once it's back.
- **GitHub's 60-day rule.** This repo is public, and GitHub disables scheduled
  workflows in public repos after 60 days without repository activity. GitHub
  emails a warning first. To recover:
  ```bash
  gh workflow enable supabase-keepalive.yml
  gh workflow run supabase-keepalive.yml
  ```
  Any push to the repo also resets the 60-day clock.
- **Key rotation.** After rotating the anon/publishable key in the dashboard,
  re-run the wizard (stages 3 and 6 update `.env` and the secrets), and ship an
  app build with the new key. Old builds keep using the old key until it is revoked.

## 4. Schema changes

Add a **new** file under `supabase/migrations/` (never edit an applied one) and
run it in the SQL Editor. Rows must stay replayable, so the `turns` shape is
fixed: `[{"d":[d0,d1],"m":[[start,die_idx],...]}]`.

- New `opponent_kind` (e.g. online play): drop and re-create the check
  constraint. Existing rows are untouched.
- `seat2_user` is already nullable for online multiplayer.

## 5. Reading the data

Query in the dashboard's SQL Editor. `outcome` is from seat 1's view: `2` mars
win, `1` win, `-1` loss, `-2` mars loss.

```sql
-- Seat 1's results by opponent
select opponent_kind, bot_model, count(*) games,
       round(avg((outcome > 0)::int), 3) win_rate,
       round(avg((abs(outcome) = 2)::int), 3) mars_rate
from games group by 1, 2 order by 1, 2;

-- First-move advantage (pass & play only, so both seats are human)
select user_first, count(*), round(avg((outcome > 0)::int), 3) first_seat_win_rate
from games where opponent_kind = 'local_human' group by 1;

-- Uploads by release
select app_version, build, count(*) from games group by 1, 2 order by 1 desc, 2 desc;
```

**Replaying a game.** Turns alternate starting with seat 1 when `user_first`,
otherwise seat 2. For each turn, set the dice to `d`, then for each `[start, die]`
in `m`, select square `(start / 12, start % 12)` and move by die index `die`
(`d` is stored larger first, matching the engine). Then confirm. An empty `m` is
a pass. `DecisionEngine/tests/native/swift/main.swift` (`replayMatchesRecord`)
does exactly this through the C API.

## 6. Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| Wizard stage 5: "anonymous sign-in failed" | Anonymous sign-ins not enabled (stage 2), or wrong URL/key. |
| Insert-own probe returns `401`/`403` | Migration not applied, or the `insert own games` policy is missing. |
| Select probe returns `200` | A select grant or policy was added by hand; remove it. |
| Keep-alive red with `404` on `/rpc/keepalive` | Migration not applied (function missing). |
| Keep-alive green but log says "skipping" | Repo secrets not set. |
| No scheduled keep-alive runs | Disabled by GitHub's 60-day rule; see §3. |

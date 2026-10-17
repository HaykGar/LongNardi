-- Anonymous cloud game history (#1 / #2). One row per finished game, written
-- directly by the app with the anon key under an anonymous-auth session.

create table if not exists public.games (
  id            uuid primary key,             -- generated on device
  seat1_user    uuid not null,                -- install ID (anonymous auth uid)
  opponent_kind text not null check (opponent_kind in ('bot_easy','bot_medium','bot_hard','local_human','remote_human')),  -- 'bot_easy' | 'bot_medium' | 'bot_hard' | 'local_human' (later 'remote_human')
  seat2_user    uuid,                         -- null unless opponent is an online human
  bot_model     text,                         -- e.g. 'heuristic', 'mlp', 'vzg0'; null for humans
  user_first    boolean not null,             -- seat 1 took the first turn
  outcome       smallint not null check (outcome in (-2,-1,1,2)),  -- seat 1's view
  resigned      boolean not null,
  app_version   text not null,
  build         text not null,
  finished_at   timestamptz not null,
  received_at   timestamptz not null default now(),
  turns         jsonb not null                -- [{"d":[5,3],"m":[[0,0],[5,1]]}, ...]
);

-- Insert-only: an install may add its own games and nothing else. No select,
-- update or delete policies exist, so RLS refuses those for anon/authenticated.
alter table public.games enable row level security;

drop policy if exists "insert own games" on public.games;
create policy "insert own games" on public.games
  for insert to authenticated
  with check (seat1_user = auth.uid());

revoke all on public.games from anon, authenticated;
grant insert on public.games to authenticated;

-- Trivial query for the keep-alive workflow (.github/workflows/supabase-keepalive.yml).
create or replace function public.keepalive() returns timestamptz
  language sql stable as $$ select now() $$;
revoke all on function public.keepalive() from public;
grant execute on function public.keepalive() to anon;

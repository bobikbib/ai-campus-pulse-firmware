-- AI Campus Pulse: sensor tables for the existing `rooms` table.
--
-- Run once in Supabase → SQL Editor → Run. Safe to run again.
-- Nothing in the existing tables (rooms, bookings, booking_log, ...) is changed.
--
-- The board / simulator reports its room as rooms.board_key (e.g. "Z-3-04a");
-- the bridge maps that to rooms.id (e.g. "Z.3.04a") before writing here.

-- Latest state of every room: one row per room, overwritten by each message.
create table if not exists public.room_presence (
    room_id       text primary key references public.rooms(id) on delete cascade,
    board_key     text        not null,
    people_count  integer     not null check (people_count >= 0),
    occupied      boolean     not null,
    empty_for_s   integer     not null check (empty_for_s >= 0),
    sensor        text        not null,          -- ok | stale | starting
    mode          text,                          -- camera (real board) | sim (simulated)
    uptime_s      bigint,
    updated_at    timestamptz not null default now()
);

-- History: only real changes (not the 5-second heartbeats), so it stays small.
create table if not exists public.presence_log (
    id            bigint generated always as identity primary key,
    room_id       text        not null references public.rooms(id) on delete cascade,
    people_count  integer     not null,
    occupied      boolean     not null,
    empty_for_s   integer     not null,
    sensor        text        not null,
    mode          text,
    reason        text,
    received_at   timestamptz not null default now()
);
create index if not exists presence_log_room_time on public.presence_log (room_id, received_at desc);

-- updated_at is always the database's own clock, not the laptop's.
create or replace function public.room_presence_touch() returns trigger
language plpgsql as $$
begin
    new.updated_at := now();
    return new;
end $$;

drop trigger if exists room_presence_touch on public.room_presence;
create trigger room_presence_touch before insert or update on public.room_presence
    for each row execute function public.room_presence_touch();

-- Website reads, only the bridge writes. The bridge uses the service_role key,
-- which bypasses RLS, so no insert/update policies are needed (or wanted).
alter table public.room_presence enable row level security;
alter table public.presence_log  enable row level security;

drop policy if exists "room_presence readable" on public.room_presence;
create policy "room_presence readable" on public.room_presence for select using (true);
drop policy if exists "presence_log readable" on public.presence_log;
create policy "presence_log readable" on public.presence_log for select using (true);

-- Ready-to-use status per room for the website.
--   status: occupied | empty | release_due | sensor_unavailable | no_sensor
--   release_due = nobody for 45 min (2700 s) with a working sensor.
--   A silent (>15 s) or stale sensor is never treated as an empty room.
create or replace view public.room_status with (security_invoker = true) as
select
    r.id                                   as room_id,
    r.board_key,
    r.floor_id,
    r.type,
    r.capacity,
    r.bookable,
    p.people_count,
    p.occupied,
    p.empty_for_s,
    p.sensor,
    p.mode,
    p.updated_at,
    case
        when p.room_id is null                                  then 'no_sensor'
        when p.updated_at < now() - interval '15 seconds'
          or p.sensor <> 'ok'                                   then 'sensor_unavailable'
        when p.occupied                                         then 'occupied'
        when p.empty_for_s >= 2700                              then 'release_due'
        else 'empty'
    end                                    as status
from public.rooms r
left join public.room_presence p on p.room_id = r.id;

#!/usr/bin/env python3
"""
AI Campus Pulse fleet simulator: one simulated sensor per room.

We have one real board, but the campus has many rooms. This starts one copy of
the firmware simulator (build/cp_sim, the same C code as the board) per room
and drives it with people arriving and leaving at random. Output is the usual
CP> lines on stdout, with "mode":"sim" so simulated rooms are easy to tell
apart from the real board ("mode":"camera").

    make
    # rooms from Supabase (key from SUPABASE_KEY), skipping the real board's room
    python3 bridge/fleet_sim.py --supabase https://<project>.supabase.co --exclude Z-3-04a \\
        | python3 bridge/cp_bridge.py --supabase https://<project>.supabase.co

    # or a fixed list, no Supabase needed
    python3 bridge/fleet_sim.py --rooms Z-3-01a,Z-3-01b,B3-2-101 --speed 60

--speed N makes time run N times faster (60: 1 real second = 1 board minute,
so the 45-minute ghost-booking rule fires after 45 real seconds).
--ghost R: share of empty periods that last past 45 minutes (ghost bookings).

Standard library only.
"""
import argparse
import json
import os
import random
import subprocess
import sys
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "..", "build", "cp_sim")
out_lock = threading.Lock()


def log(msg):
    print(f"[fleet {time.strftime('%H:%M:%S')}] {msg}", file=sys.stderr, flush=True)


def rooms_from_supabase(url, key):
    req = urllib.request.Request(
        url.rstrip("/") + "/rest/v1/rooms?select=board_key,capacity&order=board_key",
        headers={"apikey": key, "Authorization": f"Bearer {key}"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        rows = json.loads(resp.read())
    return [(r["board_key"], int(r.get("capacity") or 4)) for r in rows if r.get("board_key")]


def pump(proc):
    """Copy one simulator's output to our stdout, whole lines only."""
    for line in proc.stdout:
        if line.startswith("CP>"):
            with out_lock:
                sys.stdout.write(line)
                sys.stdout.flush()


def drive(room, capacity, proc, speed, ghost, rng, stop):
    """Random day in one room: busy for a while, empty for a while."""
    def cmd(text):
        proc.stdin.write(text + "\n")
        proc.stdin.flush()

    def wait(board_minutes):
        return stop.wait(board_minutes * 60.0 / speed)

    cmd(f"ROOM {room}")
    # stagger the rooms so they don't all change at the same moment
    if wait(rng.uniform(0, 10)):
        return
    while not stop.is_set():
        people = rng.randint(1, max(1, min(capacity, 6)))
        cmd(f"SIM {people}")
        if wait(rng.uniform(10, 40)):
            return
        cmd("SIM 0")
        empty = rng.uniform(50, 70) if rng.random() < ghost else rng.uniform(3, 30)
        if wait(empty):
            return


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--supabase", help="read rooms (board_key) from this Supabase project")
    src.add_argument("--rooms", help="comma-separated room ids (board_key values)")
    ap.add_argument("--exclude", default="", help="comma-separated rooms to skip (e.g. the real board's)")
    ap.add_argument("--speed", type=float, default=1.0, help="time multiplier (default 1 = real time)")
    ap.add_argument("--ghost", type=float, default=0.3, help="share of empty periods longer than 45 min")
    ap.add_argument("--seed", type=int, help="random seed, for a repeatable demo")
    args = ap.parse_args()

    if not os.access(SIM, os.X_OK):
        sys.exit(f"{SIM} not found: run 'make' first")
    if args.supabase:
        key = os.environ.get("SUPABASE_KEY", "")
        if not key:
            sys.exit("set the SUPABASE_KEY environment variable")
        rooms = rooms_from_supabase(args.supabase, key)
    else:
        rooms = [(r.strip(), 4) for r in args.rooms.split(",") if r.strip()]
    skip = {r.strip() for r in args.exclude.split(",") if r.strip()}
    rooms = [(r, c) for r, c in rooms if r not in skip]
    if not rooms:
        sys.exit("no rooms to simulate")
    log(f"simulating {len(rooms)} rooms at {args.speed:g}x: {', '.join(r for r, _ in rooms)}")

    rng = random.Random(args.seed)
    stop = threading.Event()
    procs = []
    for room, cap in rooms:
        p = subprocess.Popen([SIM, "--speed", str(args.speed)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True, bufsize=1)
        procs.append(p)
        threading.Thread(target=pump, args=(p,), daemon=True).start()
        threading.Thread(target=drive, daemon=True,
                         args=(room, cap, p, args.speed, args.ghost,
                               random.Random(rng.random()), stop)).start()
    try:
        while all(p.poll() is None for p in procs):
            time.sleep(0.5)
        log("a simulator exited unexpectedly")
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        for p in procs:
            p.terminate()


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        pass

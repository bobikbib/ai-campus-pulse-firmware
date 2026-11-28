#!/usr/bin/env python3
"""
AI Campus Pulse bridge: board console -> local receiver and/or Supabase.

Reads text lines from stdin, picks out the board's "CP>{json}" lines, and
sends each presence message on.

    # simulator -> local receiver
    ./build/cp_sim --speed 60 | python3 bridge/cp_bridge.py --url http://localhost:8080/api/sensor

    # all rooms (simulated) -> Supabase; key comes from the environment
    export SUPABASE_KEY=...        # service_role key, never commit it
    python3 bridge/fleet_sim.py --supabase https://<project>.supabase.co | \\
        python3 bridge/cp_bridge.py --supabase https://<project>.supabase.co

--url and --supabase can be used together.

Standard library only, so nothing needs to be installed.

Behaviour:
  - Lines without the CP> prefix (other board output) are ignored.
  - Broken / interleaved JSON is dropped and counted; the board resends
    its state every 5 s anyway.
  - Sending happens in background threads, so reading the board never
    blocks. Messages are sent in order. If a destination is down, messages
    wait in a queue (up to 5000) and are retried with backoff.
  - Supabase: the board's room id is rooms.board_key; it is mapped to
    rooms.id. Latest state is upserted into room_presence (one row per room);
    changes (not heartbeats) are also appended to presence_log. Messages are
    batched about once a second, so a whole simulated campus is a few
    requests per second.
"""
import argparse
import collections
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

PREFIX = "CP>"
FORWARD_FIELDS = (
    "room", "people_count", "occupied", "empty_for_s", "sensor",
    "mode", "reason", "uptime_s",
)


def log(msg):
    print(f"[bridge {time.strftime('%H:%M:%S')}] {msg}", file=sys.stderr, flush=True)


def parse_line(line):
    """Return the presence dict for a valid CP presence line, else None."""
    idx = line.find(PREFIX)
    if idx < 0:
        return None
    try:
        msg = json.loads(line[idx + len(PREFIX):].strip())
    except json.JSONDecodeError:
        raise ValueError("bad json")
    if not isinstance(msg, dict) or msg.get("type") != "presence":
        return None  # hello / ack / diag / error lines are not forwarded
    for key in ("room", "occupied", "sensor", "empty_for_s", "people_count"):
        if key not in msg:
            raise ValueError(f"missing {key}")
    return {k: msg[k] for k in FORWARD_FIELDS if k in msg}


def http(method, url, body=None, headers=None, timeout=5.0):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read()
    return json.loads(raw) if raw else None


class PermanentError(Exception):
    """The destination rejected this batch for good (e.g. unknown room)."""


class Sender(threading.Thread):
    """Delivers messages in order through `sink.send(batch)`. If the sink is
    down, messages wait in a bounded queue (oldest dropped first when full)
    and are retried with backoff."""

    def __init__(self, sink, max_queue=5000):
        super().__init__(daemon=True)
        self.sink = sink
        self.queue = collections.deque(maxlen=max_queue)
        self.cond = threading.Condition()
        self.inflight = False
        self.sent = 0
        self.failed = 0
        self.dropped = 0

    def submit(self, msg):
        with self.cond:
            if len(self.queue) == self.queue.maxlen:
                self.dropped += 1
            self.queue.append(msg)
            self.cond.notify()

    def idle(self):
        with self.cond:
            return not self.queue and not self.inflight

    def run(self):
        backoff = 0.5
        while True:
            with self.cond:
                while not self.queue:
                    self.cond.wait()
            if self.sink.batch_wait:
                time.sleep(self.sink.batch_wait)  # let a batch build up
            with self.cond:
                batch = list(self.queue)[: self.sink.max_batch]
                self.inflight = True
            try:
                self.sink.send(batch)
            except PermanentError as e:
                log(f"{self.sink.name}: dropped {len(batch)} message(s): {e}")
            except (urllib.error.URLError, OSError) as e:
                self.failed += 1
                with self.cond:
                    self.inflight = False
                detail = ""
                if isinstance(e, urllib.error.HTTPError):
                    try:
                        detail = " " + e.read().decode(errors="replace")[:200]
                    except OSError:
                        pass
                log(f"{self.sink.name}: send failed ({e}{detail}); {len(self.queue)} waiting, "
                    f"retry in {backoff:.1f}s")
                time.sleep(backoff)
                backoff = min(backoff * 2, 10.0)
                continue
            with self.cond:
                for _ in batch:
                    if self.queue:
                        self.queue.popleft()
                self.inflight = False
            self.sent += len(batch)
            backoff = 0.5


class ReceiverSink:
    """bridge/receiver.py (or any endpoint taking one JSON message per POST)."""
    name = "receiver"
    batch_wait = 0
    max_batch = 1

    def __init__(self, url, token, timeout):
        self.url, self.token, self.timeout = url, token, timeout

    def send(self, batch):
        headers = {"Authorization": f"Bearer {self.token}"} if self.token else {}
        for msg in batch:
            http("POST", self.url, msg, headers, self.timeout)
            log(f"sent {msg['room']}: occupied={msg['occupied']} people={msg['people_count']} "
                f"empty_for_s={msg['empty_for_s']} sensor={msg['sensor']}")


class SupabaseSink:
    """Supabase REST (PostgREST): room_presence upsert + presence_log insert."""
    name = "supabase"
    batch_wait = 1.0
    max_batch = 500

    def __init__(self, base_url, key, timeout):
        self.base = base_url.rstrip("/") + "/rest/v1"
        self.headers = {"apikey": key, "Authorization": f"Bearer {key}"}
        self.timeout = timeout
        self.room_by_key = {}
        self.unknown_logged = set()
        self.last_refresh = 0.0

    def load_rooms(self):
        rows = http("GET", f"{self.base}/rooms?select=id,board_key", None,
                    self.headers, self.timeout)
        self.room_by_key = {r["board_key"]: r["id"] for r in rows if r.get("board_key")}
        self.last_refresh = time.time()
        return self.room_by_key

    def room_id(self, board_key):
        if board_key not in self.room_by_key and time.time() - self.last_refresh > 30:
            self.load_rooms()  # a room may have been added meanwhile
        return self.room_by_key.get(board_key)

    def send(self, batch):
        latest = {}   # room_id -> row; Postgres refuses one row twice in an upsert
        changes = []
        for msg in batch:
            rid = self.room_id(msg["room"])
            if rid is None:
                if msg["room"] not in self.unknown_logged:
                    self.unknown_logged.add(msg["room"])
                    log(f"supabase: no room with board_key={msg['room']!r}; its messages are skipped. "
                        f"Set the board's room to one of: {', '.join(sorted(self.room_by_key))}")
                continue
            row = {
                "room_id": rid, "board_key": msg["room"],
                "people_count": int(msg["people_count"]), "occupied": bool(msg["occupied"]),
                "empty_for_s": int(msg["empty_for_s"]), "sensor": str(msg["sensor"]),
                "mode": msg.get("mode"), "uptime_s": msg.get("uptime_s"),
            }
            latest[rid] = row
            if msg.get("reason") != "heartbeat":
                changes.append({k: row[k] for k in
                                ("room_id", "people_count", "occupied", "empty_for_s", "sensor", "mode")}
                               | {"reason": msg.get("reason")})
        if changes:
            http("POST", f"{self.base}/presence_log", changes,
                 {**self.headers, "Prefer": "return=minimal"}, self.timeout)
        if latest:
            http("POST", f"{self.base}/room_presence?on_conflict=room_id", list(latest.values()),
                 {**self.headers, "Prefer": "resolution=merge-duplicates,return=minimal"},
                 self.timeout)
            summary = ", ".join(f"{r['board_key']}={'occ' if r['occupied'] else 'empty'}"
                                f"/{r['empty_for_s']}s" for r in latest.values())
            log(f"supabase: {len(latest)} room(s), {len(changes)} change(s): {summary[:160]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", help="local receiver endpoint, e.g. http://localhost:8080/api/sensor")
    ap.add_argument("--token", default="", help="optional shared secret for --url ('Authorization: Bearer')")
    ap.add_argument("--supabase", default=os.environ.get("SUPABASE_URL"),
                    help="Supabase project URL, e.g. https://<project>.supabase.co "
                         "(key from the SUPABASE_KEY environment variable)")
    ap.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout, seconds")
    ap.add_argument("--echo", action="store_true", help="also print every input line to stdout")
    args = ap.parse_args()
    if not args.url and not args.supabase:
        ap.error("give --url and/or --supabase")

    senders = []
    if args.url:
        senders.append(Sender(ReceiverSink(args.url, args.token, args.timeout)))
        log(f"forwarding CP presence lines to {args.url}")
    if args.supabase:
        key = os.environ.get("SUPABASE_KEY", "")
        if not key:
            ap.error("set the SUPABASE_KEY environment variable (Supabase → Project Settings → API keys)")
        sink = SupabaseSink(args.supabase, key, args.timeout)
        try:
            rooms = sink.load_rooms()
        except (urllib.error.URLError, OSError) as e:
            sys.exit(f"cannot read rooms from Supabase: {e}")
        log(f"Supabase: {len(rooms)} rooms with a board_key: {', '.join(sorted(rooms))}")
        senders.append(Sender(sink))
    for s in senders:
        s.start()

    bad = 0
    for raw in sys.stdin:
        line = raw.rstrip("\r\n")
        if args.echo:
            print(line, flush=True)
        try:
            msg = parse_line(line)
        except ValueError as e:
            bad += 1
            log(f"dropped malformed line ({e}): {line[:80]!r}")
            continue
        if msg:
            for s in senders:
                s.submit(msg)
    # stdin closed: give the senders a moment to flush
    deadline = time.time() + 10
    while not all(s.idle() for s in senders) and time.time() < deadline:
        time.sleep(0.05)
    for s in senders:
        log(f"input ended ({s.sink.name}). sent={s.sent} failed_attempts={s.failed} "
            f"dropped={s.dropped} unsent={len(s.queue)}")
    log(f"malformed lines: {bad}")


if __name__ == "__main__":
    main()

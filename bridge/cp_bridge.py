#!/usr/bin/env python3
"""
AI Campus Pulse bridge: board console -> website.

Reads text lines from stdin, picks out the board's "CP>{json}" lines, and
POSTs each presence message as JSON to the website.

    # simulator -> website
    ./build/cp_sim --speed 60 | python3 bridge/cp_bridge.py --url http://localhost:8080/api/sensor

    # physical board on a USB serial port (macOS example)
    cat /dev/cu.usbmodem* | python3 bridge/cp_bridge.py --url ...

Standard library only, so nothing needs to be installed.

Behaviour:
  - Lines without the CP> prefix (other board output) are ignored.
  - Broken / interleaved JSON is dropped and counted; the board resends
    its state every 5 s anyway.
  - Sending happens in a background thread, so reading the board never
    blocks. Every message is sent, in order. If the receiver is down, messages
    wait in a queue (up to 5000) and are retried with backoff.
"""
import argparse
import collections
import json
import sys
import threading
import time
import urllib.error
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


class Sender(threading.Thread):
    """Sends every message in order. If the site is down, messages wait in a
    bounded queue (oldest dropped first when full) and are retried."""

    def __init__(self, url, token, timeout, max_queue=5000):
        super().__init__(daemon=True)
        self.url = url
        self.token = token
        self.timeout = timeout
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

    def _post(self, msg):
        body = json.dumps(msg).encode()
        req = urllib.request.Request(self.url, data=body, method="POST",
                                     headers={"Content-Type": "application/json"})
        if self.token:
            req.add_header("Authorization", f"Bearer {self.token}")
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            resp.read()

    def run(self):
        backoff = 0.5
        while True:
            with self.cond:
                while not self.queue:
                    self.cond.wait()
                msg = self.queue[0]
                self.inflight = True
            try:
                self._post(msg)
            except (urllib.error.URLError, OSError) as e:
                self.failed += 1
                with self.cond:
                    self.inflight = False
                log(f"send failed ({e}); {len(self.queue)} waiting, retry in {backoff:.1f}s")
                time.sleep(backoff)
                backoff = min(backoff * 2, 10.0)
                continue
            with self.cond:
                if self.queue and self.queue[0] is msg:
                    self.queue.popleft()
                self.inflight = False
            self.sent += 1
            backoff = 0.5
            log(f"sent {msg['room']}: occupied={msg['occupied']} people={msg['people_count']} "
                f"empty_for_s={msg['empty_for_s']} sensor={msg['sensor']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--url", required=True, help="website endpoint, e.g. http://localhost:8080/api/sensor")
    ap.add_argument("--token", default="", help="optional shared secret sent as 'Authorization: Bearer'")
    ap.add_argument("--timeout", type=float, default=3.0, help="HTTP timeout, seconds")
    ap.add_argument("--echo", action="store_true", help="also print every input line to stdout")
    args = ap.parse_args()

    sender = Sender(args.url, args.token, args.timeout)
    sender.start()
    bad = 0
    log(f"forwarding CP presence lines to {args.url}")
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
            sender.submit(msg)
    # stdin closed: give the sender a moment to flush
    deadline = time.time() + 10
    while not sender.idle() and time.time() < deadline:
        time.sleep(0.05)
    log(f"input ended. sent={sender.sent} failed_attempts={sender.failed} "
        f"dropped={sender.dropped} malformed={bad} unsent={len(sender.queue)}")


if __name__ == "__main__":
    main()

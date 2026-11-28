#!/usr/bin/env python3
"""
AI Campus Pulse board gateway: phone (NFC) -> board -> database.

The board does the processing; this script only carries messages:

  phone app ──POST card scan──▶ gateway ──"NFC <uid>"──▶ board (or build/cp_sim)
                                                           │ camera sees someone
                                                           │ within 10 s?
  phone app ◀── result ──────── gateway ◀──CP>{"type":"entry",...}──┘
                                   │ confirmed only
                                   ▼
                     Supabase rpc/record_nfc_scan  (attendance)

  board presence lines (people, empty_for_s, noise_db, voice) ──▶ Supabase rpc/ingest_board

The phone talks to the gateway exactly like it talks to Supabase (same path
/rest/v1/rpc/record_nfc_scan, same JSON), so the Android app only needs the
gateway URL instead of the Supabase URL. With the phone on USB:
    adb reverse tcp:8090 tcp:8090      # phone's 127.0.0.1:8090 -> this laptop
and in the app: Supabase URL = http://127.0.0.1:8090

    export SUPABASE_KEY='sb_publishable_...'
    python3 bridge/board_gateway.py --supabase https://<project>.supabase.co --room Z-3-03
    # then type board commands here: SIM 1, SIM 0, NOISE 60, VOICE 1, STATUS ...

    # a real board on a serial port instead of the simulator:
    python3 bridge/board_gateway.py --serial /dev/cu.usbmodem1234 --supabase ...

Standard library only.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.error
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cp_bridge import (PREFIX, Sender, SupabaseSink, http, http_error_text,  # noqa: E402
                       log, parse_line, supabase_headers)

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "..", "build", "cp_sim")
WAIT_S = 12.0   # a little longer than the board's own 10 s confirm window


class Board:
    """Line-based link to the board: a serial port or the simulator process."""

    def __init__(self, serial_path=None, baud=115200):
        self.lock = threading.Lock()
        if serial_path:
            import termios
            import tty
            fd = os.open(serial_path, os.O_RDWR | os.O_NOCTTY)
            tty.setraw(fd)
            attrs = termios.tcgetattr(fd)
            speed = getattr(termios, f"B{baud}")
            attrs[4] = attrs[5] = speed
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            self.rx = os.fdopen(fd, "r", buffering=1, encoding="utf-8", errors="replace", newline=None)
            self.tx = os.fdopen(os.dup(fd), "w", buffering=1, encoding="utf-8")
            self.proc = None
            self.name = serial_path
        else:
            if not os.access(SIM, os.X_OK):
                sys.exit(f"{SIM} not found: run 'make' first")
            self.proc = subprocess.Popen([SIM], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         text=True, bufsize=1)
            self.rx, self.tx = self.proc.stdout, self.proc.stdin
            self.name = "simulator (build/cp_sim)"

    def send(self, line):
        with self.lock:
            self.tx.write(line.strip() + "\n")
            self.tx.flush()

    def lines(self):
        for raw in self.rx:
            yield raw.rstrip("\r\n")


class EntryWaiters:
    """HTTP requests waiting for the board's decision about a card."""

    def __init__(self):
        self.cond = threading.Condition()
        self.results = {}   # card -> entry message

    def expect(self, card):
        with self.cond:
            self.results.pop(card, None)

    def deliver(self, msg):
        with self.cond:
            self.results[msg.get("card", "")] = msg
            self.cond.notify_all()

    def wait(self, card, timeout):
        deadline = time.time() + timeout
        with self.cond:
            while card not in self.results:
                left = deadline - time.time()
                if left <= 0:
                    return None
                self.cond.wait(left)
            return self.results.pop(card)


def normalize_uid(uid):
    return re.sub(r"[^0-9A-Fa-f]", "", str(uid or "")).upper()


def make_handler(board, waiters, supabase_url, key, timeout):
    class Handler(BaseHTTPRequestHandler):
        def _reply(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            if self.path.split("?")[0] != "/rest/v1/rpc/record_nfc_scan":
                return self._reply(404, {"message": "not found"})
            try:
                n = int(self.headers.get("Content-Length", "0"))
                if not 0 < n <= 4096:
                    raise ValueError("bad length")
                req = json.loads(self.rfile.read(n))
                if not isinstance(req, dict):
                    raise ValueError("expected a JSON object")
            except (ValueError, json.JSONDecodeError) as e:
                return self._reply(400, {"message": f"bad request: {e}"})

            card = normalize_uid(req.get("card_uid"))
            if not 4 <= len(card) <= 20:
                return self._reply(400, {"message": "card_uid must be 4-20 hex digits"})

            # 1. hand the scan to the board; 2. wait for its decision
            waiters.expect(card)
            board.send(f"NFC {card}")
            log(f"gateway: card {card} -> board, waiting for the camera")
            entry = waiters.wait(card, WAIT_S)
            if entry is None:
                return self._reply(504, {"message": "Board did not answer (is it running?)"})
            if not entry.get("confirmed"):
                why = {"nobody_seen": "the camera saw nobody enter",
                       "camera_unavailable": "the board's camera is not working",
                       "replaced": "another card was scanned"}.get(entry.get("reason"), entry.get("reason"))
                log(f"gateway: card {card} rejected by the board ({entry.get('reason')}); nothing sent to the database")
                return self._reply(409, {"message": f"Entry not confirmed by the board: {why}"})

            # 3. only now: the database
            body = {"card_uid": card, "reader_id": req.get("reader_id"),
                    "reader_token": req.get("reader_token")}
            try:
                result = http("POST", f"{supabase_url.rstrip('/')}/rest/v1/rpc/record_nfc_scan",
                              body, supabase_headers(key), timeout)
            except urllib.error.HTTPError as e:
                text = e.read().decode(errors="replace")
                log(f"gateway: database refused card {card}: {text[:160]}")
                self.send_response(e.code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(text.encode())))
                self.end_headers()
                self.wfile.write(text.encode())
                return
            except (urllib.error.URLError, OSError) as e:
                return self._reply(502, {"message": f"database unreachable: {e}"})
            log(f"gateway: entry confirmed by the board and recorded: card {card}, "
                f"{(result or {}).get('display_name', '?')}, room {entry.get('room')}")
            return self._reply(200, {**(result or {}), "board_room": entry.get("room"),
                                     "board_people": entry.get("people_count")})

        def log_message(self, fmt, *args):
            pass
    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--supabase", default=os.environ.get("SUPABASE_URL"), required=False,
                    help="Supabase project URL (key from SUPABASE_KEY)")
    ap.add_argument("--serial", help="board serial port; default: run the simulator")
    ap.add_argument("--room", help="send ROOM <id> to the board at start (rooms.board_key)")
    ap.add_argument("--host", default="127.0.0.1", help="where the phone connects (default 127.0.0.1, use adb reverse)")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--timeout", type=float, default=8.0)
    args = ap.parse_args()
    key = os.environ.get("SUPABASE_KEY", "")
    if not args.supabase or not key:
        ap.error("need --supabase (or SUPABASE_URL) and the SUPABASE_KEY environment variable")

    board = Board(args.serial)
    waiters = EntryWaiters()
    sink = SupabaseSink(args.supabase, key, args.timeout)
    try:
        sink.load_rooms()
    except (urllib.error.URLError, OSError) as e:
        sys.exit(f"cannot reach Supabase: {http_error_text(e)}")
    sender = Sender(sink)
    sender.start()

    def read_board():
        for line in board.lines():
            if PREFIX not in line:
                continue
            print(line, flush=True)
            try:
                msg = json.loads(line[line.find(PREFIX) + len(PREFIX):])
            except json.JSONDecodeError:
                continue
            if msg.get("type") == "entry":
                waiters.deliver(msg)
            else:
                try:
                    presence = parse_line(line)
                except ValueError:
                    presence = None
                if presence:
                    sender.submit(presence)
        log("board link closed")
        os._exit(1)

    threading.Thread(target=read_board, daemon=True).start()
    if args.room:
        board.send(f"ROOM {args.room}")

    srv = ThreadingHTTPServer((args.host, args.port), make_handler(board, waiters, args.supabase, key, args.timeout))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log(f"board: {board.name}; phone scans: http://{args.host}:{args.port}/rest/v1/rpc/record_nfc_scan")
    log("type board commands here (SIM 1, SIM 0, NOISE 60, VOICE 1, STATUS, HELP); Ctrl+C to stop")
    try:
        for line in sys.stdin:
            if line.strip():
                board.send(line)
        while True:          # stdin closed (e.g. run in background): keep serving
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    finally:
        if board.proc:
            board.proc.terminate()


if __name__ == "__main__":
    main()

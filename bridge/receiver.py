#!/usr/bin/env python3
"""
AI Campus Pulse local receiver: where the board's data ends up.

    python3 bridge/receiver.py            # listens on http://localhost:8080

  POST /api/sensor   bridge sends presence JSON here
  GET  /api/rooms    latest state of every room (JSON)
  GET  /             live page, refreshes every 2 s

Every message received is appended to data/readings.jsonl (one JSON per line,
with the time it arrived), so the history stays on disk.

Standard library only. Listens on 127.0.0.1, so only this computer can reach it.
"""
import argparse
import html
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STALE_AFTER_S = 15  # no message for this long -> "no data"

lock = threading.Lock()
rooms = {}          # room -> {"msg": {...}, "received": epoch}
log_path = None


def room_view(entry, now):
    m = entry["msg"]
    age = now - entry["received"]
    if age > STALE_AFTER_S:
        status = "no data"
    elif m.get("sensor") != "ok":
        status = f"sensor {m.get('sensor')}"
    elif m.get("occupied"):
        status = "occupied"
    else:
        status = "empty"
    return {**m, "status": status, "last_seen_s": round(age, 1)}


PAGE = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="2"><title>Campus Pulse Receiver</title>
<style>
 body{{font:15px system-ui,sans-serif;margin:24px;background:#f6f7f9;color:#1d232b}}
 table{{border-collapse:collapse;background:#fff}} th,td{{padding:8px 12px;border-bottom:1px solid #e3e6ea;text-align:left}}
 .occupied{{color:#b42318;font-weight:600}} .empty{{color:#067647;font-weight:600}} .warn{{color:#b54708;font-weight:600}}
 @media (prefers-color-scheme:dark){{body{{background:#14171b;color:#e6e8eb}} table{{background:#1d2126}} th,td{{border-color:#2c3238}}}}
</style></head><body>
<h2>Campus Pulse — received data</h2>
<p>{count} messages saved to <code>{path}</code></p>
<table><tr><th>Room</th><th>Status</th><th>People</th><th>Empty for</th><th>Sensor</th><th>Mode</th><th>Last message</th></tr>
{rows}</table></body></html>"""


def render_page():
    now = time.time()
    with lock:
        views = [room_view(e, now) for e in rooms.values()]
        count = total_messages
    rows = []
    for v in sorted(views, key=lambda x: x["room"]):
        cls = {"occupied": "occupied", "empty": "empty"}.get(v["status"], "warn")
        mins, secs = divmod(int(v.get("empty_for_s", 0)), 60)
        rows.append(
            f"<tr><td>{html.escape(str(v['room']))}</td><td class='{cls}'>{html.escape(v['status'])}</td>"
            f"<td>{int(v.get('people_count', 0))}</td><td>{mins} min {secs} s</td>"
            f"<td>{html.escape(str(v.get('sensor')))}</td><td>{html.escape(str(v.get('mode', '')))}</td>"
            f"<td>{v['last_seen_s']} s ago</td></tr>")
    if not rows:
        rows.append("<tr><td colspan='7'>No data yet — start the bridge.</td></tr>")
    return PAGE.format(count=count, path=html.escape(log_path), rows="\n".join(rows))


total_messages = 0


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/":
            self._send(200, render_page(), "text/html")
        elif self.path == "/api/rooms":
            now = time.time()
            with lock:
                out = [room_view(e, now) for e in rooms.values()]
            self._send(200, json.dumps(out, indent=2))
        else:
            self._send(404, '{"error":"not found"}')

    def do_POST(self):
        global total_messages
        if self.path != "/api/sensor":
            return self._send(404, '{"error":"not found"}')
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 4096:
                raise ValueError("bad length")
            msg = json.loads(self.rfile.read(length))
            if not isinstance(msg, dict) or not isinstance(msg.get("room"), str) or not msg["room"]:
                raise ValueError("room missing")
        except (ValueError, json.JSONDecodeError) as e:
            return self._send(400, json.dumps({"error": str(e)}))
        now = time.time()
        with lock:
            rooms[msg["room"]] = {"msg": msg, "received": now}
            total_messages += 1
            with open(log_path, "a", encoding="utf-8") as f:
                f.write(json.dumps({"received_at": time.strftime("%Y-%m-%dT%H:%M:%S"), **msg}) + "\n")
        self._send(200, '{"ok":true}')

    def log_message(self, fmt, *args):
        pass  # keep the terminal quiet; the bridge already logs every send


def main():
    global log_path
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Local receiver for Campus Pulse data")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--log", default=os.path.join(here, "..", "data", "readings.jsonl"))
    args = ap.parse_args()
    log_path = os.path.abspath(args.log)
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Receiver on http://localhost:{args.port}  (saving to {log_path})", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

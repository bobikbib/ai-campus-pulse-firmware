# Webcam sensor: design (2026-11-27)

## Intent
The Live Lab sessions are gone, so there is no board for now. The team wants to
show that the occupancy sensor works, using a laptop webcam and microphone, in a
way that can later move to the PSOC Edge E84 with minimal change.

The laptop plays the board: the camera AI and sound analysis run **locally**,
images never leave the machine, only numbers go out.

## Goals
- Count people from the webcam with a small local model and report presence in
  the existing `CP>{json}` format.
- Report sound: a noise level and a "voice heard" flag.
- Run the presence timer and the message format on the **same C core** that the
  board will run (`src/core`, `src/app`).
- One command to start on Windows; works offline once models are in the repo.
- Dashboard showing every room: people, occupied, empty timer, noise, voice, sensor.

## Non-goals
- Booking logic / 45-minute release (belongs to the teammate's website).
- Board build. The board keeps YOLACT (aihub-deploy-vision).

## Anti-goals
- Sound must **not** affect `occupied` or `empty_for_s`. Only the camera does.
- No image or audio is stored or sent anywhere, not even to our own server.
- A stopped camera is never reported as an empty room (`sensor:"stale"`).

## Approaches considered
- **A. Browser only** (detector + timer in JS). Fastest, but the timer would be a
  JS copy, so the demo would not exercise the board code.
- **B. Python + YOLO.** More accurate, ~2 GB of dependencies, Python not installed.
- **A + C (chosen).** Browser does camera/mic/AI, the C core does the timer and
  protocol. Chosen because the user wants to show it can move to the board.

## Design
```
browser /sensor page: webcam -> MediaPipe EfficientDet-Lite0 int8 ("person" boxes)
                      mic    -> RMS level (relative dB) + YAMNet (speech classes)
   | POST /api/frame {room, people, noise_db, voice}  ~5 per second
   v
server/server.js (Node, no npm deps) --stdin--> build/cp_cam.exe (one per room)
   ^                                             "ROOM x", "DET n", "AUD db v", "TICK"
   +-------------------- stdout: CP>{json} <------+
   -> data/readings.jsonl, GET /api/rooms, dashboard at /
```

### C core changes
- `cp_app_on_audio(uint8_t noise_db, bool voice, uint32_t now_ms)`: stores the
  latest audio reading. Does not touch presence.
- Status JSON gains `"noise_db":<0-120>|null,"voice":true|false|null`
  (null when no audio reading for `CP_STALE_MS`).
- A change of `voice` triggers a report, at most once per `CP_AUDIO_REPORT_MIN_MS`
  (1000 ms) so speech flicker cannot flood the line.
- `CP_LINE_MAX` 256 -> 320 (longest status line must still fit). Board cost: +64 B
  of stack in `report()`, well inside the measured margin (BOARD_BUDGET.md).
- New host program `host/cam_main.c`: portable C11 (no `select`, builds with
  MinGW). Reads lines from stdin: `DET <n>` -> `cp_app_on_detection`,
  `AUD <db> <0|1>` -> `cp_app_on_audio`, `TICK` -> `cp_app_tick`, anything else
  -> `cp_app_on_line`. Starts in camera mode.
- On the board later: YOLACT count -> `cp_app_on_detection` (already wired),
  board mics -> `cp_app_on_audio`.

### Server (Node, standard library only)
- `GET /` dashboard, `GET /sensor` detector page, static `/web/*`.
- `POST /api/frame`: validates, starts `cp_cam` for that room if needed, writes
  `DET`/`AUD` lines. Sends `TICK` every 100 ms to every process.
- Parses `CP>` presence lines, keeps latest per room, appends to
  `data/readings.jsonl`. `GET /api/rooms`. Keeps `POST /api/sensor` so the
  existing Python bridge (real board) still works.
- Listens on 127.0.0.1 by default; `--lan` to let other devices on Wi-Fi open the
  dashboard (camera page must stay on localhost: browsers only allow the camera on
  localhost or https).

### Browser sensor page
- MediaPipe Tasks (vision + audio) and models served from `web/vendor`, `web/models`.
- Person = category "person", score >= 0.5. Shows the video with boxes locally.
- Noise: RMS of mic samples -> `20*log10(rms) + 100`, clamped 0..120. **Relative,
  not calibrated dB SPL.** Voice: YAMNet top classes include Speech /
  Conversation / Narration / Laughter / Shout with score >= 0.3.

## Builder's calls
Exact thresholds, page layout, file split inside `web/`.

# Webcam Sensor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Laptop webcam + mic act as the room sensor; the board's C core runs the presence timer and protocol; a Node server collects and shows the data.

**Architecture:** Browser page runs MediaPipe person detection and YAMNet locally and POSTs numbers to a Node server. The server feeds one `cp_cam.exe` (C core) per room over stdin and parses its `CP>` lines.

**Tech Stack:** C11 (gcc / w64devkit), Node.js standard library, MediaPipe Tasks Vision/Audio (vendored), EfficientDet-Lite0 int8, YAMNet.

**Spec:** `docs/superpowers/specs/2026-11-27-webcam-sensor-design.md`

## Global Constraints
- Sound never changes `occupied` / `empty_for_s`.
- No image or audio leaves the browser; only `{room, people, noise_db, voice}`.
- `make test` stays green; every `src/` change gets a test.
- Node: standard library only (no `npm install`).
- Room ids follow `cp_room_id_valid` (1-23 chars `A-Z a-z 0-9 - _`).
- Server binds 127.0.0.1 unless `--lan`.

## Review Focus
1. Camera tab closed / browser frozen → no `DET` → room must go `sensor:"stale"`, not empty. (Task 2 test via TICK only.)
2. Microphone denied but camera allowed → presence works, `noise_db:null, voice:null`. (Task 1 test: no audio ever.)
3. `cp_cam.exe` crashes or is missing → server keeps running, logs error, restarts on next frame. (Task 3 test.)
4. Malformed POST body (bad room, negative people, huge JSON) → 400, nothing written to stdin. (Task 3 test.)
5. Longest possible status line still fits in `CP_LINE_MAX` (23-char room, max uint32 fields). (Task 1 test.)

---

### Task 1: Audio in the C core

**Files:** Modify `src/core/protocol.h`, `src/core/protocol.c`, `src/core/cp_config.h`, `src/app/cp_app.h`, `src/app/cp_app.c`. Test: `tests/test_main.c`.

**Interfaces — Produces:**
- `void cp_app_on_audio(uint8_t noise_db, bool voice, uint32_t now_ms);` (noise_db clamped to 0..120)
- `cp_status_msg_t` gains `bool has_audio; uint8_t noise_db; bool voice;`
- JSON tail: `,"noise_db":41,"voice":true` or `,"noise_db":null,"voice":null`
- `CP_LINE_MAX 320`, `CP_AUDIO_REPORT_MIN_MS 1000u`

- [ ] Tests `test_audio`: no audio → line contains `"noise_db":null,"voice":null`; after `cp_app_on_audio(41,true,t)` next report contains `"noise_db":41,"voice":true`; audio older than `CP_STALE_MS` → null again; voice flip emits a report but two flips within 1000 ms emit only one; audio never changes `cp_app_occupied()`; `cp_format_status` with 23-char room and UINT32_MAX fields returns > 0.
- [ ] Run `make test SAN= CC=gcc` → new tests FAIL.
- [ ] Implement.
- [ ] Run → all pass.
- [ ] Commit.

### Task 2: Portable host `cp_cam`

**Files:** Create `host/cam_main.c`. Modify `Makefile` (target `cam` → `build/cp_cam`).

**Interfaces — Produces:** stdin protocol `DET <0-999>`, `AUD <0-120> <0|1>`, `TICK`, else forwarded to `cp_app_on_line`; stdout `CP>` lines, flushed per line. Clock: `timespec_get(TIME_UTC)` monotonic-enough for a demo.

- [ ] Build `make cam CC=gcc`.
- [ ] Smoke: pipe `ROOM A-101`, 5×`DET 1`, `AUD 50 1`, `TICK` → presence line with `"occupied":true` and `"noise_db":50`; only `TICK`s for 6 s → `"sensor":"stale"`.
- [ ] Commit.

### Task 3: Node server

**Files:** Create `server/server.js`, `server/test.js`.

**Interfaces — Consumes:** `build/cp_cam(.exe)` from Task 2. **Produces:** `GET /`, `GET /sensor`, static `/web/*`, `POST /api/frame {room:string, people:int 0-999, noise_db:int 0-120|null, voice:bool|null}`, `GET /api/rooms` (array of latest presence + `last_seen_s`, `status`), `POST /api/sensor` (same as receiver.py), log `data/readings.jsonl`. Flags `--port 8080`, `--lan`.

- [ ] `server/test.js` (node:test, spawns server on a random port): frames for room T-1 → `/api/rooms` shows occupied with people; bad body → 400; killing the child then posting a frame → child restarted.
- [ ] Implement, run `node --test server/test.js` → pass.
- [ ] Commit.

### Task 4: Browser sensor page + dashboard

**Files:** Create `web/sensor.html`, `web/sensor.js`, `web/dashboard.html`, `tools/fetch_models.sh`; vendored `web/vendor/*`, `web/models/*`.

- [ ] `tools/fetch_models.sh` downloads `@mediapipe/tasks-vision` + `tasks-audio` bundles/wasm (pinned version) and the two `.tflite` models.
- [ ] Sensor page: room field, start button, video with person boxes, live people/dB/voice, sends a frame every 200 ms.
- [ ] Dashboard: polls `/api/rooms` every 1 s, shows people, status, empty timer, noise, voice, sensor.
- [ ] Manual test in the in-app browser (camera may be unavailable there: then verify with a stub frame POST).
- [ ] Commit.

### Task 5: Docs
- [ ] `docs/WEBCAM_DEMO_RU.md` (how to run, in Russian); README section; HANDOFF status update. Commit.

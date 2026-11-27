# AI Campus Pulse — Firmware

Occupancy sensing for **AI Campus Pulse** (Ghost Booking Killer), in C for the
Infineon **PSOC™ Edge E84** (KIT_PSE84_EVAL).

The camera and the AI model run **on the board**. Images never leave the device.
Only this goes out:

```json
{"v":1,"type":"presence","room":"A-101","people_count":1,"occupied":true,
 "empty_for_s":0,"sensor":"ok","mode":"camera","reason":"change","uptime_s":120,"frames":2400}
```

## How it works

```
USB camera ─► AI model (on PSOC Edge) ─► person mask / boxes
                                            │
                          person_counter.c  ▼  (connected blobs → people)
                                  presence.c   (debounce, empty timer, sensor health)
                                            │
                                 protocol.c ▼  "CP>{json}" over UART
                                            │
                     bridge ─► website ─► 45-min rule releases ghost bookings
```

| File | Role |
|------|------|
| `src/core/person_counter.c` | Counts people in a segmentation mask (two-pass union-find, no malloc) |
| `src/core/presence.c` | Stable `occupied`, `empty_for_s`, camera health (`ok`/`stale`) |
| `src/core/protocol.c` | JSON-lines output, command parser |
| `src/app/cp_app.c` | Ties it together; camera **or** simulation mode |
| `port/pse84/cp_port_pse84.c` | FreeRTOS glue for the board (static task, no heap) |
| `bridge/cp_bridge.py` | Board → HTTP: forwards `CP>` lines |
| `bridge/receiver.py` | Receiver: saves data to a file, live page |
| `tools/apply_to_example.sh` | Integrates everything into the Infineon example with one command |
| `host/sim_main.c` | Runs the same core on a laptop |
| `host/cam_main.c` | `cp_cam`: the same core fed by `DET`/`AUD`/`TICK` lines on stdin (webcam demo) |
| `server/server.js` | Node server: one `cp_cam` per room, `/api/frame`, `/api/rooms`, serves `web/` |
| `web/sensor.html`, `web/sensor.js` | Browser sensor: MediaPipe person detector + YAMNet voice, sends numbers only |
| `web/dashboard.html` | Room dashboard (occupied / empty timer / noise / voice) |
| `tools/fetch_models.sh` | One-time download of the MediaPipe bundles and `.tflite` models into `web/` |
| `tests/test_main.c` | Unit tests |

Design rules:
- **One false detection doesn't reset the absence timer.** Presence needs
  `CP_ENTER_FRAMES` frames in a row.
- **A dead camera is not an empty room.** When frames stop, `sensor` becomes
  `stale`, `empty_for_s` goes to 0, and the room is never marked vacant.
- **The board doesn't know bookings.** The 45-minute release happens on the server.

## Quick start (laptop, no hardware)

```bash
make test                      # unit tests (ASan + UBSan)
make sim                       # interactive: type SIM 1, SIM 0, STATUS, HELP
./build/cp_sim --demo --speed 10   # scripted ghost-booking scenario
```

## Webcam demo (no board)

The Infineon Live Lab sessions did not happen, so the demo runs on a laptop: the
webcam and microphone stand in for the board, the browser runs the models locally
(only numbers leave the page), and the same C core (`cp_cam`) keeps the occupancy
timer. Step-by-step guide for Windows, in Russian: [docs/WEBCAM_DEMO_RU.md](docs/WEBCAM_DEMO_RU.md).

```bash
make cam CC=gcc                 # build/cp_cam
bash tools/fetch_models.sh      # once, needs internet (~30 MB)
node server/server.js           # then open http://localhost:8080/sensor and http://localhost:8080/
```

This is a laptop stand-in: nothing here has been run on the board.

## Where the data goes (bridge + receiver)

```bash
# terminal 1: receiver. Saves everything to data/readings.jsonl, page at http://localhost:8080
python3 bridge/receiver.py

# terminal 2: data source -> bridge -> receiver
./build/cp_sim --speed 60 | python3 bridge/cp_bridge.py --url http://localhost:8080/api/sensor
```

Standard library only. Instead of the simulator you can pipe in any stream of
`CP>` lines (the board's serial port, the Live Lab console). The bridge sends
every message in order. If the receiver is down, it waits and resends.

## Commands (UART / stdin)

| Command | Effect |
|---------|--------|
| `STATUS` | print status now |
| `ROOM A-101` | set room id |
| `MODE SIM` / `MODE CAM` | simulation ↔ real camera |
| `SIM 2` | simulated people count |
| `CFG HOLD 10000` | ms of emptiness before `occupied:false` |
| `CFG ENTER 3` | frames needed to confirm presence |

## On the board

Resource budget and board limits: [docs/BOARD_BUDGET.md](docs/BOARD_BUDGET.md).

See [docs/INTEGRATION.md](docs/INTEGRATION.md). Status: the core is tested on the
host. The board port is a template that has **not been built against the PSOC Edge
SDK yet**. That's the next step, in Live Lab's Developer Workspace.

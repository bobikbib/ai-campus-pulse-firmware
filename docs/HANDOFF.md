# Handoff: state of the project (2026-11-27, before the 20:00 Live Lab sessions)

## 1. The project in one paragraph

**AI Campus Pulse** is a hackathon project: a smart campus room-booking system.
Students book rooms and sometimes don't show up ("ghost bookings"). A camera +
AI on an **Infineon PSOC Edge E84** board detects whether people are in the room.
If a booked room stays empty for **45 continuous minutes**, the website releases
the booking. Images never leave the board. Only `people_count / occupied /
empty_for_s / sensor` go out. The website (campus map, bookings) belongs to the
user's teammate and is **not** in this repo, and the user said not to target it for
now. Data should go to the local receiver in `bridge/`.

Demo story (top priority): book a room → room shows as booked → nobody is inside →
absence timer runs → booking is auto-released → someone else can book it.

## 2. What's in the repo and its status

| Part | Status |
|------|--------|
| `src/core`, `src/app`: presence logic, protocol, people counter | **Done, 59 tests pass** (host, ASan/UBSan) |
| `host/sim_main.c`: simulator | Done, same output as the board |
| `port/pse84/cp_port_pse84.c` + `cp_port.h`: FreeRTOS glue | Written for the Infineon example; **compiles for Cortex-M55 against stub headers only, never built with the real SDK** |
| `tools/apply_to_example.sh`: patches the Infineon example | **Tested on local copies** of both examples (7 scenarios). Not yet run in Live Lab |
| `bridge/cp_bridge.py` + `bridge/receiver.py`: data path | **Tested end to end** with the simulator (10/10 delivered, retry works) |
| Reading `CP>` lines **from the Live Lab web page** to the bridge | **NOT BUILT.** See §6 |

Output format (one line per message on the UART):
```
CP>{"v":1,"type":"presence","room":"ROOM-101","people_count":1,"occupied":true,"empty_for_s":0,"sensor":"ok","mode":"camera","reason":"change","uptime_s":120,"frames":2400}
```
Other types: `hello` (boot), `ack`, `error`, `help`, `diag` (`stack_free_bytes`).
Spec: `src/core/protocol.h`. Rules the website should follow: §7.

## 3. Infineon Live Lab: what we saw (verified 2026-11-27)

URL: https://infineon-live-lab.tenxerlabs.com/app/psoc-edge-e84 (the user signs in).

- The board ran the **Out-of-the-Box demo** (build 4.4.0): Carousel, Music Player,
  Vision Hand Gesture, **Vision Face ID**, Video Player, Rock-Paper-Scissors.
- **EVK Display Livestream** is a video of the board's display (WebRTC/Janus). It's a
  picture, not data.
- **Console Output** panel = the board's debug UART. Our `CP>` lines will show up here.
- **Webcam Access → Enable** streams the user's own webcam to the board as its USB camera.
  The Face ID demo needs this.
- **Developer Workspace (ModusToolbox™)** is a remote desktop (Guacamole). The user says
  it **has internet**, so `git clone` from GitHub works there.
- Sessions are booked in time slots. The user has **two sessions tonight**.
- Browser-automation notes: the page overrides `eval`, so Playwright `browser_evaluate`
  fails with "eval() is blocked". Accessibility snapshots and screenshots work.
  Switching demos shows a "Proceed / Cancel" dialog. A cookie banner is present; don't
  accept it on the user's behalf.
- No public API for board data was found. The only way out is the Console Output text.

## 4. Firmware plan (based on the Infineon example sources, not yet run on the board)

**Plan A:** `mtb-example-psoc-edge-ml-aihub-deploy-vision` (YOLACT person segmentation,
USB camera, `TARGET=KIT_PSE84_EVAL_EPC2`, `TOOLCHAIN=GCC_ARM`, **Debug only**:
the README says Release fails with GCC on Helium).

- The model post-processing is a **closed `.a` library**. From its symbols and the
  disassembly of `ml_pipeline_post_process()`:
  `people = (count_idx == 0) ? 0 : (end_index - start_index)` (all `int` globals).
  When nobody is in frame, `start_index/end_index` keep stale values, so the
  `count_idx` check is needed. **Unverified. Session 1 step 6 checks it.**
- Hook point: `proj_cm55/source/inference_task.c`, right after `ml_pipeline_post_process();`.

**Plan B:** `mtb-example-psoc-edge-ml-face-id` (the OOB demo is built on it).
Count = `prediction.count` after `ifx_face_id_inference()` in `proj_cm55/lcd_task.c`.
That's a public, documented API. It counts **faces**, so someone turned away isn't counted.

**Camera:** both examples accept cameras by a **VID/PID allow-list**
(Logitech C920/C920e, HBVCAM 0.3MP 0x058F/0x5608, HBVCAM 2MP), uncompressed YUYV
320×240. Face-id's list is the same, and the OOB Face ID worked with the Live Lab
webcam, so plan A will *probably* accept it. The example logs `Vendor Id`/`Product Id`;
an unknown camera can be added to `usb_camera_task.c/.h`.

`tools/apply_to_example.sh` handles both (auto-detect). `--debug` (aihub only) inserts
a `DBG people=...` printf for verification. It's idempotent and fails loudly on a
missing anchor.

## 5. Board budget (details: docs/BOARD_BUDGET.md)

FreeRTOS heap in the example is **50 KB**, so our task is **static** (`xTaskCreateStatic`,
4 KB stack, priority = GFX level). Our cost: ~3.3 KB code, ~4.3 KB RAM, 0 B heap,
<0.1 % CPU. The mask counter is compiled out on the board (`CP_ENABLE_MASK_COUNTER=0`,
it would cost 128 KB). One `fwrite` per line so the library's per-frame `printf`s
don't split our JSON. The example converts `\n` → `\r\n`.

## 6. Open item: getting data out of Live Lab to the receiver

The board's `CP>` lines exist only as text in the Live Lab **Console Output** panel in
the user's browser. Something has to read that panel and pipe the lines into
`bridge/cp_bridge.py` (which reads stdin), or POST directly to `bridge/receiver.py`.

Options (pick with the user, ideally after seeing our firmware print `CP>` lines):
1. **From your Playwright browser session** (if your harness has Playwright MCP with a
   "run code" tool): run Node-side Playwright code that polls the Console Output
   element's text every ~1 s, keeps track of lines already seen, and `fetch`es new
   `CP>` presence lines to `http://localhost:8080/api/sensor`. Doing it Node-side
   avoids the page's CSP/eval block and browser CORS. Find the selector with a snapshot
   (the panel is titled "Console Output"). Untested.
2. **Standalone Node script with the `playwright` package**: opens Live Lab, the user
   signs in, the script polls the panel and prints `CP>` lines to stdout →
   `| python3 bridge/cp_bridge.py --url ...`. Needs `npm install playwright` (ask first).
3. **Manual fallback**: the user copies the Console Output text into a file →
   `cat file | python3 bridge/cp_bridge.py --url ...`.

Console Output probably keeps only a limited scrollback. Dedupe by line content
plus `uptime_s`, not by position.

## 6b. Supabase (added 2026-11-28)

The teammate's repo `NazariiSliusar/Room_booking_system` (private) owns the Supabase
database. Its migration `20261128000200_board_format.sql` defines
`rpc/ingest_board(room, people_count, occupied, empty_for_s, sensor, noise_db, voice,
uptime_s, device_token)`, which takes exactly our CP> fields. `room` = `rooms.board_key`
(`Z-3-03`). The database does status (`v_room_status`), history and the 45-min release
(`run_booking_jobs`). `cp_bridge.py --supabase` calls it with the public anon/publishable
key (env `SUPABASE_KEY`), latest message per room about once a second. Don't send
extra fields: PostgREST rejects unknown function arguments.
`bridge/fleet_sim.py` simulates the other rooms. Simulated data drives real bookings,
see docs/SUPABASE.md. Verified against the real project on 2026-11-28 (older
room_presence version). The ingest_board version is tested against a local fake.

## 7. What the website (or receiver) should do with the data

During an active booking:
- `occupied:true` → booked and in use.
- `sensor:"ok"` and `0 < empty_for_s < 2700` → show the absence timer.
- `sensor:"ok"` and `empty_for_s >= 2700` → **release the booking**.
- `sensor:"stale"`/`"starting"`, or no message for >15 s → **do nothing**, show
  "sensor unavailable". A dead camera is not an empty room.

The board doesn't know about bookings. The 45-minute rule lives on the server.
`bridge/receiver.py` only stores and displays data. It has no booking logic yet.

## 8. Tonight's sessions

- **Session 1 (reconnaissance):** [SESSION1_CHECKLIST.md](SESSION1_CHECKLIST.md).
  Clone, build the *unmodified* example, flash, check the camera, run `--debug` to verify
  the people count, record FPS / memory table / VID/PID / errors. Don't flash our
  firmware yet.
- **Between sessions:** fix things based on what you found, keep tests green, push
  (with the user's OK).
- **Session 2:** `git pull`, `apply_to_example.sh`, build, flash, check
  `CP>` hello/presence/diag (`stack_free_bytes ≥ 1500`), walk through the
  present → leave → `empty_for_s` grows → return scenario. Then the §6 data path.

## 9. Setting up on a new laptop

```bash
git clone https://github.com/bobikbib/ai-campus-pulse-firmware.git
cd ai-campus-pulse-firmware
make test        # expect: 59 passed, 0 failed
```
On Windows without `make`/`cc` you can skip the host build. The board build happens
in Live Lab anyway. `gh` may not be installed. Pushing needs the user's GitHub login
(account `bobikbib`), and the user handles that.

# Running on the PSOC Edge E84 (KIT_PSE84_EVAL)

What we verified in Infineon Live Lab (2026-11-27):

- The board runs the **PSOC Edge Out-of-the-Box demo** (build 4.4.0). It has
  Vision Face ID, Vision Hand Gesture and Rock-Paper-Scissors demos.
- The camera input is a **USB camera**. In Live Lab your own webcam is streamed
  to the board ("Webcam Access → Enable").
- The board's debug UART is shown in the **Console Output** panel. That is where
  our `CP>{...}` lines will appear.
- **Developer Workspace (ModusToolbox™)** gives you a remote desktop for building
  and flashing your own firmware.
- Live Lab has no public API for reading board data. A bridge has to read the
  console output (see "Getting data to the website").

## 1. Base example: `mtb-example-psoc-edge-ml-aihub-deploy-vision`

https://github.com/Infineon/mtb-example-psoc-edge-ml-aihub-deploy-vision

- YOLACT person segmentation: it finds **each person separately**, including
  people turned away from the camera.
- USB camera, `TARGET=KIT_PSE84_EVAL_EPC2`, `TOOLCHAIN=GCC_ARM`, `CONFIG=Debug`.
- The model post-processing is a closed `.a` library. We read the people
  count from its exported variables. Details and the exact code changes are in
  [port/pse84/aihub_deploy_vision.patch.md](../port/pse84/aihub_deploy_vision.patch.md).
- Plan B: `mtb-example-psoc-edge-ml-face-id`, which the Out-of-the-Box demo uses.

## 2. Add our code

Steps for session 1: [SESSION1_CHECKLIST.md](SESSION1_CHECKLIST.md).
Steps for session 2: "Patch 2" in `aihub_deploy_vision.patch.md`.

Since the example gives a people count directly, `person_counter.c` (counting
blobs in a mask) is not needed for it. We keep it for models that only output a
mask.

## 3. Tuning on the real camera

- `CP_ENTER_FRAMES` (default 3) is how many frames in a row it takes to
  count presence. Tune it to the real FPS.
- `CFG HOLD <ms>` sets how long the room must look empty before
  `occupied:false` (default 10 s).

## 4. Getting data to the website

The board prints JSON lines. A bridge reads them and POSTs them to the website:

- **Live Lab:** a Playwright script reads the Console Output panel, filters
  `CP>` lines and sends them on.
- **Physical board over USB:** the bridge reads the serial port directly.
- **Development without hardware:** `./build/cp_sim | bridge`, same format.

The board never decides to cancel bookings. It doesn't know the schedule.
It reports `empty_for_s`. The **server** releases a booking once
`sensor == "ok"` and `empty_for_s >= 2700` (45 min) during an active booking.
`sensor == "stale"` or `"starting"` means "unknown", and the server must not
release on it.

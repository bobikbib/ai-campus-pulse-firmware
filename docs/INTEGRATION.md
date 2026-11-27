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

## 1. Pick a base example (in Developer Workspace → ModusToolbox Project Creator)

Pick the example for KIT_PSE84_EVAL that uses the **USB camera**. Candidates:

| Example (Infineon docs name)                     | Model output        | Hook to call              |
|--------------------------------------------------|---------------------|---------------------------|
| Machine Learning **AI Hub Deploy Vision** (person segmentation) | person mask | `cp_port_on_person_mask` |
| Machine Learning **Face ID Demo**                | face boxes          | `cp_port_on_detections`   |
| Machine Learning **DEEPCRAFT Deploy Vision**     | object boxes        | `cp_port_on_detections`   |

Person segmentation fits best because it detects whole bodies, including people
facing away from the camera. Face ID only counts visible faces.
Check the example's README for the camera interface. The AI kit uses a DVP camera,
the EVAL kit uses USB.

## 2. Add our code

1. Copy `src/core/`, `src/app/` and `port/pse84/cp_port_pse84.c` into the CM55 project.
2. Add these folders to the project's Makefile `SOURCES`/`INCLUDES`. ModusToolbox
   usually picks up every `.c` file under the app folder on its own.
3. In `main()`, call `cp_port_start()` before `vTaskStartScheduler()`.
4. In the example's inference loop, right after it gets the model output,
   call **one** of the hooks:
   ```c
   cp_port_on_person_mask(mask_u8, mask_w, mask_h);   // segmentation
   // or
   cp_port_on_detections(num_person_boxes);           // detection
   ```
   If the mask is float/int8, convert it to 0..255 first. With a mask over
   256×256, raise `CP_MASK_MAX_PIXELS`.
5. Build, then flash via Live Lab. Watch Console Output for `CP>{"v":1,"type":"hello"...}`.

## 3. Tuning on the real camera

- `CP_MIN_BLOB_AREA` is the minimum blob size that counts as a person, in mask
  pixels. Start at roughly 1–2 % of the mask area.
- `CP_MASK_THRESHOLD` is the confidence cut-off.
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

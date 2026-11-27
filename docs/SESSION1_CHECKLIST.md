# Live Lab, session 1: reconnaissance (≈ 1 hour)

**Goal:** confirm that we can build and flash in Live Lab, find out what the camera
and the AI actually output, and leave ourselves a clean path for session 2.
Our own firmware does **not** get flashed in this session.

Record the answers in the "Results" section at the end of this file.

---

## 0. Before you start (2 min)
- [ ] Open Live Lab in the Playwright window and sign in. Claude watches through
      screenshots.
- [ ] Have this checklist open next to it.

## 1. Look around (5 min)
- [ ] Open **Developer Workspace (ModusToolbox™)**.
- [ ] Is there a terminal? (`modus-shell` on Windows, a regular terminal on Linux)
- [ ] `git --version`: is git installed?
- [ ] ModusToolbox version (Help → About, or the folder name `ModusToolbox/tools_3.x`)
- [ ] Is there a GCC_ARM toolchain? (`arm-none-eabi-gcc --version`)

## 2. Make sure our repository downloads (1 min)
```bash
git clone https://github.com/bobikbib/ai-campus-pulse-firmware.git
```
- [ ] It downloaded without asking for a password.

## 3. Get the Infineon example (10 min)
Option A, in the terminal:
```bash
git clone https://github.com/Infineon/mtb-example-psoc-edge-ml-aihub-deploy-vision.git
cd mtb-example-psoc-edge-ml-aihub-deploy-vision
make getlibs
```
Option B: Project Creator → BSP **KIT_PSE84_EVAL** → template
"Machine Learning AI Hub Deploy Vision".

- [ ] Which BSP does Live Lab use: `KIT_PSE84_EVAL_EPC2` or `KIT_PSE84_EVAL_EPC4`?
      (Our repo defaults to EPC2. Check the chip marking, or whatever Live Lab says.)
- [ ] How long did `make getlibs` take?

## 4. Build the UNMODIFIED example (10–15 min)
```bash
make build -j8
```
Leave `CONFIG=Debug` as it is. The README warns that Release does not build with GCC_ARM.
- [ ] Did it build? Build time: ____
- [ ] If it failed, copy the first error into Results.
- [ ] **Memory:** at the end of the build ModusToolbox prints a memory usage table
      (RAM/flash per region). Take a screenshot: it tells us how much headroom
      we have for our ~4.3 KB (see `docs/BOARD_BUDGET.md`).

## 5. Flash and run (5 min)
```bash
make program
```
(or the Program button in the IDE, or whatever Live Lab provides for flashing)
- [ ] Console Output shows `PSOC Edge MCU: AI Hub deploy vision`.
- [ ] Turn on **Webcam Access** in Live Lab.
- [ ] **Does the example accept the Live Lab camera?** The example checks the camera
      against an allow-list of VID/PID codes (Logitech C920/C920e, HBVCAM 0.3MP/2MP).
      The face-id example, which the Out-of-the-Box demo is built on and which has
      already worked with the Live Lab camera, uses **the same list**. So the
      camera will most likely be accepted.
      In the console, find the `Vendor Id = 0x....` / `Product Id = 0x....` lines
      and record them. If the camera is rejected, its code can be added to
      `usb_camera_task.c` / `.h`.
- [ ] Is the live image visible on the EVK display, with blue masks over people?

## 6. Check how we read the people count (10 min)
The script inserts the debug `printf` for you (you don't have to type anything):
```bash
bash ../ai-campus-pulse-firmware/tools/apply_to_example.sh . --debug
make build -j8 && make program
```

| Situation | Expected `people` | Got |
|-----------|-------------------|-----|
| Nobody in frame | 0 | |
| You in frame | 1 | |
| You + a friend | 2 | |
| You turn away from the camera | 1 | |

- [ ] Lines per second (FPS): ____
- [ ] Do the numbers match? If not, copy 10 lines of output.

## 7. Undo the changes
```bash
git checkout proj_cm55
```

---

## Plan B (if the example doesn't accept the camera or doesn't build)

1. `mtb-example-psoc-edge-ml-face-id`. The Out-of-the-Box demo is built on it, and it
   works with the Live Lab camera. The script supports it:
   ```bash
   git clone https://github.com/Infineon/mtb-example-psoc-edge-ml-face-id.git
   cd mtb-example-psoc-edge-ml-face-id && make getlibs
   bash ../ai-campus-pulse-firmware/tools/apply_to_example.sh .
   make build -j8 && make program
   ```
   Downside: it counts **faces**, so a person who has turned away is not counted.
   The 10 s hold window smooths this out.
2. If that fails too: in the demo, show Face ID on the board as "the AI is running
   on the board", and drive the booking logic from the simulator (`cp_sim`).

---

## Results (fill in during the session)

- Memory after the example build (free RAM / flash): 
- ModusToolbox: 
- git in Developer Workspace: 
- BSP (EPC2/EPC4): 
- getlibs / build / program times: 
- Camera VID/PID (from the log): 
- Camera accepted by the example: 
- Count check (step 6): 
- FPS: 
- Errors: 

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

## 5. Flash and run (5 min)
```bash
make program
```
(or the Program button in the IDE, or whatever Live Lab provides for flashing)
- [ ] Console Output shows `PSOC Edge MCU: AI Hub deploy vision`.
- [ ] Turn on **Webcam Access** in Live Lab.
- [ ] **Does the example accept the Live Lab camera?** The example only supports
      specific cameras (HBVCAM OV7675, Logitech C920/C920e, HBVCAM OS02F10).
      If the screen shows "camera not supported", that is the most important
      result of this session (see "Plan B").
- [ ] Is the live image visible on the EVK display, with blue masks over people?

## 6. Check how we read the people count (10 min)
Apply **patch 1** from `port/pse84/aihub_deploy_vision.patch.md` (debug `printf`),
then run `make build && make program` again.

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
git checkout proj_cm55/source/inference_task.c
```

---

## Plan B (if the example doesn't accept the camera or doesn't build)

1. Try `mtb-example-psoc-edge-ml-face-id`. The Out-of-the-Box demo is built on
   this one, and it **definitely** works with the Live Lab webcam.
   Find where it gets the face count (Claude will help in the session).
2. If that fails too: in the demo, show Face ID on the board as "the AI is running
   on the board", and drive the booking logic from the simulator (`cp_sim`).

---

## Results (fill in during the session)

- ModusToolbox: 
- git in Developer Workspace: 
- BSP (EPC2/EPC4): 
- getlibs / build / program times: 
- Camera accepted by the example: 
- Count check (step 6): 
- FPS: 
- Errors: 

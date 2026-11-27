# CLAUDE.md — AI Campus Pulse firmware

You are continuing work started by another Claude session on a different laptop.
**Read [docs/HANDOFF.md](docs/HANDOFF.md) first.** It has the full context, what
has been verified, and what is still open.

## Talking to the user
- The user writes in **Russian**. Answer in Russian, plainly, step by step.
  They are a student at a hackathon, so explain technical choices simply.
- The user follows a Russian step-by-step guide: [docs/LIVE_LAB_GUIDE_RU.md](docs/LIVE_LAB_GUIDE_RU.md).
  Use the same step numbers when you guide them.

## What this repo is
C firmware core for **Infineon PSOC Edge E84 (KIT_PSE84_EVAL)**: it counts people
with the on-board camera AI and prints `CP>{json}` presence lines on the UART.
A website applies the 45-minute "ghost booking" rule. Plus a Python bridge
and a local receiver for the data.

## Commands
```bash
make test                                   # 76 checks (ASan+UBSan), must stay green
./build/cp_sim --demo --speed 10            # scripted demo
python3 bridge/receiver.py                  # receiver on http://localhost:8080
./build/cp_sim --speed 60 | python3 bridge/cp_bridge.py --url http://localhost:8080/api/sensor
bash tools/apply_to_example.sh <infineon_example_dir> [--debug]   # patch the Infineon example
```
No dependencies beyond a C compiler, make and python3 (stdlib only).

## Hard rules
- **Never type passwords, GitHub tokens or other credentials** anywhere, and above
  all not into the Live Lab remote desktop. The user signs in themselves.
  The repo is public precisely so Live Lab can clone it without credentials.
- **Ask before `git push`.** Show the user what changed first.
- Commits: author `AI Campus Pulse <noreply@example.com>` (the user has no git
  identity configured), ending with the Co-Authored-By line from your harness.
  Don't put the user's email anywhere.
- Don't claim something works on the board until you've seen it in Live Lab
  Console Output. Most board-side facts in HANDOFF.md are marked
  *verified* or *unverified*. Keep that distinction.
- Keep `make test` passing. Add a test when you change `src/`.
- Board RAM is tight inside the Infineon example (FreeRTOS heap 50 KB). Read
  [docs/BOARD_BUDGET.md](docs/BOARD_BUDGET.md) before adding anything to the board side.

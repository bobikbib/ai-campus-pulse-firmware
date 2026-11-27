# Board resource budget: what the board can handle

## Chip and kit (PSOC Edge E84, KIT_PSE84_EVAL)

| Resource | Amount | Source |
|----------|--------|--------|
| Cortex-M55 (application core, our code) | 400 MHz, Helium | Infineon datasheet |
| Ethos-U55 NPU (runs the model) | 400 MHz, 128 MAC/cycle | datasheet |
| Cortex-M33 (boot/secure core) | 200 MHz | datasheet |
| System SRAM | up to 5 MB + 1 MB for the low-power M33 domain | datasheet |
| M55 tightly coupled memory (TCM) | 512 KB | datasheet |
| RRAM (non-volatile) | 512 KB | datasheet |
| On the kit (SOM) | 128 Mb QSPI flash, 1 Gb Octal flash, 128 Mb Octal HyperRAM | Zephyr board docs |

## Example `aihub-deploy-vision` (what's already running before us)

| Item | Value | Where it comes from |
|------|-------|---------------------|
| FreeRTOS heap | **50 KB** (`configTOTAL_HEAP_SIZE`) | `proj_cm55/source/FreeRTOSConfig.h` |
| Example tasks | USB camera (prio 4, 20 KB), inference (3, 64 KB), GFX (2, 10 KB) | `proj_cm55/source/main.c` |
| `configMAX_PRIORITIES` / tick | 7 / 1000 Hz | FreeRTOSConfig.h |
| Stack overflow check | `configCHECK_FOR_STACK_OVERFLOW = 2`, i.e. an overflow triggers `configASSERT` and the board halts | FreeRTOSConfig.h |
| `uxTaskGetStackHighWaterMark` | disabled | FreeRTOSConfig.h, so we measure stack ourselves |
| Model | YOLACT person segmentation int8, 224×224 input, on the NPU; model in `.cy_socmem_data` | Makefile, pipeline.h |
| UART | retarget-io, `\n` → `\r\n` conversion | Makefile `DEFINES` |
| Build | GCC_ARM 14.2.1, **Debug only** (Release fails with GCC on Helium) | README |
| Cameras | HBVCAM OV7675, Logitech C920/C920e, HBVCAM OS02F10 | README, `main.c` |

## What our code adds (measured: clang, Cortex-M55, -Os)

| Resource | Our cost | Margin |
|----------|----------|--------|
| Code (.text) | ~3.3 KB | megabytes of flash, negligible |
| Constants (.rodata) | ~0.9 KB | negligible |
| RAM (.bss) | ~4.3 KB, of which 4 KB is our task's static stack | out of ~5 MB SRAM |
| FreeRTOS heap | **0 B** (`xTaskCreateStatic`) | the example's 50 KB is left untouched |
| Stack | deepest path ≈ 1.8 KB (including libc `snprintf`) out of 4 KB | ~2× margin; the board reports the real number in a `diag` line |
| CPU | 1 tick per 50 ms, a few hundred instructions; inference hook = 2 stores | well under 0.1% of the M55 |
| UART | ~230 B / 5 s + one line per change | < 1% of 115200 baud |
| Line buffer / audio | `CP_LINE_MAX` raised 256 to 320 (+64 B stack in the report/ack/error paths; worst-case status line is about 250 chars). Audio state is about 25 B of static RAM. Not re-measured on the board | negligible |

Decisions we made because of the constraints:
- **The mask counter is off on the board** (`CP_ENABLE_MASK_COUNTER=0`). Otherwise
  it would add 128 KB of RAM for nothing, since YOLACT already returns a count.
- **Static task** instead of `xTaskCreate`, so the task cannot fail for lack of
  heap and its memory is visible at link time.
- **Our priority equals the GFX task's** (2). With priority 1 we could starve if
  the CPU is busy. We block for 50 ms between ticks, so inference (3) and USB (4)
  are never slowed down.
- **One `fwrite` per line**, so our JSON doesn't get mixed with the library's
  per-frame `printf` output.
- **No float in printf.** newlib-nano may not support `%f`.

## Still to verify on the board (session 1)

- [ ] The example builds within the memory limits **without** our code (the
      baseline). If it barely fits, the linker map shows how much is free.
- [ ] Our `diag` line shows `stack_free_bytes` ≥ 1500.
- [ ] `CP>` lines arrive intact, not mixed with other output.
- [ ] Real FPS, so we can tune `CP_ENTER_FRAMES` and `CP_STALE_MS`.
- [ ] The Live Lab camera is accepted by the example.

Sources: [PSOC Edge E8x datasheet](https://www.infineon.com/assets/row/public/documents/30/49/infineon-psoc-edge-e8x-consumer-datasheet-datasheet-en.pdf),
[Zephyr: KIT_PSE84_EVAL](https://docs.zephyrproject.org/latest/boards/infineon/kit_pse84_eval/doc/index.html),
[aihub-deploy-vision](https://github.com/Infineon/mtb-example-psoc-edge-ml-aihub-deploy-vision).

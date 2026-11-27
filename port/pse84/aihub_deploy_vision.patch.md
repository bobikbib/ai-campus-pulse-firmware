# Changes to `mtb-example-psoc-edge-ml-aihub-deploy-vision`

These changes are prepared from the example's source code (revision of 2026-10-21).
They have not been built yet.

## How we get the people count (the model code is a closed `.a` library)

`ml_pipeline_eur_libs/YOLACT_PERSON_SEGMENTATION_pipeline.a` comes without
sources. Its exported symbols and the disassembly of `ml_pipeline_post_process()`
show this:

| Symbol | Type | Meaning (from the disassembly) |
|--------|------|--------------------------------|
| `count_idx` | `int` | `= getBox()`, the number of boxes above the confidence threshold **before** de-duplication. Stays 0 when nobody is in frame |
| `candidate_indices` | `int[100]` | Box indices. Kept boxes are appended at `[start_index, end_index)` |
| `start_index`, `end_index` | `int` | Range of the people that survive NMS (IoU threshold 0.4) |

```
people = (count_idx == 0) ? 0 : (end_index - start_index)
```

When `count_idx == 0` the library returns early and does **not** update
`start_index`/`end_index`. They keep the previous frame's values, which is why
we check `count_idx` first.

⚠️ This is reverse-engineered. **Session 1 must confirm it** with the debug
`printf` below: 0 with nobody in frame, 1 with one person, 2 with two.

## Patch 1: `proj_cm55/source/inference_task.c` (session 1: debug only)

```c
#include "pipeline.h"
#include <stdio.h>

extern int count_idx, start_index, end_index;

void ml_pipeline_task(void *arg)
{
    ml_pipeline_model_init();
    while (1) {
        getData();
        ml_pipeline_preprocess();
        ml_pipeline_inference();
        ml_pipeline_post_process();

        int people = (count_idx == 0) ? 0 : (end_index - start_index);
        printf("DBG people=%d count_idx=%d start=%d end=%d\r\n",
               people, count_idx, start_index, end_index);
    }
}
```

## Patch 2: session 2, the real integration

1. Copy `src/core/*`, `src/app/*` and `port/pse84/cp_port_pse84.c` into
   `proj_cm55/source/campus_pulse/`. The ModusToolbox Makefile picks up
   every `.c` file in the project automatically.
2. `inference_task.c`: replace the `printf` from patch 1 with:
   ```c
   extern void cp_port_on_detections(uint16_t num_people);
   ...
   int people = (count_idx == 0) ? 0 : (end_index - start_index);
   if (people < 0) people = 0;          /* defensive */
   cp_port_on_detections((uint16_t)people);
   ```
3. `main.c`: before `vTaskStartScheduler();` add:
   ```c
   extern bool cp_port_start(void);
   if (!cp_port_start()) { CY_ASSERT(0); }
   ```
4. Do **not** enable `CP_ENABLE_MASK_COUNTER` (it costs 128 KB of RAM and the
   example doesn't need it).
5. Build, flash and look for these lines in Console Output:
   `CP>{"v":1,"type":"hello",...}`, followed by `CP>{..."type":"presence"...}`;
   after 30 s, `CP>{"v":1,"type":"diag","stack_free_bytes":...}`. It should be ≥ 1500.

## Known noise

The library itself `printf`s inference timing every frame. Our lines start
with `CP>`, so the bridge filters on that prefix.

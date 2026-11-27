/*
 * PSOC Edge E84 port — glue between an Infineon ModusToolbox vision example
 * and the AI Campus Pulse core.
 *
 * STATUS: template, NOT yet compiled against the board SDK.
 * It only uses things every ModusToolbox FreeRTOS example already has:
 *   - printf() routed to the debug UART by retarget-io
 *   - FreeRTOS tasks and xTaskGetTickCount()
 * Everything board-specific is marked "TODO(board)" and must be checked
 * against the exact example you start from (see docs/INTEGRATION.md).
 *
 * Build: copy src/core, src/app and this file into the CM55 application of the
 * example, add them to the Makefile SOURCES/INCLUDES, then call the hooks below.
 */
#include "cp_app.h"

#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#define CP_TASK_STACK_WORDS  (1024u)
#define CP_TASK_PRIORITY     (tskIDLE_PRIORITY + 1u)
#define CP_TICK_PERIOD_MS    (50u)

static uint32_t cp_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void cp_write_line(const char *line)
{
    /* Goes to the KitProg3 UART -> visible in Live Lab "Console Output". */
    printf("%s\r\n", line);
}

/* ---------------------------------------------------------------------------
 * Hooks to call from the vision example's inference loop (pick ONE).
 * They must be called from a single task, or protect cp_app_* with a mutex.
 * ------------------------------------------------------------------------ */

/* Object/face detection models: pass the number of "person"/"face" boxes
 * whose score passed the example's own threshold. */
void cp_port_on_detections(uint16_t num_people)
{
    cp_app_on_detection(num_people, cp_now_ms());
}

/* Person-segmentation model: pass the output mask (0..255 per pixel). */
void cp_port_on_person_mask(const uint8_t *mask, uint16_t width, uint16_t height)
{
    cp_app_on_mask(mask, width, height, cp_now_ms());
}

/* ---------------------------------------------------------------------------
 * Command input.
 * TODO(board): feed received UART lines here. Simplest option: if the example
 * already reads the console (the OOB demo does: "Enter your choice:"),
 * forward lines that start with "CP " to cp_app_on_line(line + 3, ...).
 * ------------------------------------------------------------------------ */
void cp_port_on_console_line(const char *line)
{
    cp_app_on_line(line, cp_now_ms());
}

static void cp_task(void *arg)
{
    (void)arg;
    const cp_platform_t plat = { .write_line = cp_write_line };
    /* Start in camera mode on the board; "MODE SIM" switches for demos. */
    cp_app_init(&plat, false, cp_now_ms());

    for (;;) {
        cp_app_tick(cp_now_ms());
        vTaskDelay(pdMS_TO_TICKS(CP_TICK_PERIOD_MS));
    }
}

/* Call once from main() of the CM55 app, before vTaskStartScheduler(). */
bool cp_port_start(void)
{
    return xTaskCreate(cp_task, "campus_pulse", CP_TASK_STACK_WORDS,
                       NULL, CP_TASK_PRIORITY, NULL) == pdPASS;
}

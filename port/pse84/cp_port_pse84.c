/*
 * PSOC Edge E84 port — glue between Infineon's
 * mtb-example-psoc-edge-ml-aihub-deploy-vision (YOLACT person segmentation,
 * USB camera) and the AI Campus Pulse core.
 *
 * STATUS: written against that example's sources (2026-10-21 revision),
 * NOT yet compiled on the board. See docs/SESSION1_CHECKLIST.md.
 *
 * Threading: the example's inference task calls cp_port_on_detections()
 * once per frame. It only stores the count in a mailbox; our own low-priority
 * task does all cp_app_* work. So cp_app_* is only ever called from one task
 * and needs no locking.
 */
#include "cp_app.h"

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#define CP_TASK_STACK_WORDS  (1024u)
#define CP_TASK_PRIORITY     (tskIDLE_PRIORITY + 1u)
#define CP_TICK_PERIOD_MS    (50u)

/* Mailbox written by the inference task, read by cp_task.
 * 32-bit aligned loads/stores are atomic on Cortex-M55. The writer stores
 * the count first, then bumps the sequence number. */
static volatile uint32_t s_people;
static volatile uint32_t s_frame_seq;

static uint32_t cp_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void cp_write_line(const char *line)
{
    /* Goes to the debug UART -> visible in Live Lab "Console Output". */
    printf("%s\r\n", line);
}

/* Called from the inference task after every frame. Cheap, never blocks. */
void cp_port_on_detections(uint16_t num_people)
{
    s_people = num_people;
    __asm volatile ("dmb" ::: "memory"); /* count visible before seq */
    s_frame_seq = s_frame_seq + 1u;
}

static void cp_task(void *arg)
{
    (void)arg;
    const cp_platform_t plat = { .write_line = cp_write_line };
    uint32_t seen_seq = s_frame_seq;

    /* Camera mode by default on the board. */
    cp_app_init(&plat, false, cp_now_ms());

    for (;;) {
        const uint32_t seq = s_frame_seq;
        if (seq != seen_seq) {
            __asm volatile ("dmb" ::: "memory");
            seen_seq = seq;
            /* If several frames arrived since the last tick, only the latest
             * count matters; the debounce counts ticks with new data. */
            cp_app_on_detection((uint16_t)s_people, cp_now_ms());
        }
        cp_app_tick(cp_now_ms());
        vTaskDelay(pdMS_TO_TICKS(CP_TICK_PERIOD_MS));
    }
}

/* Call once from main() of proj_cm55, before vTaskStartScheduler(). */
bool cp_port_start(void)
{
    return xTaskCreate(cp_task, "campus_pulse", CP_TASK_STACK_WORDS,
                       NULL, CP_TASK_PRIORITY, NULL) == pdPASS;
}

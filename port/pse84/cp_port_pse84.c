/*
 * PSOC Edge E84 port — glue between Infineon's
 * mtb-example-psoc-edge-ml-aihub-deploy-vision (YOLACT person segmentation,
 * USB camera) and the AI Campus Pulse core.
 *
 * STATUS: written against that example's sources (2026-10-21 revision),
 * NOT yet compiled on the board. See docs/SESSION1_CHECKLIST.md.
 *
 * Resource budget (see docs/BOARD_BUDGET.md):
 *   - RAM: 4 KB static stack + static TCB + ~0.3 KB state. Nothing from the
 *     FreeRTOS heap (the example only has 50 KB of it).
 *   - CPU: one 50 ms tick doing a few hundred instructions; the inference
 *     hook is two stores.
 *   - UART: ~230 bytes every 5 s plus one line per state change.
 *
 * Threading: the inference task calls cp_port_on_detections() once per frame.
 * It only stores the count in a mailbox; our own task does all cp_app_* work,
 * so cp_app_* is only ever called from one task and needs no locking.
 * The audio task calls cp_port_on_audio(); it analyzes the samples itself
 * (cheap: one pass, no buffering) and queues one small result per 100 ms in a
 * single-producer/single-consumer ring that our task drains.
 *
 * Commands (ROOM, NFC <uid>, ...) arrive on the debug UART RX line. Our task
 * polls the SCB RX FIFO every tick (PDL calls, no interrupt, nothing shared
 * with retarget-io's TX path). UNVERIFIED on the board: whether the Live Lab
 * console can send input at all.
 */
#include "cp_port.h"
#include "cp_app.h"
#include "cp_config.h"
#include "audio.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#ifndef CP_PORT_UART_RX
#define CP_PORT_UART_RX 1
#endif
#if CP_PORT_UART_RX
#include "cy_pdl.h"
#include "cybsp.h"   /* CYBSP_DEBUG_UART_HW */
#endif

#define CP_TASK_STACK_WORDS  (1024u)                     /* 4 KB */
/* Same level as the example's GFX task (configMAX_PRIORITIES - 5), so time
 * slicing guarantees us CPU even when GFX is busy. We block 50 ms between
 * ticks, so inference (higher priority) is never held up. */
#define CP_TASK_PRIORITY     (configMAX_PRIORITIES - 5)
#define CP_TICK_PERIOD_MS    (50u)
#define CP_DIAG_PERIOD_MS    (600000u)                   /* every 10 min */

static StackType_t  s_stack[CP_TASK_STACK_WORDS];
static StaticTask_t s_tcb;

/* Mailbox written by the inference task, read by cp_task.
 * Aligned 32-bit loads/stores are atomic on Cortex-M55. The writer stores
 * the count first, then bumps the sequence number. */
static volatile uint32_t s_people;
static volatile uint32_t s_frame_seq;
/* Written by cp_task: may the inference loop analyze the next frame? */
static volatile uint32_t s_frame_wanted = 1u;

/* Audio results: written by the audio task, read by cp_task. */
#define CP_AUDIO_RING 16u   /* 1.6 s of windows: plenty for a 50 ms consumer */
static cp_audio_t        s_audio;
static bool              s_audio_ready;
static cp_audio_window_t s_audio_ring[CP_AUDIO_RING];
static volatile uint32_t s_audio_head;  /* written by producer */
static volatile uint32_t s_audio_tail;  /* written by consumer */

#if CP_PORT_UART_RX
static char     s_rx_line[64];
static uint32_t s_rx_len;
static bool     s_rx_overflow;
#endif

static uint32_t cp_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* Several tasks print to the same UART (the AI library prints timing every
 * frame). Build the whole line first and hand it over in ONE fwrite call so
 * retarget-io writes it in one go instead of character runs that could
 * interleave with another task's output. The example defines
 * CY_RETARGET_IO_CONVERT_LF_TO_CRLF, so "\n" already becomes "\r\n". */
static void cp_write_line(const char *line)
{
    char buf[CP_LINE_MAX + 2];
    size_t n = strlen(line);
    if (n > CP_LINE_MAX) {
        n = CP_LINE_MAX;
    }
    memcpy(buf, line, n);
    buf[n++] = '\n';
    fwrite(buf, 1, n, stdout);
    fflush(stdout);
}

/* FreeRTOS pre-fills task stacks with 0xA5 (configCHECK_FOR_STACK_OVERFLOW=2
 * in the example). The stack grows down, so untouched words sit at the start
 * of our own buffer. uxTaskGetStackHighWaterMark is disabled in the example's
 * FreeRTOSConfig.h, so we measure it ourselves. */
static uint32_t cp_stack_free_words(void)
{
    uint32_t n = 0;
    while (n < CP_TASK_STACK_WORDS && s_stack[n] == (StackType_t)0xA5A5A5A5u) {
        n++;
    }
    return n;
}

static void cp_report_diag(void)
{
    char line[96];
    snprintf(line, sizeof line,
             "CP>{\"v\":1,\"type\":\"diag\",\"stack_free_bytes\":%lu,\"stack_bytes\":%lu}",
             (unsigned long)(cp_stack_free_words() * sizeof(StackType_t)),
             (unsigned long)sizeof s_stack);
    cp_write_line(line);
}

/* Called from the inference task after every frame. Cheap, never blocks. */
void cp_port_on_detections(uint16_t num_people)
{
    s_people = num_people;
    __asm volatile ("dmb" ::: "memory"); /* count visible before seq */
    s_frame_seq = s_frame_seq + 1u;
}

void cp_port_on_audio(const int16_t *pcm, uint32_t n)
{
    cp_audio_window_t w[4];
    if (!s_audio_ready) {
        cp_audio_cfg_t cfg;
        cp_audio_default_cfg(&cfg);
        cp_audio_init(&s_audio, &cfg);
        s_audio_ready = true;
    }
    const size_t k = cp_audio_feed(&s_audio, pcm, n, w, 4);
    for (size_t i = 0; i < k; i++) {
        const uint32_t head = s_audio_head;
        if (head - s_audio_tail >= CP_AUDIO_RING) {
            break;                              /* consumer behind: drop newest */
        }
        s_audio_ring[head % CP_AUDIO_RING] = w[i];
        __asm volatile ("dmb" ::: "memory");    /* data visible before head */
        s_audio_head = head + 1u;
    }
}

static void cp_drain_audio(void)
{
    while (s_audio_tail != s_audio_head) {
        __asm volatile ("dmb" ::: "memory");
        const cp_audio_window_t w = s_audio_ring[s_audio_tail % CP_AUDIO_RING];
        __asm volatile ("dmb" ::: "memory");
        s_audio_tail = s_audio_tail + 1u;
        cp_app_on_audio_window(w.level_spl, w.voice);
    }
}

#if CP_PORT_UART_RX
/* Collect command characters from the debug UART; one line -> cp_app_on_line. */
static void cp_poll_uart(uint32_t now)
{
    while (Cy_SCB_UART_GetNumInRxFifo(CYBSP_DEBUG_UART_HW) > 0u) {
        const char c = (char)Cy_SCB_UART_Get(CYBSP_DEBUG_UART_HW);
        if (c == '\r' || c == '\n') {
            if (s_rx_len > 0u && !s_rx_overflow) {
                s_rx_line[s_rx_len] = '\0';
                cp_app_on_line(s_rx_line, now);
            }
            s_rx_len = 0u;
            s_rx_overflow = false;
        } else if (s_rx_len + 1u < sizeof s_rx_line) {
            s_rx_line[s_rx_len++] = c;
        } else {
            s_rx_overflow = true;               /* drop over-long lines */
        }
    }
}
#endif

void cp_port_frame_gate(void)
{
    while (!s_frame_wanted) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void cp_task(void *arg)
{
    (void)arg;
    const cp_platform_t plat = { .write_line = cp_write_line };
    uint32_t seen_seq = s_frame_seq;
    uint32_t last_diag = cp_now_ms();
    bool first_diag_done = false;

    /* Camera mode by default on the board. */
    cp_app_init(&plat, false, cp_now_ms());

    for (;;) {
        const uint32_t now = cp_now_ms();
        const uint32_t seq = s_frame_seq;
        if (seq != seen_seq) {
            __asm volatile ("dmb" ::: "memory");
            seen_seq = seq;
            /* If several frames arrived since the last tick only the latest
             * count matters; debounce counts ticks that carried new data. */
            cp_app_on_detection((uint16_t)s_people, now);
        }
        cp_drain_audio();
#if CP_PORT_UART_RX
        cp_poll_uart(now);
#endif
        cp_app_tick(now);
        s_frame_wanted = cp_app_camera_frame_wanted(cp_now_ms()) ? 1u : 0u;

        /* First diag after 30 s (stack has seen every code path by then),
         * then every 10 min. */
        if ((!first_diag_done && (now - last_diag) >= 30000u) ||
            (now - last_diag) >= CP_DIAG_PERIOD_MS) {
            cp_report_diag();
            last_diag = now;
            first_diag_done = true;
        }
        vTaskDelay(pdMS_TO_TICKS(CP_TICK_PERIOD_MS));
    }
}

/* Call once from main() of proj_cm55, before vTaskStartScheduler().
 * Static allocation: cannot fail for lack of FreeRTOS heap. */
bool cp_port_start(void)
{
    return xTaskCreateStatic(cp_task, "campus_pulse", CP_TASK_STACK_WORDS,
                             NULL, CP_TASK_PRIORITY, s_stack, &s_tcb) != NULL;
}

/*
 * AI Campus Pulse application core — platform independent.
 *
 * The same code runs:
 *   - on the PSOC Edge E84 (Cortex-M55), fed by the camera + AI model, and
 *   - on a laptop (host/sim_main.c) for development and the website demo.
 *
 * The platform only has to provide "write one line" and a millisecond clock,
 * then call the cp_app_* functions below.
 */
#ifndef CP_APP_H
#define CP_APP_H

#include <stdint.h>
#include <stdbool.h>

#ifndef CP_MASK_MAX_PIXELS
#define CP_MASK_MAX_PIXELS (256u * 256u)
#endif

/* In SIM mode the app produces a synthetic frame this often, so the
 * presence logic is exercised exactly as with a real camera. */
#ifndef CP_SIM_FRAME_MS
#define CP_SIM_FRAME_MS 200u
#endif

typedef struct {
    /* Write one complete line (no trailing newline included). */
    void (*write_line)(const char *line);
} cp_platform_t;

void cp_app_init(const cp_platform_t *platform, bool start_in_sim, uint32_t now_ms);

/* Camera path, option A: you already have a people count (e.g. from an
 * object-detection or face-detection model: number of boxes). */
void cp_app_on_detection(uint16_t people, uint32_t now_ms);

/* Camera path, option B: a person-segmentation mask (0..255 per pixel).
 * Blobs are counted with person_counter. */
void cp_app_on_mask(const uint8_t *mask, uint16_t width, uint16_t height, uint32_t now_ms);

/* One received command line from UART / stdin. */
void cp_app_on_line(const char *line, uint32_t now_ms);

/* Call every ~50-100 ms from the main loop. */
void cp_app_tick(uint32_t now_ms);

/* Read-only views for tests and on-screen overlays. */
bool     cp_app_occupied(void);
uint16_t cp_app_people(void);
bool     cp_app_is_sim(void);

#endif /* CP_APP_H */

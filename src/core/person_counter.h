/*
 * Person counter: turns an AI segmentation mask into a people count.
 *
 * The PSOC Edge person-segmentation model outputs a per-pixel "person" score.
 * We threshold it, find connected blobs (4-connectivity, two-pass union-find)
 * and count blobs that are big enough to be a person.
 *
 * No dynamic allocation: the caller provides the label workspace, so this runs
 * the same on the Cortex-M55 and on a laptop for unit tests.
 */
#ifndef CP_PERSON_COUNTER_H
#define CP_PERSON_COUNTER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t  threshold;     /* mask value >= threshold is foreground */
    uint32_t min_area;      /* blobs smaller than this are ignored */
} cp_counter_cfg_t;

typedef struct {
    uint16_t people;        /* blobs with area >= min_area */
    uint32_t largest_area;  /* biggest blob, pixels */
    uint32_t fg_pixels;     /* total foreground pixels */
    uint8_t  saturated;     /* 1 if label table overflowed (count may be low) */
} cp_counter_result_t;

/* Workspace: labels must hold width*height entries. */
typedef struct {
    uint16_t *labels;
    size_t    labels_len;
} cp_counter_ws_t;

/* Returns 0 on success, -1 on bad arguments. */
int cp_count_people(const uint8_t *mask, uint16_t width, uint16_t height,
                    const cp_counter_cfg_t *cfg, cp_counter_ws_t *ws,
                    cp_counter_result_t *out);

#endif /* CP_PERSON_COUNTER_H */

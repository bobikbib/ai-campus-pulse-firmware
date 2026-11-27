/*
 * Presence state machine.
 *
 * Input:  a stream of detector frames (people_count, timestamp).
 * Output: a stable "occupied" flag, how long the room has been empty,
 *         and the sensor health.
 *
 * The board does NOT decide to cancel bookings — it doesn't know the schedule.
 * It reports empty_for_s; the web server applies the 45-minute rule.
 * A stale sensor must never be treated as "empty" by the server.
 */
#ifndef CP_PRESENCE_H
#define CP_PRESENCE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CP_SENSOR_STARTING = 0, /* no frame received yet */
    CP_SENSOR_OK,
    CP_SENSOR_STALE         /* frames stopped arriving */
} cp_sensor_state_t;

typedef struct {
    uint32_t enter_frames;  /* consecutive positive frames to become occupied */
    uint32_t empty_hold_ms; /* continuous emptiness before occupied=false */
    uint32_t stale_ms;      /* no frames for this long -> STALE */
} cp_presence_cfg_t;

typedef struct {
    cp_presence_cfg_t cfg;

    bool     occupied;          /* debounced output */
    uint16_t people_count;      /* last raw count */
    uint16_t people_stable;     /* count while occupied (max over hold window) */
    cp_sensor_state_t sensor;

    uint32_t positive_streak;
    bool     seen_empty;        /* currently in an empty run */
    uint32_t empty_since_ms;    /* start of the current empty run */
    uint32_t last_frame_ms;
    uint32_t frames_total;
    bool     has_frame;
} cp_presence_t;

typedef enum {
    CP_EVT_NONE           = 0,
    CP_EVT_OCCUPIED       = 1 << 0, /* became occupied */
    CP_EVT_VACANT         = 1 << 1, /* became vacant */
    CP_EVT_COUNT_CHANGED  = 1 << 2,
    CP_EVT_SENSOR_CHANGED = 1 << 3
} cp_presence_evt_t;

void cp_presence_init(cp_presence_t *p, const cp_presence_cfg_t *cfg, uint32_t now_ms);

/* Feed one detector result. Returns a bitmask of cp_presence_evt_t. */
uint32_t cp_presence_on_frame(cp_presence_t *p, uint16_t people, uint32_t now_ms);

/* Call periodically (e.g. every 100 ms) to advance timers. Returns events. */
uint32_t cp_presence_tick(cp_presence_t *p, uint32_t now_ms);

/* Seconds the room has been continuously empty (0 if people present). */
uint32_t cp_presence_empty_for_s(const cp_presence_t *p, uint32_t now_ms);

const char *cp_sensor_str(cp_sensor_state_t s);

#endif /* CP_PRESENCE_H */

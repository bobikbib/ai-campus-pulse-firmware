#include "presence.h"

#include <string.h>

/* Unsigned subtraction is wrap-safe for a 32-bit millisecond tick (~49 days). */
static uint32_t elapsed(uint32_t now, uint32_t since)
{
    return now - since;
}

void cp_presence_init(cp_presence_t *p, const cp_presence_cfg_t *cfg, uint32_t now_ms)
{
    memset(p, 0, sizeof(*p));
    p->cfg = *cfg;
    if (p->cfg.enter_frames == 0) {
        p->cfg.enter_frames = 1;
    }
    p->sensor = CP_SENSOR_STARTING;
    /* Assume empty from boot; the server ignores empty_for_s until sensor=ok. */
    p->seen_empty = true;
    p->empty_since_ms = now_ms;
}

uint32_t cp_presence_tick(cp_presence_t *p, uint32_t now_ms)
{
    uint32_t evt = CP_EVT_NONE;

    if (p->has_frame && p->sensor == CP_SENSOR_OK &&
        elapsed(now_ms, p->last_frame_ms) >= p->cfg.stale_ms) {
        p->sensor = CP_SENSOR_STALE;
        evt |= CP_EVT_SENSOR_CHANGED;
    }

    /* Only release "occupied" on evidence: a healthy sensor that has seen
     * nobody for the whole hold window. A dead camera is not an empty room. */
    if (p->occupied && p->sensor == CP_SENSOR_OK && p->seen_empty &&
        elapsed(now_ms, p->empty_since_ms) >= p->cfg.empty_hold_ms) {
        p->occupied = false;
        p->people_stable = 0;
        evt |= CP_EVT_VACANT | CP_EVT_COUNT_CHANGED;
    }
    return evt;
}

uint32_t cp_presence_on_frame(cp_presence_t *p, uint16_t people, uint32_t now_ms)
{
    uint32_t evt = CP_EVT_NONE;

    if (p->sensor != CP_SENSOR_OK) {
        /* Recovering from STALE/STARTING: we don't know what happened while
         * blind, so an empty run restarts now instead of counting dark time. */
        if (p->seen_empty) {
            p->empty_since_ms = now_ms;
        }
        p->sensor = CP_SENSOR_OK;
        evt |= CP_EVT_SENSOR_CHANGED;
    }
    p->has_frame = true;
    p->last_frame_ms = now_ms;
    p->frames_total++;

    if (people != p->people_count) {
        evt |= CP_EVT_COUNT_CHANGED;
    }
    p->people_count = people;

    if (people > 0) {
        p->positive_streak++;
        /* Require several frames in a row so one false detection
         * doesn't reset the absence timer. */
        if (p->positive_streak >= p->cfg.enter_frames) {
            p->seen_empty = false;
            if (p->people_stable != people) {
                p->people_stable = people;
                evt |= CP_EVT_COUNT_CHANGED;
            }
            if (!p->occupied) {
                p->occupied = true;
                evt |= CP_EVT_OCCUPIED;
            }
        }
    } else {
        p->positive_streak = 0;
        if (!p->seen_empty) {
            p->seen_empty = true;
            p->empty_since_ms = now_ms;
        }
    }

    return evt | cp_presence_tick(p, now_ms);
}

uint32_t cp_presence_empty_for_s(const cp_presence_t *p, uint32_t now_ms)
{
    if (p->sensor != CP_SENSOR_OK || !p->seen_empty) {
        return 0;
    }
    return elapsed(now_ms, p->empty_since_ms) / 1000u;
}

const char *cp_sensor_str(cp_sensor_state_t s)
{
    switch (s) {
    case CP_SENSOR_OK:    return "ok";
    case CP_SENSOR_STALE: return "stale";
    default:              return "starting";
    }
}

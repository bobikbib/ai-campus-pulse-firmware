#include "cp_app.h"
#include "../core/cp_config.h"
#include "../core/presence.h"
#include "../core/protocol.h"
#include "../core/person_counter.h"

#include <string.h>

static struct {
    cp_platform_t  plat;
    cp_presence_t  presence;
    char           room[CP_ROOM_ID_MAX];
    bool           sim;
    uint16_t       sim_people;
    uint32_t       boot_ms;
    uint32_t       last_report_ms;
    uint32_t       last_sim_frame_ms;
    bool           pending_change;
    bool           has_audio;
    uint8_t        noise_db;
    bool           voice;
    uint32_t       audio_ms;       /* time of last microphone reading */
    uint8_t        audio_reported; /* audio_state() sent in the last report */
} g;

#define CP_AUDIO_NONE 0u
#define CP_AUDIO_QUIET 1u
#define CP_AUDIO_VOICE 2u

/* Audio is absent before the first reading and once it goes stale. */
static bool audio_fresh(uint32_t now_ms)
{
    return g.has_audio && (now_ms - g.audio_ms) <= CP_STALE_MS;
}

static uint8_t audio_state(uint32_t now_ms)
{
    if (!audio_fresh(now_ms)) {
        return CP_AUDIO_NONE;
    }
    return g.voice ? CP_AUDIO_VOICE : CP_AUDIO_QUIET;
}

#if CP_ENABLE_MASK_COUNTER
static uint16_t g_labels[CP_MASK_MAX_PIXELS];
#endif

static void emit(const char *line)
{
    if (line && g.plat.write_line) {
        g.plat.write_line(line);
    }
}

static void report(const char *reason, uint32_t now_ms)
{
    char buf[CP_LINE_MAX];
    cp_status_msg_t m = {
        .room         = g.room,
        .reason       = reason,
        .mode         = g.sim ? "sim" : "camera",
        .sensor       = cp_sensor_str(g.presence.sensor),
        .people_count = g.presence.occupied ? g.presence.people_stable : 0,
        .occupied     = g.presence.occupied,
        .empty_for_s  = cp_presence_empty_for_s(&g.presence, now_ms),
        .uptime_s     = (now_ms - g.boot_ms) / 1000u,
        .frames       = g.presence.frames_total,
        .has_audio    = audio_fresh(now_ms),
        .noise_db     = g.noise_db,
        .voice        = g.voice,
    };
    if (cp_format_status(buf, sizeof buf, &m) > 0) {
        emit(buf);
    }
    g.audio_reported = audio_state(now_ms);
    g.last_report_ms = now_ms;
    g.pending_change = false;
}

static void ack(const char *what)
{
    char buf[CP_LINE_MAX];
    if (cp_format_ack(buf, sizeof buf, what) > 0) {
        emit(buf);
    }
}

static void error(const char *what)
{
    char buf[CP_LINE_MAX];
    if (cp_format_error(buf, sizeof buf, what) > 0) {
        emit(buf);
    }
}

static void handle_events(uint32_t evt, uint32_t now_ms)
{
    /* Report immediately on a state change the website cares about.
     * Raw count jitter while occupied is folded into the next heartbeat. */
    if (evt & (CP_EVT_OCCUPIED | CP_EVT_VACANT | CP_EVT_SENSOR_CHANGED)) {
        report("change", now_ms);
    } else if (evt & CP_EVT_COUNT_CHANGED) {
        g.pending_change = true;
    }
}

static void reset_presence(uint32_t now_ms)
{
    const cp_presence_cfg_t cfg = g.presence.cfg;
    cp_presence_init(&g.presence, &cfg, now_ms);
}

void cp_app_init(const cp_platform_t *platform, bool start_in_sim, uint32_t now_ms)
{
    memset(&g, 0, sizeof g);
    g.plat = *platform;
    g.sim = start_in_sim;
    g.boot_ms = now_ms;
    strncpy(g.room, CP_DEFAULT_ROOM_ID, sizeof g.room - 1);

    const cp_presence_cfg_t cfg = {
        .enter_frames  = CP_ENTER_FRAMES,
        .empty_hold_ms = CP_EMPTY_HOLD_MS,
        .stale_ms      = CP_STALE_MS,
    };
    cp_presence_init(&g.presence, &cfg, now_ms);
    g.last_sim_frame_ms = now_ms;

    emit("CP>{\"v\":1,\"type\":\"hello\",\"fw\":\"" CP_FW_VERSION "\"}");
    report("boot", now_ms);
}

void cp_app_on_detection(uint16_t people, uint32_t now_ms)
{
    if (g.sim) {
        return; /* camera frames are ignored while simulating */
    }
    handle_events(cp_presence_on_frame(&g.presence, people, now_ms), now_ms);
}

/* True when a voice change is waiting and the rate limit has passed. */
static bool audio_report_due(uint32_t now_ms)
{
    const uint8_t s = audio_state(now_ms);
    return s != CP_AUDIO_NONE && s != g.audio_reported &&
           (now_ms - g.last_report_ms) >= CP_AUDIO_REPORT_MIN_MS;
}

void cp_app_on_audio(uint8_t noise_db, bool voice, uint32_t now_ms)
{
    g.has_audio = true;
    g.noise_db = noise_db > 120u ? 120u : noise_db;
    g.voice = voice;
    g.audio_ms = now_ms;
    if (audio_report_due(now_ms)) {
        report("audio", now_ms);
    }
}

#if CP_ENABLE_MASK_COUNTER
void cp_app_on_mask(const uint8_t *mask, uint16_t width, uint16_t height, uint32_t now_ms)
{
    if (g.sim) {
        return;
    }
    const cp_counter_cfg_t cfg = { CP_MASK_THRESHOLD, CP_MIN_BLOB_AREA };
    cp_counter_ws_t ws = { g_labels, CP_MASK_MAX_PIXELS };
    cp_counter_result_t r;
    if (cp_count_people(mask, width, height, &cfg, &ws, &r) != 0) {
        error("mask too large or invalid");
        return;
    }
    cp_app_on_detection(r.people, now_ms);
}
#endif /* CP_ENABLE_MASK_COUNTER */

void cp_app_on_line(const char *line, uint32_t now_ms)
{
    cp_cmd_t c;
    cp_parse_command(line, &c);

    switch (c.type) {
    case CP_CMD_STATUS:
        report("status", now_ms);
        break;
    case CP_CMD_ROOM:
        strncpy(g.room, c.room, sizeof g.room - 1);
        g.room[sizeof g.room - 1] = '\0';
        ack("ROOM");
        report("status", now_ms);
        break;
    case CP_CMD_MODE:
        if (g.sim != c.mode_sim) {
            g.sim = c.mode_sim;
            /* New source: forget the old one's history. */
            reset_presence(now_ms);
            g.last_sim_frame_ms = now_ms;
        }
        ack("MODE");
        report("change", now_ms);
        break;
    case CP_CMD_SIM:
        if (!g.sim) {
            error("SIM needs MODE SIM first");
            break;
        }
        g.sim_people = (uint16_t)c.value;
        ack("SIM");
        break;
    case CP_CMD_CFG_HOLD:
        g.presence.cfg.empty_hold_ms = c.value;
        ack("CFG HOLD");
        break;
    case CP_CMD_CFG_ENTER:
        g.presence.cfg.enter_frames = c.value;
        ack("CFG ENTER");
        break;
    case CP_CMD_HELP:
        emit("CP>{\"v\":1,\"type\":\"help\",\"commands\":"
             "[\"STATUS\",\"ROOM <id>\",\"MODE SIM|CAM\",\"SIM <n>\","
             "\"CFG HOLD <ms>\",\"CFG ENTER <n>\"]}");
        break;
    default:
        error(c.error ? c.error : "invalid");
        break;
    }
}

void cp_app_tick(uint32_t now_ms)
{
    if (g.sim && (now_ms - g.last_sim_frame_ms) >= CP_SIM_FRAME_MS) {
        g.last_sim_frame_ms = now_ms;
        handle_events(cp_presence_on_frame(&g.presence, g.sim_people, now_ms), now_ms);
    }

    handle_events(cp_presence_tick(&g.presence, now_ms), now_ms);

    if ((now_ms - g.last_report_ms) >= CP_HEARTBEAT_MS) {
        report(g.pending_change ? "change" : "heartbeat", now_ms);
    } else if (audio_report_due(now_ms)) {
        report("audio", now_ms);
    }
}

bool cp_app_occupied(void) { return g.presence.occupied; }
uint16_t cp_app_people(void) { return g.presence.occupied ? g.presence.people_stable : 0; }
bool cp_app_is_sim(void) { return g.sim; }

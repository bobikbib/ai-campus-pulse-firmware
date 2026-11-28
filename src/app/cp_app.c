#include "cp_app.h"
#include "../core/cp_config.h"
#include "../core/presence.h"
#include "../core/protocol.h"
#include "../core/person_counter.h"
#include "../core/audio.h"

#include <math.h>
#include <string.h>

/* Simulated microphone: in SIM mode the app synthesizes PCM (background noise
 * at NOISE dB, plus a speech-like voice when VOICE 1) and runs it through the
 * very same analyzer the board uses on real microphone samples. */
#define CP_SIM_AUDIO_CHUNK 160u

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

    /* audio: aggregated over one report interval */
    float          audio_energy;     /* sum of 10^(dB/10) over windows */
    uint32_t       audio_windows;
    uint32_t       audio_voice_windows;
    cp_noise_level_t noise_level;
    uint8_t        noise_scale;
    cp_audio_t     sim_audio;
    uint8_t        sim_noise_db;
    bool           sim_voice;
    uint32_t       sim_rng;
    float          sim_phase;        /* voice pitch phase, radians */
    uint32_t       sim_samples;      /* for the syllable envelope */

    /* NFC: one scan waiting for the camera to confirm */
    bool           nfc_pending;
    char           nfc_card[24];
    uint32_t       nfc_since_ms;
} g;

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
    };
    if (g.audio_windows > 0u) {
        const float mean = g.audio_energy / (float)g.audio_windows;
        float db = 10.0f * log10f(mean > 1.0f ? mean : 1.0f);
        if (db > 120.0f) {
            db = 120.0f;
        }
        m.has_audio = true;
        m.noise_db  = (uint8_t)(db + 0.5f);
        m.voice     = g.audio_voice_windows * 100u >= g.audio_windows * CP_VOICE_MIN_PERCENT;
        g.noise_level = cp_noise_classify(m.noise_db, m.voice, m.occupied, g.noise_level);
        m.noise_level = cp_noise_level_str(g.noise_level);
        g.noise_scale = cp_noise_scale(m.noise_db, g.noise_scale);
        m.noise_scale = g.noise_scale;
        g.audio_energy = 0.0f;
        g.audio_windows = 0u;
        g.audio_voice_windows = 0u;
    }
    if (cp_format_status(buf, sizeof buf, &m) > 0) {
        emit(buf);
    }
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

static void nfc_decide(bool confirmed, const char *reason, uint32_t now_ms)
{
    char buf[CP_LINE_MAX];
    const cp_entry_msg_t m = {
        .room         = g.room,
        .card         = g.nfc_card,
        .confirmed    = confirmed,
        .reason       = reason,
        .people_count = g.presence.people_count,
        .wait_ms      = now_ms - g.nfc_since_ms,
        .uptime_s     = (now_ms - g.boot_ms) / 1000u,
    };
    g.nfc_pending = false;
    if (cp_format_entry(buf, sizeof buf, &m) > 0) {
        emit(buf);
    }
}

/* After every camera frame: a pending card scan is confirmed as soon as the
 * camera sees at least one person in the room. */
static void nfc_on_frame(uint32_t now_ms)
{
    if (g.nfc_pending && g.presence.people_count > 0u) {
        nfc_decide(true, "person_seen", now_ms);
    }
}

static void nfc_tick(uint32_t now_ms)
{
    if (g.nfc_pending && (now_ms - g.nfc_since_ms) >= CP_NFC_CONFIRM_MS) {
        nfc_decide(false, g.presence.sensor == CP_SENSOR_OK ? "nobody_seen"
                                                            : "camera_unavailable", now_ms);
    }
}

static uint32_t rng_next(void)
{
    g.sim_rng = g.sim_rng * 1664525u + 1013904223u;
    return g.sim_rng;
}

/* float only: this also runs on the board in SIM mode, where double math
 * would be software-emulated. */
static void sim_audio_generate(uint32_t ms)
{
    const cp_audio_cfg_t *cfg = &g.sim_audio.cfg;
    const float two_pi = 6.2831853f;
    uint32_t total = (uint32_t)((uint64_t)cfg->sample_rate_hz * ms / 1000u);
    const float noise_rms = 32768.0f * powf(10.0f, ((float)g.sim_noise_db - (float)cfg->dbfs_to_spl) / 20.0f);
    const float noise_amp = noise_rms * 1.7320508f; /* uniform in [-a, a] has rms a/sqrt(3) */
    /* voice ~20 dB above the background (at least 55 dB SPL), pitch 180 Hz,
     * 4 syllables per second with silent gaps between them */
    float voice_spl = (float)g.sim_noise_db + 20.0f;
    if (voice_spl < 55.0f) {
        voice_spl = 55.0f;
    }
    const float voice_amp = 32768.0f * powf(10.0f, (voice_spl - (float)cfg->dbfs_to_spl) / 20.0f) * 1.4142136f;
    const float step = two_pi * 180.0f / (float)cfg->sample_rate_hz;
    const uint32_t syllable_period = cfg->sample_rate_hz / 4u;
    int16_t pcm[CP_SIM_AUDIO_CHUNK];
    cp_audio_window_t w[4];

    while (total > 0u) {
        const uint32_t n = total < CP_SIM_AUDIO_CHUNK ? total : CP_SIM_AUDIO_CHUNK;
        for (uint32_t i = 0; i < n; i++) {
            float x = ((float)(rng_next() >> 8) / 8388608.0f - 1.0f) * noise_amp;
            if (g.sim_voice) {
                const float t = (float)(g.sim_samples % syllable_period) / (float)syllable_period;
                float env = sinf(two_pi * t);
                env = env > 0.0f ? env * env : 0.0f;
                x += env * voice_amp * (sinf(g.sim_phase) + 0.5f * sinf(2.0f * g.sim_phase));
                g.sim_phase += step;
                if (g.sim_phase > two_pi) {
                    g.sim_phase -= two_pi;
                }
            }
            g.sim_samples++;
            if (x > 32767.0f) {
                x = 32767.0f;
            } else if (x < -32768.0f) {
                x = -32768.0f;
            }
            pcm[i] = (int16_t)x;
        }
        const size_t k = cp_audio_feed(&g.sim_audio, pcm, n, w, 4);
        for (size_t j = 0; j < k; j++) {
            cp_app_on_audio_window(w[j].level_spl, w[j].voice);
        }
        total -= n;
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

    cp_audio_cfg_t acfg;
    cp_audio_default_cfg(&acfg);
    cp_audio_init(&g.sim_audio, &acfg);
    g.sim_noise_db = 40u;   /* quiet room */
    g.sim_rng = 12345u;

    emit("CP>{\"v\":1,\"type\":\"hello\",\"fw\":\"" CP_FW_VERSION "\"}");
    report("boot", now_ms);
}

void cp_app_on_detection(uint16_t people, uint32_t now_ms)
{
    if (g.sim) {
        return; /* camera frames are ignored while simulating */
    }
    handle_events(cp_presence_on_frame(&g.presence, people, now_ms), now_ms);
    nfc_on_frame(now_ms);
}

void cp_app_on_audio_window(float level_spl, bool voice)
{
    g.audio_energy += powf(10.0f, level_spl / 10.0f);
    g.audio_windows++;
    if (voice) {
        g.audio_voice_windows++;
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
    case CP_CMD_NFC:
        if (g.nfc_pending) {
            nfc_decide(false, "replaced", now_ms);   /* a newer scan wins */
        }
        strncpy(g.nfc_card, c.card, sizeof g.nfc_card - 1);
        g.nfc_card[sizeof g.nfc_card - 1] = '\0';
        g.nfc_pending = true;
        g.nfc_since_ms = now_ms;
        ack("NFC");
        break;
    case CP_CMD_NOISE:
    case CP_CMD_VOICE:
        if (!g.sim) {
            error("NOISE/VOICE need MODE SIM first");
            break;
        }
        if (c.type == CP_CMD_NOISE) {
            g.sim_noise_db = (uint8_t)c.value;
        } else {
            g.sim_voice = c.value != 0u;
        }
        ack(c.type == CP_CMD_NOISE ? "NOISE" : "VOICE");
        break;
    case CP_CMD_HELP:
        emit("CP>{\"v\":1,\"type\":\"help\",\"commands\":"
             "[\"STATUS\",\"ROOM <id>\",\"MODE SIM|CAM\",\"SIM <n>\","
             "\"CFG HOLD <ms>\",\"CFG ENTER <n>\",\"NFC <uid>\","
             "\"NOISE <db>\",\"VOICE 0|1\"]}");
        break;
    default:
        error(c.error ? c.error : "invalid");
        break;
    }
}

void cp_app_tick(uint32_t now_ms)
{
    if (g.sim && (now_ms - g.last_sim_frame_ms) >= CP_SIM_FRAME_MS) {
        const uint32_t elapsed = now_ms - g.last_sim_frame_ms;
        g.last_sim_frame_ms = now_ms;
        handle_events(cp_presence_on_frame(&g.presence, g.sim_people, now_ms), now_ms);
        nfc_on_frame(now_ms);
        sim_audio_generate(elapsed < 1000u ? elapsed : 1000u);
    }

    handle_events(cp_presence_tick(&g.presence, now_ms), now_ms);
    nfc_tick(now_ms);

    if ((now_ms - g.last_report_ms) >= CP_HEARTBEAT_MS) {
        report(g.pending_change ? "change" : "heartbeat", now_ms);
    }
}

bool cp_app_occupied(void) { return g.presence.occupied; }
uint16_t cp_app_people(void) { return g.presence.occupied ? g.presence.people_stable : 0; }
bool cp_app_is_sim(void) { return g.sim; }

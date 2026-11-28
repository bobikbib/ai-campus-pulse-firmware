/* Unit tests for the platform-independent core. Run: make test */
#include "../src/core/person_counter.h"
#include "../src/core/presence.h"
#include "../src/core/protocol.h"
#include "../src/core/audio.h"
#include "../src/app/cp_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_pass;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---------- person counter ---------- */

static void fill_rect(uint8_t *m, int w, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            m[y * w + x] = 255;
}

static void test_counter(void)
{
    enum { W = 40, H = 30 };
    uint8_t mask[W * H];
    uint16_t labels[W * H];
    cp_counter_ws_t ws = { labels, W * H };
    cp_counter_cfg_t cfg = { 128, 20 };
    cp_counter_result_t r;

    memset(mask, 0, sizeof mask);
    CHECK(cp_count_people(mask, W, H, &cfg, &ws, &r) == 0);
    CHECK(r.people == 0);

    fill_rect(mask, W, 2, 2, 8, 12);     /* person 1: 60 px */
    fill_rect(mask, W, 20, 5, 27, 20);   /* person 2: 105 px */
    mask[29 * W + 39] = 255;             /* 1 px noise */
    CHECK(cp_count_people(mask, W, H, &cfg, &ws, &r) == 0);
    CHECK(r.people == 2);
    CHECK(r.largest_area == 105);

    /* U-shape: two arms joined at the bottom must count as ONE person
     * (exercises the union-find merge). */
    memset(mask, 0, sizeof mask);
    fill_rect(mask, W, 5, 0, 8, 20);
    fill_rect(mask, W, 15, 0, 18, 20);
    fill_rect(mask, W, 5, 20, 18, 23);
    CHECK(cp_count_people(mask, W, H, &cfg, &ws, &r) == 0);
    CHECK(r.people == 1);

    /* Bad args / workspace too small. */
    cp_counter_ws_t small = { labels, 10 };
    CHECK(cp_count_people(mask, W, H, &cfg, &small, &r) == -1);
    CHECK(cp_count_people(NULL, W, H, &cfg, &ws, &r) == -1);
}

/* ---------- presence state machine ---------- */

static void test_presence(void)
{
    cp_presence_cfg_t cfg = { .enter_frames = 3, .empty_hold_ms = 10000, .stale_ms = 5000 };
    cp_presence_t p;
    cp_presence_init(&p, &cfg, 0);
    CHECK(p.sensor == CP_SENSOR_STARTING);
    CHECK(cp_presence_empty_for_s(&p, 1000) == 0); /* unknown until first frame */

    /* One noisy positive frame must not mark occupied. */
    uint32_t t = 0;
    cp_presence_on_frame(&p, 0, t += 200);
    cp_presence_on_frame(&p, 1, t += 200);
    cp_presence_on_frame(&p, 0, t += 200);
    CHECK(!p.occupied);

    /* Three in a row -> occupied. */
    uint32_t e = 0;
    for (int i = 0; i < 3; i++) e |= cp_presence_on_frame(&p, 2, t += 200);
    CHECK(p.occupied);
    CHECK(e & CP_EVT_OCCUPIED);
    CHECK(p.people_stable == 2);
    CHECK(cp_presence_empty_for_s(&p, t) == 0);

    /* Empty for less than hold window -> still occupied. */
    const uint32_t empty_start = t + 200;
    for (int i = 0; i < 40; i++) cp_presence_on_frame(&p, 0, t += 200); /* 8 s */
    CHECK(p.occupied);
    CHECK(cp_presence_empty_for_s(&p, t) == (t - empty_start) / 1000);

    /* Past hold window -> vacant. */
    e = 0;
    for (int i = 0; i < 15; i++) e |= cp_presence_on_frame(&p, 0, t += 200);
    CHECK(!p.occupied);
    CHECK(e & CP_EVT_VACANT);
    CHECK(cp_presence_empty_for_s(&p, t) >= 10);

    /* Single false positive does NOT reset the absence timer. */
    const uint32_t before = cp_presence_empty_for_s(&p, t);
    cp_presence_on_frame(&p, 1, t += 200);
    cp_presence_on_frame(&p, 0, t += 200);
    CHECK(cp_presence_empty_for_s(&p, t) >= before);

    /* Camera stops -> stale, and stale never produces "empty" seconds. */
    e = cp_presence_tick(&p, t + 6000);
    CHECK(p.sensor == CP_SENSOR_STALE);
    CHECK(e & CP_EVT_SENSOR_CHANGED);
    CHECK(cp_presence_empty_for_s(&p, t + 6000) == 0);

    /* Recovery restarts the empty run at recovery time. */
    t += 7000;
    cp_presence_on_frame(&p, 0, t);
    CHECK(p.sensor == CP_SENSOR_OK);
    CHECK(cp_presence_empty_for_s(&p, t + 3000) == 3);
}

static void test_stale_keeps_occupied(void)
{
    cp_presence_cfg_t cfg = { .enter_frames = 1, .empty_hold_ms = 1000, .stale_ms = 500 };
    cp_presence_t p;
    cp_presence_init(&p, &cfg, 0);
    cp_presence_on_frame(&p, 1, 100);
    CHECK(p.occupied);
    cp_presence_tick(&p, 5000); /* camera died with someone in the room */
    CHECK(p.sensor == CP_SENSOR_STALE);
    CHECK(p.occupied);          /* not released on a dead sensor */
}

static void test_wraparound(void)
{
    cp_presence_cfg_t cfg = { .enter_frames = 1, .empty_hold_ms = 1000, .stale_ms = 5000 };
    cp_presence_t p;
    const uint32_t base = 0xFFFFFF00u; /* ms counter about to wrap */
    cp_presence_init(&p, &cfg, base);
    cp_presence_on_frame(&p, 1, base);
    cp_presence_on_frame(&p, 0, base + 100);
    cp_presence_on_frame(&p, 0, base + 1200); /* wraps past 0 */
    CHECK(!p.occupied);
    CHECK(cp_presence_empty_for_s(&p, base + 2100) == 2);
}

/* ---------- protocol ---------- */

static void test_protocol(void)
{
    cp_cmd_t c;
    cp_parse_command("SIM 3\r\n", &c);      CHECK(c.type == CP_CMD_SIM && c.value == 3);
    cp_parse_command("sim 0", &c);          CHECK(c.type == CP_CMD_SIM && c.value == 0);
    cp_parse_command("SIM -1", &c);         CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("SIM 99999999999", &c);CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("SIM", &c);            CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("MODE cam", &c);       CHECK(c.type == CP_CMD_MODE && !c.mode_sim);
    cp_parse_command("ROOM A-101", &c);     CHECK(c.type == CP_CMD_ROOM && strcmp(c.room, "A-101") == 0);
    cp_parse_command("ROOM a\"b", &c);      CHECK(c.type == CP_CMD_INVALID); /* JSON injection */
    cp_parse_command("ROOM AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", &c); CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("CFG HOLD 2700000", &c); CHECK(c.type == CP_CMD_CFG_HOLD && c.value == 2700000u);
    cp_parse_command("CFG ENTER 0", &c);    CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("STATUS extra", &c);   CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("", &c);               CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("FOO", &c);            CHECK(c.type == CP_CMD_INVALID);

    char buf[256];
    cp_status_msg_t m = { "A-101", "change", "camera", "ok", 2, true, 0, 12, 60, false, 0, false, NULL, 0 };
    CHECK(cp_format_status(buf, sizeof buf, &m) > 0);
    CHECK(strncmp(buf, "CP>{", 4) == 0);
    CHECK(strstr(buf, "\"people_count\":2") != NULL);
    CHECK(strstr(buf, "\"occupied\":true") != NULL);
    CHECK(cp_format_status(buf, 20, &m) == -1); /* truncation detected */
    CHECK(cp_format_status(buf, sizeof buf, &m) > 0 && strstr(buf, "noise_db") == NULL);
    m.has_audio = true; m.noise_db = 63; m.voice = true;
    CHECK(cp_format_status(buf, sizeof buf, &m) > 0);
    CHECK(strstr(buf, "\"noise_db\":63,\"voice\":true,\"noise_level\":\"quiet\",\"noise_scale\":1}") != NULL);
    m.noise_level = "too_loud";
    m.noise_scale = 5;
    CHECK(cp_format_status(buf, sizeof buf, &m) > 0 && strstr(buf, "\"noise_level\":\"too_loud\",\"noise_scale\":5}"));

    /* noise scale 1..5: steps at 40/50/60/70 dB, 3 dB hysteresis going down */
    CHECK(cp_noise_scale(0, 0) == 1);
    CHECK(cp_noise_scale(39, 0) == 1);
    CHECK(cp_noise_scale(40, 0) == 2);
    CHECK(cp_noise_scale(55, 0) == 3);
    CHECK(cp_noise_scale(65, 0) == 4);
    CHECK(cp_noise_scale(70, 0) == 5);
    CHECK(cp_noise_scale(120, 0) == 5);
    CHECK(cp_noise_scale(68, 5) == 5);   /* within 3 dB of 70: stays 5 */
    CHECK(cp_noise_scale(66, 5) == 4);
    CHECK(cp_noise_scale(48, 3) == 3);
    CHECK(cp_noise_scale(46, 3) == 2);
    CHECK(cp_noise_scale(30, 5) == 1);   /* big drop: straight down */
    CHECK(cp_noise_scale(52, 1) == 3);   /* going up: no delay */

    /* noise categories: thresholds 50 / 70 dB, 3 dB hysteresis */
    CHECK(cp_noise_classify(40, false, false, CP_NOISE_EMPTY) == CP_NOISE_EMPTY);
    CHECK(cp_noise_classify(40, false, true,  CP_NOISE_EMPTY) == CP_NOISE_QUIET);   /* people in the room */
    CHECK(cp_noise_classify(40, true,  false, CP_NOISE_EMPTY) == CP_NOISE_QUIET);   /* talking, camera sees nobody */
    CHECK(cp_noise_classify(55, false, true,  CP_NOISE_QUIET) == CP_NOISE_NOISY);
    CHECK(cp_noise_classify(75, false, true,  CP_NOISE_QUIET) == CP_NOISE_TOO_LOUD);
    CHECK(cp_noise_classify(68, false, true,  CP_NOISE_TOO_LOUD) == CP_NOISE_TOO_LOUD); /* hysteresis */
    CHECK(cp_noise_classify(66, false, true,  CP_NOISE_TOO_LOUD) == CP_NOISE_NOISY);
    CHECK(cp_noise_classify(48, false, true,  CP_NOISE_NOISY) == CP_NOISE_NOISY);       /* hysteresis */
    CHECK(cp_noise_classify(46, false, true,  CP_NOISE_NOISY) == CP_NOISE_QUIET);
    CHECK(cp_noise_classify(46, false, false, CP_NOISE_NOISY) == CP_NOISE_EMPTY);
    CHECK(cp_noise_classify(120, true, true,  CP_NOISE_EMPTY) == CP_NOISE_TOO_LOUD);
    CHECK(strcmp(cp_noise_level_str(CP_NOISE_NOISY), "noisy") == 0);

    /* NFC / NOISE / VOICE commands */
    cp_parse_command("NFC 04a1b2c3", &c);   CHECK(c.type == CP_CMD_NFC && strcmp(c.card, "04A1B2C3") == 0);
    cp_parse_command("NFC 04A1B2C3D4E5F6A7B8C9", &c); CHECK(c.type == CP_CMD_NFC); /* 20 hex */
    cp_parse_command("NFC 04A1B2C3D4E5F6A7B8C9D", &c); CHECK(c.type == CP_CMD_INVALID); /* 21 */
    cp_parse_command("NFC 04A", &c);        CHECK(c.type == CP_CMD_INVALID);   /* too short */
    cp_parse_command("NFC 04:A1:B2:C3", &c); CHECK(c.type == CP_CMD_INVALID);  /* separators */
    cp_parse_command("NFC 04A1\"B2", &c);   CHECK(c.type == CP_CMD_INVALID);   /* JSON injection */
    cp_parse_command("NFC", &c);            CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("NOISE 65", &c);       CHECK(c.type == CP_CMD_NOISE && c.value == 65);
    cp_parse_command("NOISE 121", &c);      CHECK(c.type == CP_CMD_INVALID);
    cp_parse_command("VOICE 1", &c);        CHECK(c.type == CP_CMD_VOICE && c.value == 1);
    cp_parse_command("VOICE 2", &c);        CHECK(c.type == CP_CMD_INVALID);

    const cp_entry_msg_t e = { "Z-3-03", "04A1B2C3", true, "person_seen", 1, 400, 30 };
    CHECK(cp_format_entry(buf, sizeof buf, &e) > 0);
    CHECK(strstr(buf, "\"type\":\"entry\"") && strstr(buf, "\"card\":\"04A1B2C3\"") &&
          strstr(buf, "\"confirmed\":true"));
}

/* ---------- audio analyzer ---------- */

static uint32_t g_rng = 1u;
static double urand(void) /* -1..1 */
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (double)(g_rng >> 8) / 8388608.0 - 1.0;
}

/* Feeds `ms` of signal, returns the last window. kind: 0 silence, 1 white
 * noise at spl, 2 voiced tone (180 Hz + harmonic) at spl over a 40 dB floor. */
static cp_audio_window_t feed(cp_audio_t *a, int kind, double spl, int ms, int *voice_windows)
{
    static double phase;
    int16_t buf[160];
    cp_audio_window_t w[2], last = { 0, false };
    const double amp = 32768.0 * pow(10.0, (spl - 120.0) / 20.0);
    const double floor_amp = 32768.0 * pow(10.0, (40.0 - 120.0) / 20.0) * 1.732;
    int total = 16 * ms;
    *voice_windows = 0;
    while (total > 0) {
        for (int i = 0; i < 160; i++) {
            double x = 0;
            if (kind == 1) x = urand() * amp * 1.732;
            if (kind == 2 || kind == 3) {
                const double fa = kind == 3 ? floor_amp * 31.6 : floor_amp;  /* 3: 65 dB floor */
                x = urand() * fa + amp * 1.155 * (sin(phase) + 0.5 * sin(2 * phase));
                phase += 2 * 3.14159265358979 * 180.0 / 16000.0;
            }
            buf[i] = (int16_t)x;
        }
        const size_t k = cp_audio_feed(a, buf, 160, w, 2);
        for (size_t j = 0; j < k; j++) { last = w[j]; *voice_windows += w[j].voice; }
        total -= 160;
    }
    return last;
}

static void test_audio(void)
{
    cp_audio_cfg_t cfg;
    cp_audio_t a;
    cp_audio_window_t w;
    int vw;
    cp_audio_default_cfg(&cfg);
    cp_audio_init(&a, &cfg);
    CHECK(a.window_samples == 1600u);

    w = feed(&a, 0, 0, 300, &vw);
    CHECK(w.level_spl == 0.0f && !w.voice);

    w = feed(&a, 1, 50, 500, &vw);           /* quiet room */
    CHECK(fabsf(w.level_spl - 50.0f) < 1.5f);
    CHECK(vw == 0);
    w = feed(&a, 1, 80, 500, &vw);           /* loud hiss: louder, but not speech */
    CHECK(fabsf(w.level_spl - 80.0f) < 1.5f);
    CHECK(vw == 0);

    cp_audio_init(&a, &cfg);
    feed(&a, 1, 40, 500, &vw);               /* learn a 40 dB floor */
    w = feed(&a, 2, 65, 500, &vw);           /* someone talking */
    CHECK(w.voice);
    CHECK(w.level_spl > 60.0f && w.level_spl < 70.0f);

    /* talking in a noisy room: voice 20 dB over a 65 dB background
     * (regression: background noise used to add zero crossings and hide the voice) */
    cp_audio_init(&a, &cfg);
    feed(&a, 1, 65, 500, &vw);               /* learn a 65 dB floor */
    CHECK(vw == 0);
    w = feed(&a, 3, 85, 500, &vw);
    CHECK(vw >= 3);

    /* odd block sizes and NULL safety */
    int16_t one = 1000;
    CHECK(cp_audio_feed(&a, &one, 1, NULL, 0) == 0);
    CHECK(cp_audio_feed(NULL, &one, 1, NULL, 0) == 0);
}

/* ---------- app (end to end, simulated clock) ---------- */

static char g_last[400];
static int  g_lines;
static char g_last_entry[400];
static void capture(const char *l)
{
    strncpy(g_last, l, sizeof g_last - 1);
    if (strstr(l, "\"type\":\"entry\"")) strncpy(g_last_entry, l, sizeof g_last_entry - 1);
    g_lines++;
}

static void test_app_sim_flow(void)
{
    const cp_platform_t plat = { capture };
    uint32_t t = 0;
    cp_app_init(&plat, true, t);
    CHECK(strstr(g_last, "\"reason\":\"boot\"") != NULL);

    cp_app_on_line("CFG HOLD 2000", t);
    cp_app_on_line("SIM 1", t);
    for (int i = 0; i < 20; i++) cp_app_tick(t += 100);
    CHECK(cp_app_occupied());
    CHECK(cp_app_people() == 1);

    cp_app_on_line("SIM 0", t);
    for (int i = 0; i < 30; i++) cp_app_tick(t += 100);
    CHECK(!cp_app_occupied());

    /* Camera frames are ignored in SIM mode... */
    cp_app_on_detection(3, t);
    CHECK(!cp_app_occupied());
    /* ...and SIM is rejected in CAM mode. */
    cp_app_on_line("MODE CAM", t);
    cp_app_on_line("SIM 2", t);
    CHECK(strstr(g_last, "\"type\":\"error\"") != NULL);

    /* NFC entry in CAM mode: confirmed only when the camera sees someone. */
    cp_app_on_line("NFC 04A1B2C3", t);
    for (int i = 0; i < 5; i++) { cp_app_on_detection(0, t += 100); cp_app_tick(t); }
    CHECK(strstr(g_last, "\"type\":\"entry\"") == NULL);
    cp_app_on_detection(1, t += 100);
    CHECK(strstr(g_last, "\"type\":\"entry\"") && strstr(g_last, "\"confirmed\":true") &&
          strstr(g_last, "\"card\":\"04A1B2C3\"") && strstr(g_last, "person_seen"));

    /* nobody comes in -> rejected after CP_NFC_CONFIRM_MS */
    cp_app_on_line("NFC 0A0B0C0D", t);
    for (int i = 0; i < 99; i++) { cp_app_on_detection(0, t += 100); cp_app_tick(t); }
    CHECK(strstr(g_last, "\"type\":\"entry\"") == NULL);
    cp_app_on_detection(0, t += 100); cp_app_tick(t);
    CHECK(strstr(g_last, "\"confirmed\":false") && strstr(g_last, "nobody_seen"));

    /* a second scan replaces a pending one */
    cp_app_on_line("NFC 11111111", t);
    cp_app_on_line("NFC 22222222", t);
    CHECK(strstr(g_last, "\"type\":\"ack\"") != NULL);
    cp_app_on_detection(2, t += 100);
    CHECK(strstr(g_last, "22222222") && strstr(g_last, "\"confirmed\":true"));

    /* camera dead -> rejected as camera_unavailable, never confirmed */
    cp_app_on_line("NFC 33333333", t);
    for (int i = 0; i < 110; i++) cp_app_tick(t += 100);
    CHECK(strstr(g_last_entry, "33333333") && strstr(g_last_entry, "camera_unavailable") &&
          strstr(g_last_entry, "\"confirmed\":false"));

#if CP_ENABLE_MASK_COUNTER
    /* Segmentation mask path in CAM mode. */
    static uint8_t mask[32 * 32];
    memset(mask, 0, sizeof mask);
    fill_rect(mask, 32, 4, 4, 14, 20);
    for (int i = 0; i < 4; i++) cp_app_on_mask(mask, 32, 32, t += 100);
    CHECK(cp_app_occupied());
    CHECK(cp_app_people() == 1);
#endif
}

static void test_app_audio(void)
{
    const cp_platform_t plat = { capture };
    uint32_t t = 0;
    cp_app_init(&plat, true, t);
    /* no microphone data yet in CAM mode -> no audio fields */
    cp_app_on_line("MODE CAM", t);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "noise_db") == NULL);

    /* board path: analyzer windows reported by the port */
    for (int i = 0; i < 50; i++) cp_app_on_audio_window(60.0f, i < 10);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"noise_db\":60,\"voice\":true,\"noise_level\":\"noisy\"") != NULL);  /* 20% of windows */
    for (int i = 0; i < 50; i++) cp_app_on_audio_window(60.0f, i < 2);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"voice\":false") != NULL);                 /* 4% */
    /* energy average, not dB average: one loud window dominates */
    for (int i = 0; i < 9; i++) cp_app_on_audio_window(40.0f, false);
    cp_app_on_audio_window(80.0f, false);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"noise_db\":70") != NULL);

    /* SIM mode synthesizes microphone data through the same analyzer */
    cp_app_on_line("MODE SIM", t);
    cp_app_on_line("NOISE 50", t);
    for (int i = 0; i < 60; i++) cp_app_tick(t += 100);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"noise_db\":5") != NULL && strstr(g_last, "\"voice\":false"));
    CHECK(strstr(g_last, "\"noise_level\":\"noisy\"") != NULL);
    CHECK(strstr(g_last, "\"noise_scale\":3") != NULL);   /* ~50 dB */
    cp_app_on_line("VOICE 1", t);
    for (int i = 0; i < 60; i++) cp_app_tick(t += 100);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"voice\":true") != NULL);
    cp_app_on_line("NOISE 200", t);
    CHECK(strstr(g_last, "\"type\":\"error\"") != NULL);
}

int main(void)
{
    test_counter();
    test_presence();
    test_stale_keeps_occupied();
    test_wraparound();
    test_protocol();
    test_app_sim_flow();
    test_audio();
    test_app_audio();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

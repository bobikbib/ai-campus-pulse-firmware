/* Unit tests for the platform-independent core. Run: make test */
#include "../src/core/person_counter.h"
#include "../src/core/presence.h"
#include "../src/core/protocol.h"
#include "../src/core/cp_config.h"
#include "../src/app/cp_app.h"

#include <stdio.h>
#include <stdlib.h>
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
    cp_status_msg_t m = { "A-101", "change", "camera", "ok", 2, true, 0, 12, 60, false, 0, false };
    CHECK(cp_format_status(buf, sizeof buf, &m) > 0);
    CHECK(strncmp(buf, "CP>{", 4) == 0);
    CHECK(strstr(buf, "\"people_count\":2") != NULL);
    CHECK(strstr(buf, "\"occupied\":true") != NULL);
    CHECK(cp_format_status(buf, 20, &m) == -1); /* truncation detected */
}

/* ---------- app (end to end, simulated clock) ---------- */

static char g_last[512];
static int  g_lines;
static void capture(const char *l) { strncpy(g_last, l, sizeof g_last - 1); g_lines++; }

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

/* ---------- audio (noise level + voice; never affects presence) ---------- */

static void test_audio(void)
{
    const cp_platform_t plat = { capture };
    uint32_t t = 0;
    cp_app_init(&plat, false, t);
    /* No microphone reading yet -> null, not 0. */
    CHECK(strstr(g_last, "\"noise_db\":null,\"voice\":null") != NULL);

    /* First reading with voice -> reported right away. */
    t = 2000;
    int lines = g_lines;
    cp_app_on_audio(41, true, t);
    CHECK(g_lines == lines + 1);
    CHECK(strstr(g_last, "\"noise_db\":41,\"voice\":true") != NULL);
    CHECK(strstr(g_last, "\"reason\":\"audio\"") != NULL);

    /* Voice flicker inside 1 s -> no flood; one report once 1 s has passed. */
    lines = g_lines;
    cp_app_on_audio(41, false, t += 100);
    cp_app_on_audio(42, true, t += 100);
    cp_app_on_audio(43, false, t += 100);
    cp_app_tick(t += 100);
    CHECK(g_lines == lines);
    cp_app_tick(t = 2000 + CP_AUDIO_REPORT_MIN_MS);
    CHECK(g_lines == lines + 1);
    CHECK(strstr(g_last, "\"noise_db\":43,\"voice\":false") != NULL);

    /* Loud voices with an empty camera view never make the room occupied. */
    for (int i = 0; i < 20; i++) {
        cp_app_on_detection(0, t += 200);
        cp_app_on_audio(90, true, t);
    }
    CHECK(!cp_app_occupied());

    /* Clamped to 0..120. */
    cp_app_on_audio(200, true, t += 100);
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"noise_db\":120") != NULL);

    /* Microphone stops -> back to null after CP_STALE_MS. */
    t += CP_STALE_MS + 100;
    cp_app_on_line("STATUS", t);
    CHECK(strstr(g_last, "\"noise_db\":null,\"voice\":null") != NULL);

    /* Longest possible status line still fits in CP_LINE_MAX. */
    char buf[CP_LINE_MAX];
    cp_status_msg_t m = { "AAAAAAAAAAAAAAAAAAAAAAA", "heartbeat", "camera", "starting",
                          999, true, UINT32_MAX, UINT32_MAX, UINT32_MAX,
                          true, 120, false };
    CHECK(cp_format_status(buf, sizeof buf, &m) > 0);
}

/* Audio must not touch the empty timer: run the same scenario with and
 * without loud speech and compare occupancy and empty_for_s. */
static long status_empty_for_s(uint32_t t)
{
    cp_app_on_line("STATUS", t);
    const char *k = strstr(g_last, "\"empty_for_s\":");
    return k ? strtol(k + 14, NULL, 10) : -1;
}

static void run_empty_scenario(bool with_audio, long *empty_s, bool *occ_in_hold, bool *occ_after_hold)
{
    const cp_platform_t plat = { capture };
    uint32_t t = 0;
    cp_app_init(&plat, false, t);
    for (unsigned i = 0; i < CP_ENTER_FRAMES + 2; i++) cp_app_on_detection(1, t += 200);
    CHECK(cp_app_occupied());
    const uint32_t empty_start = t;
    *occ_in_hold = true;
    while (t < empty_start + CP_EMPTY_HOLD_MS - 400) {
        cp_app_on_detection(0, t += 200);
        if (with_audio) cp_app_on_audio(90, true, t);
        cp_app_tick(t);
        if (!cp_app_occupied()) *occ_in_hold = false;
    }
    while (t < empty_start + CP_EMPTY_HOLD_MS + 4000) {
        cp_app_on_detection(0, t += 200);
        if (with_audio) cp_app_on_audio(90, true, t);
        cp_app_tick(t);
    }
    *occ_after_hold = cp_app_occupied();
    *empty_s = status_empty_for_s(t);
}

static void test_audio_never_changes_empty_timer(void)
{
    long e0, e1; bool b0, b1, a0, a1;
    run_empty_scenario(false, &e0, &b0, &a0);
    run_empty_scenario(true, &e1, &b1, &a1);
    CHECK(b0 && b1);     /* still occupied inside the hold window */
    CHECK(!a0 && !a1);   /* released after CP_EMPTY_HOLD_MS, audio or not */
    CHECK(e0 > 0);       /* timer is running */
    CHECK(e1 == e0);     /* identical with loud speech */
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
    test_audio_never_changes_empty_timer();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

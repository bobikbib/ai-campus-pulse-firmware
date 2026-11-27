/*
 * Portable host program for the webcam sensor: the laptop stand-in for the
 * board. A detector (browser) feeds per-frame numbers on stdin; the same app
 * core the board runs turns them into "CP>{json}" lines on stdout.
 *
 *   DET <people 0-999>      frame result from the person detector
 *   AUD <db 0-120> <0|1>    noise level and voice flag
 *   TICK                    clock pulse (the server sends one every ~100 ms)
 *   anything else           forwarded to cp_app_on_line (ROOM, STATUS, CFG ...)
 *
 * Only C11 + stdio: builds on Windows, Linux and macOS. EOF on stdin exits.
 */
#include "../src/app/cp_app.h"
#include "../src/core/cp_config.h"
#include "../src/core/protocol.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Clock: C11 timespec_get where the C library has it. Older MinGW (msvcrt,
 * non-UCRT) lacks it, so fall back to the Windows tick counter there. */
#if defined(TIME_UTC)
static double raw_ms(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}
#elif defined(_WIN32)
#include <windows.h>
static double raw_ms(void)
{
    return (double)GetTickCount64();
}
#else
#error "no portable millisecond clock available"
#endif

static double g_t0_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(long long)(raw_ms() - g_t0_ms);
}

static void write_line(const char *line)
{
    puts(line);
    fflush(stdout);
}

static void report_error(const char *what)
{
    char buf[CP_LINE_MAX];
    if (cp_format_error(buf, sizeof buf, what) > 0) {
        write_line(buf);
    }
}

/* Parse a whole token as an integer in [0, max]. */
static bool parse_uint(const char *tok, long max, long *out)
{
    if (!tok || !isdigit((unsigned char)tok[0])) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    const long v = strtol(tok, &end, 10);
    if (errno != 0 || *end != '\0' || v < 0 || v > max) {
        return false;
    }
    *out = v;
    return true;
}

static bool is_cmd(const char *tok, const char *name)
{
    if (!tok) {
        return false;
    }
    for (; *name; name++, tok++) {
        if (toupper((unsigned char)*tok) != *name) {
            return false;
        }
    }
    return *tok == '\0';
}

static void handle_line(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' '  || line[n - 1] == '\t')) {
        line[--n] = '\0';
    }

    char work[CP_LINE_MAX];
    snprintf(work, sizeof work, "%s", line);
    /* portable tokenizer (strtok_r is POSIX only) */
    char *toks[4] = { NULL, NULL, NULL, NULL };
    int count = 0;
    for (char *p = work; *p;) {
        while (*p == ' ' || *p == '\t') { p++; }
        if (!*p) { break; }
        if (count < 4) { toks[count] = p; }
        count++;
        while (*p && *p != ' ' && *p != '\t') { p++; }
        if (*p) { *p++ = '\0'; }
    }

    if (count == 0) {
        return;
    }

    if (is_cmd(toks[0], "DET")) {
        long people;
        if (count != 2 || !parse_uint(toks[1], 999, &people)) {
            report_error("bad DET: expected DET <0-999>");
            return;
        }
        cp_app_on_detection((uint16_t)people, now_ms());
        cp_app_tick(now_ms());
    } else if (is_cmd(toks[0], "AUD")) {
        long db, voice;
        if (count != 3 || !parse_uint(toks[1], 120, &db) ||
            !parse_uint(toks[2], 1, &voice)) {
            report_error("bad AUD: expected AUD <0-120> <0|1>");
            return;
        }
        cp_app_on_audio((uint8_t)db, voice != 0, now_ms());
        cp_app_tick(now_ms());
    } else if (is_cmd(toks[0], "TICK")) {
        cp_app_tick(now_ms());
    } else {
        cp_app_on_line(line, now_ms());
    }
}

int main(void)
{
    g_t0_ms = raw_ms();
    const cp_platform_t plat = { .write_line = write_line };
    cp_app_init(&plat, false, now_ms());

    char line[CP_LINE_MAX];
    while (fgets(line, sizeof line, stdin)) {
        if (!strchr(line, '\n') && !feof(stdin)) {
            /* overlong line: drop the remainder, reject the frame */
            int c;
            while ((c = getchar()) != EOF && c != '\n') { }
            report_error("line too long");
            continue;
        }
        handle_line(line);
    }
    return 0;
}

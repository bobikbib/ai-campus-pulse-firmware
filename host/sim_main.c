/*
 * Host simulator: runs the exact firmware app core on a laptop.
 *
 *   ./build/cp_sim                 interactive: type commands (SIM 1, SIM 0, STATUS ...)
 *   ./build/cp_sim --speed 60      time runs 60x faster (1 real s = 1 board min)
 *   ./build/cp_sim --demo          scripted Ghost-Booking scenario, then exit
 *
 * Output is the same "CP>{json}" lines the board prints on its UART, so the
 * website/bridge can be developed against this before touching hardware.
 */
#define _POSIX_C_SOURCE 200809L

#include "../src/app/cp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <unistd.h>

static double g_speed = 1.0;
static struct timespec g_t0;

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    const double real_ms = (double)(t.tv_sec - g_t0.tv_sec) * 1000.0 +
                           (double)(t.tv_nsec - g_t0.tv_nsec) / 1e6;
    return (uint32_t)(real_ms * g_speed);
}

static void write_line(const char *line)
{
    puts(line);
    fflush(stdout);
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void run_for(uint32_t board_ms)
{
    const uint32_t until = now_ms() + board_ms;
    while ((int32_t)(until - now_ms()) > 0) {
        cp_app_tick(now_ms());
        sleep_ms(10);
    }
}

static void say(const char *cmd)
{
    printf("# >> %s\n", cmd);
    cp_app_on_line(cmd, now_ms());
}

/* Demo: student arrives, leaves, room stays empty -> server would release. */
static int run_demo(void)
{
    say("ROOM A-101");
    say("SIM 2");          run_for(3000);
    printf("# two students present\n");
    say("SIM 0");          run_for(15000);
    printf("# room empty; empty_for_s keeps growing -> server releases at 2700 s\n");
    say("SIM 1");          run_for(2000);
    printf("# someone came back -> timer reset\n");
    say("STATUS");
    return 0;
}

int main(int argc, char **argv)
{
    bool demo = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            g_speed = atof(argv[++i]);
            if (g_speed <= 0) {
                fprintf(stderr, "--speed must be > 0\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo = true;
        } else {
            fprintf(stderr, "usage: %s [--speed N] [--demo]\n", argv[0]);
            return 2;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    const cp_platform_t plat = { .write_line = write_line };
    cp_app_init(&plat, true, now_ms());

    if (demo) {
        return run_demo();
    }

    char line[256];
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { 0, 50000 };
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            if (!fgets(line, sizeof line, stdin)) {
                break; /* EOF */
            }
            cp_app_on_line(line, now_ms());
        }
        cp_app_tick(now_ms());
    }
    return 0;
}

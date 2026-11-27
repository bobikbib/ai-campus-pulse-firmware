#include "protocol.h"
#include "cp_config.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int finish(int n, size_t len)
{
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

bool cp_room_id_valid(const char *id)
{
    size_t n = 0;
    if (!id || !*id) {
        return false;
    }
    for (; id[n]; n++) {
        const char c = id[n];
        if (n >= CP_ROOM_ID_MAX - 1) {
            return false;
        }
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) {
            return false; /* also keeps room ids safe to embed in JSON */
        }
    }
    return true;
}

/* Copies the next whitespace-separated token into tok. Returns pointer after it. */
static const char *next_token(const char *s, char *tok, size_t tok_len)
{
    size_t n = 0;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') {
        if (n + 1 < tok_len) {
            tok[n++] = *s;
        } else {
            tok[0] = '\0';  /* token too long: report as empty-invalid */
            n = 0;
            tok_len = 1;    /* swallow the rest of the token */
        }
        s++;
    }
    tok[n] = '\0';
    return s;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (!*s) {
        return false;
    }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        const uint32_t d = (uint32_t)(*s - '0');
        if (v > (UINT32_MAX - d) / 10u) {
            return false;
        }
        v = v * 10u + d;
    }
    *out = v;
    return true;
}

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

static bool rest_is_empty(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return *s == '\0';
}

void cp_parse_command(const char *line, cp_cmd_t *out)
{
    char cmd[16], a1[32], a2[16];
    memset(out, 0, sizeof(*out));
    out->type = CP_CMD_INVALID;

    if (!line) {
        out->error = "empty";
        return;
    }
    const char *p = next_token(line, cmd, sizeof cmd);
    p = next_token(p, a1, sizeof a1);
    p = next_token(p, a2, sizeof a2);

    if (!rest_is_empty(p)) {
        out->error = "too many arguments";
        return;
    }
    if (!cmd[0]) {
        out->error = "empty";
        return;
    }

    if (ieq(cmd, "STATUS") || ieq(cmd, "HELP")) {
        if (a1[0]) {
            out->error = "no arguments expected";
            return;
        }
        out->type = ieq(cmd, "STATUS") ? CP_CMD_STATUS : CP_CMD_HELP;
    } else if (ieq(cmd, "ROOM")) {
        if (a2[0] || !cp_room_id_valid(a1)) {
            out->error = "room id: 1-23 chars of A-Z a-z 0-9 - _";
            return;
        }
        strncpy(out->room, a1, sizeof out->room - 1);
        out->type = CP_CMD_ROOM;
    } else if (ieq(cmd, "MODE")) {
        if (a2[0] || !(ieq(a1, "SIM") || ieq(a1, "CAM"))) {
            out->error = "usage: MODE SIM|CAM";
            return;
        }
        out->mode_sim = ieq(a1, "SIM");
        out->type = CP_CMD_MODE;
    } else if (ieq(cmd, "SIM")) {
        if (a2[0] || !parse_u32(a1, &out->value) || out->value > 999u) {
            out->error = "usage: SIM <0-999>";
            return;
        }
        out->type = CP_CMD_SIM;
    } else if (ieq(cmd, "CFG")) {
        if (!parse_u32(a2, &out->value)) {
            out->error = "usage: CFG HOLD <ms> | CFG ENTER <frames>";
            return;
        }
        if (ieq(a1, "HOLD")) {
            out->type = CP_CMD_CFG_HOLD;
        } else if (ieq(a1, "ENTER") && out->value >= 1u && out->value <= 100u) {
            out->type = CP_CMD_CFG_ENTER;
        } else {
            out->error = "usage: CFG HOLD <ms> | CFG ENTER <1-100>";
        }
    } else {
        out->error = "unknown command (try HELP)";
    }
}

int cp_format_status(char *buf, size_t len, const cp_status_msg_t *m)
{
    char noise[8] = "null";
    if (m->has_audio) {
        snprintf(noise, sizeof noise, "%u", (unsigned)m->noise_db);
    }
    const char *voice = m->has_audio ? (m->voice ? "true" : "false") : "null";
    const int n = snprintf(buf, len,
        CP_LINE_PREFIX "{\"v\":%d,\"type\":\"presence\",\"room\":\"%s\","
        "\"people_count\":%u,\"occupied\":%s,\"empty_for_s\":%lu,"
        "\"sensor\":\"%s\",\"mode\":\"%s\",\"reason\":\"%s\","
        "\"uptime_s\":%lu,\"frames\":%lu,"
        "\"noise_db\":%s,\"voice\":%s}",
        CP_PROTOCOL_VERSION, m->room, (unsigned)m->people_count,
        m->occupied ? "true" : "false", (unsigned long)m->empty_for_s,
        m->sensor, m->mode, m->reason,
        (unsigned long)m->uptime_s, (unsigned long)m->frames,
        noise, voice);
    return finish(n, len);
}

int cp_format_ack(char *buf, size_t len, const char *what)
{
    return finish(snprintf(buf, len,
        CP_LINE_PREFIX "{\"v\":%d,\"type\":\"ack\",\"cmd\":\"%s\"}",
        CP_PROTOCOL_VERSION, what), len);
}

int cp_format_error(char *buf, size_t len, const char *what)
{
    return finish(snprintf(buf, len,
        CP_LINE_PREFIX "{\"v\":%d,\"type\":\"error\",\"message\":\"%s\"}",
        CP_PROTOCOL_VERSION, what), len);
}

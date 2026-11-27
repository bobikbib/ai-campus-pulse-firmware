/*
 * Wire protocol between the board and the outside world (UART / console).
 *
 * Board -> host: one JSON object per line ("JSON Lines"), always prefixed
 * with "CP>" so a bridge can pick our lines out of other console output:
 *
 *   CP>{"v":1,"type":"presence","room":"ROOM-101","people_count":0,
 *       "occupied":false,"empty_for_s":63,"sensor":"ok","mode":"camera",
 *       "reason":"heartbeat","uptime_s":120,"frames":2400}
 *
 * Host -> board: one text command per line:
 *   STATUS                 force a status line now
 *   ROOM <id>              set the room id (A-Z a-z 0-9 - _, max 23 chars)
 *   MODE SIM | MODE CAM    switch detector source
 *   SIM <n>                simulated people count (SIM mode only)
 *   CFG HOLD <ms>          empty-hold window
 *   CFG ENTER <frames>     frames needed to confirm presence
 *   HELP
 * Replies: CP>{"v":1,"type":"ack",...} or CP>{"v":1,"type":"error",...}
 */
#ifndef CP_PROTOCOL_H
#define CP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CP_LINE_PREFIX "CP>"

typedef enum {
    CP_CMD_INVALID = 0,
    CP_CMD_STATUS,
    CP_CMD_ROOM,
    CP_CMD_MODE,
    CP_CMD_SIM,
    CP_CMD_CFG_HOLD,
    CP_CMD_CFG_ENTER,
    CP_CMD_HELP
} cp_cmd_type_t;

typedef struct {
    cp_cmd_type_t type;
    uint32_t      value;       /* SIM / CFG numeric argument */
    bool          mode_sim;    /* MODE: true=SIM, false=CAM */
    char          room[24];    /* ROOM argument */
    const char   *error;       /* set when type == CP_CMD_INVALID */
} cp_cmd_t;

/* Parse one command line (trailing \r\n allowed). Never fails silently:
 * on error, type=CP_CMD_INVALID and error explains why. */
void cp_parse_command(const char *line, cp_cmd_t *out);

typedef struct {
    const char *room;
    const char *reason;      /* "heartbeat", "change", "status", "boot" */
    const char *mode;        /* "camera" or "sim" */
    const char *sensor;
    uint16_t    people_count;
    bool        occupied;
    uint32_t    empty_for_s;
    uint32_t    uptime_s;
    uint32_t    frames;
} cp_status_msg_t;

/* Format helpers. Return bytes written (excluding NUL), or -1 if truncated. */
int cp_format_status(char *buf, size_t len, const cp_status_msg_t *m);
int cp_format_ack(char *buf, size_t len, const char *what);
int cp_format_error(char *buf, size_t len, const char *what);

bool cp_room_id_valid(const char *id);

#endif /* CP_PROTOCOL_H */

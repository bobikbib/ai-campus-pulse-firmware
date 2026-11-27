/*
 * AI Campus Pulse — compile-time defaults.
 * Every value can be overridden with -D at build time or at runtime via CFG commands.
 */
#ifndef CP_CONFIG_H
#define CP_CONFIG_H

/* Firmware/protocol version reported in every status message. */
#define CP_FW_VERSION          "0.1.0"
#define CP_PROTOCOL_VERSION    1

/* Default room this board is installed in (override with "ROOM <id>"). */
#ifndef CP_DEFAULT_ROOM_ID
#define CP_DEFAULT_ROOM_ID     "ROOM-101"
#endif
#define CP_ROOM_ID_MAX         24

/* Frames in a row with people before the room counts as occupied. */
#ifndef CP_ENTER_FRAMES
#define CP_ENTER_FRAMES        3u
#endif

/* How long the room must look empty before we report occupied=false.
 * Prevents flicker when a person is briefly hidden (turned away, bent down). */
#ifndef CP_EMPTY_HOLD_MS
#define CP_EMPTY_HOLD_MS       10000u
#endif

/* No detector frame for this long -> sensor="stale" (camera/AI stopped). */
#ifndef CP_STALE_MS
#define CP_STALE_MS            5000u
#endif

/* Periodic heartbeat even when nothing changes. */
#ifndef CP_HEARTBEAT_MS
#define CP_HEARTBEAT_MS        5000u
#endif

/* Person counter: mask pixels >= threshold are "person". */
#ifndef CP_MASK_THRESHOLD
#define CP_MASK_THRESHOLD      128u
#endif

/* Blobs smaller than this (in mask pixels) are treated as noise. */
#ifndef CP_MIN_BLOB_AREA
#define CP_MIN_BLOB_AREA       40u
#endif

/* Max distinct blobs the labeler tracks before merging (bounds RAM). */
#ifndef CP_MAX_LABELS
#define CP_MAX_LABELS          256u
#endif

/* Min gap between audio-triggered reports (voice flicker must not flood). */
#ifndef CP_AUDIO_REPORT_MIN_MS
#define CP_AUDIO_REPORT_MIN_MS 1000u
#endif

#define CP_LINE_MAX            320

#endif /* CP_CONFIG_H */

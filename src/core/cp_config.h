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

/* NFC: after a card scan, the camera must see someone within this time,
 * otherwise the entry is rejected. */
#ifndef CP_NFC_CONFIRM_MS
#define CP_NFC_CONFIRM_MS      10000u
#endif

/* Voice in the report interval if at least this % of 100 ms windows had speech. */
#ifndef CP_VOICE_MIN_PERCENT
#define CP_VOICE_MIN_PERCENT   10u
#endif

/* Noise level categories, dB SPL averaged over a report interval:
 *   too_loud >= CP_NOISE_LOUD_MIN
 *   noisy    >= CP_NOISE_QUIET_MAX
 *   quiet    below that
 *   empty    quiet + nobody on camera + no voice
 * A category is only left again once the level is CP_NOISE_HYSTERESIS_DB past
 * the threshold, so a room hovering at 50 dB doesn't flip every 5 s. */
#ifndef CP_NOISE_QUIET_MAX
#define CP_NOISE_QUIET_MAX     50u
#endif
#ifndef CP_NOISE_LOUD_MIN
#define CP_NOISE_LOUD_MIN      70u
#endif
#ifndef CP_NOISE_HYSTERESIS_DB
#define CP_NOISE_HYSTERESIS_DB 3u
#endif

/* Noise scale 1-5 (dB SPL, report-interval average), lower bound of each step:
 *   1 silent < 40 <= 2 quiet < 50 <= 3 moderate < 60 <= 4 noisy < 70 <= 5 too loud
 * Going back down needs CP_NOISE_HYSTERESIS_DB below the step's lower bound. */
#ifndef CP_NOISE_SCALE_2
#define CP_NOISE_SCALE_2       40u
#endif
#ifndef CP_NOISE_SCALE_3
#define CP_NOISE_SCALE_3       50u
#endif
#ifndef CP_NOISE_SCALE_4
#define CP_NOISE_SCALE_4       60u
#endif
#ifndef CP_NOISE_SCALE_5
#define CP_NOISE_SCALE_5       70u
#endif

/* Camera duty cycle. The camera is "active" (every frame analyzed) while
 * something is happening: people on camera, an NFC scan waiting, voice or a
 * noise level >= CP_CAM_WAKE_DB. After CP_CAM_HOLD_MS without any of that it
 * goes "asleep": one frame every CP_CAM_SLEEP_PERIOD_MS, enough for the
 * 45-minute rule and to notice someone sitting silently. */
#ifndef CP_CAM_SLEEP_PERIOD_MS
#define CP_CAM_SLEEP_PERIOD_MS 15000u
#endif
#ifndef CP_CAM_HOLD_MS
#define CP_CAM_HOLD_MS         30000u
#endif
#ifndef CP_CAM_WAKE_DB
#define CP_CAM_WAKE_DB         50u
#endif

#define CP_LINE_MAX            320

#endif /* CP_CONFIG_H */

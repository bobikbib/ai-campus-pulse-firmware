/*
 * Room noise level and voice activity from microphone samples, on the board.
 *
 * Input:  16-bit PCM from the kit's PDM microphone (any block size).
 * Output: one result per analysis window (default 100 ms):
 *           - level in dB SPL (estimated from dBFS + mic calibration offset)
 *           - voice: true if the window looks like speech
 *
 * Nothing about the audio leaves the board: only these two numbers.
 *
 * Voice detection is a deliberately simple, explainable heuristic that fits
 * a microcontroller:
 *   - the window is clearly louder than the background noise floor
 *     (floor tracked per window: drops at once, rises slowly), and
 *   - its zero-crossing rate is low, as for voiced speech (pitch ~80-400 Hz),
 *     unlike hiss / fan / white noise, which cross zero constantly.
 *     Crossings are counted with hysteresis at twice the background noise
 *     amplitude, so background noise riding on a voice doesn't add crossings
 *     (without it, speech in a 55+ dB room was missed).
 * It is not a speech recognizer: it answers "is someone talking here?".
 */
#ifndef CP_AUDIO_H
#define CP_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t sample_rate_hz;   /* e.g. 16000 */
    uint32_t window_ms;        /* analysis window, e.g. 100 */
    int32_t  dbfs_to_spl;      /* dB SPL = dBFS + this; 120 for a -26 dBFS @ 94 dB SPL mic */
    float    voice_margin_db;  /* how far above the noise floor speech must be */
    float    voice_zcr_max;    /* max zero crossings per sample for speech */
    float    voice_min_spl;    /* ignore anything quieter than this */
    float    floor_rise_db;    /* how fast the noise floor may rise, per window */
} cp_audio_cfg_t;

typedef struct {
    float level_spl;  /* dB SPL, clamped to 0..120 */
    bool  voice;
} cp_audio_window_t;

typedef struct {
    cp_audio_cfg_t cfg;
    uint32_t window_samples;
    uint64_t sum_sq;          /* sum of squares in the current window */
    uint32_t n;               /* samples in the current window */
    uint32_t zero_crossings;
    int      last_sign;       /* -1, 0 (unknown), +1 */
    float    floor_spl;       /* background noise estimate */
    float    zc_threshold;    /* zero crossings count only past +-this (2x floor RMS) */
    bool     has_floor;
} cp_audio_t;

void cp_audio_init(cp_audio_t *a, const cp_audio_cfg_t *cfg);

/* Defaults: 16 kHz, 100 ms windows, +120 dB calibration, 8 dB margin. */
void cp_audio_default_cfg(cp_audio_cfg_t *cfg);

/* Feed samples. Writes up to max_out finished windows to out and returns how
 * many were written. Samples beyond max_out finished windows are still
 * consumed (their windows are dropped), so callers can't stall the analyzer. */
size_t cp_audio_feed(cp_audio_t *a, const int16_t *pcm, size_t n,
                     cp_audio_window_t *out, size_t max_out);

#endif /* CP_AUDIO_H */

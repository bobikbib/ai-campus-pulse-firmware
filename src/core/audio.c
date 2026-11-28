#include "audio.h"

#include <math.h>
#include <string.h>

void cp_audio_default_cfg(cp_audio_cfg_t *cfg)
{
    cfg->sample_rate_hz  = 16000u;
    cfg->window_ms       = 100u;
    cfg->dbfs_to_spl     = 120;
    cfg->voice_margin_db = 8.0f;
    cfg->voice_zcr_max   = 0.15f;
    cfg->voice_min_spl   = 35.0f;
    cfg->floor_rise_db   = 0.05f;   /* 0.5 dB/s with 100 ms windows */
}

void cp_audio_init(cp_audio_t *a, const cp_audio_cfg_t *cfg)
{
    memset(a, 0, sizeof *a);
    a->cfg = *cfg;
    a->window_samples = (uint32_t)((uint64_t)cfg->sample_rate_hz * cfg->window_ms / 1000u);
    if (a->window_samples == 0u) {
        a->window_samples = 1u;
    }
}

static cp_audio_window_t finish_window(cp_audio_t *a)
{
    cp_audio_window_t w;
    /* Single-precision math only: the M55 FPU does it in hardware, double
     * would be software-emulated. */
    const float mean_sq = (float)a->sum_sq / (float)a->n;
    float spl;
    if (mean_sq <= 0.0f) {
        spl = 0.0f;
    } else {
        /* dBFS of the RMS relative to full scale (32768). */
        const float dbfs = 10.0f * log10f(mean_sq / (32768.0f * 32768.0f));
        spl = dbfs + (float)a->cfg.dbfs_to_spl;
    }
    if (spl < 0.0f) {
        spl = 0.0f;
    }
    if (spl > 120.0f) {
        spl = 120.0f;
    }
    const float zcr = (float)a->zero_crossings / (float)a->n;

    /* Noise floor: follows quiet windows immediately, rises only slowly, so a
     * person talking doesn't become "background" within a few seconds. */
    if (!a->has_floor || spl < a->floor_spl) {
        a->floor_spl = spl;
        a->has_floor = true;
    } else {
        a->floor_spl += a->cfg.floor_rise_db;
    }

    /* Next window counts zero crossings only past twice the floor amplitude. */
    a->zc_threshold = 2.0f * 32768.0f *
        powf(10.0f, (a->floor_spl - (float)a->cfg.dbfs_to_spl) / 20.0f);

    w.level_spl = spl;
    w.voice = spl >= a->cfg.voice_min_spl &&
              spl >= a->floor_spl + a->cfg.voice_margin_db &&
              zcr <= a->cfg.voice_zcr_max;

    a->sum_sq = 0u;
    a->n = 0u;
    a->zero_crossings = 0u;
    return w;
}

size_t cp_audio_feed(cp_audio_t *a, const int16_t *pcm, size_t n,
                     cp_audio_window_t *out, size_t max_out)
{
    size_t produced = 0;
    if (!a || (!pcm && n)) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        const int32_t s = pcm[i];
        a->sum_sq += (uint64_t)((int64_t)s * s);
        const float th = a->zc_threshold;
        const int sign = ((float)s > th) ? 1 : (((float)s < -th) ? -1 : 0);
        if (sign != 0) {
            if (a->last_sign != 0 && sign != a->last_sign) {
                a->zero_crossings++;
            }
            a->last_sign = sign;
        }
        if (++a->n >= a->window_samples) {
            const cp_audio_window_t w = finish_window(a);
            if (out && produced < max_out) {
                out[produced++] = w;
            }
        }
    }
    return produced;
}

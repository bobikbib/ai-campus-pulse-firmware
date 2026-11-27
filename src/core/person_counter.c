#include "person_counter.h"
#include "cp_config.h"

#include <string.h>

static uint16_t uf_find(uint16_t *parent, uint16_t x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; /* path halving */
        x = parent[x];
    }
    return x;
}

static void uf_union(uint16_t *parent, uint16_t a, uint16_t b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a == b) {
        return;
    }
    if (a < b) {
        parent[b] = a;
    } else {
        parent[a] = b;
    }
}

int cp_count_people(const uint8_t *mask, uint16_t width, uint16_t height,
                    const cp_counter_cfg_t *cfg, cp_counter_ws_t *ws,
                    cp_counter_result_t *out)
{
    if (!mask || !cfg || !ws || !ws->labels || !out || width == 0 || height == 0) {
        return -1;
    }
    const size_t n = (size_t)width * height;
    if (ws->labels_len < n) {
        return -1;
    }

    uint16_t parent[CP_MAX_LABELS];
    uint32_t area[CP_MAX_LABELS];
    uint16_t next_label = 1; /* 0 = background */

    memset(out, 0, sizeof(*out));
    parent[0] = 0;

    /* Pass 1: provisional labels + record equivalences. */
    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            const size_t i = (size_t)y * width + x;
            if (mask[i] < cfg->threshold) {
                ws->labels[i] = 0;
                continue;
            }
            out->fg_pixels++;
            const uint16_t left = (x > 0) ? ws->labels[i - 1] : 0;
            const uint16_t up   = (y > 0) ? ws->labels[i - width] : 0;

            if (left == 0 && up == 0) {
                if (next_label < CP_MAX_LABELS) {
                    parent[next_label] = next_label;
                    ws->labels[i] = next_label++;
                } else {
                    /* Out of labels: attach to the last label and flag it. */
                    out->saturated = 1;
                    ws->labels[i] = (uint16_t)(CP_MAX_LABELS - 1);
                }
            } else if (left != 0 && up != 0) {
                ws->labels[i] = (left < up) ? left : up;
                uf_union(parent, left, up);
            } else {
                ws->labels[i] = left ? left : up;
            }
        }
    }

    /* Pass 2: resolve roots and accumulate area per root. */
    memset(area, 0, sizeof(uint32_t) * next_label);
    for (size_t i = 0; i < n; i++) {
        if (ws->labels[i] != 0) {
            const uint16_t root = uf_find(parent, ws->labels[i]);
            ws->labels[i] = root;
            area[root]++;
        }
    }

    for (uint16_t l = 1; l < next_label; l++) {
        if (area[l] == 0) {
            continue; /* not a root */
        }
        if (area[l] > out->largest_area) {
            out->largest_area = area[l];
        }
        if (area[l] >= cfg->min_area) {
            out->people++;
        }
    }
    return 0;
}

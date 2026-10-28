/* SPDX-License-Identifier: MPL-2.0 */
/* The mod matrix: see wind_matrix.h. */
#include "wind_matrix.h"

const char *const WM_SRC_NAME[WM_NSRC] = {"Off", "Breath", "Bite", "Mod wh", "Press", "Veloc", "LFO"};
const char *const WM_DST_NAME[WM_NDST] = {"Cutoff", "Reso", "Amp", "Morph", "Timbre", "Noise",
                                               "Harmonics", "Aux"};

void wm_apply(const wm_slot_t slot[WM_NSLOT], const float base[WM_NDST], const float src[WM_NSRC],
              float out[WM_NDST])
{
    int d, i;
    for (d = 0; d < WM_NDST; d++)
        out[d] = d == WM_DST_AMP ? 1.0f : base[d];
    for (i = 0; i < WM_NSLOT; i++) {
        const wm_slot_t *s = &slot[i];
        float a = (float)s->amount * 0.01f, x;
        if (s->src == WM_SRC_OFF || s->src >= WM_NSRC || s->dst >= WM_NDST || !s->amount)
            continue;
        x = src[s->src];
        if (s->dst == WM_DST_AMP) {          /* each slot's gain stops at 0: two below it don't multiply up */
            float g = a > 0.0f ? 1.0f - a + a * x : 1.0f + a * x;
            out[WM_DST_AMP] *= g < 0.0f ? 0.0f : g;
        } else
            out[s->dst] += a * x;
    }
    for (d = 0; d < WM_NDST; d++)
        out[d] = out[d] < 0.0f ? 0.0f : out[d] > 1.0f ? 1.0f : out[d];
}

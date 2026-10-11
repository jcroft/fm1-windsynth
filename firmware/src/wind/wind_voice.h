/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice: one mono Plaits engine, its gate envelope and VCA, behind a C API.
 * All the C++ stays in wind_voice.cc. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WV_SR 44117.647f                /* the FM-1's I2S rate (and the host's, for renders) */

typedef struct {            /* control state, per 64-frame block */
    float note;             /* MIDI note + bend + glide, in semitones */
    float gate;             /* 0 or 1 */
    float cutoff, res, amp; /* 0..1 after the matrix */
    float timbre, morph, harmonics;
} wind_frame_t;

enum { WV_CLASSIC, WV_NENGINE };        /* 0: Plaits VA-VCF */
enum { WV_SLOPE_12, WV_SLOPE_24 };      /* VA-VCF's filter: 12 dB/oct (LP) or 24 dB/oct (LP>LP) */

void wv_init(void);
void wv_engine(uint32_t id);
void wv_slope(uint32_t slope);
void wv_env(float attack_s, float release_s);  /* the gate envelope: rise time, fall to -40 dB */
/* frames stereo frames, 24-bit in int32, the same signal on L and R */
void wv_render(const wind_frame_t *f, int32_t *out_lr, uint32_t frames);

/* the engine alone, as upstream Plaits sees it: no envelope, VCA, DC blocker or clip (tests) */
void wv_render_raw(float note, float timbre, float morph, float harmonics, float *out, uint32_t n);

#ifdef __cplusplus
}
#endif

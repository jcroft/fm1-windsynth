/* SPDX-License-Identifier: MPL-2.0 */
/* The maths the wind code uses, in one seam. In this firmware it is dsp/fastmath.h (X0X's, GPL-3.0-only:
 * no libm on the device, and the host and browser builds compute the device's samples). That file is
 * not part of the wind code's MPL-2.0 licence. To use these files elsewhere, build with
 * -DWIND_MATH_LIBM and the C library's maths stands in. */
#pragma once

#ifndef WIND_MATH_LIBM
#include "../dsp/fastmath.h"
#else
#include <math.h>

#define FM_PI 3.14159265358979f
#define FM_TWO_PI 6.28318530717959f
#define FM_LN2 0.693147180559945f
#define FM_LOG2E 1.44269504088896f

static inline float fm_fabsf(float x) { return fabsf(x); }
static inline float fm_minf(float a, float b) { return a < b ? a : b; }
static inline float fm_maxf(float a, float b) { return a > b ? a : b; }
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float fm_floorf(float x) { return floorf(x); }
static inline float fm_exp2f(float x) { return exp2f(x); }
static inline float fm_expf(float x) { return expf(x); }
static inline float fm_log2f(float x) { return x > 0.0f ? log2f(x) : -127.0f; }
static inline float fm_logf(float x) { return x > 0.0f ? logf(x) : -88.0f; }
static inline float fm_powf(float x, float y) { return x > 0.0f ? powf(x, y) : 0.0f; }
static inline float fm_db2lin(float db) { return powf(10.0f, db * 0.05f); }
static inline float fm_tanhf(float x) { return tanhf(x); }
static inline float fm_sinf(float x) { return sinf(x); }
static inline float fm_cosf(float x) { return cosf(x); }
static inline float fm_tanf(float x) { return tanf(x); }
static inline float fm_sin_turns(float ph) { return sinf(FM_TWO_PI * (ph - floorf(ph))); }
static inline float fm_sqrtf(float x) { return x > 0.0f ? sqrtf(x) : 0.0f; }
static inline float fm_rsqrtf(float x) { return 1.0f / sqrtf(x); }
static inline float fm_flush(float x) { return fabsf(x) < 1e-20f ? 0.0f : x; }
#endif

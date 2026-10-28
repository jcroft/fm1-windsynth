/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
/* The maths the wind code uses, in one seam. In this firmware it is dsp/fastmath.h (FoMni / X0X, GPL-3.0-only:
 * no libm on the device, and the host and browser builds compute the device's samples). That file is
 * not part of the wind code's MPL-2.0 licence. To use these files elsewhere, build with
 * -DWIND_MATH_LIBM and the C library's maths stands in. That build sounds the same but is not
 * sample-identical, and at the range edges libm can return infinities and denormals where fastmath
 * clamps or flushes. */
#pragma once

#ifndef WIND_MATH_LIBM
#include "../dsp/fastmath.h"
#else
/* the C library's maths, through GCC's and Clang's builtins rather than <math.h>: the freestanding
 * C++ build puts shim/ first on the include path, and shim/math.h maps sinf and the rest to these */

#define FM_PI 3.14159265358979f
#define FM_TWO_PI 6.28318530717959f
#define FM_LN2 0.693147180559945f
#define FM_LOG2E 1.44269504088896f

static inline float fm_fabsf(float x) { return __builtin_fabsf(x); }
static inline float fm_minf(float a, float b) { return a < b ? a : b; }
static inline float fm_maxf(float a, float b) { return a > b ? a : b; }
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float fm_floorf(float x) { return __builtin_floorf(x); }
static inline float fm_exp2f(float x) { return __builtin_exp2f(x); }
static inline float fm_expf(float x) { return __builtin_expf(x); }
static inline float fm_log2f(float x) { return x > 0.0f ? __builtin_log2f(x) : -127.0f; }
static inline float fm_logf(float x) { return x > 0.0f ? __builtin_logf(x) : -88.0f; }
static inline float fm_powf(float x, float y) { return x > 0.0f ? __builtin_powf(x, y) : 0.0f; }
static inline float fm_db2lin(float db) { return __builtin_powf(10.0f, db * 0.05f); }
static inline float fm_tanhf(float x) { return __builtin_tanhf(x); }
static inline float fm_sinf(float x) { return __builtin_sinf(x); }
static inline float fm_cosf(float x) { return __builtin_cosf(x); }
static inline float fm_tanf(float x) { return __builtin_tanf(x); }
static inline float fm_sin_turns(float ph) { return __builtin_sinf(FM_TWO_PI * (ph - __builtin_floorf(ph))); }
static inline float fm_sqrtf(float x) { return x > 0.0f ? __builtin_sqrtf(x) : 0.0f; }
static inline float fm_rsqrtf(float x) { return 1.0f / __builtin_sqrtf(x); }
static inline float fm_flush(float x) { return __builtin_fabsf(x) < 1e-20f ? 0.0f : x; }
#endif

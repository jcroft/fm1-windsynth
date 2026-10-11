/* SPDX-License-Identifier: GPL-3.0-only */
/* The vendored Plaits and stmlib code, with no libm: its few maths calls go to dsp/fastmath.h.
 * Every build of the wind voice (device, host, web) uses this, so they compute the same samples. */
#pragma once
#include "../../dsp/fastmath.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846     /* folded into float constants at compile time */
#endif
#define fabsf fm_fabsf
#define sqrtf fm_sqrtf
#define sinf fm_sinf
#define cosf fm_cosf
#define tanf fm_tanf
#define expf fm_expf
#define powf fm_powf
#define floorf fm_floorf

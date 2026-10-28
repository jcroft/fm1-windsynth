/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
/* The mod matrix: WM_NSLOT slots, each a source, a destination and an amount (-100..100).
 *
 *   Cutoff, Resonance, Morph, Timbre, Noise, Harmonics, Aux   the knob's value plus each slot's amount x its source,
 *                              clamped 0..1 (Timbre: the engine's own, FM's index; Classic has none;
 *                              Noise: the noise layer's level)
 *   Amp                        a gain from 1: a slot of amount a makes it (1 - a) + a x source, so at
 *                              100 the source alone sets the level (breath dynamics); a negative
 *                              amount turns it down as the source rises
 *
 * The last slot is fixed, breath to the cutoff (the Filter page's Breath, +75 to start); the Mod pages'
 * four start off. */
#pragma once
#include <stdint.h>

/* (the LFO is a source for the LFO page's own slot only: the Mod slots pick from the ones before it) */
enum { WM_SRC_OFF, WM_SRC_BREATH, WM_SRC_BITE, WM_SRC_MOD, WM_SRC_PRESSURE, WM_SRC_VELOCITY, WM_SRC_LFO, WM_NSRC };
/* (Harmonics: the engine's HARMONICS, on the engines where it's smooth; Aux: the Aux mix) */
enum { WM_DST_CUTOFF, WM_DST_RES, WM_DST_AMP, WM_DST_MORPH, WM_DST_TIMBRE, WM_DST_NOISE, WM_DST_HARM, WM_DST_AUX,
       WM_NDST };
#define WM_DST_PITCH WM_NDST           /* the LFO page's slot only: the note, outside the matrix */
#define WM_NSLOT 6                     /* the Mod pages' four, the LFO page's, then the Filter page's Breath */

typedef struct {
    uint8_t src, dst;
    int8_t amount;                     /* -100..100 */
} wm_slot_t;

extern const char *const WM_SRC_NAME[WM_NSRC];
extern const char *const WM_DST_NAME[WM_NDST];

/* base[d]: the knobs' values 0..1 (base[WM_DST_AMP] is ignored: the gain starts at 1);
 * src[s]: the sources now (src[WM_SRC_OFF] is ignored); out[d]: the results */
void wm_apply(const wm_slot_t slot[WM_NSLOT], const float base[WM_NDST], const float src[WM_NSRC],
              float out[WM_NDST]);

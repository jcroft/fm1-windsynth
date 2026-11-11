/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
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
    float timbre, morph, harmonics;  /* the engine's own: FM uses all three (index, feedback, ratio) */
    float noise;            /* the noise layer's level 0..1, after the matrix */
    float aux;              /* 0: the engine's main output; 1: its AUX output alone (6-op: the same) */
    float sample;           /* the sample layer's level 0..1, after the matrix (wind_sample.h plays it) */
    float engine;           /* the engine's level 0..1 (0: not run at all) */
    uint32_t start;         /* WS_START_*: a phrase or a note starts with this block (the sample's trigger) */
} wind_frame_t;

/* Plaits' engines: VA-VCF; then, into our filter, 2-op FM, 6-op FM (DX7 patches), granular formant,
 * waveshaping, virtual analog (two oscillators), phase distortion and additive (Harmonic). Add new
 * ones at the end. (0.0.10 and 0.0.11 had swarm at 6, dropped in 0.0.12: a sound saved on it then
 * loads as Classic, see project.c.) */
enum { WV_CLASSIC, WV_FM, WV_SIXOP, WV_GRAIN, WV_SHAPE, WV_VA, WV_PD, WV_HARM, WV_NENGINE };
#define WV_DX_NPATCH 96                 /* 6-op: Plaits' three banks of 32 */
enum { WV_SLOPE_12, WV_SLOPE_24 };      /* VA-VCF's filter: 12 dB/oct (LP) or 24 dB/oct (LP>LP) */

void wv_init(void);
void wv_engine(uint32_t id);
float wv_fm_ratio(float harmonics);     /* the FM engine's modulator ratio for a HARMONICS value */
float wv_note_freq(float note);        /* the note's frequency as the engines play it, cycles a sample */
void wv_dx_patch(uint32_t patch);      /* 6-op: the patch, 0..95 */
void wv_dx_name(uint32_t patch, char *name);   /* ...its name, as the DX7 has it (11 bytes) */
void wv_slope(uint32_t slope);
void wv_noise(float type, float semis, float reso);   /* the noise layer: LP..BP..HP 0..1, its pitch
                                                      * from the note, resonance 0..1 */
void wv_env(float attack_s, float release_s);  /* the gate envelope: rise time, fall to -40 dB */
float wv_env_level(void);              /* ...where it is now, 0..1 (at the end of the last render) */
/* frames stereo frames, 24-bit in int32, the same signal on L and R */
void wv_render(const wind_frame_t *f, int32_t *out_lr, uint32_t frames);
/* the next wv_render also writes its signal before the VCA (the gate envelope on, the amp and MASTER not)
 * to `to`, a float a frame (0: not) */
void wv_tap(float *to);

/* the engine alone, as upstream Plaits sees it: no envelope, VCA, DC blocker or clip (tests);
 * wv_render_raw is VA-VCF, wv_render_raw_engine any engine (the post-filter left out) */
void wv_render_raw(float note, float timbre, float morph, float harmonics, float *out, uint32_t n);
void wv_render_raw_engine(uint32_t id, float note, float timbre, float morph, float harmonics, float *out,
                          uint32_t n);
/* Classic playing f into ref, its oscillator through our filter into ours, both from their start (tests) */
void wv_filter_check(const wind_frame_t *f, float *ref, float *ours, uint32_t n);

#ifdef __cplusplus
}
#endif

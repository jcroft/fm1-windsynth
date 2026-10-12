/* SPDX-License-Identifier: GPL-3.0-only */
/* Windsynth app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. (FoMni's app.h, reshaped.) */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../wind/wind_app.h"
#include "../wind/wind_fx.h"
#include "../wind/wind_voice.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x444E4957u           /* "WIND": never a FiMba or FoMni project */
/* The format counts the settings a project holds: a newer version reads an older project's values and
 * gives the settings it lacks their defaults (its spare bytes, zeros, are never taken for settings).
 * Add a setting at the end of P_*, bump the format, and add its count here. */
#define PROJ_FORMAT 12u                   /* 1: the first (30 settings); 2: the Engine page (33); 3: FX (45);
                                          * 4: Glide depth (46); 5: Shimmer (47); 6: 6-op's patch (48);
                                          * 7: Glide in ms, the least glide time (48); 8: Distortion and
                                          * Modulation (56); 9: Noise (60); 10: Aux (61);
                                          * 11: the LFO (67); 12: the Filter page's Breath (68) */
#define PROJ_NPAR_OF(f) ((f) == 1u ? 30 : (f) == 2u ? 33 : (f) == 3u ? 45 : (f) == 4u ? 46 : \
                         (f) == 5u ? 47 : (f) == 6u || (f) == 7u ? 48 : (f) == 8u ? 56 : (f) == 9u ? 60 : (f) == 10u ? 61 : (f) == 11u ? 67 : P_NPARAMS)

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* wind_app.h P_*: the sound being played (the edit buffer) and the rig */
    uint8_t preset;                      /* the preset it came from, 0..NPRESETS-1 */
    uint8_t rsv[31];                     /* room to grow: an older, shorter project loads (zeros here) */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */

/* presets: NPRESETS sounds in flash, 16 to a bank object. A slot never saved is the init sound. */
#define NPRESETS 64
#define PRESET_NPAR 112                  /* room per preset: settings to come fit without a new format */
void preset_load(int slot);              /* its sound into the project, applied (the rig is kept) */
int preset_store(int slot);              /* the project's sound into the slot: 0 = saved */
int preset_edited(void);                 /* 1: the sound differs from its preset's */
void preset_peek(void);                  /* the project's preset as stored, for preset_edited (project_load) */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
#ifdef OM_HOST
void ui_repaint(void);
#endif
int ui_dirty(void);
void ui_say(const char *a, const char *b);

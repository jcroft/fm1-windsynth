/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: wind control (wind_ctl) into the mod matrix (wind_matrix) into the voice
 * (wind_voice), with the settings that are saved (P_*) and the state the screen shows.
 *
 * Threads: wa_render() runs in the audio interrupt and is the only caller of wind_ctl: it drains the
 * MIDI packets (through the caller's reader) and the panel's keys (queued by wa_key) at the start of
 * every 64-frame block. wa_set() and wa_key() come from the UI. */
#pragma once
#include <stdint.h>
#include "wind_matrix.h"
#include "wind_ctl.h"

#define WA_BLOCK 64                    /* frames per control block (1.45 ms) */

/* parameters, in the order they are saved: add new ones at the end */
enum {
    P_CUTOFF, P_RESO, P_MORPH, P_SLOPE,              /* Filter page (Morph: on the Engine page) */
    P_BSRC, P_CURVE, P_ATTACK, P_RELEASE,            /* Breath page (0.0.30: Breath CC on Setup) */
    P_BEND, P_GSRC, P_GLIDE, P_GMODE,                /* Pitch page (0.0.30: Glide CC on Setup) */
    P_M1SRC, P_M1DST, P_M1AMT,                       /* Mod 1..4 pages: source, destination, amount */
    P_M2SRC, P_M2DST, P_M2AMT,
    P_M3SRC, P_M3DST, P_M3AMT,
    P_M4SRC, P_M4DST, P_M4AMT,
    P_KEYBREATH, P_OCTAVE,                           /* the panel's keys (0.0.18: Key breath unused, Octave on OCT-/+) */
    P_TRANSPOSE, P_TUNE, P_MIDICH, P_THEME,          /* Setup page (0.0.30: Transpose, Tune on Pitch 2) */
    P_ENGINE, P_RATIO, P_INDEX,                      /* Engine page (0.0.6) */
    P_RVTYPE, P_RVMIX, P_RVDECAY, P_RVTONE,          /* Reverb page (0.0.7) */
    P_RVPRE, P_RVSIZE, P_RVMOD, P_RVLOW,             /* Reverb 2 page */
    P_DLMODE, P_DLMIX, P_DLTIME, P_DLFB,             /* Delay page */
    P_GDEPTH,                                        /* Pitch 2 page (0.0.8) */
    P_RVSHIM,                                        /* Reverb 2, in Mod's place on Shimmer (0.0.9) */
    P_DXPATCH,                                       /* Engine page, in Ratio's place on 6-op (0.0.10) */
    P_DSTYPE, P_DSMIX, P_DSDRIVE, P_DSTONE,          /* Distortion page (0.0.14) */
    P_MOTYPE, P_MOMIX, P_MORATE, P_MODEPTH,          /* Modulation page */
    P_NZLEVEL, P_NZTYPE, P_NZFREQ, P_NZRESO,         /* Noise page (0.0.16) */
    P_AUX,                                           /* Engine page: Plaits' AUX output, mixed in (0.0.17) */
    P_LFOSHAPE, P_LFORATE, P_LFODST, P_LFOAMT,       /* LFO page (0.0.18) */
    P_LFOFADE, P_LFOSYNC,                            /* LFO 2 page */
    P_BRCUT,                                         /* Filter page: breath to cutoff, always there (0.0.19) */
    P_STAGEROW,                                      /* Stage: the row K1-K4 turn (0.0.24) */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} wa_param_t;
const wa_param_t *wa_param_info(int i);
const char *wa_param_name(int i);      /* its name as the screen shows it: the engine's own for Ratio, Index, Morph */
int wa_param_global(int i);            /* 1: the rig's, not the sound's (kept out of presets) */
void wa_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

enum { THEME_LIGHT, THEME_DARK, THEME_N };
enum { WA_LFO_SINE, WA_LFO_TRI, WA_LFO_SAW, WA_LFO_SQUARE, WA_LFO_SH, WA_LFO_NSHAPE };
int wa_harm_smooth(int engine);        /* 1: HARMONICS can be modulated on it (Formant, Waveshape, VA, Harmonic) */
extern const char *const WA_NOTE_NAME[12];
extern const char *const WA_ENGINE_NAME[];   /* (WV_NENGINE) */

void wa_init(void);
void wa_set(int k, int v);             /* a parameter's value to the instrument */
void wa_key(int note, int on);         /* a panel key (queued for the next block) */
void wa_panic(void);                   /* everything quiet (queued for the next block) */
/* n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot); midi_in: the next USB-MIDI packet,
 * 1 = got one (plat_midi_in) */
void wa_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12, int (*midi_in)(uint32_t *pkt));

/* the state the screen shows, written by wa_render */
typedef struct {
    float note, gate, breath, bend;    /* wind_ctl's */
    float cutoff, res, amp, morph;     /* after the matrix */
    float timbre, noise, harmonics, aux;
    float lfo;                         /* the LFO, -1..1 (faded in) */
    float pitch;                       /* the note sounding, with the LFO's vibrato (note: without it) */
    float level;                       /* the output's peak in the last block, 0..1 */
    float glide_ms;                    /* the glide time in force (Glide, plus the glide CC's share) */
    int top;                           /* the note sounding (MIDI number), -1 none yet */
    uint32_t midi_seen;                /* packets received */
    float src[WM_NSRC];                /* the matrix's sources now (WM_SRC_*; Bite -1..1, the LFO faded) */
    wc_mon_t mon;                      /* the controller monitor: raw values in, and when (wind_ctl) */
    /* for the pages' views (0.0.29) */
    float breath_in;                   /* the breath as it comes in, 0..1: before the curve and the smoothing */
    float env;                         /* the gate envelope (Attack, Release), 0..1 */
    float lfo_fade;                    /* the LFO's fade-in, 0..1 */
    uint32_t lfo_starts;               /* notes from silence: each starts the fade again (Sync Note: the LFO) */
    float lfo_ph;                      /* the LFO's phase, 0..1 */
    float gliding;                     /* 1 while a glide moves (wind_ctl's) */
} wa_state_t;
extern volatile wa_state_t wa_state;

/* a knob's live value, in its own units, when something moves it (a Mod slot, the LFO, the Filter's
 * Breath, the glide CC): 1 and *v; 0 when nothing does (its value is its setting) */
int wa_live(int k, float *v);

/* the scope (the Engine page): n points, -1..1, spanning `cycles` periods of the note sounding from where
 * one began, out of the voice (before the effects and MASTER), so a held note's steady wave stands still.
 * Returns the period in samples (0: nothing to show yet, not enough of this note yet, or the audio moved
 * on while it read). From the UI; the audio interrupt only writes the ring it reads. */
float wa_scope(float *y, int n, int cycles);

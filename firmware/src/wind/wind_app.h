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

#define WA_BLOCK 64                    /* frames per control block (1.45 ms) */

/* parameters, in the order they are saved: add new ones at the end */
enum {
    P_CUTOFF, P_RESO, P_MORPH, P_SLOPE,              /* Voice page */
    P_BSRC, P_CURVE, P_ATTACK, P_RELEASE,            /* Breath page */
    P_BEND, P_GSRC, P_GLIDE, P_GMODE,                /* Pitch page */
    P_M1SRC, P_M1DST, P_M1AMT,                       /* Mod 1..4 pages: source, destination, amount */
    P_M2SRC, P_M2DST, P_M2AMT,
    P_M3SRC, P_M3DST, P_M3AMT,
    P_M4SRC, P_M4DST, P_M4AMT,
    P_KEYBREATH, P_OCTAVE,                           /* Keys page (the panel's keys) */
    P_TRANSPOSE, P_TUNE, P_MIDICH, P_THEME,          /* Setup page */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} wa_param_t;
const wa_param_t *wa_param_info(int i);
int wa_param_global(int i);            /* 1: the rig's, not the sound's (kept out of presets) */
void wa_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

enum { THEME_LIGHT, THEME_DARK, THEME_N };
extern const char *const WA_NOTE_NAME[12];

void wa_init(void);
void wa_set(int k, int v);             /* a parameter's value to the instrument */
void wa_key(int note, int on);         /* a panel key (queued for the next block) */
void wa_panic(void);                   /* everything quiet (queued for the next block) */
/* n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot); midi_in: the next USB-MIDI packet,
 * 1 = got one (plat_midi_in) */
void wa_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12, int (*midi_in)(uint32_t *pkt));
int wa_take_glide(void);               /* the Glide value a CC last set (0..100), -1: none since */

/* the state the screen shows, written by wa_render */
typedef struct {
    float note, gate, breath, bend;    /* wind_ctl's */
    float cutoff, res, amp, morph;     /* after the matrix */
    float level;                       /* the output's peak in the last block, 0..1 */
    int top;                           /* the note sounding (MIDI number), -1 none yet */
    uint32_t midi_seen;                /* packets received */
} wa_state_t;
extern volatile wa_state_t wa_state;

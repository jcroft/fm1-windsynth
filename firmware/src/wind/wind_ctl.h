/* SPDX-License-Identifier: GPL-3.0-only */
/* Wind control: MIDI from any wind controller (and the panel's keys) to the voice's control state.
 *
 *   notes    a stack of WC_NSTACK, last note wins: letting go of a note falls back to the one still
 *            held, without a new attack. The gate is open while any note is held.
 *   breath   any CC (CC2 by default, 14-bit with its LSB, CC n+32, when the controller sends it) or
 *            channel pressure; through a curve and a short smoothing
 *   bite     pitch bend, +-range semitones
 *   glide    constant time, linear in semitones; on legato notes (a note while another sounds), or
 *            always. Its time is a setting that a CC (CC5 by default) also sets.
 *
 * Threads: everything here runs in the audio interrupt (wind_app drains MIDI and the panel's keys
 * into it at the start of each block); the settings (wc_cfg) are written by the UI between blocks. */
#pragma once
#include <stdint.h>

#define WC_NSTACK 10
#define WC_SRC_AT 128                  /* breath source: channel pressure */
#define WC_SRC_OFF 128                 /* glide source: none */

typedef struct {
    uint8_t channel;                   /* 0: omni, 1..16 */
    uint8_t breath_src;                /* CC 0..127, or WC_SRC_AT */
    int8_t breath_curve;               /* -100..100: + is more sensitive at the start, - less */
    uint8_t bend_range;                /* semitones each way */
    uint8_t glide_src;                 /* CC 0..127, or WC_SRC_OFF */
    uint8_t glide;                     /* glide time 0..100 (0: none, 100: 2 s) */
    uint8_t glide_depth;               /* the glide time the glide CC reaches at 127 (0..100) */
    uint8_t glide_always;              /* 0: legato notes only */
    int8_t transpose;                  /* semitones */
    int16_t tune;                      /* cents */
    float key_breath;                  /* 0..1: the breath while a panel key is held */
} wc_cfg_t;

typedef struct {
    float note;                        /* semitones: note, glide, bend, transpose and tune */
    float gate;                        /* 0 or 1 */
    float breath;                      /* 0..1, curved and smoothed */
    float bend;                        /* -1..1, the bite */
    float mod;                         /* 0..1, the mod wheel (CC1) */
    float pressure;                    /* 0..1, channel pressure */
    float velocity;                    /* 0..1, the last note's */
} wc_out_t;

extern wc_cfg_t wc_cfg;

void wc_init(void);                    /* the defaults in wc_cfg, and nothing held */
void wc_reset(void);                   /* nothing held, nothing bent (wc_cfg kept) */
/* one MIDI channel message: status byte and two data bytes (realtime and system: ignored) */
void wc_midi(uint32_t st, uint32_t d1, uint32_t d2);
void wc_key(int note, int on);         /* a panel key: its MIDI note, down or up */
/* the next block of frames at sr: glide and smoothing move on, the state comes out */
void wc_block(uint32_t frames, float sr, wc_out_t *o);
int wc_take_glide_cc(void);            /* the glide time a CC last set (0..100), -1: none since */
int wc_held(void);                     /* notes held (MIDI and keys) */
int wc_top_note(void);                 /* the note sounding (MIDI note number), -1: none ever */

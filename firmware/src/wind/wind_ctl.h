/* SPDX-License-Identifier: GPL-3.0-only */
/* Wind control: MIDI from any wind controller (and the panel's keys) to the voice's control state.
 *
 *   notes    a stack of WC_NSTACK, last note wins: letting go of a note falls back to the one still
 *            held, without a new attack. The gate is open while any note is held.
 *   breath   any CC (CC2 by default, 14-bit with its LSB, CC n+32, when the controller sends it) or
 *            channel pressure; through a curve and a short smoothing
 *   bite     pitch bend, +-range semitones
 *   glide    constant time, linear in semitones; on legato notes (a note while another sounds), or
 *            always. Its time is the Glide setting, plus what a CC (CC5 by default) adds: up to the CC
 *            depth at 127, nothing at 0. The CC only adds: it never takes the time below Glide.
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
    uint16_t glide;                    /* the glide time with the glide CC at 0, ms (0..2000; 0: none) */
    uint16_t glide_depth;              /* what the glide CC adds to it at 127, ms (0..2000) */
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
    float glide_ms;                    /* the glide time in force: Glide, plus what the glide CC adds */
    float breath_in;                   /* 0..1, the breath as it comes in: before the curve and the smoothing */
    float gliding;                     /* 1 while a glide is on its way, else 0 */
} wc_out_t;

/* the controller monitor (Setup): the raw values as they came in, on any channel (before the MIDI
 * channel setting picks what plays), the channel each came on and the block each last came in (0: never).
 * Blocks count from 1 at wc_init, one per wc_block (WA_BLOCK frames). */
typedef struct {
    uint32_t clock;                    /* the blocks so far */
    uint32_t t_breath, t_bend, t_glide, t_note;   /* when each last arrived (a block of clock) */
    uint16_t breath;                   /* the breath source, 14-bit (a 7-bit CC: v << 7 | v) */
    int16_t bend;                      /* pitch bend, -8192..8191 */
    uint8_t glide;                     /* the glide CC, 0..127 */
    uint8_t note, velocity, on;        /* the last note on or off: its number, velocity, 1 on */
    uint8_t ch[4];                     /* the channel each came on (breath, bend, glide, note): 1..16, 0 a panel key */
    uint8_t breath_src, glide_src;     /* the source the breath and glide values came as (wc_cfg's then) */
    uint8_t other_src, other_n;        /* the last other CC (or channel pressure, WC_SRC_AT) that moved, and how
                                        * many messages in a row it sent (to 255): breath on a source not chosen */
    uint32_t t_other;                  /* when it last came (a block of clock; 0: never) */
} wc_mon_t;

extern wc_cfg_t wc_cfg;

void wc_init(void);                    /* the defaults in wc_cfg, and nothing held */
void wc_reset(void);                   /* nothing held, nothing bent (wc_cfg kept) */
/* one MIDI channel message: status byte and two data bytes (realtime and system: ignored) */
void wc_midi(uint32_t st, uint32_t d1, uint32_t d2);
void wc_key(int note, int on);         /* a panel key: its MIDI note, down or up */
/* the source a controller seems to send its breath on when it isn't the one chosen (src): a CC 0..127,
 * WC_SRC_AT, or -1 (none: the chosen source is coming, or nothing else is streaming) */
int wc_breath_hint(const wc_mon_t *m, uint32_t src);
/* the next block of frames at sr: glide and smoothing move on, the state comes out */
void wc_block(uint32_t frames, float sr, wc_out_t *o);
int wc_held(void);                     /* notes held (MIDI and keys) */
int wc_top_note(void);                 /* the note sounding (MIDI note number), -1: none ever */
const wc_mon_t *wc_monitor(void);      /* the controller monitor (written by wc_midi, wc_key, wc_block) */

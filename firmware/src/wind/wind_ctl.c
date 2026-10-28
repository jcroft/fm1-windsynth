/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
/* Wind control: see wind_ctl.h. Float only, no libm (wind_math.h). */
#include "wind_ctl.h"
#include "wind_math.h"

#define BREATH_MS 3.0f                 /* smoothing: 7-bit breath steps, ironed out */
#define HINT_BLOCKS 690u               /* the breath hint: gone a second (of 1.451 ms blocks) after the last */
#define HINT_GAP 138u                  /* ...a source's run of messages: each within 0.2 s of the one before */
#define HINT_N 8u                      /* ...and a run of 8 is a stream (breath: tens a second; a knob's nudge: not) */

wc_cfg_t wc_cfg;

static struct {
    uint8_t note[WC_NSTACK], key[WC_NSTACK];   /* held, oldest first; key: a panel key's */
    uint8_t n;
    int16_t top;                       /* the note sounding, or the last one (-1: none yet) */
    float cur, target, rate;           /* pitch in semitones; glide per second */
    uint16_t breath_msb, breath_raw;   /* 14-bit */
    float breath_cc, breath;           /* raw 0..1, smoothed after the curve */
    float bend, mod, pressure, velocity;
    uint8_t glide_cc;                  /* the glide CC's value */
    uint8_t glide_src;                 /* ...the CC it came on: another (or Off) and it's 0 */
} w;
static wc_mon_t mon;                   /* the controller monitor */
static uint8_t mon_msb;                /* the breath CC's MSB, as the monitor saw it */
static uint8_t mon_bsrc, mon_gsrc;     /* the breath and glide sources the monitor last saw set */
static uint8_t run[WC_SRC_AT + 1];     /* the other sources (CCs, channel pressure): each one's run of messages */
static uint16_t run_t[WC_SRC_AT + 1];  /* ...and when its last came (the clock's low 16 bits) */

void wc_reset(void)
{
    w.n = 0;
    w.bend = w.mod = w.pressure = 0.0f;
    w.glide_cc = 0;
    w.breath_msb = w.breath_raw = 0;
    w.breath_cc = w.breath = 0.0f;
}

void wc_init(void)
{
    wc_cfg.channel = 0;
    wc_cfg.breath_src = 2;
    wc_cfg.breath_curve = 0;
    wc_cfg.bend_range = 2;
    wc_cfg.glide_src = 5;
    wc_cfg.glide = 4;
    wc_cfg.glide_depth = 400;
    wc_cfg.glide_always = 0;
    wc_cfg.transpose = 0;
    wc_cfg.tune = 0;
    wc_cfg.key_breath = 1.0f;
    w.top = -1;
    w.cur = w.target = 60.0f;
    w.rate = 0.0f;
    w.velocity = 0.0f;
    wc_reset();
    mon.t_breath = mon.t_bend = mon.t_glide = mon.t_note = 0u;
    mon.breath = 0u;
    mon.bend = 0;
    mon.glide = mon.note = mon.velocity = mon.on = 0u;
    mon.ch[0] = mon.ch[1] = mon.ch[2] = mon.ch[3] = 0u;
    mon.breath_src = mon.glide_src = 255u;
    mon.other_src = mon.other_n = 0u;
    mon.t_other = 0u;
    for (int i = 0; i <= WC_SRC_AT; i++)
        run[i] = 0u;
    mon_msb = 0u;
    mon_bsrc = wc_cfg.breath_src;
    mon_gsrc = wc_cfg.glide_src;
    mon.clock = 1u;
}

/* a source chosen again: what the old one sent no longer shows, even if it's chosen back before the new
 * one sends (its values weren't kept meanwhile) */
static void mon_sync(void)
{
    if (mon_bsrc != wc_cfg.breath_src) {
        mon_bsrc = wc_cfg.breath_src;
        mon.breath_src = 255u;
        mon_msb = 0u;
    }
    if (mon_gsrc != wc_cfg.glide_src) {
        mon_gsrc = wc_cfg.glide_src;
        mon.glide_src = 255u;
    }
}

/* the glide CC chosen again (another, or Off): what the old one set no longer adds */
static void glide_sync(void)
{
    if (w.glide_src != wc_cfg.glide_src) {
        w.glide_src = wc_cfg.glide_src;
        w.glide_cc = 0;
    }
}

/* the glide time: Glide, and what the glide CC adds to it (only adds: at 0, Glide itself) */
static float glide_s(void)
{
    glide_sync();
    return ((float)wc_cfg.glide + (float)wc_cfg.glide_depth * (float)w.glide_cc * (1.0f / 127.0f)) * 0.001f;
}

/* the stack's top changed: glide there (legato, or always) or jump */
static void go_to(int note, int legato)
{
    float s = glide_s();
    w.target = (float)note;
    if (w.top >= 0 && s > 0.0f && (legato || wc_cfg.glide_always)) {
        w.rate = fm_fabsf(w.target - w.cur) / s;
    } else {
        w.cur = w.target;
        w.rate = 0.0f;
    }
    w.top = (int16_t)note;
}

static void drop(int note)
{
    int i, j;
    for (i = 0; i < w.n; i++)
        if (w.note[i] == note) {
            for (j = i; j + 1 < w.n; j++) {
                w.note[j] = w.note[j + 1];
                w.key[j] = w.key[j + 1];
            }
            w.n--;
            return;
        }
}

/* a note on or off, for the monitor; ch 0: a panel key */
static void seen_note(int note, int vel, int on, uint32_t ch)
{
    mon.note = (uint8_t)note;
    mon.velocity = (uint8_t)vel;
    mon.on = (uint8_t)on;
    mon.ch[3] = (uint8_t)ch;
    mon.t_note = mon.clock;
}

static void seen_breath(uint32_t raw, uint32_t ch)
{
    mon.breath = (uint16_t)raw;
    mon.breath_src = wc_cfg.breath_src;
    mon.ch[0] = (uint8_t)ch;
    mon.t_breath = mon.clock;
}

/* a source's run, as of now: 0 once its last message is more than HINT_GAP old */
static uint32_t run_now(uint32_t src)
{
    return (uint16_t)((uint16_t)mon.clock - run_t[src]) <= HINT_GAP ? run[src] : 0u;
}

/* a CC (or channel pressure) that isn't a chosen source: each runs on its own, and the longest run is the
 * one named, so streams interleaved hold (breath on a CC and on pressure, or with CC11 beside it) */
static void seen_other(uint32_t src)
{
    uint32_t n = run_now(src);
    run[src] = (uint8_t)(n < 255u ? n + 1u : 255u);
    run_t[src] = (uint16_t)mon.clock;
    if (src == mon.other_src || run[src] >= run_now(mon.other_src))
        mon.other_src = (uint8_t)src;
    mon.other_n = (uint8_t)run_now(mon.other_src);
    mon.t_other = mon.clock;
}

/* a CC a controller might send breath on: not bank select, data entry, the pedals' switches, (N)RPN, the
 * channel mode messages, or any LSB (32..63) */
static int breathy(uint32_t n)
{
    return n != 0u && n != 6u && !(n >= 32u && n <= 69u) && !(n >= 96u && n <= 101u) && n < 120u;
}

int wc_breath_hint(const wc_mon_t *m, uint32_t src)
{
    if (!m->t_other || m->clock - m->t_other > HINT_BLOCKS || m->other_n < HINT_N || m->other_src == src)
        return -1;
    if (m->t_breath && m->breath_src == src && m->clock - m->t_breath <= HINT_BLOCKS)
        return -1;                     /* the chosen source is coming: the other is something else */
    return m->other_src;
}

/* the MIDI ch setting plays what comes on ch (the hint names only what would play once chosen) */
static int heard(uint32_t ch) { return !wc_cfg.channel || ch == wc_cfg.channel; }

/* the monitor: every channel message, before the channel setting */
static void monitor(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t ch = (st & 0x0Fu) + 1u;
    mon_sync();
    switch (st & 0xF0u) {
    case 0x90u: seen_note((int)d1, (int)d2, d2 != 0u, ch); break;
    case 0x80u: seen_note((int)d1, (int)d2, 0, ch); break;
    case 0xB0u:
        if (d1 == wc_cfg.breath_src) {
            mon_msb = (uint8_t)d2;
            seen_breath(d2 << 7 | d2, ch);
        } else if (wc_cfg.breath_src < 32u && d1 == wc_cfg.breath_src + 32u) {
            seen_breath((uint32_t)mon_msb << 7 | d2, ch);
        }
        if (d1 == wc_cfg.glide_src) {
            mon.glide = (uint8_t)d2;
            mon.glide_src = wc_cfg.glide_src;
            mon.ch[2] = (uint8_t)ch;
            mon.t_glide = mon.clock;
        } else if (d1 != wc_cfg.breath_src && breathy(d1) && heard(ch)) {
            seen_other(d1);
        }
        break;
    case 0xD0u:
        if (wc_cfg.breath_src == WC_SRC_AT)
            seen_breath(d1 << 7 | d1, ch);
        else if (heard(ch))
            seen_other(WC_SRC_AT);
        break;
    case 0xE0u:
        mon.bend = (int16_t)((int)(d2 << 7 | d1) - 8192);
        mon.ch[1] = (uint8_t)ch;
        mon.t_bend = mon.clock;
        break;
    default: break;
    }
}

static void note_on(int note, int key)
{
    int legato = w.n > 0;
    drop(note);                        /* (the same note again: to the top) */
    if (w.n == WC_NSTACK)
        drop(w.note[0]);               /* full: the oldest goes */
    w.note[w.n] = (uint8_t)note;
    w.key[w.n] = (uint8_t)key;
    w.n++;
    go_to(note, legato);
}

static void note_off(int note)
{
    int was_top = w.n && w.note[w.n - 1] == note;
    drop(note);
    if (was_top && w.n)                /* back to the note still held: legato, no new attack */
        go_to(w.note[w.n - 1], 1);
}

static void set_breath(void)
{
    w.breath_cc = (float)w.breath_raw * (1.0f / 16383.0f);
}

static void cc(uint32_t n, uint32_t v)
{
    if (n == wc_cfg.breath_src) {      /* the MSB: until its LSB comes, a 7-bit value */
        w.breath_msb = (uint16_t)v;
        w.breath_raw = (uint16_t)(v << 7 | v);           /* 127 -> 16383: full is full */
        set_breath();
    } else if (wc_cfg.breath_src < 32u && n == wc_cfg.breath_src + 32u) {
        w.breath_raw = (uint16_t)(w.breath_msb << 7 | v);
        set_breath();
    }
    glide_sync();
    if (n == wc_cfg.glide_src)
        w.glide_cc = (uint8_t)v;
    if (n == 1u)
        w.mod = (float)v * (1.0f / 127.0f);
    if (n == 120u || n == 123u) {      /* all sound off, all notes off */
        w.n = 0;
    }
}

void wc_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t type = st & 0xF0u;
    if (st >= 0xF0u || st < 0x80u)
        return;
    d1 &= 0x7Fu;
    d2 &= 0x7Fu;
    monitor(st, d1, d2);
    if (wc_cfg.channel && (st & 0x0Fu) + 1u != wc_cfg.channel)
        return;
    switch (type) {
    case 0x90u:
        if (d2) {
            w.velocity = (float)d2 * (1.0f / 127.0f);
            note_on((int)d1, 0);
        } else {
            note_off((int)d1);         /* velocity 0: a note off */
        }
        break;
    case 0x80u: note_off((int)d1); break;
    case 0xB0u: cc(d1, d2); break;
    case 0xD0u:
        w.pressure = (float)d1 * (1.0f / 127.0f);
        if (wc_cfg.breath_src == WC_SRC_AT) {
            w.breath_raw = (uint16_t)(d1 << 7 | d1);
            set_breath();
        }
        break;
    case 0xE0u: {
        int b = (int)(d2 << 7 | d1) - 8192;
        w.bend = (float)b * (b > 0 ? 1.0f / 8191.0f : 1.0f / 8192.0f);
        break;
    }
    default: break;
    }
}

void wc_key(int note, int on)
{
    if (note < 0 || note > 127)
        return;
    if (on) {
        w.velocity = 100.0f / 127.0f;   /* the panel's keys have no velocity: a fixed one, not the last MIDI note's */
        seen_note(note, 100, 1, 0u);
        note_on(note, 1);
    } else {
        seen_note(note, 0, 0, 0u);
        note_off(note);
    }
}

static float curve(float x)
{
    float g;
    if (x <= 0.0f)
        return 0.0f;
    if (!wc_cfg.breath_curve)
        return x;
    g = fm_exp2f((float)-wc_cfg.breath_curve * (1.0f / 50.0f));   /* +100: x^0.25, -100: x^4 */
    return fm_powf(x, g);
}

void wc_block(uint32_t frames, float sr, wc_out_t *o)
{
    float t = (float)frames / sr, b = curve(w.breath_cc), a;
    if (!++mon.clock)                  /* (0 is "never") */
        mon.clock = 1u;
    mon_sync();
    o->breath_in = w.breath_cc;
    if (w.n && w.key[w.n - 1] && wc_cfg.key_breath > b)   /* a panel key's note: as if blown that hard, */
        o->breath_in = b = wc_cfg.key_breath;      /* curve and all (a MIDI note over it: the breath that came) */
    a = 1.0f - fm_expf(-t * (1000.0f / BREATH_MS));
    w.breath += (b - w.breath) * a;
    w.breath = fm_flush(w.breath);
    if (w.rate > 0.0f) {               /* glide: a constant time, whatever the interval */
        float step = w.rate * t;
        if (fm_fabsf(w.target - w.cur) <= step) {
            w.cur = w.target;
            w.rate = 0.0f;
        } else {
            w.cur += w.target > w.cur ? step : -step;
        }
    }
    o->note = w.cur + w.bend * (float)wc_cfg.bend_range + (float)wc_cfg.transpose + (float)wc_cfg.tune * 0.01f;
    o->gate = w.n ? 1.0f : 0.0f;
    o->breath = w.breath;
    o->bend = w.bend;
    o->mod = w.mod;
    o->pressure = w.pressure;
    o->velocity = w.velocity;
    o->glide_ms = glide_s() * 1000.0f;
    o->gliding = w.rate > 0.0f ? 1.0f : 0.0f;
}


int wc_held(void) { return w.n; }
int wc_top_note(void) { return w.top; }
const wc_mon_t *wc_monitor(void) { return &mon; }

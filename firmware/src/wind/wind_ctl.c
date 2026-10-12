/* SPDX-License-Identifier: GPL-3.0-only */
/* Wind control: see wind_ctl.h. Float only, no libm (fastmath.h). */
#include "wind_ctl.h"
#include "../dsp/fastmath.h"

#define BREATH_MS 3.0f                 /* smoothing: 7-bit breath steps, ironed out */

wc_cfg_t wc_cfg;

static struct {
    uint8_t note[WC_NSTACK], key[WC_NSTACK];   /* held, oldest first; key: a panel key's */
    uint8_t n, keys;
    int16_t top;                       /* the note sounding, or the last one (-1: none yet) */
    float cur, target, rate;           /* pitch in semitones; glide per second */
    uint16_t breath_msb, breath_raw;   /* 14-bit */
    float breath_cc, breath;           /* raw 0..1, smoothed after the curve */
    float bend, mod, pressure, velocity;
    uint8_t glide_cc;                  /* the glide CC's value */
} w;

void wc_reset(void)
{
    w.n = w.keys = 0;
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
}

/* the glide time: Glide, and what the glide CC adds to it (only adds: at 0, Glide itself) */
static float glide_s(void)
{
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
            w.keys -= w.key[i];
            for (j = i; j + 1 < w.n; j++) {
                w.note[j] = w.note[j + 1];
                w.key[j] = w.key[j + 1];
            }
            w.n--;
            return;
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
    w.keys += (uint8_t)key;
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
    if (n == wc_cfg.glide_src)
        w.glide_cc = (uint8_t)v;
    if (n == 1u)
        w.mod = (float)v * (1.0f / 127.0f);
    if (n == 120u || n == 123u) {      /* all sound off, all notes off */
        w.n = w.keys = 0;
    }
}

void wc_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t type = st & 0xF0u;
    if (st >= 0xF0u || st < 0x80u)
        return;
    if (wc_cfg.channel && (st & 0x0Fu) + 1u != wc_cfg.channel)
        return;
    d1 &= 0x7Fu;
    d2 &= 0x7Fu;
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
        note_on(note, 1);
    }
    else
        note_off(note);
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
    if (w.keys && wc_cfg.key_breath > b)
        b = wc_cfg.key_breath;
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
}


int wc_held(void) { return w.n; }
int wc_top_note(void) { return w.top; }

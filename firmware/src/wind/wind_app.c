/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
/* The instrument: see wind_app.h. Float only, no libm (wind_math.h). */
#include "wind_app.h"
#include "wind_ctl.h"
#include "wind_voice.h"
#include "wind_fx.h"
#include "wind_math.h"

const char *const WA_NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static const wa_param_t PARAMS[P_NPARAMS] = {
    {"Cutoff", 0, 100, 15}, {"Reso", 0, 100, 0}, {"Morph", 0, 100, 25}, {"Slope", 0, 1, 1},
    {"Breath CC", 0, 128, 2}, {"Curve", -100, 100, 0}, {"Attack", 1, 200, 5}, {"Release", 5, 2000, 60},
    {"Bend", 0, 12, 2}, {"Glide CC", 0, 128, 5}, {"Glide", 0, 2000, 4}, {"Glide on", 0, 1, 0},
    {"Source", 0, WM_SRC_LFO - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_SRC_LFO - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_SRC_LFO - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_SRC_LFO - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
    {"Key brth", 0, 100, 100}, {"Octave", -3, 3, 0},
    {"Transpose", -24, 24, 0}, {"Tune", -100, 100, 0}, {"MIDI ch", 0, 16, 0}, {"Theme", 0, THEME_N - 1, 0},
    {"Engine", 0, WV_NENGINE - 1, WV_CLASSIC}, {"Ratio", 0, 100, 24}, {"Index", 0, 100, 40},
    {"Type", 0, WF_RV_NTYPE - 1, WF_RV_CLOUD}, {"Mix", 0, 100, 3}, {"Decay", 0, 100, 50}, {"Tone", 0, 100, 60},
    {"Pre-delay", 0, WF_PRE_MAX_MS, 20}, {"Size", 0, 100, 50}, {"Mod", 0, 100, 50}, {"Low end", -50, 50, 0},
    {"Mode", 0, WF_DL_NMODE - 1, WF_DL_DIGITAL}, {"Mix", 0, 100, 0}, {"Time", 20, WF_TIME_MAX_MS, 350},
    {"Feedback", 0, 100, 35}, {"CC depth", 0, 2000, 400},
    {"Shimmer", 0, 100, 50},
    {"Patch", 0, WV_DX_NPATCH - 1, 94},    /* 6-op: BRASS 6 BC (bank 3), one that holds while you blow */
    {"Type", 0, WF_DS_NTYPE - 1, WF_DS_OVERDRIVE}, {"Mix", 0, 100, 0}, {"Drive", 0, 100, 40}, {"Tone", 0, 100, 60},
    {"Type", 0, WF_MO_NTYPE - 1, WF_MO_CHORUS}, {"Mix", 0, 100, 0}, {"Rate", 0, 100, 40}, {"Depth", 0, 100, 50},
    {"Level", 0, 100, 0}, {"Type", 0, 100, 50}, {"Freq", -48, 48, 24}, {"Reso", 0, 100, 0},
    {"Aux", 0, 100, 0},
    {"Shape", 0, WA_LFO_NSHAPE - 1, WA_LFO_SINE}, {"Rate", 0, 100, 77},   /* (77: 5 Hz) */
    {"Dest", 0, WM_DST_PITCH, WM_DST_PITCH}, {"Amount", -100, 100, 0},  /* (Pitch: cents) */
    {"Fade", 0, 2000, 0}, {"Sync", 0, 1, 0},
    {"Breath", -100, 100, 75},             /* breath to cutoff */
    {"Row", 0, 2, 0},                      /* Stage: Filter, Engine, FX */
    {"Speaker", 0, WA_SPK_N - 1, WA_SPK_AUTO},
};

const wa_param_t *wa_param_info(int i) { return &PARAMS[(unsigned)i < P_NPARAMS ? i : 0]; }

const char *const WA_ENGINE_NAME[WV_NENGINE] = {"Classic", "FM", "6-op", "Formant", "Waveshape", "VA",
                                                 "PD", "Harmonic"};
static int engine;                     /* the engine playing: its knobs' names */
int wa_harm_smooth(int e) { return e == WV_GRAIN || e == WV_SHAPE || e == WV_VA || e == WV_HARM; }

/* What HARMONICS (Ratio), TIMBRE (Index) and MORPH do on each engine, after Plaits' manual. Classic has
 * no Ratio or Index (the Engine page hides them). */
static const char *const ENGINE_KNOB[WV_NENGINE][3] = {
    {"Ratio", "Index", "Morph"},          /* Classic */
    {"Ratio", "Index", "Feedback"},       /* FM: the modulator's ratio, the FM index, feedback */
    {"Patch", "Bright", "Env"},           /* 6-op: the modulators' level, the envelopes' time */
    {"Ratio", "Formant", "Width"},        /* granular formant: the two formants' ratio, the first, their width */
    {"Shape", "Fold", "Asym"},            /* waveshaping: the shaper's curve, the folder, the asymmetry */
    {"Detune", "Square", "Saw"},          /* virtual analog: the two oscillators' detune and shapes */
    {"Ratio", "Amount", "Asym"},          /* phase distortion: the modulator's ratio, how much, its asymmetry */
    {"Bumps", "Peak", "Shape"},           /* additive: the spectrum's bumps, its loudest harmonic, their shape */
};

const char *wa_param_name(int i)
{
    switch (i) {
    case P_RATIO: return ENGINE_KNOB[engine][0];
    case P_INDEX: return ENGINE_KNOB[engine][1];
    case P_MORPH: return ENGINE_KNOB[engine][2];
    default: return wa_param_info(i)->name;
    }
}

/* the controller's and the room's settings: the breath curve, Pitch 2 (the glide CC's depth, Transpose,
 * Tune), the panel keys and Setup (the breath and glide CCs, MIDI ch, Theme; the Speaker). A preset is the
 * rest, the sound. */
int wa_param_global(int i)
{
    switch (i) {
    case P_BSRC: case P_CURVE: case P_GSRC: case P_GDEPTH: case P_KEYBREATH: case P_OCTAVE:
    case P_TRANSPOSE: case P_TUNE: case P_MIDICH: case P_THEME: case P_STAGEROW: case P_SPEAKER:
        return 1;
    default:
        return 0;
    }
}

static void copy_s(char *d, const char *s)
{
    while ((*d++ = *s++))
        ;
}

static char *put_u(char *b, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

static char *put_i(char *b, int v, int plus)
{
    if (v < 0)
        *b++ = '-';
    else if (plus && v > 0)
        *b++ = '+';
    return put_u(b, (uint32_t)(v < 0 ? -v : v));
}

static void put_src(char *b, int v, const char *none)   /* a CC number, "Press" or "Off" */
{
    if (v >= 128) {
        copy_s(b, none);
        return;
    }
    b[0] = 'C';
    b[1] = 'C';
    put_u(b + 2, (uint32_t)v);
}

static wm_slot_t slot[WM_NSLOT];       /* the Mod pages' four, the LFO page's, the Filter page's Breath */
#define WA_LFO_SLOT 4
#define WA_BREATH_SLOT 5

static float lfo_hz(float x) { return 0.05f * fm_exp2f(x * 8.6439f); }   /* 0.05 x 400^x: 0.05..20 Hz */

static void put_hz(char *b, float hz)                    /* "0.42Hz", "2.5Hz", "10Hz" */
{
    uint32_t h = (uint32_t)(hz * 100.0f + 0.5f);
    if (h < 100u) {
        *b++ = '0';
        *b++ = '.';
        *b++ = (char)('0' + h / 10u);
        *b++ = (char)('0' + h % 10u);
    } else if (h < 1000u) {
        h = (h + 5u) / 10u;
        b = put_u(b, h / 10u);
        *b++ = '.';
        *b++ = (char)('0' + h % 10u);
    } else {
        b = put_u(b, (h + 50u) / 100u);
    }
    copy_s(b, "Hz");
}


static void put_dst(char *b, int v)                      /* a destination; Harmonics where it's stepped: "Harm off" */
{
    if (v == WM_DST_PITCH)
        copy_s(b, "Pitch");
    else if (v == WM_DST_HARM && !wa_harm_smooth(engine))
        copy_s(b, "Harm off");
    else
        copy_s(b, WM_DST_NAME[(unsigned)v < WM_NDST ? v : 0]);
}

static const char *const LFO_SHAPE_NAME[WA_LFO_NSHAPE] = {"Sine", "Triangle", "Saw", "Square", "S&H"};

static void put_ms(char *b, int ms)                      /* "60ms", "1.2s" */
{
    if (ms < 1000) {
        copy_s(put_u(b, (uint32_t)ms), "ms");
    } else {
        b = put_u(b, (uint32_t)(ms / 1000));
        *b++ = '.';
        copy_s(put_u(b, (uint32_t)(ms % 1000 / 100)), "s");
    }
}

void wa_param_text(int i, int v, char *b)
{
    switch (i) {
    case P_SLOPE: copy_s(b, v ? "24dB" : "12dB"); return;
    case P_BSRC: put_src(b, v, "Press"); return;
    case P_GSRC: put_src(b, v, "Off"); return;
    case P_ATTACK: case P_RELEASE: case P_RVPRE: case P_DLTIME: put_ms(b, v); return;
    case P_RVTYPE: copy_s(b, WF_RV_NAME[(unsigned)v < WF_RV_NTYPE ? v : 0]); return;
    case P_DLMODE: copy_s(b, WF_DL_NAME[(unsigned)v < WF_DL_NMODE ? v : 0]); return;
    case P_DSTYPE: copy_s(b, WF_DS_NAME[(unsigned)v < WF_DS_NTYPE ? v : 0]); return;
    case P_NZTYPE:                                       /* LP .. BP .. HP, the mix in between */
        copy_s(b, v == 0 ? "LP" : v == 50 ? "BP" : v == 100 ? "HP" : v < 50 ? "LP-BP" : "BP-HP");
        return;
    case P_NZFREQ: copy_s(put_i(b, v, 1), " st"); return;
    case P_MOTYPE: copy_s(b, WF_MO_NAME[(unsigned)v < WF_MO_NTYPE ? v : 0]); return;
    case P_MORATE: put_hz(b, wf_rate_hz((float)v * 0.01f)); return;
    case P_LFORATE: put_hz(b, lfo_hz((float)v * 0.01f)); return;
    case P_LFOSHAPE: copy_s(b, LFO_SHAPE_NAME[(unsigned)v < WA_LFO_NSHAPE ? v : 0]); return;
    case P_LFODST: put_dst(b, v); return;
    case P_LFOAMT:                                       /* on Pitch, in cents */
        if (slot[WA_LFO_SLOT].dst == WM_DST_PITCH)
            copy_s(put_i(b, v, 1), "c");
        else
            put_i(b, v, 1);
        return;
    case P_LFOFADE:
        if (!v)
            copy_s(b, "Off");
        else
            put_ms(b, v);
        return;
    case P_LFOSYNC: copy_s(b, v ? "Note" : "Free"); return;
    case P_RVDECAY: {                                    /* the reverb time: "0.8s", "3.5s", "17s" */
        uint32_t t = (uint32_t)(wf_decay_s((float)v * 0.01f) * 10.0f + 0.5f);
        if (t < 100u) {
            b = put_u(b, t / 10u);
            *b++ = '.';
            b = put_u(b, t % 10u);
        } else {
            b = put_u(b, (t + 5u) / 10u);
        }
        copy_s(b, "s");
        return;
    }
    case P_GLIDE: case P_GDEPTH:
        if (!v)
            copy_s(b, "Off");
        else
            put_ms(b, v);
        return;
    case P_GMODE: copy_s(b, v ? "Always" : "Legato"); return;
    case P_BEND: copy_s(put_u(b, (uint32_t)v), " st"); return;
    case P_M1SRC: case P_M2SRC: case P_M3SRC: case P_M4SRC:
        copy_s(b, WM_SRC_NAME[(unsigned)v < WM_NSRC ? v : 0]);
        return;
    case P_M1DST: case P_M2DST: case P_M3DST: case P_M4DST:
        put_dst(b, v);
        return;
    case P_M1AMT: case P_M2AMT: case P_M3AMT: case P_M4AMT: case P_BRCUT: case P_CURVE: case P_OCTAVE: case P_TRANSPOSE:
    case P_RVLOW:
        put_i(b, v, 1);
        return;
    case P_TUNE: copy_s(put_i(b, v, 1), "c"); return;
    case P_MIDICH:
        if (!v)
            copy_s(b, "Omni");
        else
            put_u(b, (uint32_t)v);
        return;
    case P_THEME: copy_s(b, v ? "Dark" : "Light"); return;
    case P_SPEAKER: copy_s(b, v == WA_SPK_ON ? "On" : "Auto"); return;
    case P_ENGINE: copy_s(b, WA_ENGINE_NAME[(unsigned)v < WV_NENGINE ? v : 0]); return;
    case P_DXPATCH: wv_dx_name((uint32_t)v, b); return;
    case P_RATIO:                                        /* FM's: the modulator's ratio, "1.00", "3.50" */
        if (engine == WV_FM) {                                      /* the modulator's ratio: "1.00", "3.50" */
            uint32_t r = (uint32_t)(wv_fm_ratio((float)v * 0.01f) * 100.0f + 0.5f);
            b = put_u(b, r / 100u);
            *b++ = '.';
            *b++ = (char)('0' + r / 10u % 10u);
            *b++ = (char)('0' + r % 10u);
            *b = 0;
        } else {
            put_u(b, (uint32_t)v);
        }
        return;
    default: put_i(b, v, 0); return;
    }
}

/* ------------------------------------------------------------- state --- */
static float base[WM_NDST];
static int attack_ms = 5, release_ms = 60;
static float ratio = 0.24f;            /* FM's HARMONICS */
/* the LFO: its rate, shape, fade-in after a note starts from silence, and whether a note restarts it */
static struct {
    float hz, fade_s, ph, fade, sh, last_gate;
    uint32_t shape, sync, rnd;
} lfo = {5.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, WA_LFO_SINE, 0u, 22222u};
static int nz_par[3] = {50, 24, 0};    /* the noise layer's Type, Freq, Reso */
volatile wa_state_t wa_state;

/* the panel's keys and a panic, from the UI to the next block: a ring with one writer, one reader */
#define KQ 32u
static volatile uint32_t kq[KQ];
static volatile uint32_t kq_w, kq_r;
#define KQ_PANIC 0xFFFFFFFFu

static void kq_put(uint32_t v)
{
    if (kq_w - kq_r >= KQ)
        return;                        /* (full: 32 key changes inside 1.45 ms) */
    kq[kq_w % KQ] = v;
    __asm__ volatile("" ::: "memory");
    kq_w++;
}

void wa_key(int note, int on) { kq_put((uint32_t)(note & 0x7F) | (on ? 0x100u : 0u)); }
void wa_panic(void) { kq_put(KQ_PANIC); }

/* the LFO's value for a block of n frames, -1..1, unfaded: advanced by the block, restarted by a note
 * from silence on Sync Note. lfo.fade (0..1), its depth, fades in after such a note. */
static float lfo_block(float gate, uint32_t n)
{
    float t = (float)n * (1.0f / WV_SR), x, ph;
    if (gate > 0.5f && lfo.last_gate <= 0.5f) {
        if (lfo.sync)
            lfo.ph = 0.0f;
        lfo.fade = lfo.fade_s > 0.0f ? 0.0f : 1.0f;
        wa_state.lfo_starts++;
    }
    lfo.last_gate = gate;
    lfo.ph += lfo.hz * t;
    if (lfo.ph >= 1.0f) {                  /* a new cycle: S&H takes a new value */
        lfo.ph -= fm_floorf(lfo.ph);
        lfo.rnd = lfo.rnd * 1664525u + 1013904223u;
        lfo.sh = (float)(lfo.rnd >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    if (lfo.fade < 1.0f)
        lfo.fade = fm_minf(lfo.fade + t / lfo.fade_s, 1.0f);
    ph = lfo.ph;
    switch (lfo.shape) {
    case WA_LFO_TRI: x = 1.0f - 4.0f * fm_fabsf(ph + 0.25f - fm_floorf(ph + 0.25f) - 0.5f); break;   /* from 0, up first */
    case WA_LFO_SAW: x = 2.0f * ph - 1.0f; break;
    case WA_LFO_SQUARE: x = ph < 0.5f ? 1.0f : -1.0f; break;
    case WA_LFO_SH: x = lfo.sh; break;
    default: x = fm_sin_turns(ph); break;
    }
    return x;
}

void wa_init(void)
{
    int i;
    wc_init();
    wv_init();
    wf_init();
    slot[WA_LFO_SLOT].src = WM_SRC_LFO;
    slot[WA_BREATH_SLOT].src = WM_SRC_BREATH;
    slot[WA_BREATH_SLOT].dst = WM_DST_CUTOFF;
    for (i = 0; i < P_NPARAMS; i++)
        wa_set(i, PARAMS[i].def);
    wa_state.top = -1;
}

void wa_set(int k, int v)
{
    if (k >= P_M1SRC && k <= P_M4AMT) {
        wm_slot_t *s = &slot[(k - P_M1SRC) / 3];
        switch ((k - P_M1SRC) % 3) {
        case 0: s->src = (uint8_t)v; break;
        case 1: s->dst = (uint8_t)v; break;
        default: s->amount = (int8_t)v; break;
        }
        return;
    }
    switch (k) {
    case P_CUTOFF: base[WM_DST_CUTOFF] = (float)v * 0.01f; break;
    case P_RESO: base[WM_DST_RES] = (float)v * 0.01f; break;
    case P_MORPH: base[WM_DST_MORPH] = (float)v * 0.01f; break;
    case P_INDEX: base[WM_DST_TIMBRE] = (float)v * 0.01f; break;
    case P_RATIO: ratio = base[WM_DST_HARM] = (float)v * 0.01f; break;
    case P_AUX: base[WM_DST_AUX] = (float)v * 0.01f; break;
    case P_LFOSHAPE: lfo.shape = (uint32_t)v; break;
    case P_LFORATE: lfo.hz = lfo_hz((float)v * 0.01f); break;
    case P_LFODST: slot[WA_LFO_SLOT].dst = (uint8_t)v; break;
    case P_LFOAMT: slot[WA_LFO_SLOT].amount = (int8_t)v; break;
    case P_LFOFADE: lfo.fade_s = (float)v * 0.001f; break;
    case P_LFOSYNC: lfo.sync = (uint32_t)(v != 0); break;
    case P_BRCUT: slot[WA_BREATH_SLOT].amount = (int8_t)v; break;
    case P_ENGINE:
        engine = (unsigned)v < WV_NENGINE ? v : WV_CLASSIC;
        wv_engine((uint32_t)engine);
        break;
    case P_DXPATCH: wv_dx_patch((uint32_t)v); break;
    case P_RVTYPE: wf_cfg.rv_type = (uint8_t)v; break;
    case P_RVMIX: wf_cfg.rv_mix = (float)v * 0.01f; break;
    case P_RVDECAY: wf_cfg.rv_decay = (float)v * 0.01f; break;
    case P_RVTONE: wf_cfg.rv_tone = (float)v * 0.01f; break;
    case P_RVPRE: wf_cfg.rv_pre_ms = (float)v; break;
    case P_RVSIZE: wf_cfg.rv_size = (float)v * 0.01f; break;
    case P_RVMOD: wf_cfg.rv_mod = (float)v * 0.01f; break;
    case P_RVLOW: wf_cfg.rv_low = (float)v * 0.02f; break;
    case P_RVSHIM: wf_cfg.rv_shim = (float)v * 0.01f; break;
    case P_DLMODE: wf_cfg.dl_mode = (uint8_t)v; break;
    case P_DLMIX: wf_cfg.dl_mix = (float)v * 0.01f; break;
    case P_DLTIME: wf_cfg.dl_ms = (float)v; break;
    case P_DLFB: wf_cfg.dl_fb = (float)v * 0.01f; break;
    case P_DSTYPE: wf_cfg.ds_type = (uint8_t)v; break;
    case P_DSMIX: wf_cfg.ds_mix = (float)v * 0.01f; break;
    case P_DSDRIVE: wf_cfg.ds_drive = (float)v * 0.01f; break;
    case P_DSTONE: wf_cfg.ds_tone = (float)v * 0.01f; break;
    case P_MOTYPE: wf_cfg.mo_type = (uint8_t)v; break;
    case P_MOMIX: wf_cfg.mo_mix = (float)v * 0.01f; break;
    case P_MORATE: wf_cfg.mo_hz = wf_rate_hz((float)v * 0.01f); break;
    case P_MODEPTH: wf_cfg.mo_depth = (float)v * 0.01f; break;
    case P_NZLEVEL: base[WM_DST_NOISE] = (float)v * 0.01f; break;
    case P_NZTYPE: case P_NZFREQ: case P_NZRESO:
        nz_par[k - P_NZTYPE] = v;
        wv_noise((float)nz_par[0] * 0.01f, (float)nz_par[1], (float)nz_par[2] * 0.01f);
        break;
    case P_SLOPE: wv_slope(v ? WV_SLOPE_24 : WV_SLOPE_12); break;
    case P_BSRC: wc_cfg.breath_src = (uint8_t)v; break;
    case P_CURVE: wc_cfg.breath_curve = (int8_t)v; break;
    case P_ATTACK: case P_RELEASE:
        *(k == P_ATTACK ? &attack_ms : &release_ms) = v;
        wv_env((float)attack_ms * 0.001f, (float)release_ms * 0.001f);
        break;
    case P_BEND: wc_cfg.bend_range = (uint8_t)v; break;
    case P_GSRC: wc_cfg.glide_src = (uint8_t)v; break;
    case P_GLIDE: wc_cfg.glide = (uint16_t)v; break;
    case P_GDEPTH: wc_cfg.glide_depth = (uint16_t)v; break;
    case P_GMODE: wc_cfg.glide_always = (uint8_t)(v != 0); break;
    case P_TRANSPOSE: wc_cfg.transpose = (int8_t)v; break;
    case P_TUNE: wc_cfg.tune = (int16_t)v; break;
    case P_MIDICH: wc_cfg.channel = (uint8_t)v; break;
    default: break;                    /* the UI's own (Octave, Theme) */
    }
}

/* is a route reaching destination d: a slot with a source and an amount */
static int routed(int d)
{
    int i;
    for (i = 0; i < WM_NSLOT; i++)
        if (slot[i].dst == d && slot[i].src != WM_SRC_OFF && slot[i].amount != 0)
            return 1;
    return 0;
}

int wa_live(int k, float *v)
{
    int d;
    switch (k) {
    case P_CUTOFF: d = WM_DST_CUTOFF; *v = wa_state.cutoff; break;
    case P_RESO: d = WM_DST_RES; *v = wa_state.res; break;
    case P_MORPH: d = WM_DST_MORPH; *v = wa_state.morph; break;
    case P_INDEX: d = WM_DST_TIMBRE; *v = wa_state.timbre; break;
    case P_NZLEVEL: d = WM_DST_NOISE; *v = wa_state.noise; break;
    case P_AUX: d = WM_DST_AUX; *v = wa_state.aux; break;
    case P_RATIO:                      /* (only the smooth engines' HARMONICS can be moved) */
        if (!wa_harm_smooth(engine))
            return 0;
        d = WM_DST_HARM;
        *v = wa_state.harmonics;
        break;
    case P_GLIDE: case P_GDEPTH:       /* the glide CC adds to Glide, up to CC depth */
        if (wc_cfg.glide_src == WC_SRC_OFF || !wc_cfg.glide_depth)
            return 0;
        *v = wa_state.glide_ms - (k == P_GDEPTH ? (float)wc_cfg.glide : 0.0f);
        return 1;
    default: return 0;
    }
    if (k == P_INDEX && engine == WV_CLASSIC)   /* (Classic's TIMBRE is our cutoff) */
        return 0;
    *v *= 100.0f;
    return routed(d);
}

/* the scope: the voice (before the effects: their tails would smear a note's shape) in a ring of
 * points, each the mean of d samples, d so a period of the note takes at most SC_PER points (a little
 * more before d changes: a note wobbling at a boundary keeps its d): two periods of any note, and the one
 * it is in, fit the ring. A phase turning once a period of the note, at the frequency the voice plays it
 * (wv_note_freq: Plaits' own tables), marks where the last one began (sc_sync, and sc_sync_f points on
 * from it), so the trace starts at the same place in the wave every time. Read only from points of the
 * note playing (sc_gen: a d or a note, 3 % away, since). Tapped before the VCA, then scaled by the amp
 * the sound asks for (breath, the Mod slots) but not by MASTER: the trace's size is the sound's, not the
 * room's. A few operations a sample, no allocation. */
#define SC_N 512u
#define SC_PER 160.0f
#define SC_HI 168.0f                                /* points a period: past this, a bigger d... */
#define SC_LO 152.0f                                /* ...and under this with one less, a smaller */
static volatile int16_t sc_buf[SC_N];
static volatile uint32_t sc_w, sc_sync, sc_d;      /* points written; the point a period began in; d */
static volatile uint32_t sc_gen;                   /* the first point of this d and this note */
static volatile float sc_sync_f, sc_per;           /* where in that point (-1..1); the period, in points */
static float sc_acc, sc_gen_per;                   /* (the audio interrupt's own; the period at sc_gen) */
static uint32_t sc_n, sc_ph;                       /* (the phase: a turn is 2^32) */

static void scope_block(const float *x, uint32_t k, float note, float amp)
{
    float f = fm_clampf(wv_note_freq(note), 1.0f / 8000.0f, 0.5f), per = 1.0f / f, rd,
          sc = fm_clampf(amp, 0.0f, 1.0f) * 32767.0f;
    uint32_t d = sc_d, i, w = sc_w, inc = (uint32_t)(f * 4294967296.0f);
    int fresh;
    if (!d || per > SC_HI * (float)d || per <= SC_LO * (float)(d - 1u))
        d = (uint32_t)(per * (1.0f / SC_PER)) + 1u;    /* (at most 51: a block of 64 writes a point) */
    fresh = d != sc_d;
    if (fresh) {                                   /* (the points before were means of another d) */
        sc_d = d;
        sc_acc = 0.0f;
        sc_n = 0u;
    }
    if (fresh || sc_gen_per <= 0.0f || fm_fabsf(per - sc_gen_per) > 0.03f * sc_gen_per) {
        sc_gen = w;                                /* ...or of another note: read none of them */
        sc_gen_per = per;
    }
    rd = 1.0f / (float)d;
    sc_per = per * rd;
    for (i = 0; i < k; i++) {
        uint32_t ph = sc_ph + inc;
        if (ph < sc_ph) {                          /* a period began ph/inc samples before this one */
            sc_sync = w;
            sc_sync_f = ((float)sc_n - (float)ph / (float)inc) * rd;
        }
        sc_ph = ph;
        sc_acc += x[i];
        if (++sc_n >= d) {
            float v = sc_acc * rd * sc;
            sc_buf[w % SC_N] = (int16_t)fm_clampf(v, -32767.0f, 32767.0f);
            w++;
            sc_acc = 0.0f;
            sc_n = 0u;
        }
    }
    sc_w = w;
}

float wa_scope(float *y, int n, int cycles)
{
    int tries, i;
    for (tries = 0; tries < 4; tries++) {
        uint32_t w0 = sc_w, sync = sc_sync, d = sc_d, gen = sc_gen;
        float per = sc_per, span, s, m, c;
        if (!d || per <= 0.0f || n < 2 || (int32_t)(sync - gen) < 0)
            return 0.0f;                          /* (no period begun since d changed) */
        span = per * (float)cycles;
        /* the latest period start with `span` written points after it (and one more, to blend with: the
         * last written is w0 - 1) */
        s = (float)(int32_t)(sync - w0) + sc_sync_f;
        m = s + span + 2.0f;
        if (m > 0.0f) {
            c = fm_floorf(m / per);
            s -= per * (c * per < m ? c + 1.0f : c);
        }
        if (s < -(float)(SC_N - 2u) || s < (float)(int32_t)(gen - w0))
            return 0.0f;                          /* (not yet two periods of points with this d) */
        for (i = 0; i < n; i++) {
            float t = s + span * (float)i / (float)(n - 1), fl = fm_floorf(t), fr = t - fl;
            uint32_t j = w0 + (uint32_t)(int32_t)fl;
            float a = (float)sc_buf[j % SC_N], b = (float)sc_buf[(j + 1u) % SC_N];
            y[i] = (a + (b - a) * fr) * (1.0f / 32767.0f);
        }
        if (sc_w == w0 && sc_sync == sync)        /* (the audio interrupt didn't run while it read) */
            return per * (float)d;
    }
    return 0.0f;
}

static void drain(int (*midi_in)(uint32_t *pkt))
{
    uint32_t pkt;
    while (kq_r != kq_w) {
        uint32_t v = kq[kq_r % KQ];
        kq_r++;
        if (v == KQ_PANIC)
            wc_reset();
        else
            wc_key((int)(v & 0x7Fu), (v & 0x100u) != 0u);
    }
    while (midi_in && midi_in(&pkt)) {
        wc_midi((pkt >> 8) & 0xFFu, (pkt >> 16) & 0xFFu, (pkt >> 24) & 0xFFu);
        wa_state.midi_seen++;
    }
}

/* the breath gate: a phrase with no breath behind it is silent, as on a wind patch whose level follows the
 * breath (a controller's stray notes at its threshold, with the breath near 0). It reads the breath as it
 * comes (before the curve: the same CCs at any curve), smoothed as the breath is, opens over the first
 * BREATH_GATE of it, squared, and holds what it reached until the next phrase (a note from silence): a
 * phrase's release and a breath tapering off keep their level. It waits for the breath source to have been
 * heard, so a controller with no breath (or one sending it on another CC: Setup names it) still plays. */
#define BREATH_GATE 0.08f
#define GATE_SMOOTH 0.38f              /* the breath's 3 ms one-pole, per 1.451 ms block */
static float gate_in, gate_held, gate_last;
static float breath_gate(const wc_out_t *c)
{
    const wc_mon_t *mon = wc_monitor();
    float x;
    if (c->gate > 0.5f && gate_last <= 0.5f)
        gate_held = 0.0f;              /* a phrase starts: it opens as far as its breath does */
    gate_last = c->gate;
    gate_in += (c->breath_in - gate_in) * GATE_SMOOTH;
    gate_in = fm_flush(gate_in);
    x = gate_in >= BREATH_GATE ? 1.0f : gate_in <= 0.0f ? 0.0f : gate_in * (1.0f / BREATH_GATE);
    x *= x;
    if (c->gate > 0.5f && x > gate_held)
        gate_held = x;
    if (!mon->t_breath || mon->breath_src != wc_cfg.breath_src)
        return 1.0f;                   /* no breath heard on the source chosen (yet) */
    return gate_held;
}

void wa_render(int32_t *out, uint32_t n, uint32_t gain_q12, int (*midi_in)(uint32_t *pkt))
{
    float g = (float)gain_q12 * (1.0f / 4096.0f), pk = 0.0f;
    while (n) {
        uint32_t k = n > WA_BLOCK ? WA_BLOCK : n, i;
        wc_out_t c;
        float src[WM_NSRC], m[WM_NDST], lv;
        wind_frame_t f;
        float tap[WA_BLOCK];                       /* (the voice before its VCA: the scope's) */
        drain(midi_in);
        wc_block(k, WV_SR, &c);
        src[WM_SRC_OFF] = 0.0f;
        src[WM_SRC_BREATH] = c.breath;
        src[WM_SRC_BITE] = c.bend;
        src[WM_SRC_MOD] = c.mod;
        src[WM_SRC_PRESSURE] = c.pressure;
        src[WM_SRC_VELOCITY] = c.velocity;
        lv = lfo_block(c.gate, k);
        if (slot[WA_LFO_SLOT].dst == WM_DST_AMP) {    /* Amp: from 1 down, faded in as depth (no dip at 0) */
            float s = 0.5f + 0.5f * lv;
            if (lfo.fade < 1.0f)
                s = slot[WA_LFO_SLOT].amount > 0 ? 1.0f - lfo.fade * (1.0f - s) : lfo.fade * s;
            src[WM_SRC_LFO] = s;
        } else {
            src[WM_SRC_LFO] = lv * lfo.fade;
        }
        lv *= lfo.fade;
        wm_apply(slot, base, src, m);
        f.note = c.note;
        if (slot[WA_LFO_SLOT].dst == WM_DST_PITCH)                /* the LFO's vibrato: Amount in cents */
            f.note += lv * (float)slot[WA_LFO_SLOT].amount * 0.01f;
        f.gate = c.gate;
        f.cutoff = m[WM_DST_CUTOFF];
        f.res = m[WM_DST_RES];
        m[WM_DST_AMP] *= breath_gate(&c);
        f.amp = m[WM_DST_AMP] * g;
        f.morph = m[WM_DST_MORPH];
        f.timbre = m[WM_DST_TIMBRE];
        f.harmonics = wa_harm_smooth(engine) ? m[WM_DST_HARM] : ratio;
        f.noise = m[WM_DST_NOISE];
        f.aux = m[WM_DST_AUX];
        wv_tap(tap);
        wv_render(&f, out, k);
        scope_block(tap, k, f.note, m[WM_DST_AMP]);
        wf_process(out, k);
        for (i = 0; i < k; i++) {
            float a = fm_fabsf((float)out[2 * i] * (1.0f / 8388608.0f)),
                  b = fm_fabsf((float)out[2 * i + 1] * (1.0f / 8388608.0f));
            pk = a > pk ? a : pk;
            pk = b > pk ? b : pk;
        }
        wa_state.note = c.note;
        wa_state.gate = c.gate;
        wa_state.breath = c.breath;
        wa_state.bend = c.bend;
        wa_state.cutoff = f.cutoff;
        wa_state.res = f.res;
        wa_state.amp = m[WM_DST_AMP];
        wa_state.morph = f.morph;
        wa_state.timbre = f.timbre;
        wa_state.noise = f.noise;
        wa_state.harmonics = f.harmonics;
        wa_state.aux = f.aux;
        wa_state.lfo = lv;
        wa_state.pitch = f.note;
        wa_state.top = wc_top_note();
        wa_state.breath_in = c.breath_in;
        wa_state.env = wv_env_level();
        wa_state.lfo_fade = lfo.fade;
        wa_state.lfo_ph = lfo.ph;
        wa_state.glide_ms = c.glide_ms;
        for (i = 0; i < WM_NSRC; i++)
            wa_state.src[i] = src[i];
        wa_state.mon = *wc_monitor();
        wa_state.gliding = c.gliding;
        out += 2 * k;
        n -= k;
    }
    wa_state.level = pk;
}

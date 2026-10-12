/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: see wind_app.h. Float only, no libm (fastmath.h). */
#include "wind_app.h"
#include "wind_ctl.h"
#include "wind_voice.h"
#include "wind_fx.h"
#include "../dsp/fastmath.h"

const char *const WA_NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static const wa_param_t PARAMS[P_NPARAMS] = {
    {"Cutoff", 0, 100, 15}, {"Reso", 0, 100, 0}, {"Morph", 0, 100, 25}, {"Slope", 0, 1, 1},
    {"Breath", 0, 128, 2}, {"Curve", -100, 100, 0}, {"Attack", 1, 200, 5}, {"Release", 5, 2000, 60},
    {"Bend", 0, 12, 2}, {"Glide CC", 0, 128, 5}, {"Glide", 0, 2000, 4}, {"Glide on", 0, 1, 0},
    {"Source", 0, WM_NSRC - 1, WM_SRC_BREATH}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 75},
    {"Source", 0, WM_NSRC - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_NSRC - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_NSRC - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 0},
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
};

const wa_param_t *wa_param_info(int i) { return &PARAMS[(unsigned)i < P_NPARAMS ? i : 0]; }

const char *const WA_ENGINE_NAME[WV_NENGINE] = {"Classic", "FM", "6-op", "Formant", "Waveshape", "VA",
                                                 "PD", "Harmonic"};
static int engine;                     /* the engine playing: its knobs' names */

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

/* the controller's and the room's settings: breath source and curve, the glide CC, the panel keys and
 * Setup. A preset is the rest, the sound. */
int wa_param_global(int i)
{
    switch (i) {
    case P_BSRC: case P_CURVE: case P_GSRC: case P_GDEPTH: case P_KEYBREATH: case P_OCTAVE:
    case P_TRANSPOSE: case P_TUNE: case P_MIDICH: case P_THEME:
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
    case P_MORATE: {                                     /* the LFO: "0.42Hz", "2.5Hz", "10Hz" */
        uint32_t h = (uint32_t)(wf_rate_hz((float)v * 0.01f) * 100.0f + 0.5f);
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
        return;
    }
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
        copy_s(b, WM_DST_NAME[(unsigned)v < WM_NDST ? v : 0]);
        return;
    case P_M1AMT: case P_M2AMT: case P_M3AMT: case P_M4AMT: case P_CURVE: case P_OCTAVE: case P_TRANSPOSE:
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
static wm_slot_t slot[WM_NSLOT];
static int attack_ms = 5, release_ms = 60;
static float ratio = 0.24f;            /* FM's HARMONICS */
static float aux_mix;                  /* Aux: 0 the engine's main output, 1 its AUX output */
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

void wa_init(void)
{
    int i;
    wc_init();
    wv_init();
    wf_init();
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
    case P_RATIO: ratio = (float)v * 0.01f; break;
    case P_AUX: aux_mix = (float)v * 0.01f; break;
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
    case P_KEYBREATH: wc_cfg.key_breath = (float)v * 0.01f; break;
    case P_TRANSPOSE: wc_cfg.transpose = (int8_t)v; break;
    case P_TUNE: wc_cfg.tune = (int16_t)v; break;
    case P_MIDICH: wc_cfg.channel = (uint8_t)v; break;
    default: break;                    /* the UI's own (Octave, Theme) */
    }
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

void wa_render(int32_t *out, uint32_t n, uint32_t gain_q12, int (*midi_in)(uint32_t *pkt))
{
    float g = (float)gain_q12 * (1.0f / 4096.0f), pk = 0.0f;
    while (n) {
        uint32_t k = n > WA_BLOCK ? WA_BLOCK : n, i;
        wc_out_t c;
        float src[WM_NSRC], m[WM_NDST];
        wind_frame_t f;
        drain(midi_in);
        wc_block(k, WV_SR, &c);
        src[WM_SRC_OFF] = 0.0f;
        src[WM_SRC_BREATH] = c.breath;
        src[WM_SRC_BITE] = c.bend;
        src[WM_SRC_MOD] = c.mod;
        src[WM_SRC_PRESSURE] = c.pressure;
        src[WM_SRC_VELOCITY] = c.velocity;
        wm_apply(slot, base, src, m);
        f.note = c.note;
        f.gate = c.gate;
        f.cutoff = m[WM_DST_CUTOFF];
        f.res = m[WM_DST_RES];
        f.amp = m[WM_DST_AMP] * g;
        f.morph = m[WM_DST_MORPH];
        f.timbre = m[WM_DST_TIMBRE];
        f.harmonics = ratio;
        f.noise = m[WM_DST_NOISE];
        f.aux = aux_mix;
        wv_render(&f, out, k);
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
        wa_state.top = wc_top_note();
        out += 2 * k;
        n -= k;
    }
    wa_state.level = pk;
}

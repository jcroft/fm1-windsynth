/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: see wind_app.h. Float only, no libm (fastmath.h). */
#include "wind_app.h"
#include "wind_ctl.h"
#include "wind_voice.h"
#include "../dsp/fastmath.h"

const char *const WA_NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static const wa_param_t PARAMS[P_NPARAMS] = {
    {"Cutoff", 0, 100, 15}, {"Reso", 0, 100, 20}, {"Morph", 0, 100, 30}, {"Slope", 0, 1, 1},
    {"Breath", 0, 128, 2}, {"Curve", -100, 100, 0}, {"Attack", 1, 200, 5}, {"Release", 5, 2000, 60},
    {"Bend", 0, 12, 2}, {"Glide CC", 0, 128, 5}, {"Glide", 0, 100, 0}, {"Glide on", 0, 1, 0},
    {"Source", 0, WM_NSRC - 1, WM_SRC_BREATH}, {"Dest", 0, WM_NDST - 1, WM_DST_CUTOFF}, {"Amount", -100, 100, 75},
    {"Source", 0, WM_NSRC - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_AMP}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_NSRC - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_RES}, {"Amount", -100, 100, 0},
    {"Source", 0, WM_NSRC - 1, WM_SRC_OFF}, {"Dest", 0, WM_NDST - 1, WM_DST_MORPH}, {"Amount", -100, 100, 0},
    {"Key brth", 0, 100, 100}, {"Octave", -3, 3, 0},
    {"Transpose", -24, 24, 0}, {"Tune", -100, 100, 0}, {"MIDI ch", 0, 16, 0}, {"Theme", 0, THEME_N - 1, 0},
};

const wa_param_t *wa_param_info(int i) { return &PARAMS[(unsigned)i < P_NPARAMS ? i : 0]; }

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

static int glide_ms(int v) { return (v * v * 2000 + 5000) / 10000; }   /* wind_ctl's curve: 100 -> 2 s */

void wa_param_text(int i, int v, char *b)
{
    switch (i) {
    case P_SLOPE: copy_s(b, v ? "24dB" : "12dB"); return;
    case P_BSRC: put_src(b, v, "Press"); return;
    case P_GSRC: put_src(b, v, "Off"); return;
    case P_ATTACK: case P_RELEASE: put_ms(b, v); return;
    case P_GLIDE:
        if (!v)
            copy_s(b, "Off");
        else
            put_ms(b, glide_ms(v));
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
    default: put_i(b, v, 0); return;
    }
}

/* ------------------------------------------------------------- state --- */
static float base[WM_NDST];
static wm_slot_t slot[WM_NSLOT];
static int attack_ms = 5, release_ms = 60;
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
    case P_SLOPE: wv_slope(v ? WV_SLOPE_24 : WV_SLOPE_12); break;
    case P_BSRC: wc_cfg.breath_src = (uint8_t)v; break;
    case P_CURVE: wc_cfg.breath_curve = (int8_t)v; break;
    case P_ATTACK: case P_RELEASE:
        *(k == P_ATTACK ? &attack_ms : &release_ms) = v;
        wv_env((float)attack_ms * 0.001f, (float)release_ms * 0.001f);
        break;
    case P_BEND: wc_cfg.bend_range = (uint8_t)v; break;
    case P_GSRC: wc_cfg.glide_src = (uint8_t)v; break;
    case P_GLIDE: wc_cfg.glide = (uint8_t)v; break;
    case P_GMODE: wc_cfg.glide_always = (uint8_t)(v != 0); break;
    case P_KEYBREATH: wc_cfg.key_breath = (float)v * 0.01f; break;
    case P_TRANSPOSE: wc_cfg.transpose = (int8_t)v; break;
    case P_TUNE: wc_cfg.tune = (int16_t)v; break;
    case P_MIDICH: wc_cfg.channel = (uint8_t)v; break;
    default: break;                    /* the UI's own (Octave, Theme) */
    }
}

int wa_take_glide(void) { return wc_take_glide_cc(); }

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
        f.timbre = f.harmonics = 0.5f;
        wv_render(&f, out, k);
        for (i = 0; i < k; i++) {
            float a = fm_fabsf((float)out[2 * i] * (1.0f / 8388608.0f));
            pk = a > pk ? a : pk;
        }
        wa_state.note = c.note;
        wa_state.gate = c.gate;
        wa_state.breath = c.breath;
        wa_state.bend = c.bend;
        wa_state.cutoff = f.cutoff;
        wa_state.res = f.res;
        wa_state.amp = m[WM_DST_AMP];
        wa_state.morph = f.morph;
        wa_state.top = wc_top_note();
        out += 2 * k;
        n -= k;
    }
    wa_state.level = pk;
}

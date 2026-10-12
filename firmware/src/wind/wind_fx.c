/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects: see wind_fx.h. Float only, no libm (fastmath.h).
 *
 * Half rate: a 6-point half-band filter (Lagrange's midpoint weights, 3 -25 150 150 -25 3) takes the
 * voice down to 22,059 Hz, and the same weights bring the wet signal back up. The wet path is 11 kHz
 * wide, as the classic digital reverbs were; the dry voice never goes through it.
 *
 * The reverb's lines share one buffer, as Mutable's FxEngine does: a write pointer that steps back one
 * place a sample, each line a fixed region of it, so a delay of d reads d places ahead of where the
 * line writes. Lengths (Size) and the pre-delay glide, read with linear interpolation. */
#include "wind_fx.h"
#include "wind_voice.h"
#include "../dsp/fastmath.h"

#ifdef FELUCCA_ID                       /* the device: the lines go in the .pool RAM region (app.ld) */
#define WF_POOL __attribute__((section(".pool")))
#else
#define WF_POOL
#endif

#define SR2 (WV_SR * 0.5f)
#define RV_N 65536u                     /* 2.97 s of reverb lines */
#define RV_MASK (RV_N - 1u)
#define DL_N 49152u                     /* 2.23 s of delay */
#define DL_Q 16384.0f                   /* +-2 */
#define SH_N 4096u                      /* the shimmer's pitch shifter */
#define SH_W 2048.0f                    /* ...its grain, 93 ms */
#define SMAX 1.5f                       /* the longest Size: a Type's own size x 1.5 */
#define MARGIN 64u                      /* room for a line's modulation (and its interpolation) */

const char *const WF_RV_NAME[WF_RV_NTYPE] = {"Cloud", "Hall", "Plate", "Room", "Shimmer"};
const char *const WF_DL_NAME[WF_DL_NMODE] = {"Digital", "Tape", "Analog", "Pingpong"};
const char *const WF_DS_NAME[WF_DS_NTYPE] = {"Overdrive", "Distort", "Fuzz", "Fold", "Crush"};
const char *const WF_MO_NAME[WF_MO_NTYPE] = {"Chorus", "Ensemble", "Flanger", "Phaser"};

volatile wf_cfg_t wf_cfg;

static uint16_t rv_buf[RV_N] WF_POOL;   /* half floats: a tail fades to nothing, not to a hiss */
static int16_t dl_buf[DL_N] WF_POOL;
static float sh_buf[SH_N] WF_POOL;
#define MO_N 1024u                      /* the modulation's two lines: 23 ms each */
static float mo_buf[2][MO_N] WF_POOL;

/* ------------------------------------------------------------ reverb --- */
/* the lines: pre-delay, 8 input allpasses, then per ring block an allpass (modulated), an allpass and a
 * delay; lengths at Size 1 in half-rate samples (the input ones are Dattorro's first four, then longer) */
enum { L_PRE, L_IN, L_AP1 = L_IN + 8, L_AP2 = L_AP1 + 4, L_DL = L_AP2 + 4, L_N = L_DL + 4 };
static const float LEN[L_N] = {
    0.0f,
    142.0f, 107.0f, 379.0f, 277.0f, 613.0f, 461.0f, 827.0f, 1013.0f,
    467.0f, 571.0f, 523.0f, 619.0f,
    1163.0f, 1381.0f, 1249.0f, 1451.0f,
    2903.0f, 3217.0f, 2647.0f, 3461.0f,
};
#define PRE_MAX ((uint32_t)(WF_PRE_MAX_MS * SR2 / 1000.0f) + 4u)

/* a Type: its size, input allpasses and their coefficient, the ring's two allpass coefficients, how
 * much the Mod knob moves the input and the ring, whether the tail climbs in octaves (Shimmer), and the
 * output taps' gain */
typedef struct {
    float size;
    uint32_t nin;
    float kin, k1, k2, mod_in, mod_ring, shimmer, out;
} rv_type_t;
static const rv_type_t TYPES[WF_RV_NTYPE] = {
    {1.00f, 8, 0.70f, 0.70f, 0.55f, 1.0f, 0.4f, 0.0f, 0.30f},     /* Cloud */
    {0.80f, 4, 0.70f, 0.65f, 0.50f, 0.3f, 0.5f, 0.0f, 0.33f},     /* Hall */
    {0.50f, 4, 0.75f, 0.70f, 0.50f, 0.0f, 0.5f, 0.0f, 0.30f},     /* Plate */
    {0.28f, 2, 0.60f, 0.50f, 0.40f, 0.0f, 0.3f, 0.0f, 0.14f},     /* Room */
    {1.00f, 8, 0.70f, 0.70f, 0.55f, 0.8f, 0.4f, 1.0f, 0.30f},     /* Shimmer */
};

static uint32_t rv_base[L_N], rv_span, rv_wp;
static float rv_size = 1.0f, rv_pre;   /* gliding: the Size scale, the pre-delay in samples */
static float rv_lp[4], rv_low[4], rv_in_lp;
static float lfo_c[2], lfo_s[2];       /* two quadrature LFOs: the input's, the ring's */
static uint32_t sh_w;
static float sh_ph, sh_hp, sh_fb;      /* the shifter's grain; its input's high-pass; what it feeds back */

static int16_t q16(float v)
{
    v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
    return (int16_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

static float sat(float x)               /* a soft clip: x below about 0.5, 1 at 3 */
{
    x = x > 3.0f ? 3.0f : x < -3.0f ? -3.0f : x;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* The reverb's lines hold IEEE half floats (11 bits, as the signal gets quieter so does their error): in
 * 16-bit fixed point a tail fades into a hiss, or with truncation stops dead, 70 to 80 dB down. After
 * Fabian Giesen's float_to_half_fast3 and half_to_float, with no infinities (the lines stay inside +-8). */
typedef union {
    float f;
    uint32_t u;
} fbits_t;

static uint16_t h_enc(float v)
{
    fbits_t x, m;
    uint32_t sign, o;
    v = v > 8.0f ? 8.0f : v < -8.0f ? -8.0f : v;
    x.f = v;
    sign = x.u & 0x80000000u;
    x.u ^= sign;
    if (x.u < (113u << 23)) {          /* below half's normal range: a subnormal, rounded */
        m.u = 126u << 23;
        x.f += m.f;
        o = x.u - m.u;
    } else {                           /* rounded to the nearest, ties to even */
        o = (x.u >> 13) & 1u;
        x.u += 0xC8000FFFu + o;        /* (the exponent's rebias, 15 - 127, and the half step) */
        o = x.u >> 13;
    }
    return (uint16_t)(o | sign >> 16);
}

static float h_dec(uint16_t h)
{
    fbits_t o, m;
    o.u = (uint32_t)(h & 0x7FFFu) << 13;
    if (!(o.u & 0x0F800000u)) {        /* a subnormal (or 0) */
        m.u = 113u << 23;
        o.u += 113u << 23;
        o.f -= m.f;
    } else {
        o.u += 112u << 23;
    }
    o.u |= (uint32_t)(h & 0x8000u) << 16;
    return o.f;
}

static float rv_rd(int l, float d)     /* line l, d samples back */
{
    int32_t i = (int32_t)d;
    float f = d - (float)i, a, b;
    uint32_t p = rv_wp + rv_base[l] + (uint32_t)i;
    a = h_dec(rv_buf[p & RV_MASK]);
    b = h_dec(rv_buf[(p + 1u) & RV_MASK]);
    return a + f * (b - a);
}

static void rv_wr(int l, float x) { rv_buf[(rv_wp + rv_base[l]) & RV_MASK] = h_enc(x); }

static float rv_ap(int l, float d, float k, float x)
{
    float z = rv_rd(l, d), w = x + k * z;
    rv_wr(l, w);
    return z - k * w;
}

/* the shimmer's shifter: two 93 ms grains reading back at twice the speed, an octave up, crossfaded
 * with raised-cosine windows (each fades in as the other fades out) */
static float shimmer(float x)
{
    float p2 = sh_ph + 0.5f, y = 0.0f, c = fm_cosf(FM_TWO_PI * sh_ph);
    int g;
    sh_buf[sh_w & (SH_N - 1u)] = x;
    p2 = p2 >= 1.0f ? p2 - 1.0f : p2;
    for (g = 0; g < 2; g++) {
        float p = g ? p2 : sh_ph, d = p * SH_W + 1.0f, f, a, b;
        int32_t i = (int32_t)d;
        uint32_t j = sh_w - (uint32_t)i;
        f = d - (float)i;
        a = sh_buf[j & (SH_N - 1u)];
        b = sh_buf[(j - 1u) & (SH_N - 1u)];
        y += (a + f * (b - a)) * (g ? 0.5f + 0.5f * c : 0.5f - 0.5f * c);
    }
    y *= 1.155f;                        /* the two grains' power, as a rule unrelated, back to about 1 */
    sh_w++;
    sh_ph -= 1.0f / SH_W;
    if (sh_ph < 0.0f)
        sh_ph += 1.0f;
    return y;
}

float wf_decay_s(float decay) { return 0.2f * fm_exp2f(decay * 8.2288f); }   /* 0.2 x 300^decay */

/* per block: everything the knobs set, for the samples of the block */
typedef struct {
    const rv_type_t *t;
    float len[L_N], g[4], glow[4], damp, low_a, in_a, din, dring, pre, wc[2], ws[2], shg, sh_a;
} rv_k_t;

static void rv_block(rv_k_t *k)
{
    const rv_type_t *t = &TYPES[wf_cfg.rv_type < WF_RV_NTYPE ? wf_cfg.rv_type : 0];
    float target = t->size * (0.5f + wf_cfg.rv_size), rt = wf_decay_s(wf_cfg.rv_decay), mod = wf_cfg.rv_mod;
    float lowm = fm_exp2f(wf_cfg.rv_low * 1.3219f), fc, rate[2];   /* the bass's decay x 0.4 .. 2.5 */
    int i;
    k->t = t;
    rv_size += (target - rv_size) * 0.02f;          /* ~70 ms glides */
    rv_pre += (wf_cfg.rv_pre_ms * (SR2 / 1000.0f) - rv_pre) * 0.02f;
    k->pre = rv_pre < 1.0f ? 1.0f : rv_pre > (float)(PRE_MAX - 2u) ? (float)(PRE_MAX - 2u) : rv_pre;
    for (i = L_IN; i < L_N; i++)
        k->len[i] = LEN[i] * rv_size;
    for (i = 0; i < 4; i++) {                       /* each block's gain for the reverb time, and the bass's */
        float l = k->len[L_AP1 + i] + k->len[L_AP2 + i] + k->len[L_DL + i];
        k->g[i] = fm_exp2f(-9.9658f * l / (rt * SR2));
        k->glow[i] = fm_exp2f(-9.9658f * l / (rt * lowm * SR2));
    }
    /* Tone: the ring's damping, 1 kHz .. 10 kHz, open at the top */
    fc = 1000.0f * fm_exp2f(wf_cfg.rv_tone * 3.3219f) * (t->shimmer > 0.0f ? 1.6f : 1.0f);  /* (room to climb) */
    k->damp = wf_cfg.rv_tone >= 0.995f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * fc / SR2);
    fc = fc * 1.5f;
    k->in_a = fc >= 10000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * fc / SR2);
    k->low_a = 1.0f - fm_expf(-FM_TWO_PI * 250.0f / SR2);
    /* Shimmer: the octave fed back into the input, high-passed so the low end stays clear */
    k->shg = t->shimmer * wf_cfg.rv_shim * 4.0f;
    k->sh_a = 1.0f - fm_expf(-FM_TWO_PI * 400.0f / SR2);
    /* Mod: the input diffusers by up to 25 samples, the ring by up to 12; faster above 70 */
    k->din = t->mod_in * mod * 25.0f;
    k->dring = t->mod_ring * mod * 12.0f;
    rate[0] = mod < 0.7f ? 0.25f + mod : 0.95f + (mod - 0.7f) * 7.0f;
    rate[1] = 0.11f + mod * 0.3f;
    for (i = 0; i < 2; i++) {
        float w = FM_TWO_PI * rate[i] / SR2, r;
        k->wc[i] = fm_cosf(w);
        k->ws[i] = fm_sinf(w);
        r = lfo_c[i] * lfo_c[i] + lfo_s[i] * lfo_s[i];   /* keep the LFOs on the unit circle */
        r = 1.5f - 0.5f * r;
        lfo_c[i] *= r;
        lfo_s[i] *= r;
    }
}

/* one half-rate sample: mono in, stereo out */
static void rv_sample(const rv_k_t *k, float x, float *l, float *r)
{
    const rv_type_t *t = k->t;
    float d[4], fb[4], a[4], ph[2][4], c, s;
    uint32_t i;
    for (i = 0; i < 2; i++) {
        c = lfo_c[i] * k->wc[i] - lfo_s[i] * k->ws[i];
        s = lfo_s[i] * k->wc[i] + lfo_c[i] * k->ws[i];
        lfo_c[i] = c;
        lfo_s[i] = s;
        ph[i][0] = 1.0f + c;
        ph[i][1] = 1.0f + s;
        ph[i][2] = 1.0f - c;
        ph[i][3] = 1.0f - s;
    }
    /* pre-delay, the input's band, the input diffusers */
    rv_wr(L_PRE, x);
    x = rv_rd(L_PRE, k->pre);
    rv_in_lp += k->in_a * (x - rv_in_lp);
    x = rv_in_lp + sh_fb;
    for (i = 0; i < t->nin; i++)
        x = rv_ap(L_IN + (int)i, k->len[L_IN + i] + k->din * ph[0][i & 3u], t->kin, x);
    x *= 0.7071f;                       /* (into four blocks, not two: the same power in all) */
    /* the ring: each block's delay out, damped, its bass kept longer or shorter */
    for (i = 0; i < 4; i++) {
        d[i] = rv_rd(L_DL + (int)i, k->len[L_DL + i]);
        rv_lp[i] += k->damp * (d[i] - rv_lp[i]);
        rv_low[i] += k->low_a * (rv_lp[i] - rv_low[i]);
        fb[i] = k->g[i] * rv_lp[i] + (k->glow[i] - k->g[i]) * rv_low[i];
    }
    for (i = 0; i < 4; i++) {
        float y = fb[(i + 3u) & 3u] + ((i & 1u) ? -x : x);     /* the input into every block */
        y = rv_ap(L_AP1 + (int)i, k->len[L_AP1 + i] + k->dring * ph[1][i], -t->k1, y);
        y = rv_ap(L_AP2 + (int)i, k->len[L_AP2 + i], t->k2, y);
        a[i] = y;
        rv_wr(L_DL + (int)i, y);
    }
    /* the outputs: taps all round the ring, as Dattorro's, so the tail is dense from the start and its
     * round trip doesn't show (left and right from different points) */
    *l = t->out * (d[0] + rv_rd(L_DL + 0, k->len[L_DL + 0] * 0.39f) - rv_rd(L_DL + 1, k->len[L_DL + 1] * 0.63f) +
                 rv_rd(L_DL + 2, k->len[L_DL + 2] * 0.21f) - rv_rd(L_DL + 3, k->len[L_DL + 3] * 0.82f) +
                 0.6f * (a[1] - a[3]));
    *r = t->out * (d[2] + rv_rd(L_DL + 2, k->len[L_DL + 2] * 0.43f) - rv_rd(L_DL + 3, k->len[L_DL + 3] * 0.27f) +
                 rv_rd(L_DL + 0, k->len[L_DL + 0] * 0.71f) - rv_rd(L_DL + 1, k->len[L_DL + 1] * 0.17f) +
                 0.6f * (a[3] - a[0]));
    if (k->shg > 0.0f) {                /* the tail, an octave up, back into the input: it climbs */
        float y = 0.3f * (d[0] + d[2] - 0.6f * (a[1] + a[3]));   /* (blocks 1 and 3 take the input inverted) */
        sh_hp += k->sh_a * (y - sh_hp);
        sh_fb = 0.25f * sat(shimmer(y - sh_hp) * k->shg * 4.0f);   /* (held under 0.25: it can't run away) */
    } else {
        sh_fb = 0.0f;
    }
    rv_wp--;
}

static void rv_clear(void)
{
    uint32_t i;
    for (i = 0; i < RV_N; i++)
        rv_buf[i] = 0;
    for (i = 0; i < SH_N; i++)
        sh_buf[i] = 0.0f;
    for (i = 0; i < 4; i++)
        rv_lp[i] = rv_low[i] = 0.0f;
    rv_in_lp = sh_hp = sh_fb = 0.0f;
}

uint32_t wf_reverb_span(void) { return rv_span; }

/* ------------------------------------------------------------- delay --- */
/* a mode: its feedback path's low-pass and high-pass, its drive, its wow and flutter (samples), and how
 * long a change of Time takes */
typedef struct {
    float lp_hz, hp_hz, drive, wow, flutter, slew_s;
} dl_mode_t;
static const dl_mode_t MODES[WF_DL_NMODE] = {
    {11000.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.12f},     /* Digital */
    {3800.0f, 90.0f, 1.3f, 10.0f, 0.8f, 0.35f},     /* Tape */
    {2400.0f, 60.0f, 1.7f, 2.5f, 0.0f, 0.20f},      /* Analog */
    {11000.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.12f},     /* Ping-pong */
};

static uint32_t dl_wp;
static float dl_t = 8000.0f, dl_lp, dl_lp2, dl_hp, dl_wow, dl_flut;

typedef struct {
    const dl_mode_t *m;
    float t, slew, lp_a, hp_a, fb, wow_w, flut_w;
    int ping;
} dl_k_t;

static void dl_block(dl_k_t *k)
{
    uint32_t mode = wf_cfg.dl_mode < WF_DL_NMODE ? wf_cfg.dl_mode : 0u;
    float t = wf_cfg.dl_ms * (SR2 / 1000.0f), lim = (float)(DL_N - 64u);
    k->m = &MODES[mode];
    k->ping = mode == WF_DL_PINGPONG;
    if (k->ping)
        lim *= 0.5f;
    k->t = t < 2.0f ? 2.0f : t > lim ? lim : t;
    k->slew = 1.0f - fm_expf(-1.0f / (k->m->slew_s * SR2));
    k->lp_a = 1.0f - fm_expf(-FM_TWO_PI * k->m->lp_hz / SR2);
    k->hp_a = 1.0f - fm_expf(-FM_TWO_PI * k->m->hp_hz / SR2);
    k->fb = wf_cfg.dl_fb;
    k->wow_w = FM_TWO_PI * 0.55f / SR2;
    k->flut_w = FM_TWO_PI * 7.1f / SR2;
}

static float dl_rd(float d)
{
    float p = (float)dl_wp - d, f, a, b;
    int32_t i;
    uint32_t j;
    if (p < 0.0f)
        p += (float)DL_N;
    i = (int32_t)p;
    f = p - (float)i;
    j = (uint32_t)i + 1u;
    a = (float)dl_buf[i];
    b = (float)dl_buf[j >= DL_N ? 0u : j];
    return (a + f * (b - a)) * (1.0f / DL_Q);
}

static void dl_sample(const dl_k_t *k, float x, float *l, float *r)
{
    const dl_mode_t *m = k->m;
    float t, y1, y2, w;
    dl_t += (k->t - dl_t) * k->slew;
    t = dl_t;
    if (m->wow > 0.0f) {
        dl_wow += k->wow_w;
        dl_wow = dl_wow > FM_PI ? dl_wow - FM_TWO_PI : dl_wow;
        dl_flut += k->flut_w;
        dl_flut = dl_flut > FM_PI ? dl_flut - FM_TWO_PI : dl_flut;
        t += m->wow * (1.0f + fm_sinf(dl_wow)) + m->flutter * (1.0f + fm_sinf(dl_flut));
    }
    y1 = dl_rd(t);
    y2 = k->ping ? dl_rd(2.0f * t) : y1;
    /* the feedback: from the last tap, high-passed, low-passed, saturated */
    w = y2 * k->fb;
    dl_hp += k->hp_a * (w - dl_hp);
    w -= dl_hp;
    dl_lp += k->lp_a * (w - dl_lp);
    dl_lp2 += k->lp_a * (dl_lp - dl_lp2);           /* 12 dB an octave */
    w = dl_lp2 + x;
    w = m->drive > 0.0f ? sat(w * m->drive) / m->drive : 2.0f * sat(w * 0.5f);
    dl_buf[dl_wp] = q16(w * DL_Q);
    dl_wp = dl_wp + 1u >= DL_N ? 0u : dl_wp + 1u;
    *l = y1;
    *r = y2;
}

static void dl_clear(void)
{
    uint32_t i;
    for (i = 0; i < DL_N; i++)
        dl_buf[i] = 0;
    dl_lp = dl_lp2 = dl_hp = 0.0f;
}

/* -------------------------------------------------------- distortion --- */
/* Two times oversampled: the same half-band weights take the signal up (a midpoint between each two
 * samples) and, after the shaper, back down, both linear phase, so the wet path is WF_DS_LATENCY samples
 * late and the dry is delayed as much. Crush skips it (its aliasing is the point). */
static float ds_x[6];                  /* the input, newest first */
static float ds_o[6], ds_e[3];         /* the shaped 2x stream: its odd samples, its even ones */
static float ds_lp1, ds_lp2, ds_dcx, ds_dcy, ds_hold;
static uint32_t ds_n;

typedef struct {
    uint32_t type, hold;
    float g, comp, lp_a, step;
} ds_k_t;

static void ds_block(ds_k_t *k)
{
    float d = wf_cfg.ds_drive, fc;
    k->type = wf_cfg.ds_type < WF_DS_NTYPE ? wf_cfg.ds_type : 0u;
    k->step = 1.0f;
    k->hold = 1u;
    switch (k->type) {
    case WF_DS_FOLD:                   /* the folds: 1 to 8 times over */
        k->g = 1.0f + d * 7.0f;
        k->comp = 0.55f * fm_rsqrtf(k->g);
        break;
    case WF_DS_CRUSH: {                /* 16 bits down to 3, the rate down to a sixteenth */
        float bits = 16.0f - d * 13.0f;
        k->g = 1.0f;
        k->comp = 1.0f;
        k->step = fm_exp2f(1.0f - bits);
        k->hold = 1u + (uint32_t)(d * d * 15.0f);
        break;
    }
    default:                           /* 0 to 36 dB (Fuzz 6 dB more); the level held near the dry's */
        k->g = fm_exp2f(d * 5.98f);
        k->comp = fm_exp2f(d * 5.98f * -0.4f) * (k->type == WF_DS_FUZZ ? 0.65f : 1.0f);
        k->g *= k->type == WF_DS_FUZZ ? 2.0f : 1.0f;
        break;
    }
    /* Tone: a 12 dB low-pass after the shaper, 700 Hz .. 14 kHz, open at the top */
    fc = 700.0f * fm_exp2f(wf_cfg.ds_tone * 4.3219f);
    k->lp_a = wf_cfg.ds_tone >= 0.995f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * fc / WV_SR);
}

static float shape(uint32_t type, float u)
{
    switch (type) {
    case WF_DS_DISTORT: return u > 1.0f ? 1.0f : u < -1.0f ? -1.0f : u;
    case WF_DS_FUZZ: return u > 0.0f ? (u > 0.6f ? 0.6f : u) : fm_tanhf(u);   /* hard above, soft below */
    case WF_DS_FOLD: return fm_sinf(u * (FM_PI * 0.5f));
    default: return fm_tanhf(u + 0.15f) - 0.148885f;          /* (tanh 0.15: a touch of even harmonics) */
    }
}

/* one sample in, the wet (late by WF_DS_LATENCY) out; the dry, as late, in *dry */
static float ds_sample(const ds_k_t *k, float x, float *dry)
{
    float y;
    int j;
    for (j = 5; j > 0; j--)
        ds_x[j] = ds_x[j - 1];
    ds_x[0] = x;
    *dry = ds_x[5];
    if (k->type == WF_DS_CRUSH) {
        if (ds_n == 0u)
            ds_hold = k->step * fm_floorf(ds_x[5] / k->step + 0.5f);
        ds_n = ds_n + 1u >= k->hold ? 0u : ds_n + 1u;
        y = ds_hold;
    } else {
        float mid = (150.0f * (ds_x[3] + ds_x[2]) - 25.0f * (ds_x[4] + ds_x[1]) + 3.0f * (ds_x[5] + ds_x[0])) *
                    (1.0f / 256.0f);
        for (j = 5; j > 0; j--)
            ds_o[j] = ds_o[j - 1];
        ds_e[2] = ds_e[1];
        ds_e[1] = ds_e[0];
        ds_e[0] = shape(k->type, ds_x[3] * k->g);
        ds_o[0] = shape(k->type, mid * k->g);
        y = (256.0f * ds_e[2] + 150.0f * (ds_o[2] + ds_o[3]) - 25.0f * (ds_o[1] + ds_o[4]) +
             3.0f * (ds_o[0] + ds_o[5])) * (1.0f / 512.0f);
    }
    y *= k->comp;
    ds_lp1 += k->lp_a * (y - ds_lp1);
    ds_lp2 += k->lp_a * (ds_lp1 - ds_lp2);
    y = ds_dcy * 0.99715f + ds_lp2 - ds_dcx;   /* ~20 Hz: the asymmetric shapers' offset out */
    ds_dcx = ds_lp2;
    ds_dcy = y;
    return y;
}

static void ds_clear(void)
{
    int j;
    for (j = 0; j < 6; j++)
        ds_x[j] = ds_o[j] = 0.0f;
    ds_e[0] = ds_e[1] = ds_e[2] = 0.0f;
    ds_lp1 = ds_lp2 = ds_dcx = ds_dcy = ds_hold = 0.0f;
    ds_n = 0u;
}

/* -------------------------------------------------------- modulation --- */
/* Two lines (left, right) the input goes into, read by taps an LFO moves: a quadrature pair of them
 * (the slow one), and a fast one for the Ensemble's vibrato. Flanger feeds each side's tap back into
 * its own line; Phaser uses six one-pole allpasses a side instead of the lines. */
static uint32_t mo_w;
static float mo_c[2], mo_s[2];         /* the slow and the fast LFO */
static float mo_fb[2], mo_ap[2][6];

float wf_rate_hz(float rate) { return 0.05f * fm_exp2f(rate * 7.6439f); }   /* 0.05 x 200^rate */

typedef struct {
    uint32_t type;
    float wc[2], ws[2], depth;
} mo_k_t;

static void mo_block(mo_k_t *k)
{
    float rate[2], r;
    int i;
    k->type = wf_cfg.mo_type < WF_MO_NTYPE ? wf_cfg.mo_type : 0u;
    k->depth = wf_cfg.mo_depth;
    rate[0] = wf_cfg.mo_hz;
    rate[1] = 6.3f;
    for (i = 0; i < 2; i++) {
        float w = FM_TWO_PI * rate[i] / WV_SR;
        k->wc[i] = fm_cosf(w);
        k->ws[i] = fm_sinf(w);
        r = 1.5f - 0.5f * (mo_c[i] * mo_c[i] + mo_s[i] * mo_s[i]);   /* back onto the unit circle */
        mo_c[i] *= r;
        mo_s[i] *= r;
    }
}

static float mo_rd(int ch, float d)    /* line ch, d samples back */
{
    int32_t i = (int32_t)d;
    float f = d - (float)i, a, b;
    uint32_t p = mo_w - (uint32_t)i;
    a = mo_buf[ch][p & (MO_N - 1u)];
    b = mo_buf[ch][(p - 1u) & (MO_N - 1u)];
    return a + f * (b - a);
}

static float allpass6(float *z, float a, float x)   /* six one-pole allpasses, coefficient a */
{
    int i;
    for (i = 0; i < 6; i++) {
        float y = z[i] - a * x;
        z[i] = x + a * y;
        x = y;
    }
    return x;
}

static void mo_sample(const mo_k_t *k, float x, float *l, float *r)
{
    float c, s, ms = WV_SR * 0.001f, dp = k->depth;
    int i;
    for (i = 0; i < 2; i++) {
        c = mo_c[i] * k->wc[i] - mo_s[i] * k->ws[i];
        s = mo_s[i] * k->wc[i] + mo_c[i] * k->ws[i];
        mo_c[i] = c;
        mo_s[i] = s;
    }
    c = mo_c[0];
    s = mo_s[0];
    switch (k->type) {
    case WF_MO_ENSEMBLE: {             /* three taps 120 degrees apart, each with the fast vibrato too */
        float p1 = -0.5f * c + 0.8660254f * s, p2 = -0.5f * c - 0.8660254f * s;
        float f1 = -0.5f * mo_c[1] + 0.8660254f * mo_s[1], f2 = -0.5f * mo_c[1] - 0.8660254f * mo_s[1];
        float b = 8.0f * ms, ds = dp * 3.0f * ms, df = dp * 0.35f * ms, t0, t1, t2;
        mo_buf[0][mo_w & (MO_N - 1u)] = x;
        t0 = mo_rd(0, b + ds * c + df * mo_c[1]);
        t1 = mo_rd(0, b + ds * p1 + df * f1);
        t2 = mo_rd(0, b + ds * p2 + df * f2);
        *l = 0.7f * (t0 + 0.5f * t2);
        *r = 0.7f * (t1 + 0.5f * t2);
        break;
    }
    case WF_MO_FLANGER: {              /* 0.3 .. 6.3 ms, swept one way on the left, the other on the right */
        float b = 0.3f * ms, sw = dp * 6.0f * ms;
        mo_buf[0][mo_w & (MO_N - 1u)] = x + 0.65f * mo_fb[0];
        mo_buf[1][mo_w & (MO_N - 1u)] = x + 0.65f * mo_fb[1];
        *l = mo_fb[0] = mo_rd(0, b + sw * 0.5f * (1.0f + c));
        *r = mo_fb[1] = mo_rd(1, b + sw * 0.5f * (1.0f - c));
        break;
    }
    case WF_MO_PHASER: {               /* the notches swept from 150 Hz up 1 to 6 octaves, the sides 90 apart */
        float span = 1.0f + dp * 5.0f, ph[2];
        ph[0] = c;
        ph[1] = s;
        for (i = 0; i < 2; i++) {
            float fc = 150.0f * fm_exp2f(span * 0.5f * (1.0f + ph[i])), w = FM_PI * fc / WV_SR, a, y;
            w = w > 1.2f ? 1.2f : w;
            w = w * (1.0f + w * w * (1.0f / 3.0f));          /* tan, near enough below 1.2 */
            a = (1.0f - w) / (1.0f + w);
            y = allpass6(mo_ap[i], a, x + 0.5f * mo_fb[i]);
            mo_fb[i] = y;
            *(i ? r : l) = y;
        }
        break;
    }
    default: {                         /* Chorus: 10 ms, +-4 ms at full Depth, the sides in quadrature */
        float b = 10.0f * ms, d = dp * 4.0f * ms;
        mo_buf[0][mo_w & (MO_N - 1u)] = x;
        *l = mo_rd(0, b + d * c);
        *r = mo_rd(0, b + d * s);
        break;
    }
    }
    mo_w++;
}

static void mo_clear(void)
{
    uint32_t i;
    for (i = 0; i < MO_N; i++)
        mo_buf[0][i] = mo_buf[1][i] = 0.0f;
    for (i = 0; i < 6; i++)
        mo_ap[0][i] = mo_ap[1][i] = 0.0f;
    mo_fb[0] = mo_fb[1] = 0.0f;
}

/* -------------------------------------------------------------- mix --- */
static float dec_x[12];                 /* the half-band filters' histories */
static float up_l[6], up_r[6];
static uint32_t phase;
static float g_dry = 1.0f, g_dw, g_dd = 1.0f, g_rw, g_rd = 1.0f;   /* the gains at the end of the last block */
static float g_sw, g_sd = 1.0f, g_mw, g_md = 1.0f;
static int rv_on, dl_on, ds_on, mo_on;

void wf_init(void)
{
    uint32_t i, off = 0;
    for (i = 0; i < L_N; i++) {
        rv_base[i] = off;
        off += i == L_PRE ? PRE_MAX : (uint32_t)(LEN[i] * SMAX) + MARGIN;
    }
    rv_span = off;
    rv_wp = 0;
    rv_size = TYPES[0].size;
    rv_pre = 0.0f;
    lfo_c[0] = lfo_c[1] = 1.0f;
    lfo_s[0] = lfo_s[1] = 0.0f;
    sh_w = 0;
    sh_ph = 0.0f;
    dl_wp = 0;
    dl_wow = dl_flut = 0.0f;
    rv_clear();
    dl_clear();
    for (i = 0; i < 12; i++)
        dec_x[i] = 0.0f;
    for (i = 0; i < 6; i++)
        up_l[i] = up_r[i] = 0.0f;
    phase = 0;
    g_dry = g_dd = g_rd = 1.0f;
    g_dw = g_rw = 0.0f;
    g_sd = g_md = 1.0f;
    g_sw = g_mw = 0.0f;
    rv_on = dl_on = ds_on = mo_on = 0;
    ds_clear();
    mo_clear();
    mo_w = 0;
    mo_c[0] = mo_c[1] = 1.0f;
    mo_s[0] = mo_s[1] = 0.0f;
    wf_cfg.rv_type = WF_RV_CLOUD;
    wf_cfg.rv_mix = wf_cfg.dl_mix = 0.0f;
    wf_cfg.rv_decay = 0.5f;
    wf_cfg.rv_tone = 0.6f;
    wf_cfg.rv_size = 0.5f;
    wf_cfg.rv_mod = 0.5f;
    wf_cfg.rv_low = 0.0f;
    wf_cfg.rv_shim = 0.5f;
    wf_cfg.rv_pre_ms = 20.0f;
    wf_cfg.dl_mode = WF_DL_DIGITAL;
    wf_cfg.dl_fb = 0.35f;
    wf_cfg.dl_ms = 350.0f;
    wf_cfg.ds_type = WF_DS_OVERDRIVE;
    wf_cfg.ds_mix = wf_cfg.mo_mix = 0.0f;
    wf_cfg.ds_drive = 0.4f;
    wf_cfg.ds_tone = 0.6f;
    wf_cfg.mo_type = WF_MO_CHORUS;
    wf_cfg.mo_hz = wf_rate_hz(0.4f);
    wf_cfg.mo_depth = 0.5f;
}

static float wet_of(float mix) { return mix >= 0.5f ? 1.0f : mix * 2.0f; }
static float dry_of(float mix) { return mix <= 0.5f ? 1.0f : (1.0f - mix) * 2.0f; }

static float halfband(const float *h, int c)   /* the midpoint weights about h[c] and h[c+1] */
{
    return (150.0f * (h[c] + h[c + 1]) - 25.0f * (h[c - 1] + h[c + 2]) + 3.0f * (h[c - 2] + h[c + 3])) *
           (1.0f / 256.0f);
}

void wf_process(int32_t *lr, uint32_t n)
{
    float rm = wf_cfg.rv_mix, dm = wf_cfg.dl_mix, sm = wf_cfg.ds_mix, mm = wf_cfg.mo_mix;
    float dw = wet_of(dm), dd = dry_of(dm), rw = wet_of(rm), rd = dry_of(rm);
    float sw = wet_of(sm), sd = dry_of(sm), mw = wet_of(mm), md = dry_of(mm);
    float inc_dry, inc_dw, inc_dd, inc_rw, inc_rd, inc_sw, inc_sd, inc_mw, inc_md, inv = 1.0f / (float)(n ? n : 1u);
    rv_k_t rk;
    dl_k_t dk;
    ds_k_t sk;
    mo_k_t mk;
    uint32_t i, j;
    if (sw > 0.0f && !ds_on) {         /* switched on: from silence, not what it last held */
        ds_clear();
        ds_on = 1;
    }
    if (mw > 0.0f && !mo_on) {
        mo_clear();
        mo_on = 1;
    }
    if (!dl_on && !rv_on && (dw > 0.0f || rw > 0.0f)) {   /* the resamplers too */
        for (i = 0; i < 12; i++)
            dec_x[i] = 0.0f;
        for (i = 0; i < 6; i++)
            up_l[i] = up_r[i] = 0.0f;
    }
    if (dw > 0.0f && !dl_on) {          /* ...and the lines, not what they last held, at their settings */
        dl_clear();
        dl_block(&dk);
        dl_t = dk.t;
        dl_on = 1;
    }
    if (rw > 0.0f && !rv_on) {
        rv_clear();
        rv_size = TYPES[wf_cfg.rv_type < WF_RV_NTYPE ? wf_cfg.rv_type : 0].size * (0.5f + wf_cfg.rv_size);
        rv_pre = wf_cfg.rv_pre_ms * (SR2 / 1000.0f);
        rv_on = 1;
    }
    if (!dl_on && !rv_on && !ds_on && !mo_on)
        return;                         /* all off: the voice as it is */
    inc_dry = (dd * rd - g_dry) * inv;
    inc_dw = (dw - g_dw) * inv;
    inc_dd = (dd - g_dd) * inv;
    inc_rw = (rw - g_rw) * inv;
    inc_rd = (rd - g_rd) * inv;
    inc_sw = (sw - g_sw) * inv;
    inc_sd = (sd - g_sd) * inv;
    inc_mw = (mw - g_mw) * inv;
    inc_md = (md - g_md) * inv;
    ds_block(&sk);
    mo_block(&mk);
    dl_block(&dk);
    rv_block(&rk);
    for (i = 0; i < n; i++) {
        float x = (float)lr[2 * i] * (1.0f / 8388608.0f), xl, xr, yl, yr;
        g_sw += inc_sw;
        g_sd += inc_sd;
        g_mw += inc_mw;
        g_md += inc_md;
        if (ds_on) {                    /* the distortion, then the modulation: stereo from here */
            float dry, w = ds_sample(&sk, x, &dry);
            x = dry * g_sd + w * g_sw;
        }
        xl = xr = x;
        if (mo_on) {
            float wl, wr;
            mo_sample(&mk, x, &wl, &wr);
            xl = x * g_md + wl * g_mw;
            xr = x * g_md + wr * g_mw;
        }
        if (dl_on || rv_on) {
            g_dry += inc_dry;
            g_dw += inc_dw;
            g_dd += inc_dd;
            g_rw += inc_rw;
            g_rd += inc_rd;
            for (j = 11; j > 0; j--)
                dec_x[j] = dec_x[j - 1];
            dec_x[0] = 0.5f * (xl + xr);
            if (phase) {                     /* a half-rate sample: down, the effects, back up */
                float h = (256.0f * dec_x[5] + 150.0f * (dec_x[4] + dec_x[6]) - 25.0f * (dec_x[2] + dec_x[8]) +
                           3.0f * (dec_x[0] + dec_x[10])) * (1.0f / 512.0f);
                float el = 0.0f, er = 0.0f, vl = 0.0f, vr = 0.0f, a;
                if (dl_on)
                    dl_sample(&dk, h, &el, &er);
                a = h * g_dd + g_dw * 0.5f * (el + er);
                if (rv_on)
                    rv_sample(&rk, a, &vl, &vr);
                for (j = 5; j > 0; j--) {
                    up_l[j] = up_l[j - 1];
                    up_r[j] = up_r[j - 1];
                }
                up_l[0] = g_dw * el * g_rd + g_rw * vl;
                up_r[0] = g_dw * er * g_rd + g_rw * vr;
                yl = halfband(up_l, 2);
                yr = halfband(up_r, 2);
            } else {
                yl = up_l[2];
                yr = up_r[2];
            }
            phase ^= 1u;
            yl += xl * g_dry;
            yr += xr * g_dry;
        } else {
            yl = xl;
            yr = xr;
        }
        yl = yl > 1.0f ? 1.0f : yl < -1.0f ? -1.0f : yl;
        yr = yr > 1.0f ? 1.0f : yr < -1.0f ? -1.0f : yr;
        lr[2 * i] = (int32_t)(yl * 8388607.0f);
        lr[2 * i + 1] = (int32_t)(yr * 8388607.0f);
    }
    g_sw = sw;                          /* (exact at the block's end) */
    g_sd = sd;
    g_mw = mw;
    g_md = md;
    if (dl_on || rv_on) {
        g_dry = dd * rd;
        g_dw = dw;
        g_dd = dd;
        g_rw = rw;
        g_rd = rd;
    }
    if (dl_on)
        dl_lp = fm_flush(dl_lp), dl_lp2 = fm_flush(dl_lp2), dl_hp = fm_flush(dl_hp);
    if (rv_on) {
        for (i = 0; i < 4; i++)
            rv_lp[i] = fm_flush(rv_lp[i]), rv_low[i] = fm_flush(rv_low[i]);
        rv_in_lp = fm_flush(rv_in_lp);
        sh_hp = fm_flush(sh_hp);
    }
    if (ds_on) {
        ds_lp1 = fm_flush(ds_lp1), ds_lp2 = fm_flush(ds_lp2), ds_dcy = fm_flush(ds_dcy);
        if (sw == 0.0f)
            ds_on = 0;
    }
    if (mo_on) {
        for (i = 0; i < 6; i++)
            mo_ap[0][i] = fm_flush(mo_ap[0][i]), mo_ap[1][i] = fm_flush(mo_ap[1][i]);
        mo_fb[0] = fm_flush(mo_fb[0]), mo_fb[1] = fm_flush(mo_fb[1]);
        if (mw == 0.0f)
            mo_on = 0;
    }
    if (dw == 0.0f && g_dw == 0.0f)
        dl_on = 0;
    if (rw == 0.0f && g_rw == 0.0f)
        rv_on = 0;
}

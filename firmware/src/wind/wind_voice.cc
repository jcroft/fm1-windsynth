/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice: a Plaits engine into a gate envelope and VCA, a DC blocker and a soft clip.
 * Classic (VA-VCF) has its own filter; every other engine goes through ours, the same two SVFs
 * VA-VCF uses, key-tracked and scaled the same way, so Cutoff, Resonance and Slope mean the same on all.
 * The others take Plaits' own HARMONICS, TIMBRE and MORPH. 6-op plays a DX7 patch from Plaits' three
 * banks, its envelopes started by each new breath (the gate) and held through legato notes; the rest
 * have no envelope of their own.
 *
 * Plaits is vendored unmodified (third_party/mutable, see MANIFEST). It runs at the FM-1's rate
 * rather than its own 47,872 Hz, so the pitch is raised by WV_TUNE to keep it in tune; breath is
 * the envelope of these engines, so the 8.5 % longer internal time constants don't matter.
 *
 * Built freestanding: no exceptions, RTTI, static constructors or libm (shim/). The engines live
 * in static storage and are built there by wv_init with placement new. */
#include <new>
#include "wind_voice.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/resources.h"
#include "stmlib/utils/random.h"

using namespace plaits;

/* 12 log2(47872.34 / 44117.647): Plaits' tuning tables at the FM-1's rate */
#define WV_TUNE 1.41404f
#define WV_BLK 12                       /* Plaits' own block */
#define WV_FULL 8388607.0f              /* 24-bit full scale */

static union {
    char raw[sizeof(VirtualAnalogVCFEngine)];
    double align;                       /* only for its alignment: never read */
} vcf_mem;
static union {
    char raw[sizeof(FMEngine)];
    double align;
} fm_mem;
#define WV_MEM(T, name) static union { char raw[sizeof(T)]; double align; } name##_mem
WV_MEM(SixOpEngine, six);
WV_MEM(GrainEngine, grain);
WV_MEM(WaveshapingEngine, shape);
WV_MEM(VirtualAnalogEngine, va);
static union {
    char raw[16384];                    /* what the engines allocate: 6-op's patches */
    double align;
} arena;
static VirtualAnalogVCFEngine *vcf;
static FMEngine *fm2;
static SixOpEngine *six;
static Engine *engines[WV_NENGINE];
static Engine *eng;                     /* the engine playing */
static uint32_t engine;
static uint32_t dx_bank = 0xFFu, dx_want, dx_patch;   /* 6-op: the bank loaded, the one wanted; the patch */
/* 6-op renders its two voices staggered, each call a block ahead into a buffer that assumes every block
 * is as long as the last: a short one (our 64 is 5 x 12 + 4) loses time, so it played 7/8 flat and
 * rough. So it always renders 12, and this hands them out. */
static float six_q[WV_BLK];
static uint32_t six_at = WV_BLK;
static float last_gate;                 /* 6-op: a new breath starts its envelopes */
static union {
    char raw[2 * sizeof(stmlib::Svf)];
    double align;
} post_mem;
static stmlib::Svf *post;               /* FM's filter: LP, then LP for 24 dB (built in wv_init: a static
                                         * object with a constructor would need one run at boot) */
static float post_f, post_q;            /* its cutoff and Q at the end of the last block */
static uint32_t slope;
static float env, env_up, env_down;     /* the gate envelope: 5 ms up, 60 ms down by default */
static float dc_x, dc_y;                /* DC blocker state */
static float vca;                       /* the VCA gain at the end of the last block */

static float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

static void params_for(const wind_frame_t *f, EngineParameters *p)
{
    float r = clamp01(f->res);
    p->trigger = TRIGGER_UNPATCHED;
    p->note = f->note + WV_TUNE;
    p->accent = 1.0f;
    p->morph = f->morph;
    if (engine == WV_SIXOP)             /* Env: the patch's own times at 25 (Morph's default), 0 to 1 either side */
        p->morph = f->morph < 0.25f ? 2.0f * clamp01(f->morph) : 0.5f + (clamp01(f->morph) - 0.25f) * (2.0f / 3.0f);
    if (engine != WV_CLASSIC) {         /* the engine's own: FM's index, feedback and ratio, and so on */
        p->timbre = clamp01(f->timbre);
        p->harmonics = clamp01(f->harmonics);
        if (engine == WV_SIXOP) {       /* the patch, in the middle of its step of HARMONICS (x 1.02 there) */
            p->harmonics = ((float)(dx_patch % 32u) + 0.5f) * (1.0f / (32.0f * 1.02f));
            p->trigger = f->gate > 0.5f ? (last_gate > 0.5f ? TRIGGER_HIGH : TRIGGER_HIGH | TRIGGER_RISING_EDGE)
                                        : TRIGGER_LOW;
        }
        return;
    }
    /* VA-VCF: TIMBRE is its key-tracked cutoff, HARMONICS its resonance on the half that picks the
     * slope (resonance 0 at 0.625 / 0.375, full at 1.0 / 0.0) */
    p->timbre = f->cutoff;
    p->harmonics = slope == WV_SLOPE_24 ? 0.375f - 0.375f * r : 0.625f + 0.375f * r;
}

/* FM's filter, as VA-VCF's: the cutoff tracks the note (TIMBRE 0.2 at the note, 10 octaves a unit),
 * the resonance is its HARMONICS curve, and the second stage barely resonates */
static void post_targets(const wind_frame_t *f, float *fc, float *q)
{
    float r = clamp01(f->res), r2 = r * r;
    *fc = NoteToFrequency(f->note + WV_TUNE) * stmlib::SemitonesToRatio((clamp01(f->cutoff) - 0.2f) * 120.0f);
    *fc = *fc < 0.25f ? *fc : 0.25f;
    *q = r2 * r2 * 48.0f;
}

static void post_filter(float *x, uint32_t n, float fc, float q)
{
    stmlib::ParameterInterpolator fi(&post_f, fc, (size_t)n), qi(&post_q, q, (size_t)n);
    uint32_t i;
    for (i = 0; i < n; i++) {
        float f = fi.Next(), qq = qi.Next(), y;
        post[0].set_f_q<stmlib::FREQUENCY_FAST>(f, 0.5f + qq);
        y = post[0].Process<stmlib::FILTER_MODE_LOW_PASS>(x[i] * 0.7f);
        if (slope == WV_SLOPE_24) {
            post[1].set_f_q<stmlib::FREQUENCY_FAST>(f, 0.5f + 0.025f * qq);
            y = post[1].Process<stmlib::FILTER_MODE_LOW_PASS>(y);
        }
        x[i] = stmlib::SoftClip(y);
    }
}

extern "C" void wv_init(void)
{
    stmlib::BufferAllocator a(arena.raw, sizeof arena.raw);
    uint32_t i;
    stmlib::Random::Seed(0x21);         /* Plaits' own start (6-op's LFO draws on it): the same each time */
    engines[WV_CLASSIC] = vcf = new (vcf_mem.raw) VirtualAnalogVCFEngine();
    engines[WV_FM] = fm2 = new (fm_mem.raw) FMEngine();
    engines[WV_SIXOP] = six = new (six_mem.raw) SixOpEngine();
    engines[WV_GRAIN] = new (grain_mem.raw) GrainEngine();
    engines[WV_SHAPE] = new (shape_mem.raw) WaveshapingEngine();
    engines[WV_VA] = new (va_mem.raw) VirtualAnalogEngine();
    for (i = 0; i < WV_NENGINE; i++) {
        engines[i]->Init(&a);
        engines[i]->Reset();
    }
    dx_bank = 0xFFu;
    dx_want = dx_patch = 0;
    last_gate = 0.0f;
    eng = vcf;
    engine = WV_CLASSIC;
    post = new (post_mem.raw) stmlib::Svf();
    new (post_mem.raw + sizeof(stmlib::Svf)) stmlib::Svf();
    post[0].Init();
    post[1].Init();
    post_f = 0.25f;
    post_q = 0.0f;
    slope = WV_SLOPE_12;
    env = vca = dc_x = dc_y = 0.0f;
    wv_env(0.005f, 0.060f);
}

extern "C" void wv_env(float attack_s, float release_s)
{
    env_up = 1.0f / ((attack_s > 0.0005f ? attack_s : 0.0005f) * WV_SR);
    env_down = 1.0f - fm_expf(-4.6f / ((release_s > 0.002f ? release_s : 0.002f) * WV_SR));   /* -40 dB */
}

extern "C" void wv_engine(uint32_t id)
{
    id = id < WV_NENGINE ? id : WV_CLASSIC;
    if (id == engine)
        return;
    engine = id;
    eng = engines[id];
    eng->Reset();
    six_at = WV_BLK;
    last_gate = 0.0f;                   /* a note held through the change starts again (6-op's envelopes) */
}

extern "C" void wv_dx_patch(uint32_t p)
{
    p = p < WV_DX_NPATCH ? p : 0u;
    dx_want = p / 32u;                  /* (the bank is unpacked by the next render: the audio's thread) */
    dx_patch = p;
    last_gate = 0.0f;                   /* (a note held: it starts again, as the new patch) */
}

extern "C" void wv_dx_name(uint32_t p, char *name)
{
    const uint8_t *b = fm_patches_table[(p < WV_DX_NPATCH ? p : 0u) / 32u] + (p % 32u) * fm::Patch::SYX_SIZE;
    int i, n = 10;
    while (n > 0 && (b[118 + n - 1] & 0x7F) == ' ')
        n--;
    for (i = 0; i < n; i++) {
        char c = (char)(b[118 + i] & 0x7F);
        name[i] = c >= ' ' && c < 0x7F ? c : ' ';
    }
    name[n] = 0;
}

static void dx_load(void)
{
    if (dx_bank != dx_want) {
        dx_bank = dx_want;
        six->LoadUserData(fm_patches_table[dx_bank]);
    }
}

extern "C" float wv_fm_ratio(float h)
{
    return stmlib::SemitonesToRatio(stmlib::Interpolate(lut_fm_frequency_quantizer, clamp01(h), 128.0f));
}
extern "C" void wv_slope(uint32_t s) { slope = s == WV_SLOPE_24 ? WV_SLOPE_24 : WV_SLOPE_12; }

extern "C" void wv_render(const wind_frame_t *f, int32_t *out_lr, uint32_t frames)
{
    EngineParameters p;
    float out[WV_BLK], aux[WV_BLK], fc = 0.0f, q = 0.0f;
    float amp = f->amp < 0.0f ? 0.0f : f->amp > 1.0f ? 1.0f : f->amp;
    float dvca = (amp - vca) / (float)(frames ? frames : 1);   /* the VCA ramps across the block */
    bool enveloped;
    if (engine == WV_SIXOP)
        dx_load();
    params_for(f, &p);
    last_gate = f->gate;
    if (engine != WV_CLASSIC)
        post_targets(f, &fc, &q);
    while (frames) {
        uint32_t k = frames > WV_BLK ? WV_BLK : frames, i;
        if (engine == WV_SIXOP) {       /* always Plaits' 12 (see six_q), handed out k at a time */
            for (i = 0; i < k; i++) {
                if (six_at >= WV_BLK) {
                    eng->Render(p, six_q, aux, WV_BLK, &enveloped);
                    if (p.trigger & TRIGGER_RISING_EDGE)
                        p.trigger = TRIGGER_HIGH;
                    six_at = 0;
                }
                out[i] = six_q[six_at++];
            }
        } else {
            eng->Render(p, out, aux, k, &enveloped);
            if (p.trigger & TRIGGER_RISING_EDGE)
                p.trigger = TRIGGER_HIGH;   /* (the edge: once) */
        }
        if (engine != WV_CLASSIC)
            post_filter(out, k, fc, q);
        for (i = 0; i < k; i++) {
            float x, y;
            if (f->gate > 0.5f)
                env = env + env_up < 1.0f ? env + env_up : 1.0f;
            else
                env -= env * env_down;
            vca += dvca;
            x = out[i] * env * vca;
            y = dc_y * 0.99715f + x - dc_x;  /* ~20 Hz */
            dc_x = x;
            dc_y = y = fm_flush(y);
            y = stmlib::SoftClip(y);
            out_lr[0] = out_lr[1] = (int32_t)(y * WV_FULL);
            out_lr += 2;
        }
        frames -= k;
    }
    vca = amp;
}

extern "C" void wv_render_raw_engine(uint32_t id, float note, float timbre, float morph, float harmonics,
                                     float *out, uint32_t n)
{
    EngineParameters p;
    float aux[WV_BLK];
    bool enveloped;
    Engine *e = engines[id < WV_NENGINE ? id : WV_CLASSIC];
    if (id == WV_SIXOP) {               /* (bank 0, unpatched: what Plaits plays with no trigger) */
        dx_want = 0;
        dx_load();
    }
    p.trigger = TRIGGER_UNPATCHED;
    p.note = note;
    p.timbre = timbre;
    p.morph = morph;
    p.harmonics = harmonics;
    p.accent = 1.0f;
    while (n) {
        uint32_t k = n > WV_BLK ? WV_BLK : n;
        e->Render(p, out, aux, k, &enveloped);
        out += k;
        n -= k;
    }
}

extern "C" void wv_render_raw(float note, float timbre, float morph, float harmonics, float *out,
                              uint32_t n)
{
    EngineParameters p;
    float aux[WV_BLK];
    bool enveloped;
    p.trigger = TRIGGER_UNPATCHED;
    p.note = note;
    p.timbre = timbre;
    p.morph = morph;
    p.harmonics = harmonics;
    p.accent = 1.0f;
    while (n) {
        uint32_t k = n > WV_BLK ? WV_BLK : n;
        vcf->Render(p, out, aux, k, &enveloped);
        out += k;
        n -= k;
    }
}

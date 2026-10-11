/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice: a Plaits engine into a gate envelope and VCA, a DC blocker and a soft clip.
 * Classic (VA-VCF) has its own filter; FM (2-op) goes through ours, the same two SVFs VA-VCF uses,
 * key-tracked and scaled the same way, so Cutoff, Resonance and Slope mean the same on both.
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
#include "plaits/resources.h"

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
static VirtualAnalogVCFEngine *vcf;
static FMEngine *fm;
static Engine *eng;                     /* the engine playing */
static uint32_t engine;
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
    if (engine == WV_FM) {              /* FM: TIMBRE the index, MORPH the feedback, HARMONICS the ratio */
        p->timbre = clamp01(f->timbre);
        p->harmonics = clamp01(f->harmonics);
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
    static char arena[16];              /* VA-VCF allocates nothing */
    stmlib::BufferAllocator a(arena, sizeof arena);
    vcf = new (vcf_mem.raw) VirtualAnalogVCFEngine();
    vcf->Init(&a);
    vcf->Reset();
    fm = new (fm_mem.raw) FMEngine();
    fm->Init(&a);
    fm->Reset();
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
    eng = id == WV_FM ? static_cast<Engine *>(fm) : static_cast<Engine *>(vcf);
    eng->Reset();
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
    params_for(f, &p);
    if (engine == WV_FM)
        post_targets(f, &fc, &q);
    while (frames) {
        uint32_t k = frames > WV_BLK ? WV_BLK : frames, i;
        eng->Render(p, out, aux, k, &enveloped);
        if (engine == WV_FM)
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
    Engine *e = id == WV_FM ? static_cast<Engine *>(fm) : static_cast<Engine *>(vcf);
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

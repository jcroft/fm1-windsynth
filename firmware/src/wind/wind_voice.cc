/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice: a Plaits engine into a gate envelope and VCA, a DC blocker and a soft clip.
 * Classic (VA-VCF) has its own filter; every other engine goes through ours, which is VA-VCF's, line for
 * line: the same two SVFs, key-tracked and scaled the same way, driven the same way (soft clipping into
 * the first, between the two and in the second, the drive easing off as resonance rises), so Cutoff,
 * Resonance and Slope mean and sound the same on all. (A test holds it to VA-VCF bit for bit.)
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
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/noise/clocked_noise.h"
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
WV_MEM(PhaseDistortionEngine, pd);
WV_MEM(AdditiveEngine, harm);
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
    char raw[5 * sizeof(stmlib::Svf) + sizeof(ClockedNoise)];
    double align;
} post_mem;                             /* (built in wv_init: a static object with a constructor would
                                         * need one run at boot) */
typedef struct {
    float f, q, gain, stage2;           /* our filter's targets for a block (and, kept, at the end of the last) */
} post_k_t;
typedef struct {
    stmlib::Svf *svf;                   /* LP, then LP for 24 dB */
    post_k_t at;
} filt_t;
static filt_t post, nz_post;            /* our filter; the noise's own copy of it, on Classic */
/* The noise layer: Plaits' clocked noise (white, here) through a filter that morphs from low-pass through
 * band-pass to high-pass at a pitch that tracks the note, as Plaits' filtered noise engine does; mixed in
 * before our filter (on Classic, through its own copy of it), so breath opens it with the tone, and
 * through the same gate and VCA. Its level is a Mod destination. */
static ClockedNoise *nz;
static stmlib::Svf *nz_svf;
static float nz_type = 0.5f, nz_semis = 24.0f, nz_reso;   /* the Noise page: LP..HP, the pitch, resonance */
static float nz_f, nz_q, nz_mode, nz_lvl;                 /* ...at the end of the last block */
static uint32_t slope;
static float env, env_up, env_down;     /* the gate envelope: 5 ms up, 60 ms down by default */
static float dc_x, dc_y;                /* DC blocker state */
static float vca;                       /* the VCA gain at the end of the last block */
static float aux_at;                    /* the Aux mix at the end of the last block */
static float *tap;                      /* wv_tap: where the voice before the VCA goes, or none */

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

/* Our filter's settings for a block, as VA-VCF works out its own from TIMBRE and HARMONICS (the cutoff
 * tracks the note, TIMBRE 0.2 at it and 10 octaves a unit; HARMONICS, as params_for sets it, picks the
 * slope and the resonance and sets the drive) */
static void post_targets(const wind_frame_t *f, post_k_t *k)
{
    float r = clamp01(f->res);
    float h = slope == WV_SLOPE_24 ? 0.375f - 0.375f * r : 0.625f + 0.375f * r;
    float res = 2.667f * fm_maxf(fm_fabsf(h - 0.5f) - 0.125f, 0.0f), res2 = res * res;
    k->f = NoteToFrequency(f->note + WV_TUNE) * stmlib::SemitonesToRatio((clamp01(f->cutoff) - 0.2f) * 120.0f);
    k->stage2 = 1.0f - (h - 0.4f) * 4.0f;
    CONSTRAIN(k->stage2, 0.0f, 1.0f);
    k->q = res2 * res2 * 48.0f;
    k->gain = (h - 0.7f) + 0.85f;
    CONSTRAIN(k->gain, 0.7f - res2 * 0.3f, 1.0f);
}

/* VA-VCF's filter loop, on the engine's output */
static void post_filter(filt_t *p, float *x, uint32_t n, const post_k_t *k)
{
    stmlib::ParameterInterpolator fi(&p->at.f, k->f, (size_t)n), qi(&p->at.q, k->q, (size_t)n),
        si(&p->at.stage2, k->stage2, (size_t)n), gi(&p->at.gain, k->gain, (size_t)n);
    stmlib::Svf *svf = p->svf;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float f = fi.Next(), q = qi.Next(), s2 = si.Next(), g, lp;
        f = f < 0.25f ? f : 0.25f;
        svf[0].set_f_q<stmlib::FREQUENCY_FAST>(f, 0.5f + q);
        svf[1].set_f_q<stmlib::FREQUENCY_FAST>(f, 0.5f + 0.025f * q);
        g = gi.Next();
        lp = svf[0].Process<stmlib::FILTER_MODE_LOW_PASS>(stmlib::SoftClip(x[i] * g));
        lp = stmlib::SoftClip(lp * g);
        lp += s2 * (stmlib::SoftClip(svf[1].Process<stmlib::FILTER_MODE_LOW_PASS>(lp)) - lp);
        x[i] = lp;
    }
}

static void filt_reset(filt_t *p)
{
    p->svf[0].Init();
    p->svf[1].Init();
    p->at.f = p->at.q = p->at.gain = p->at.stage2 = 0.0f;   /* (as VA-VCF's Init) */
}

/* the noise layer for n frames, level ramping from the last block's to lvl (0: silent, nothing run) */
static int noise_block(const wind_frame_t *f, float lvl, float *x, uint32_t n)
{
    float fc = NoteToFrequency(f->note + WV_TUNE + nz_semis), q = 0.5f * stmlib::SemitonesToRatio(nz_reso * 60.0f);
    uint32_t i;
    if (lvl <= 0.0f && nz_lvl <= 0.0f)
        return 0;
    fc = fc < 0.4f ? fc : 0.4f;
    nz->Render(false, 1.0f, x, n);
    stmlib::ParameterInterpolator fi(&nz_f, fc, (size_t)n), qi(&nz_q, q, (size_t)n), mi(&nz_mode, nz_type, (size_t)n),
        li(&nz_lvl, lvl, (size_t)n);
    for (i = 0; i < n; i++) {
        float ff = fi.Next(), qq = qi.Next(), y, in;
        nz_svf->set_f_q<stmlib::FREQUENCY_ACCURATE>(ff, qq);
        in = x[i] * fm_rsqrtf((0.5f + qq) * 40.0f * ff);   /* (Plaits': the same loudness at any width) */
        nz_svf->ProcessMultimodeLPtoHP(&in, &y, 1, mi.Next());
        x[i] = y * li.Next();
    }
    if (lvl <= 0.0f)
        nz_lvl = 0.0f;
    return 1;
}

extern "C" void wv_noise(float type, float semis, float reso)
{
    nz_type = clamp01(type);
    nz_semis = semis;
    nz_reso = clamp01(reso);
}

static void post_reset(void) { filt_reset(&post); }

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
    engines[WV_PD] = new (pd_mem.raw) PhaseDistortionEngine();
    engines[WV_HARM] = new (harm_mem.raw) AdditiveEngine();
    for (i = 0; i < WV_NENGINE; i++) {
        engines[i]->Init(&a);
        engines[i]->Reset();
    }
    dx_bank = 0xFFu;
    dx_want = dx_patch = 0;
    last_gate = 0.0f;
    eng = vcf;
    engine = WV_CLASSIC;
    for (i = 0; i < 5; i++)
        new (post_mem.raw + i * sizeof(stmlib::Svf)) stmlib::Svf();
    post.svf = (stmlib::Svf *)post_mem.raw;
    nz_post.svf = post.svf + 2;
    nz_svf = post.svf + 4;
    nz = new (post_mem.raw + 5 * sizeof(stmlib::Svf)) ClockedNoise();
    post_reset();
    filt_reset(&nz_post);
    nz_svf->Init();
    nz->Init();
    nz_f = nz_q = nz_mode = nz_lvl = 0.0f;
    slope = WV_SLOPE_12;
    env = vca = aux_at = dc_x = dc_y = 0.0f;
    wv_env(0.005f, 0.060f);
}

extern "C" void wv_env(float attack_s, float release_s)
{
    env_up = 1.0f / ((attack_s > 0.0005f ? attack_s : 0.0005f) * WV_SR);
    env_down = 1.0f - fm_expf(-4.6f / ((release_s > 0.002f ? release_s : 0.002f) * WV_SR));   /* -40 dB */
}

extern "C" float wv_env_level(void) { return env; }

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
/* (Plaits' tables quantise the pitch to 1/256 semitone: the scope locks to what plays, not the exact note) */
extern "C" float wv_note_freq(float note) { return NoteToFrequency(note + WV_TUNE); }
extern "C" void wv_slope(uint32_t s) { slope = s == WV_SLOPE_24 ? WV_SLOPE_24 : WV_SLOPE_12; }

extern "C" void wv_tap(float *to) { tap = to; }

extern "C" void wv_render(const wind_frame_t *f, int32_t *out_lr, uint32_t frames)
{
    EngineParameters p;
    float out[WV_BLK], aux[WV_BLK], nzb[WV_BLK];
    post_k_t pk;
    float nlvl = clamp01(f->noise);
    float amp = f->amp < 0.0f ? 0.0f : f->amp > 1.0f ? 1.0f : f->amp;
    float dvca = (amp - vca) / (float)(frames ? frames : 1);   /* the VCA ramps across the block */
    float ax = engine == WV_SIXOP ? 0.0f : clamp01(f->aux);    /* (6-op's AUX is its main output) */
    float dax = (ax - aux_at) / (float)(frames ? frames : 1);  /* ...and so does the Aux mix */
    bool mix_aux = ax > 0.0f || aux_at > 0.0f;                /* (at 0, the main output bit for bit) */
    bool enveloped;
    if (engine == WV_SIXOP)
        dx_load();
    params_for(f, &p);
    last_gate = f->gate;
    post_targets(f, &pk);
    nlvl = nlvl * nlvl * 2.0f;          /* (Level: a curve, and room on top) */
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
            if (mix_aux)                /* Plaits' AUX: Classic's high-pass, FM's sub, and so on */
                for (i = 0; i < k; i++) {
                    aux_at += dax;
                    out[i] += (aux[i] - out[i]) * aux_at;
                }
        }
        if (noise_block(f, nlvl, nzb, k)) {
            if (engine == WV_CLASSIC)   /* (Classic's filter is inside it: the noise gets a copy) */
                post_filter(&nz_post, nzb, k, &pk);
            for (i = 0; i < k; i++)
                out[i] += nzb[i];
        }
        if (engine != WV_CLASSIC)
            post_filter(&post, out, k, &pk);
        for (i = 0; i < k; i++) {
            float x, y;
            if (f->gate > 0.5f)
                env = env + env_up < 1.0f ? env + env_up : 1.0f;
            else
                env -= env * env_down;
            vca += dvca;
            if (tap)
                *tap++ = out[i] * env;
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
    aux_at = ax;
    tap = 0;                            /* (this render's only) */
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

/* Classic playing f, from its start, into ref; and its own oscillator alone (the same saw, Morph 25: no
 * sub) through our filter, from its start, into ours: the two the same means our filter is VA-VCF's.
 * (Tests: it starts Classic over. wv_init after.) */
extern "C" void wv_filter_check(const wind_frame_t *f, float *ref, float *ours, uint32_t n)
{
    EngineParameters p;
    float aux[WV_BLK];
    bool enveloped;
    uint32_t keep = engine, i;
    post_k_t k;
    VariableShapeOscillator osc;
    stmlib::BufferAllocator a(arena.raw, sizeof arena.raw);
    float shape, pw;
    engine = WV_CLASSIC;
    params_for(f, &p);
    engine = keep;
    vcf->Init(&a);
    for (i = 0; i < n; i += WV_BLK)
        vcf->Render(p, ref + i, aux, n - i < WV_BLK ? n - i : WV_BLK, &enveloped);
    shape = (p.morph - 0.25f) * 2.0f + 0.5f;          /* (VA-VCF's, from MORPH) */
    CONSTRAIN(shape, 0.5f, 1.0f);
    pw = (p.morph - 0.5f) * 2.0f + 0.5f;
    if (p.morph > 0.75f)
        pw = 2.5f - p.morph * 2.0f;
    CONSTRAIN(pw, 0.5f, 0.98f);
    osc.Init();
    post_reset();
    post_targets(f, &k);
    for (i = 0; i < n; i += WV_BLK) {
        uint32_t m = n - i < WV_BLK ? n - i : WV_BLK;
        osc.Render(NoteToFrequency(p.note), pw, shape, ours + i, m);
        post_filter(&post, ours + i, m, &k);
    }
}

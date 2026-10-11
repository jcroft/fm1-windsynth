/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice: a Plaits engine into a gate envelope and VCA, a DC blocker and a soft clip.
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

using namespace plaits;

/* 12 log2(47872.34 / 44117.647): Plaits' tuning tables at the FM-1's rate */
#define WV_TUNE 1.41404f
#define WV_BLK 12                       /* Plaits' own block */
#define WV_FULL 8388607.0f              /* 24-bit full scale */

static union {
    char raw[sizeof(VirtualAnalogVCFEngine)];
    double align;                       /* only for its alignment: never read */
} vcf_mem;
static VirtualAnalogVCFEngine *vcf;
static uint32_t slope;
static float env, env_up, env_down;     /* the gate envelope: 5 ms up, 60 ms down by default */
static float dc_x, dc_y;                /* DC blocker state */
static float vca;                       /* the VCA gain at the end of the last block */

static void params_for(const wind_frame_t *f, EngineParameters *p)
{
    float r = f->res < 0.0f ? 0.0f : f->res > 1.0f ? 1.0f : f->res;
    p->trigger = TRIGGER_UNPATCHED;
    p->note = f->note + WV_TUNE;
    p->accent = 1.0f;
    p->morph = f->morph;
    /* VA-VCF: TIMBRE is its key-tracked cutoff, HARMONICS its resonance on the half that picks the
     * slope (resonance 0 at 0.625 / 0.375, full at 1.0 / 0.0); frame->harmonics is unused */
    p->timbre = f->cutoff;
    p->harmonics = slope == WV_SLOPE_24 ? 0.375f - 0.375f * r : 0.625f + 0.375f * r;
}

extern "C" void wv_init(void)
{
    static char arena[16];              /* VA-VCF allocates nothing */
    stmlib::BufferAllocator a(arena, sizeof arena);
    vcf = new (vcf_mem.raw) VirtualAnalogVCFEngine();
    vcf->Init(&a);
    vcf->Reset();
    slope = WV_SLOPE_12;
    env = vca = dc_x = dc_y = 0.0f;
    wv_env(0.005f, 0.060f);
}

extern "C" void wv_env(float attack_s, float release_s)
{
    env_up = 1.0f / ((attack_s > 0.0005f ? attack_s : 0.0005f) * WV_SR);
    env_down = 1.0f - fm_expf(-4.6f / ((release_s > 0.002f ? release_s : 0.002f) * WV_SR));   /* -40 dB */
}

extern "C" void wv_engine(uint32_t id) { (void)id; }        /* one engine for now: Classic */
extern "C" void wv_slope(uint32_t s) { slope = s == WV_SLOPE_24 ? WV_SLOPE_24 : WV_SLOPE_12; }

extern "C" void wv_render(const wind_frame_t *f, int32_t *out_lr, uint32_t frames)
{
    EngineParameters p;
    float out[WV_BLK], aux[WV_BLK];
    float amp = f->amp < 0.0f ? 0.0f : f->amp > 1.0f ? 1.0f : f->amp;
    float dvca = (amp - vca) / (float)(frames ? frames : 1);   /* the VCA ramps across the block */
    bool enveloped;
    params_for(f, &p);
    while (frames) {
        uint32_t k = frames > WV_BLK ? WV_BLK : frames, i;
        vcf->Render(p, out, aux, k, &enveloped);
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

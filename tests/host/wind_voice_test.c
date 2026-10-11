/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice on the host, linked with a C compiler and no libm or C++ runtime, as on the device:
 *   the engine: the fixed sweep (wind_sweep.h) against upstream Plaits (wind_ref), within 1 LSB
 *   the voice: silent with the gate shut, sounding with it open, in tune at the FM-1's rate,
 *              the gate's attack and release, cutoff brightening, L = R, no NaN, bounded peaks
 * and build/host/wind_sweep_N.f32 (each engine's sweep; VA-VCF's, 0, for wind_wasm_test.mjs) and wind_demo.wav, to listen to.
 *   wind_voice_test <reference.f32> <OUTDIR> */
#include <stdio.h>
#include <string.h>
#include "wind/wind_voice.h"
#include "dsp/fastmath.h"
#include "wind_sweep.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define SR 44118
#define MAXS (SR * 4)
static float sweep[SWEEP_STEPS * SWEEP_LEN], ref[SWEEP_STEPS * SWEEP_LEN];
static float buf[MAXS];
static int32_t blk[2 * 64];
static int nan_seen, lr_differ;

/* n frames of the voice into buf from at, in the device's 64-frame blocks */
static void play(const wind_frame_t *f, uint32_t at, uint32_t n)
{
    while (n) {
        uint32_t k = n > 64 ? 64 : n, i;
        wv_render(f, blk, k);
        for (i = 0; i < k; i++) {
            float v = (float)blk[2 * i] * (1.0f / 8388608.0f);
            nan_seen |= v != v;
            lr_differ |= blk[2 * i] != blk[2 * i + 1];
            buf[at + i] = v;
        }
        at += k;
        n -= k;
    }
}

static float peak(uint32_t a, uint32_t n)
{
    float p = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++)
        p = fm_maxf(p, fm_fabsf(buf[a + i]));
    return p;
}

/* high-frequency share: the first difference's energy over the signal's */
static float brightness(uint32_t a, uint32_t n)
{
    float d = 0.0f, s = 0.0f;
    uint32_t i;
    for (i = 1; i < n; i++) {
        d += (buf[a + i] - buf[a + i - 1]) * (buf[a + i] - buf[a + i - 1]);
        s += buf[a + i] * buf[a + i];
    }
    return d / (s + 1e-12f);
}

static void wav_write(const char *path, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, v;
    uint16_t h16;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f);
    v = 36 + n * 2;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    h16 = 1;
    fwrite(&h16, 2, 1, f);
    fwrite(&h16, 2, 1, f);                          /* mono */
    v = SR;
    fwrite(&v, 4, 1, f);
    v = SR * 2;
    fwrite(&v, 4, 1, f);
    h16 = 2;
    fwrite(&h16, 2, 1, f);
    h16 = 16;
    fwrite(&h16, 2, 1, f);
    fwrite("data", 1, 4, f);
    v = n * 2;
    fwrite(&v, 4, 1, f);
    for (i = 0; i < n; i++) {
        int16_t s = (int16_t)(fm_clampf(buf[i], -1.0f, 1.0f) * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

static const char *const ENGINE_NAME[WV_NENGINE] = {"VA-VCF", "FM", "6-op", "Formant", "Waveshaping", "VA",
                                                    "Swarm"};

/* engine's sweep against upstream Plaits' (refdir/ref_N.f32, from wind_ref) */
static void test_sweep(uint32_t engine, const char *refdir, const char *dir)
{
    FILE *f;
    char path[512];
    float worst = 0.0f;
    int s, worst_at = 0;
    uint32_t i;
    snprintf(path, sizeof path, "%s/ref_%u.f32", refdir, (unsigned)engine);
    f = fopen(path, "rb");
    CHECK(f && fread(ref, sizeof ref, 1, f) == 1, "no reference sweep in %s", path);
    if (f)
        fclose(f);
    wv_init();
    for (s = 0; s < SWEEP_STEPS; s++) {
        float note, timbre, morph, harmonics;
        sweep_params(s, &note, &timbre, &morph, &harmonics);
        wv_render_raw_engine(engine, note, timbre, morph, harmonics, sweep + s * SWEEP_LEN, SWEEP_LEN);
    }
    for (i = 0; i < SWEEP_STEPS * SWEEP_LEN; i++) {
        float d = fm_fabsf(sweep[i] - ref[i]);
        if (d > worst || d != d) {
            worst = d;
            worst_at = (int)i;
        }
    }
    CHECK(worst <= 1.0f / 32768.0f, "%s sweep differs from upstream Plaits by %g at sample %d", ENGINE_NAME[engine],
          (double)worst, worst_at);
    printf("  %s sweep: %d samples, worst difference from upstream %g\n", ENGINE_NAME[engine],
           SWEEP_STEPS * SWEEP_LEN, (double)worst);
    snprintf(path, sizeof path, "%s/wind_sweep_%u.f32", dir, (unsigned)engine);
    f = fopen(path, "wb");
    if (f) {
        fwrite(sweep, sizeof sweep, 1, f);
        fclose(f);
    }
}

static void test_voice(const char *dir)
{
    wind_frame_t f;
    char path[512];
    float p_open, p_rel, b_dark, b_bright, first = -1.0f, last = 0.0f, hz;
    uint32_t i, rises = 0;

    wv_init();
    memset(&f, 0, sizeof f);
    f.note = 57.0f;
    f.cutoff = 0.7f;
    f.amp = 1.0f;
    f.morph = 0.0f;
    play(&f, 0, SR / 10);                           /* gate shut */
    CHECK(peak(0, SR / 10) == 0.0f, "sound with the gate shut: %g", (double)peak(0, SR / 10));

    f.gate = 1.0f;                                  /* gate open: the attack is 5 ms */
    play(&f, 0, SR / 2);
    p_open = peak(SR / 10, SR / 10);
    CHECK(p_open > 0.1f && p_open < 1.0f, "open-gate peak %g", (double)p_open);
    CHECK(peak(0, 32) < 0.5f * p_open, "no attack: %g at once", (double)peak(0, 32));
    CHECK(peak(SR * 6 / 1000, 256) > 0.5f * p_open, "slow attack: %g after 6 ms", (double)peak(SR * 6 / 1000, 256));

    f.gate = 0.0f;                                  /* release: -40 dB in 60 ms */
    play(&f, 0, SR / 5);
    p_rel = peak(SR * 70 / 1000, SR / 100);
    CHECK(p_rel < 0.012f * p_open, "release: %g of the open level after 70 ms", (double)(p_rel / p_open));
    CHECK(peak(SR * 10 / 1000, SR / 100) > 0.05f * p_open, "release too fast");

    f.gate = 1.0f;                                  /* cutoff brightens it */
    f.cutoff = 0.3f;
    play(&f, 0, SR / 4);
    b_dark = brightness(SR / 8, SR / 8);
    f.cutoff = 0.9f;
    play(&f, 0, SR / 4);
    b_bright = brightness(SR / 8, SR / 8);
    CHECK(b_bright > 4.0f * b_dark, "cutoff: brightness %g at 0.3, %g at 0.9", (double)b_dark, (double)b_bright);

    /* in tune at the FM-1's rate: A4 (69) at 440 Hz. A square (MORPH 0.5) through the 24 dB filter
     * set an octave over the note: nearly a sine, one rising zero crossing a period */
    wv_slope(WV_SLOPE_24);
    f.note = 69.0f;
    f.morph = 0.5f;
    f.cutoff = 0.3f;
    play(&f, 0, SR);                                /* settle */
    play(&f, 0, SR);
    for (i = 1; i < SR; i++)
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) {  /* where it crosses, between the samples */
            last = (float)(i - 1) + buf[i - 1] / (buf[i - 1] - buf[i]);
            first = first < 0.0f ? last : first;
            rises++;
        }
    hz = (float)(rises - 1) * WV_SR / (last - first);
    CHECK(fm_fabsf(1200.0f * fm_log2f(hz / 440.0f)) < 1.0f, "A4 measures %.3f Hz", (double)hz);
    printf("  A4 measures %.3f Hz at %.1f Hz\n", (double)hz, (double)WV_SR);
    wv_slope(WV_SLOPE_12);

    CHECK(!nan_seen, "NaN in the output");
    CHECK(!lr_differ, "L and R differ");

    /* the demo: a breathy phrase, cutoff following a swell on each note, the resonance rising */
    wv_init();
    {
        static const float notes[] = {62, 65, 69, 72, 74, 72, 69, 65, 62};
        uint32_t at = 0, n;
        memset(&f, 0, sizeof f);
        f.amp = 1.0f;
        f.morph = 0.35f;
        for (n = 0; n < sizeof notes / sizeof notes[0]; n++) {
            uint32_t len = n + 1 < sizeof notes / sizeof notes[0] ? SR * 3 / 10 : SR * 9 / 10, j;
            f.note = notes[n];
            f.gate = 1.0f;
            f.res = (float)n / 8.0f;
            for (j = 0; j < len; j += 64) {
                float x = (float)j / (float)len;
                f.cutoff = 0.25f + 0.5f * fm_sin_turns(0.5f * x);
                play(&f, at + j, 64);
            }
            at += len;
        }
        f.gate = 0.0f;
        play(&f, at, SR / 4);
        at += SR / 4;
        snprintf(path, sizeof path, "%s/wind_demo.wav", dir);
        wav_write(path, at);
        printf("  wrote %s\n", path);
    }
}

/* the n-th harmonic's level in buf[0..len), the note's period P samples: a Hann-windowed DFT bin */
static float harmonic(uint32_t len, float period, int n)
{
    float re = 0.0f, im = 0.0f;
    uint32_t i;
    for (i = 0; i < len; i++) {
        float w = 0.5f - 0.5f * fm_sin_turns((float)i / (float)len + 0.25f), t = (float)n * (float)i / period;
        t -= (float)(int)t;
        re += buf[i] * w * fm_sin_turns(t + 0.25f);
        im += buf[i] * w * fm_sin_turns(t);
    }
    return fm_sqrtf(re * re + im * im);
}

/* the init sound is one saw: MORPH 0.25 is VA-VCF's pure saw (no square, no sub). With the filter open
 * and no resonance its harmonics fall as 1/n. */
static void test_saw(void)
{
    const float note = 57.0f, period = 47872.34f / (440.0f * fm_exp2f((note - 69.0f) / 12.0f));
    const uint32_t len = 32768;
    float h1, r[4];
    int n;
    wv_init();
    wv_render_raw(note, 1.0f, 0.25f, 0.625f, buf, len);    /* settle */
    wv_render_raw(note, 1.0f, 0.25f, 0.625f, buf, len);
    h1 = harmonic(len, period, 1);
    for (n = 2; n <= 5; n++) {
        r[n - 2] = harmonic(len, period, n) / h1;
        CHECK(fm_fabsf(r[n - 2] * (float)n - 1.0f) < 0.15f, "saw: harmonic %d at %.3f of the first, not 1/%d", n,
              (double)r[n - 2], n);
    }
    printf("  saw harmonics 2-5: %.3f %.3f %.3f %.3f (1/n: .500 .333 .250 .200)\n", (double)r[0], (double)r[1],
           (double)r[2], (double)r[3]);
}

/* FM through our filter: it sounds, the cutoff darkens and brightens it as on Classic, the index adds
 * overtones, and switching engines back and forth leaves no NaN */
static void test_fm(void)
{
    wind_frame_t f;
    float b_dark, b_bright, b_low, b_high, p;
    wv_init();
    wv_engine(WV_FM);
    memset(&f, 0, sizeof f);
    f.note = 57.0f;
    f.gate = 1.0f;
    f.amp = 1.0f;
    f.morph = 0.5f;                                 /* no feedback */
    f.harmonics = 0.24f;                            /* 1:1 */
    f.timbre = 0.8f;                                /* bright: plenty for the filter to take away */
    f.cutoff = 0.3f;
    play(&f, 0, SR / 4);
    b_dark = brightness(SR / 8, SR / 8);
    p = peak(SR / 8, SR / 8);
    f.cutoff = 1.0f;
    play(&f, 0, SR / 4);
    b_bright = brightness(SR / 8, SR / 8);
    CHECK(p > 0.01f && peak(SR / 8, SR / 8) > 0.1f && peak(SR / 8, SR / 8) < 1.0f, "FM level %g (dark %g)",
          (double)peak(SR / 8, SR / 8), (double)p);
    CHECK(b_bright > 4.0f * b_dark, "FM cutoff: brightness %g at 0.3, %g at 1.0", (double)b_dark, (double)b_bright);
    f.timbre = 0.0f;                                /* the index: a sine at 0, overtones above */
    play(&f, 0, SR / 4);
    b_low = brightness(SR / 8, SR / 8);
    f.timbre = 0.8f;
    play(&f, 0, SR / 4);
    b_high = brightness(SR / 8, SR / 8);
    CHECK(b_high > 2.0f * b_low, "FM index: brightness %g at 0, %g at 0.8", (double)b_low, (double)b_high);
    wv_engine(WV_CLASSIC);
    play(&f, 0, SR / 20);
    wv_engine(WV_FM);
    play(&f, 0, SR / 20);
    CHECK(!nan_seen, "NaN after switching engines");
    CHECK(fm_fabsf(wv_fm_ratio(0.24f) - 1.0f) < 1e-4f && fm_fabsf(wv_fm_ratio(0.48f) - 2.0f) < 1e-3f,
          "FM ratio %g at 0.24, %g at 0.48", (double)wv_fm_ratio(0.24f), (double)wv_fm_ratio(0.48f));
    printf("  FM: cutoff brightness %.4f -> %.4f, index %.4f -> %.4f\n", (double)b_dark, (double)b_bright,
           (double)b_low, (double)b_high);
    wv_engine(WV_CLASSIC);
}

int main(int argc, char **argv)
{
    uint32_t e;
    if (argc < 3) {
        printf("usage: wind_voice_test <REFDIR> <OUTDIR>   (REFDIR: ref_0.f32 .. from wind_ref)\n");
        return 2;
    }
    for (e = 0; e < WV_NENGINE; e++)
        test_sweep(e, argv[1], argv[2]);
    test_voice(argv[2]);
    test_saw();
    test_fm();
    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails ? 1 : 0;
}

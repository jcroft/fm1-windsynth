/* SPDX-License-Identifier: GPL-3.0-only */
/* The wind voice on the host, linked with a C compiler and no libm or C++ runtime, as on the device:
 *   the engine: the fixed sweep (wind_sweep.h) against upstream Plaits (wind_ref), within 1 LSB
 *   the voice: silent with the gate shut, sounding with it open, in tune at the FM-1's rate,
 *              the gate's attack and release, cutoff brightening, L = R, no NaN, bounded peaks
 * and build/host/wind_sweep.f32 (the sweep, for wind_wasm_test.mjs) and wind_demo.wav, to listen to.
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

static void test_sweep(const char *refpath, const char *dir)
{
    FILE *f = fopen(refpath, "rb");
    char path[512];
    float worst = 0.0f;
    int s, worst_at = 0;
    uint32_t i;
    CHECK(f && fread(ref, sizeof ref, 1, f) == 1, "no reference sweep in %s", refpath);
    if (f)
        fclose(f);
    wv_init();
    for (s = 0; s < SWEEP_STEPS; s++) {
        float note, timbre, morph, harmonics;
        sweep_params(s, &note, &timbre, &morph, &harmonics);
        wv_render_raw(note, timbre, morph, harmonics, sweep + s * SWEEP_LEN, SWEEP_LEN);
    }
    for (i = 0; i < SWEEP_STEPS * SWEEP_LEN; i++) {
        float d = fm_fabsf(sweep[i] - ref[i]);
        if (d > worst || d != d) {
            worst = d;
            worst_at = (int)i;
        }
    }
    CHECK(worst <= 1.0f / 32768.0f, "sweep differs from upstream Plaits by %g at sample %d", (double)worst,
          worst_at);
    printf("  sweep: %d samples, worst difference from upstream %g\n", SWEEP_STEPS * SWEEP_LEN,
           (double)worst);
    snprintf(path, sizeof path, "%s/wind_sweep.f32", dir);
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

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: wind_voice_test <reference.f32> <OUTDIR>\n");
        return 2;
    }
    test_sweep(argv[1], argv[2]);
    test_voice(argv[2]);
    if (fails)
        printf("%d check(s) failed\n", fails);
    return fails ? 1 : 0;
}

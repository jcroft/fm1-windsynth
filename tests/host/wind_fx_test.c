/* SPDX-License-Identifier: GPL-3.0-only */
/* The delay and the reverb (wind_fx), on the host, linked without libm:
 *
 *   off          Mix 0 on both: the voice, bit for bit; switched on again, they start from silence
 *   lines        the reverb's lines at their longest fit its buffer
 *   reverb       every Type: the tail's decay matches the Decay knob's time, left and right differ, and
 *                it stays bounded at the longest Decay with Size moving
 *   shimmer      an octave above the note, where Cloud has next to nothing
 *   delay        the echo lands at Time, the next one at Feedback x it; Ping-pong: left at Time, right
 *                at twice it; Tape: its echo is darker than Digital's */
#include <stdio.h>
#include <string.h>
#include "wind/wind_fx.h"
#include "wind/wind_voice.h"
#include "dsp/fastmath.h"

#define SR 44118
#define MAXN (SR * 12)
static int32_t buf[MAXN * 2];
static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok" : "FAIL", what);
    fails += !ok;
}

static void run(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 64)
        wf_process(buf + 2 * i, n - i < 64 ? n - i : 64);
}

static float at(uint32_t i, int ch) { return (float)buf[2 * i + ch] * (1.0f / 8388608.0f); }

static void fill_click(uint32_t n)        /* a 2 ms burst of noise at the start, then silence */
{
    uint32_t i, r = 12345;
    memset(buf, 0, sizeof buf);
    for (i = 0; i < 88 && i < n; i++) {
        r = r * 1664525u + 1013904223u;
        buf[2 * i] = buf[2 * i + 1] = (int32_t)((float)(int32_t)r * (1.0f / 2147483648.0f) * 0.5f * 8388607.0f);
    }
}

static float rms(uint32_t from, uint32_t len, int ch)
{
    double e = 0.0;
    uint32_t i;
    for (i = from; i < from + len; i++)
        e += (double)at(i, ch) * at(i, ch);
    return fm_sqrtf((float)(e / len) + 1e-30f);
}

static void defaults(void)
{
    wf_init();
    wf_cfg.rv_mix = 0.0f;
    wf_cfg.dl_mix = 0.0f;
}

static void test_off(void)
{
    uint32_t i, n = SR, same = 1, quiet = 1;
    defaults();
    for (i = 0; i < n; i++)
        buf[2 * i] = buf[2 * i + 1] = (int32_t)(i * 2654435761u) >> 8;
    run(n);
    for (i = 0; i < n; i++)
        same &= buf[2 * i] == (int32_t)(i * 2654435761u) >> 8 && buf[2 * i + 1] == buf[2 * i];
    check(same, "Mix 0: the voice, bit for bit");
    /* a tail, then off, then on again with nothing playing: silence, not the old tail */
    wf_cfg.rv_mix = 1.0f;
    wf_cfg.dl_mix = 1.0f;
    fill_click(SR / 2);
    run(SR / 2);
    wf_cfg.rv_mix = wf_cfg.dl_mix = 0.0f;
    memset(buf, 0, sizeof buf);
    run(256);
    wf_cfg.rv_mix = wf_cfg.dl_mix = 1.0f;
    memset(buf, 0, sizeof buf);
    run(SR);
    for (i = 0; i < SR; i++)
        quiet &= buf[2 * i] == 0 && buf[2 * i + 1] == 0;
    check(quiet, "switched on again: from silence");
}

/* the time a tail takes to fall 60 dB, from the slope of its level between 0.3 s and 1.5 s */
static float rt60(int ch)
{
    float a = rms(SR * 3 / 10, SR / 10, ch), b = rms(SR * 15 / 10, SR / 10, ch);
    float db = 20.0f * fm_log2f(a / b) * 0.30103f;
    return db > 0.0f ? 60.0f * 1.2f / db : 999.0f;
}

static void test_reverb(void)
{
    int t;
    char m[160];
    check(wf_reverb_span() <= 65536u, "the reverb's lines fit its buffer");
    for (t = 0; t < WF_RV_NTYPE; t++) {
        float want, got, c = 0.0f, el = 0.0f, er = 0.0f;
        uint32_t i;
        defaults();
        wf_cfg.rv_type = (uint8_t)t;
        wf_cfg.rv_mix = 1.0f;
        wf_cfg.rv_decay = 0.45f;
        wf_cfg.rv_tone = 1.0f;          /* (no damping: the time is the ring's gain alone) */
        wf_cfg.rv_pre_ms = 0.0f;
        want = wf_decay_s(0.45f);
        fill_click(2 * SR);
        run(2 * SR);
        got = 0.5f * (rt60(0) + rt60(1));
        snprintf(m, sizeof m, "%s: decays in %.2f s, Decay says %.2f s", WF_RV_NAME[t], got, want);
        check(got > want * 0.8f && got < want * 1.25f, m);
        for (i = SR / 5; i < SR; i++) {
            c += at(i, 0) * at(i, 1);
            el += at(i, 0) * at(i, 0);
            er += at(i, 1) * at(i, 1);
        }
        c /= fm_sqrtf(el * er) + 1e-30f;
        snprintf(m, sizeof m, "%s: left and right differ (correlation %.2f)", WF_RV_NAME[t], c);
        check(c < 0.5f && c > -0.5f && el > 1e-6f, m);
    }
    /* the longest Decay, noise in, Size swept: it stays bounded, and keeps ringing after */
    for (t = 0; t < WF_RV_NTYPE; t++) {
        uint32_t i, r = 99, bad = 0;
        float pk = 0.0f;
        defaults();
        wf_cfg.rv_type = (uint8_t)t;
        wf_cfg.rv_mix = 1.0f;
        wf_cfg.rv_decay = 1.0f;
        wf_cfg.rv_mod = 1.0f;
        wf_cfg.rv_low = 1.0f;
        for (i = 0; i < 4 * SR; i++) {
            r = r * 1664525u + 1013904223u;
            buf[2 * i] = buf[2 * i + 1] = (int32_t)r >> 10;
        }
        for (i = 0; i < 4 * SR; i += 64) {
            wf_cfg.rv_size = (float)(i % (SR)) / (float)SR;
            wf_process(buf + 2 * i, 64);
        }
        memset(buf, 0, sizeof buf);
        run(6 * SR);
        for (i = 0; i < 6 * SR; i++) {
            float a = fm_fabsf(at(i, 0));
            pk = a > pk ? a : pk;
            bad += !(a == a);
        }
        snprintf(m, sizeof m, "%s at the longest Decay: bounded (peak %.2f), still ringing (%.3f)", WF_RV_NAME[t],
                 pk, rms(5 * SR, SR / 2, 0));
        /* (Shimmer's tail climbs an octave a turn, out through the damping: it rings shorter) */
        check(!bad && pk < 0.99f && rms(5 * SR, SR / 2, 0) > (t == WF_RV_SHIMMER ? 0.002f : 0.01f), m);
    }
}

/* the level at f Hz, by correlation with a sine and a cosine over 1,024-sample windows (the shifter's
 * grains don't keep the phase across a longer one) */
static float tone_at(uint32_t from, uint32_t len, float f)
{
    float w = FM_TWO_PI * f / WV_SR, sum = 0.0f;
    uint32_t i, j, nw = len / 1024u;
    for (j = 0; j < nw; j++) {
        float s = 0.0f, c = 0.0f;
        for (i = 0; i < 1024u; i++) {
            float p = w * (float)i, x = at(from + j * 1024u + i, 0);
            s += x * fm_sinf(p);
            c += x * fm_cosf(p);
        }
        sum += fm_sqrtf(s * s + c * c) * 2.0f / 1024.0f;
    }
    return sum / (float)nw;
}

static void test_shimmer(void)
{
    float o[2], f[2];
    char m[160];
    int k;
    for (k = 0; k < 2; k++) {
        uint32_t i, n = 3 * SR;
        defaults();
        wf_cfg.rv_type = k ? WF_RV_SHIMMER : WF_RV_CLOUD;
        wf_cfg.rv_mix = 1.0f;
        wf_cfg.rv_decay = 0.6f;
        wf_cfg.rv_mod = 0.0f;
        for (i = 0; i < n; i++)
            buf[2 * i] = buf[2 * i + 1] = (int32_t)(fm_sinf(FM_TWO_PI * 330.0f * (float)i / WV_SR) * 0.3f * 8388607.0f);
        run(n);
        o[k] = tone_at(2 * SR, SR / 2, 660.0f);
        f[k] = tone_at(2 * SR, SR / 2, 330.0f);
    }
    snprintf(m, sizeof m, "Shimmer: an octave up, %.4f against Cloud's %.4f (the note: %.3f, %.3f)", o[1], o[0], f[1], f[0]);
    check(o[1] > 4.0f * o[0] && o[1] > 0.05f * f[1], m);
}

static uint32_t peak_near(uint32_t c, int ch)   /* the loudest sample within 5 ms of c */
{
    uint32_t i, best = c;
    float b = 0.0f;
    for (i = c - 220; i < c + 220; i++)
        if (fm_fabsf(at(i, ch)) > b) {
            b = fm_fabsf(at(i, ch));
            best = i;
        }
    return best;
}

static void test_delay(void)
{
    uint32_t t = 11029, p1, p2;     /* 250 ms */
    float a1, a2, hd, ht;
    char m[160];
    defaults();
    wf_cfg.dl_mix = 1.0f;
    wf_cfg.dl_ms = 250.0f;
    wf_cfg.dl_fb = 0.5f;
    fill_click(SR);
    run(SR);
    p1 = peak_near(t, 0);
    p2 = peak_near(2 * t, 0);
    a1 = rms(p1 - 60, 120, 0);
    a2 = rms(p2 - 60, 120, 0);
    snprintf(m, sizeof m, "Digital: echoes at %.1f and %.1f ms, the second %.2f of the first (Feedback 0.5)",
             p1 * 1000.0f / WV_SR, p2 * 1000.0f / WV_SR, a2 / a1);
    check(p1 > t - 60 && p1 < t + 110 && p2 > 2 * t - 60 && p2 < 2 * t + 110 && a2 > 0.4f * a1 && a2 < 0.6f * a1, m);
    check(rms(t / 2, t / 4, 0) < 0.01f * a1, "Digital: nothing between the echoes");
    /* Ping-pong: left at Time, right at twice it */
    defaults();
    wf_cfg.dl_mix = 1.0f;
    wf_cfg.dl_mode = WF_DL_PINGPONG;
    wf_cfg.dl_ms = 250.0f;
    wf_cfg.dl_fb = 0.5f;
    fill_click(SR);
    run(SR);
    snprintf(m, sizeof m, "Ping-pong: left %.3f / right %.3f at Time, left %.3f / right %.3f at twice it",
             rms(t - 100, 300, 0), rms(t - 100, 300, 1), rms(2 * t - 100, 300, 0), rms(2 * t - 100, 300, 1));
    check(rms(t - 100, 300, 0) > 20.0f * rms(t - 100, 300, 1) && rms(2 * t - 100, 300, 1) > 20.0f * rms(2 * t - 100, 300, 0), m);
    /* Tape: darker. The echo's high end (sample to sample differences) against its level */
    {
        float r[2];
        int k;
        for (k = 0; k < 2; k++) {
            uint32_t i;
            double d = 0.0, e = 0.0;
            defaults();
            wf_cfg.dl_mix = 1.0f;
            wf_cfg.dl_mode = k ? WF_DL_TAPE : WF_DL_DIGITAL;
            wf_cfg.dl_ms = 250.0f;
            wf_cfg.dl_fb = 0.5f;
            fill_click(SR);
            run(SR);
            for (i = 2 * t - 2000; i < 2 * t + 2000; i++) {   /* the second echo: through the feedback once */
                float x = at(i, 0) - at(i - 1, 0);
                d += (double)x * x;
                e += (double)at(i, 0) * at(i, 0);
            }
            r[k] = (float)(d / (e + 1e-30));
        }
        ht = r[1];
        hd = r[0];
        snprintf(m, sizeof m, "Tape: its echo darker than Digital's (%.4f against %.4f)", ht, hd);
        check(ht < 0.7f * hd, m);
    }
}

int main(void)
{
    test_off();
    test_reverb();
    test_shimmer();
    test_delay();
    if (fails)
        printf("wind_fx: %d FAILED\n", fails);
    else
        printf("wind_fx: all passed\n");
    return fails != 0;
}

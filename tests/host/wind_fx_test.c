/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects (wind_fx), on the host, linked without libm:
 *
 *   off          Mix 0 on all four: the voice, bit for bit; switched on again, they start from silence
 *   lines        the reverb's lines at their longest fit its buffer
 *   reverb       every Type: the tail's decay matches the Decay knob's time, left and right differ, and
 *                it stays bounded at the longest Decay with Size moving
 *   tail         Cloud after a short note: its level falls along a straight line in dB, without steps,
 *                and fades on past -100 dB to nothing (no hiss left circulating, and no dead stop)
 *   shimmer      an octave above the note (four notes' average), where Cloud has next to nothing, and more
 *                with more Shimmer
 *   delay        the echo lands at Time, the next one at Feedback x it; Ping-pong: left at Time, right
 *                at twice it; Tape: its echo is darker than Digital's
 *   distortion   every type holds the level near the dry's at any Drive and stays bounded; with no Drive,
 *                Distort is the dry WF_DS_LATENCY samples late (so Mix 50 doesn't comb); Drive adds
 *                harmonics; the oversampling keeps a 5 kHz tone's aliases down; Crush has few levels
 *   modulation   every type stays bounded and near the dry's level, left and right differ; Phaser and
 *                Flanger notch the spectrum at Mix 50
 *   switching    the delay into Ping-pong from a long Time, and at a Time that reads the line's very end,
 *                stays in the line; the distortion on and off mid-note: no dropout, no jump
 */
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
    wf_cfg.ds_mix = 0.0f;
    wf_cfg.mo_mix = 0.0f;
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
    wf_cfg.rv_mix = wf_cfg.dl_mix = wf_cfg.ds_mix = wf_cfg.mo_mix = 1.0f;
    wf_cfg.mo_type = WF_MO_FLANGER;
    fill_click(SR / 2);
    run(SR / 2);
    wf_cfg.rv_mix = wf_cfg.dl_mix = wf_cfg.ds_mix = wf_cfg.mo_mix = 0.0f;
    memset(buf, 0, sizeof buf);
    run(256);
    wf_cfg.rv_mix = wf_cfg.dl_mix = wf_cfg.ds_mix = wf_cfg.mo_mix = 1.0f;
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
        wf_cfg.rv_shim = 0.0f;          /* (Shimmer's octaves sustain it past the tank's own time) */
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
        wf_cfg.rv_shim = 1.0f;
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
        check(!bad && pk < 0.99f && rms(5 * SR, SR / 2, 0) > 0.01f, m);
    }
}

/* the level (dB, both sides) of 50 ms window w */
static float win_db(uint32_t w)
{
    float l = rms(w * (SR / 20), SR / 20, 0), r = rms(w * (SR / 20), SR / 20, 1);
    return 10.0f * fm_log2f(0.5f * (l * l + r * r) + 1e-30f) * 0.30103f;
}

static void test_tail(void)
{
    uint32_t i, nw = 12 * 20, pk = 0, st, en, at70, n = 0;
    float ph = 0.0f, sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f, b, a, worst = 0.0f, e[12 * 20];
    char m[200];
    defaults();
    wf_cfg.rv_mix = 1.0f;               /* Cloud, as it starts */
    memset(buf, 0, sizeof buf);
    for (i = 0; i < SR / 5; i++) {      /* a 200 ms saw, G3 */
        ph += 196.0f / WV_SR;
        ph -= ph >= 1.0f ? 1.0f : 0.0f;
        buf[2 * i] = buf[2 * i + 1] = (int32_t)((2.0f * ph - 1.0f) * 0.25f * 8388607.0f);
    }
    run(12 * SR);
    for (i = 0; i < nw; i++) {
        e[i] = win_db(i);
        pk = e[i] > e[pk] ? i : pk;
    }
    for (st = pk; st < nw && e[st] > e[pk] - 5.0f; st++)
        ;
    for (en = st; en < nw && e[en] > e[pk] - 45.0f; en++)
        ;
    for (i = st; i < en; i++, n++) {    /* a straight line through the tail, 5 to 45 dB down */
        sx += (float)i;
        sy += e[i];
        sxx += (float)i * (float)i;
        sxy += (float)i * e[i];
    }
    b = ((float)n * sxy - sx * sy) / ((float)n * sxx - sx * sx);
    a = (sy - b * sx) / (float)n;
    for (i = st; i < en; i++) {         /* the furthest the level strays from it, under it (a step down) */
        float d = a + b * (float)i - e[i];
        worst = d > worst ? d : worst;
    }
    snprintf(m, sizeof m, "Cloud's tail: falls %.0f dB/s, never more than %.1f dB under that line", b * -20.0f,
             worst);
    check(n > 20 && b < 0.0f && worst < 4.0f, m);
    for (at70 = pk; at70 < nw && e[at70] > -70.0f; at70++)
        ;
    snprintf(m, sizeof m, "Cloud's tail: a second after -70 dB still there (%.0f dB), at 12 s gone (%.0f dB)",
             at70 + 20 < nw ? e[at70 + 20] : 0.0f, e[nw - 1]);
    check(at70 + 20 < nw && e[at70 + 20] > -110.0f && e[at70 + 20] < -75.0f && e[nw - 1] < -120.0f, m);
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
    float o[3], f[3];
    char m[200];
    int k;
    static const float NOTE[4] = {262.0f, 330.0f, 392.0f, 494.0f};   /* (one note alone: wherever the */
    for (k = 0; k < 3; k++) {          /* Cloud; Shimmer; Shimmer turned down      unmoving tank's peaks fall) */
        uint32_t i, n = 3 * SR, j;
        o[k] = f[k] = 0.0f;
        for (j = 0; j < 4; j++) {
            defaults();
            wf_cfg.rv_type = k ? WF_RV_SHIMMER : WF_RV_CLOUD;
            wf_cfg.rv_shim = k == 2 ? 0.15f : 0.5f;
            wf_cfg.rv_mix = 1.0f;
            wf_cfg.rv_decay = 0.6f;
            wf_cfg.rv_mod = 0.0f;
            for (i = 0; i < n; i++)
                buf[2 * i] = buf[2 * i + 1] =
                    (int32_t)(fm_sinf(FM_TWO_PI * NOTE[j] * (float)i / WV_SR) * 0.3f * 8388607.0f);
            run(n);
            o[k] += 0.25f * tone_at(2 * SR, SR / 2, 2.0f * NOTE[j]);
            f[k] += 0.25f * tone_at(2 * SR, SR / 2, NOTE[j]);
        }
    }
    snprintf(m, sizeof m, "Shimmer: an octave up, %.4f against Cloud's %.4f (the note: %.3f, %.3f); turned down %.4f",
             o[1], o[0], f[1], f[0], o[2]);
    check(o[1] > 4.0f * o[0] && o[1] > 0.25f * f[1] && o[1] > 1.5f * o[2], m);
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

static void fill_sine(uint32_t n, float f, float a)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        buf[2 * i] = buf[2 * i + 1] = (int32_t)(a * fm_sinf(FM_TWO_PI * f * (float)i / WV_SR) * 8388607.0f);
}

static float peak_abs(uint32_t from, uint32_t len)
{
    float p = 0.0f;
    uint32_t i;
    for (i = from; i < from + len; i++) {
        p = fm_fabsf(at(i, 0)) > p ? fm_fabsf(at(i, 0)) : p;
        p = fm_fabsf(at(i, 1)) > p ? fm_fabsf(at(i, 1)) : p;
    }
    return p;
}

static void test_distortion(void)
{
    char m[160];
    uint32_t t, i, n = SR / 2;
    int ok_lvl = 1, ok_bound = 1;
    float worst_lo = 0.0f, worst_hi = -99.0f;
    for (t = 0; t < WF_DS_NTYPE; t++) {
        int d;
        for (d = 0; d <= 4; d++) {
            float db;
            defaults();
            wf_cfg.ds_mix = 1.0f;
            wf_cfg.ds_type = (uint8_t)t;
            wf_cfg.ds_drive = (float)d * 0.25f;
            wf_cfg.ds_tone = 1.0f;
            fill_sine(n, 220.0f, 0.3f);
            run(n);
            db = 20.0f * 0.30103f * fm_log2f(rms(SR / 10, SR / 4, 0) / (0.3f * 0.7071f));
            worst_lo = db < worst_lo ? db : worst_lo;
            worst_hi = db > worst_hi ? db : worst_hi;
            ok_lvl &= db > -8.0f && db < 6.0f;
            ok_bound &= peak_abs(0, n) < 0.99f;
        }
    }
    snprintf(m, sizeof m, "every type at every Drive: within -8..+6 dB of the dry (%.1f .. %.1f dB), unclipped",
             worst_lo, worst_hi);
    check(ok_lvl && ok_bound, m);
    {   /* Distort with no Drive: the dry, WF_DS_LATENCY samples late (1 kHz: the DC blocker barely turns it) */
        double e = 0.0, r = 0.0;
        defaults();
        wf_cfg.ds_mix = 1.0f;
        wf_cfg.ds_type = WF_DS_DISTORT;
        wf_cfg.ds_drive = 0.0f;
        wf_cfg.ds_tone = 1.0f;
        fill_sine(n, 1000.0f, 0.3f);
        run(n);
        for (i = SR / 10; i < SR / 10 + SR / 4; i++) {
            float want = 0.3f * fm_sinf(FM_TWO_PI * 1000.0f * (float)(i - WF_DS_LATENCY) / WV_SR), d = at(i, 0) - want;
            e += (double)d * d;
            r += (double)want * want;
        }
        snprintf(m, sizeof m, "Distort, no Drive: the dry %d samples late (the rest %.1f dB down)", WF_DS_LATENCY,
                 10.0 * 0.30103 * fm_log2f((float)(e / r)));
        check(e < r * 0.003, m);
    }
    {   /* Drive: the third harmonic */
        float h[2];
        int d;
        for (d = 0; d < 2; d++) {
            defaults();
            wf_cfg.ds_mix = 1.0f;
            wf_cfg.ds_drive = (float)d;
            wf_cfg.ds_tone = 1.0f;
            fill_sine(n, 1000.0f, 0.3f);
            run(n);
            h[d] = tone_at(SR / 10, 8192, 3000.0f) / tone_at(SR / 10, 8192, 1000.0f);
        }
        snprintf(m, sizeof m, "Overdrive: Drive adds harmonics (the 3rd at %.3f, at full Drive %.3f)", h[0], h[1]);
        check(h[0] < 0.03f && h[1] > 0.15f, m);
    }
    {   /* aliasing: Distort, full Drive, 5 kHz: its 7th, 35 kHz, would fold to 9.1 kHz */
        float a, f;
        defaults();
        wf_cfg.ds_mix = 1.0f;
        wf_cfg.ds_type = WF_DS_DISTORT;
        wf_cfg.ds_drive = 1.0f;
        wf_cfg.ds_tone = 1.0f;
        fill_sine(n, 5000.0f, 0.3f);
        run(n);
        f = tone_at(SR / 10, 8192, 5000.0f);
        a = tone_at(SR / 10, 8192, WV_SR - 35000.0f);
        snprintf(m, sizeof m, "Distort at 5 kHz: the 7th's alias %.1f dB under the note",
                 20.0f * 0.30103f * fm_log2f(f / (a + 1e-9f)));
        check(a < f * 0.03f, m);
    }
    {   /* Crush at full Drive: a handful of levels */
        int32_t seen[64];
        int ns = 0, k, over = 0;
        defaults();
        wf_cfg.ds_mix = 1.0f;
        wf_cfg.ds_type = WF_DS_CRUSH;
        wf_cfg.ds_drive = 1.0f;
        wf_cfg.ds_tone = 1.0f;
        wf_cfg.mo_mix = 0.0f;
        fill_sine(n, 220.0f, 0.6f);
        /* (the DC blocker and the low-pass smear the steps: look before them, at what the hold holds) */
        run(n);
        for (i = SR / 10; i < SR / 10 + 4410u; i++) {
            int32_t v = (int32_t)(at(i, 0) * 16.0f + (at(i, 0) < 0.0f ? -0.5f : 0.5f));
            for (k = 0; k < ns && seen[k] != v; k++)
                ;
            if (k == ns) {
                if (ns < 64)
                    seen[ns++] = v;
                else
                    over = 1;
            }
        }
        snprintf(m, sizeof m, "Crush: few levels (%d at 1/16 resolution)", ns);
        check(!over && ns <= 24, m);
    }
}

static void test_modulation(void)
{
    char m[160];
    uint32_t t, i, n = SR * 2;
    for (t = 0; t < WF_MO_NTYPE; t++) {
        float db, diff = 0.0f, pk;
        uint32_t r = 777;
        defaults();
        wf_cfg.mo_mix = 1.0f;
        wf_cfg.mo_type = (uint8_t)t;
        wf_cfg.mo_depth = 1.0f;
        wf_cfg.mo_hz = wf_rate_hz(0.6f);
        for (i = 0; i < n; i++) {        /* noise, -16 dB */
            r = r * 1664525u + 1013904223u;
            buf[2 * i] = buf[2 * i + 1] = (int32_t)((float)(int32_t)r * (1.0f / 2147483648.0f) * 0.27f * 8388607.0f);
        }
        run(n);
        db = 20.0f * 0.30103f * fm_log2f(rms(SR / 4, SR, 0) / (0.27f * 0.57735f));
        pk = peak_abs(0, n);
        for (i = SR / 4; i < SR / 4 + SR; i++)
            diff += fm_fabsf(at(i, 0) - at(i, 1));
        snprintf(m, sizeof m, "%s: %.1f dB against the dry, its peak %.2f, left and right differ", WF_MO_NAME[t], db, pk);
        check(db > -6.0f && db < 6.0f && pk < 0.99f && diff > 0.01f * (float)SR, m);
    }
    for (t = WF_MO_FLANGER; t <= WF_MO_PHASER; t++) {   /* Mix 50: notches, a sine moves in and out of them */
        float lo = 99.0f, hi = 0.0f;
        uint32_t w;
        defaults();
        wf_cfg.mo_mix = 0.5f;
        wf_cfg.mo_type = (uint8_t)t;
        wf_cfg.mo_depth = 1.0f;
        wf_cfg.mo_hz = 0.5f;
        fill_sine(n, 1000.0f, 0.2f);
        run(n);
        for (w = 0; w < 40u; w++) {
            float v = rms(SR / 4 + w * (SR / 24), SR / 100, 0);
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        snprintf(m, sizeof m, "%s at Mix 50: the note swells and dips %.1f dB", WF_MO_NAME[t],
                 20.0f * 0.30103f * fm_log2f(hi / lo));
        check(hi > lo * 4.0f, m);
    }
}

/* switching while it plays: the delay's Time glide into Ping-pong's shorter limit, and Times whose read
 * lands exactly on the line's end, stay inside the line (the sanitizer build checks every read); the
 * distortion comes on and goes off without a dropout or a jump in the dry */
static void test_switching(void)
{
    uint32_t i, n = SR, sw = 64u * 344u;     /* (on a block's start, half a second in) */
    float d, steady = 0.0f, worst = 0.0f;
    char m[160];
    defaults();
    wf_cfg.dl_mix = 0.5f;
    wf_cfg.dl_ms = 2000.0f;
    wf_cfg.dl_fb = 0.5f;
    fill_click(n);
    run(n);
    wf_cfg.dl_mode = WF_DL_PINGPONG;
    fill_click(n);
    run(n);
    check(at(n - 1, 1) == at(n - 1, 1), "Delay 2 s, then Ping-pong: plays on (reads in the line)");
    defaults();
    wf_cfg.dl_mix = 0.5f;
    wf_cfg.dl_ms = 340.0f;              /* (a read exactly at the line's end, once a trip) */
    wf_cfg.dl_fb = 0.5f;
    fill_click(n);
    run(3 * n / 4);
    check(at(10, 0) == at(10, 0), "Delay 340 ms: plays (reads in the line)");
    for (i = 0; i < 2; i++) {           /* on, then off */
        uint32_t j;
        defaults();
        wf_cfg.ds_type = WF_DS_DISTORT;
        wf_cfg.ds_drive = 0.0f;
        wf_cfg.ds_tone = 1.0f;
        wf_cfg.ds_mix = i ? 0.01f : 0.0f;
        fill_sine(n, 440.0f, 0.4f);
        for (j = 0; j < n; j += 64) {
            if (j == sw)
                wf_cfg.ds_mix = i ? 0.0f : 0.01f;
            wf_process(buf + 2 * j, 64);
        }
        steady = worst = 0.0f;
        for (j = 1000; j < sw - 1000; j++)
            d = fm_fabsf(at(j, 0) - at(j - 1, 0)), steady = d > steady ? d : steady;
        for (j = sw - 64; j < sw + 256; j++)
            d = fm_fabsf(at(j, 0) - at(j - 1, 0)), worst = d > worst ? d : worst;
        snprintf(m, sizeof m, "Distortion switched %s mid-note: the largest step %.4f, %.4f steady",
                 i ? "off" : "on", worst, steady);
        check(worst < 1.5f * steady, m);
    }
}

int main(void)
{
    test_off();
    test_reverb();
    test_tail();
    test_shimmer();
    test_delay();
    test_distortion();
    test_modulation();
    test_switching();
    if (fails)
        printf("wind_fx: %d FAILED\n", fails);
    else
        printf("wind_fx: all passed\n");
    return fails != 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* Wind control, the mod matrix and the instrument (wind_app), on the host, linked without libm:
 *   wind_ctl   channel filter, the note stack (last note wins, falling back without a new attack, the
 *              oldest dropped when full), breath from CC2 (7- and 14-bit), another CC or pressure, its
 *              curve and smoothing, the panel keys' breath, bite (pitch bend, +-range), glide (legato
 *              only or always, constant time, CC5 setting it), all notes off
 *   wind_matrix  additive destinations clamped, the Amp gain's two directions, slots off
 *   wind_app   MIDI in to sound: breath opens the cutoff, the bite bends in tune, CC5 glides
 *   wind_ctl_test */
#include <stdio.h>
#include <string.h>
#include "wind/wind_ctl.h"
#include "wind/wind_matrix.h"
#include "wind/wind_app.h"
#include "wind/wind_voice.h"
#include "dsp/fastmath.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define NEAR(a, b, tol) (fm_fabsf((a) - (b)) <= (tol))

static wc_out_t o;
static void run(float ms)                             /* wind_ctl alone, in 64-frame blocks */
{
    uint32_t n = (uint32_t)(ms * 0.001f * WV_SR);
    for (; n > 64u; n -= 64u)
        wc_block(64, WV_SR, &o);
    wc_block(n ? n : 1u, WV_SR, &o);
}

static void test_notes(void)
{
    int i;
    wc_init();
    run(1);
    CHECK(o.gate == 0.0f, "gate open with nothing held");
    wc_midi(0x90, 60, 100);
    run(1);
    CHECK(o.gate == 1.0f && o.note == 60.0f, "note on: gate %g note %g", (double)o.gate, (double)o.note);
    CHECK(NEAR(o.velocity, 100.0f / 127.0f, 1e-6f), "velocity %g", (double)o.velocity);
    wc_midi(0x90, 64, 90);                            /* legato: the new note wins */
    run(1);
    CHECK(o.note == 64.0f && o.gate == 1.0f, "second note: %g", (double)o.note);
    wc_midi(0x80, 64, 0);                             /* let go: back to 60, the gate never shut */
    run(1);
    CHECK(o.note == 60.0f && o.gate == 1.0f, "fallback: note %g gate %g", (double)o.note, (double)o.gate);
    wc_midi(0x90, 60, 0);                             /* velocity 0: a note off */
    run(1);
    CHECK(o.gate == 0.0f && o.note == 60.0f, "all up: gate %g, pitch kept %g", (double)o.gate, (double)o.note);
    for (i = 0; i < WC_NSTACK + 2; i++)               /* twelve held: the two oldest go */
        wc_midi(0x90, (uint32_t)(40 + i), 100);
    CHECK(wc_held() == WC_NSTACK, "held %d", wc_held());
    for (i = WC_NSTACK + 1; i >= 2; i--)
        wc_midi(0x80, (uint32_t)(40 + i), 0);
    run(1);
    CHECK(o.gate == 0.0f, "the dropped notes still held");
    wc_midi(0x90, 50, 100);                           /* all notes off */
    wc_midi(0xB0, 123, 0);
    run(1);
    CHECK(o.gate == 0.0f, "all notes off");

    wc_cfg.channel = 3;                               /* channel 3 only */
    wc_midi(0x90, 70, 100);
    run(1);
    CHECK(o.gate == 0.0f, "channel 1 heard on channel 3");
    wc_midi(0x92, 70, 100);
    run(1);
    CHECK(o.gate == 1.0f && o.note == 70.0f, "channel 3 not heard");
    wc_midi(0x82, 70, 0);
    wc_cfg.channel = 0;

    wc_cfg.transpose = -12;                           /* transpose and tune */
    wc_cfg.tune = 25;
    wc_midi(0x90, 70, 100);
    run(1);
    CHECK(NEAR(o.note, 58.25f, 1e-5f), "transpose + tune: %g", (double)o.note);
    wc_midi(0x80, 70, 0);
    wc_cfg.transpose = 0;
    wc_cfg.tune = 0;

    wc_key(65, 1);                                    /* a panel key: the same stack, full breath */
    run(30);
    CHECK(o.gate == 1.0f && o.note == 65.0f && o.breath > 0.99f, "panel key: gate %g note %g breath %g",
          (double)o.gate, (double)o.note, (double)o.breath);
    wc_cfg.key_breath = 0.5f;
    run(30);
    CHECK(NEAR(o.breath, 0.5f, 0.01f), "key breath 50%%: %g", (double)o.breath);
    wc_key(65, 0);
    run(30);
    CHECK(o.gate == 0.0f && o.breath < 0.01f, "panel key up: gate %g breath %g", (double)o.gate, (double)o.breath);
}

static void test_breath(void)
{
    wc_init();
    wc_midi(0xB0, 2, 127);
    run(30);
    CHECK(o.breath > 0.999f, "CC2 127: %g", (double)o.breath);
    wc_midi(0xB0, 2, 0);
    run(1.5f);                                        /* smoothed: not there at once... */
    CHECK(o.breath > 0.3f, "no smoothing: %g after 1.5 ms", (double)o.breath);
    run(30);                                          /* ...but soon */
    CHECK(o.breath < 0.001f, "CC2 0: %g", (double)o.breath);
    wc_midi(0xB0, 2, 64);                             /* 7-bit */
    run(30);
    CHECK(NEAR(o.breath, (float)(64 * 128 + 64) / 16383.0f, 1e-4f), "CC2 64: %g", (double)o.breath);
    wc_midi(0xB0, 34, 0);                             /* its LSB: 14-bit, 64 << 7 */
    run(30);
    CHECK(NEAR(o.breath, 8192.0f / 16383.0f, 1e-4f), "CC2 64 + CC34 0: %g", (double)o.breath);
    wc_midi(0xB0, 34, 100);
    run(30);
    CHECK(NEAR(o.breath, (float)(8192 + 100) / 16383.0f, 1e-4f), "CC34 100: %g", (double)o.breath);
    wc_midi(0xB0, 11, 127);                           /* not the breath CC */
    run(30);
    CHECK(o.breath < 0.6f, "CC11 taken for breath");

    wc_cfg.breath_src = 11;                           /* CC11 as breath */
    wc_midi(0xB0, 11, 127);
    run(30);
    CHECK(o.breath > 0.999f, "CC11 as the source: %g", (double)o.breath);
    wc_cfg.breath_src = WC_SRC_AT;                    /* channel pressure */
    wc_midi(0xD0, 32, 0);
    run(30);
    CHECK(NEAR(o.breath, (float)(32 * 128 + 32) / 16383.0f, 1e-4f), "pressure 32: %g", (double)o.breath);
    CHECK(NEAR(o.pressure, 32.0f / 127.0f, 1e-6f), "pressure source %g", (double)o.pressure);

    wc_cfg.breath_src = 2;                            /* the curve: + more breath early, - less */
    wc_midi(0xB0, 2, 32);
    wc_midi(0xB0, 34, 0);
    wc_cfg.breath_curve = 100;
    run(30);
    CHECK(NEAR(o.breath, fm_powf(0.25f, 0.25f), 0.002f), "curve +100: %g", (double)o.breath);
    wc_cfg.breath_curve = -100;
    run(30);
    CHECK(NEAR(o.breath, 0.25f * 0.25f * 0.25f * 0.25f, 0.002f), "curve -100: %g", (double)o.breath);
    wc_cfg.breath_curve = 0;
    run(30);
    CHECK(NEAR(o.breath, 0.25f, 0.002f), "curve 0: %g", (double)o.breath);
}

static void test_bend(void)
{
    wc_init();
    wc_midi(0x90, 60, 100);
    wc_midi(0xE0, 0x00, 0x40);
    run(1);
    CHECK(o.note == 60.0f && o.bend == 0.0f, "bend centre: %g", (double)o.note);
    wc_midi(0xE0, 0x7F, 0x7F);
    run(1);
    CHECK(NEAR(o.note, 62.0f, 1e-5f) && NEAR(o.bend, 1.0f, 1e-6f), "bite up: %g", (double)o.note);
    wc_midi(0xE0, 0x00, 0x00);
    run(1);
    CHECK(NEAR(o.note, 58.0f, 1e-5f), "bite down: %g", (double)o.note);
    wc_cfg.bend_range = 7;
    run(1);
    CHECK(NEAR(o.note, 53.0f, 1e-5f), "range 7 down: %g", (double)o.note);
    wc_midi(0xE0, 0x00, 0x50);                        /* +2048 / 8191 of 7 */
    run(1);
    CHECK(NEAR(o.note, 60.0f + 7.0f * 2048.0f / 8191.0f, 1e-4f), "part bend: %g", (double)o.note);
}

static void test_glide(void)
{
    wc_init();
    wc_midi(0xB0, 5, 127);                            /* CC5: the glide time, 2 s at full */
    CHECK(wc_take_glide_cc() == 100 && wc_cfg.glide == 100, "CC5 127: glide %d", wc_cfg.glide);
    CHECK(wc_take_glide_cc() == -1, "glide CC taken twice");
    wc_midi(0xB0, 5, 64);
    CHECK(wc_cfg.glide == 50 && wc_take_glide_cc() == 50, "CC5 64: glide %d", wc_cfg.glide);   /* 0.5 s */
    wc_midi(0x90, 60, 100);
    run(1);
    CHECK(o.note == 60.0f, "the first note glides: %g", (double)o.note);
    wc_midi(0x90, 72, 100);                           /* legato: 12 semitones in 0.5 s */
    run(250);
    CHECK(NEAR(o.note, 66.0f, 0.1f), "halfway: %g", (double)o.note);
    run(260);
    CHECK(o.note == 72.0f, "there: %g", (double)o.note);
    wc_midi(0x80, 72, 0);                             /* back down, also legato: 0.5 s again */
    run(125);
    CHECK(NEAR(o.note, 69.0f, 0.1f), "a quarter back: %g", (double)o.note);
    run(400);
    wc_midi(0x80, 60, 0);
    wc_midi(0x90, 48, 100);                           /* detached: a jump */
    run(1);
    CHECK(o.note == 48.0f, "detached note glided: %g", (double)o.note);
    wc_midi(0x80, 48, 0);
    wc_cfg.glide_always = 1;                          /* always: detached notes glide too */
    wc_midi(0x90, 60, 100);
    run(250);
    CHECK(NEAR(o.note, 54.0f, 0.1f), "always: %g after 250 ms", (double)o.note);
    wc_midi(0x80, 60, 0);
    wc_cfg.glide_depth = 40;                          /* the depth: CC5 at full reaches it */
    wc_midi(0xB0, 5, 127);
    CHECK(wc_cfg.glide == 40 && wc_take_glide_cc() == 40, "CC5 127 at depth 40: glide %d", wc_cfg.glide);
    wc_midi(0xB0, 5, 64);
    CHECK(wc_cfg.glide == 20, "CC5 64 at depth 40: glide %d", wc_cfg.glide);
    wc_cfg.glide_depth = 100;
    wc_midi(0xB0, 5, 64);
    wc_take_glide_cc();
    wc_cfg.glide_src = WC_SRC_OFF;                    /* no glide CC */
    wc_midi(0xB0, 5, 0);
    CHECK(wc_cfg.glide == 50, "CC5 heard with the source off");
}

static void test_matrix(void)
{
    wm_slot_t s[WM_NSLOT] = {{WM_SRC_BREATH, WM_DST_CUTOFF, 75}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    float base[WM_NDST] = {0.15f, 0.2f, 0.0f, 0.3f}, src[WM_NSRC] = {0}, out[WM_NDST];
    src[WM_SRC_BREATH] = 0.0f;
    wm_apply(s, base, src, out);
    CHECK(NEAR(out[WM_DST_CUTOFF], 0.15f, 1e-6f) && out[WM_DST_AMP] == 1.0f && NEAR(out[WM_DST_RES], 0.2f, 1e-6f) &&
          NEAR(out[WM_DST_MORPH], 0.3f, 1e-6f), "no breath: %g %g %g %g", (double)out[0], (double)out[1],
          (double)out[2], (double)out[3]);
    src[WM_SRC_BREATH] = 1.0f;
    wm_apply(s, base, src, out);
    CHECK(NEAR(out[WM_DST_CUTOFF], 0.9f, 1e-6f), "full breath: cutoff %g", (double)out[WM_DST_CUTOFF]);
    s[1].src = WM_SRC_BREATH;                         /* two slots on one destination: summed, clamped */
    s[1].dst = WM_DST_CUTOFF;
    s[1].amount = 50;
    wm_apply(s, base, src, out);
    CHECK(out[WM_DST_CUTOFF] == 1.0f, "clamp high: %g", (double)out[WM_DST_CUTOFF]);
    s[1].amount = -100;
    src[WM_SRC_BREATH] = 0.5f;
    wm_apply(s, base, src, out);
    CHECK(NEAR(out[WM_DST_CUTOFF], 0.15f + 0.375f - 0.5f, 1e-6f), "negative amount: %g", (double)out[WM_DST_CUTOFF]);
    s[0].amount = 0;
    s[1].amount = -100;
    src[WM_SRC_BREATH] = 1.0f;
    wm_apply(s, base, src, out);
    CHECK(out[WM_DST_CUTOFF] == 0.0f, "clamp low: %g", (double)out[WM_DST_CUTOFF]);
    s[1].amount = 0;
    s[2].src = WM_SRC_BREATH;                         /* Amp: breath dynamics */
    s[2].dst = WM_DST_AMP;
    s[2].amount = 100;
    src[WM_SRC_BREATH] = 0.25f;
    wm_apply(s, base, src, out);
    CHECK(NEAR(out[WM_DST_AMP], 0.25f, 1e-6f), "amp 100: %g", (double)out[WM_DST_AMP]);
    s[2].amount = 40;
    wm_apply(s, base, src, out);
    CHECK(NEAR(out[WM_DST_AMP], 0.6f + 0.4f * 0.25f, 1e-6f), "amp 40: %g", (double)out[WM_DST_AMP]);
    s[2].amount = -100;
    wm_apply(s, base, src, out);
    CHECK(NEAR(out[WM_DST_AMP], 0.75f, 1e-6f), "amp -100: %g", (double)out[WM_DST_AMP]);
    s[2].src = WM_SRC_OFF;                            /* a slot off does nothing */
    wm_apply(s, base, src, out);
    CHECK(out[WM_DST_AMP] == 1.0f, "slot off: amp %g", (double)out[WM_DST_AMP]);
}

/* ------------------------------------------------------------ wind_app --- */
#define MQ 64
static uint32_t mq[MQ], mq_w, mq_r;
static int midi_in(uint32_t *p)
{
    if (mq_r == mq_w)
        return 0;
    *p = mq[mq_r++ % MQ];
    return 1;
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2) { mq[mq_w++ % MQ] = (st >> 4) | st << 8 | d1 << 16 | d2 << 24; }

#define SR 44118
static float buf[SR];
static int32_t blk[2 * 256];
static void play(uint32_t n)                          /* n frames of the instrument into buf */
{
    uint32_t at = 0;
    while (at < n) {
        uint32_t k = n - at > 256u ? 256u : n - at, i;
        wa_render(blk, k, 4096u, midi_in);
        for (i = 0; i < k; i++)
            buf[at + i] = (float)blk[2 * i] * (1.0f / 8388608.0f);
        at += k;
    }
}
static float brightness(uint32_t a, uint32_t n)
{
    float d = 0.0f, s = 0.0f;
    uint32_t i;
    for (i = a + 1; i < a + n; i++) {
        d += (buf[i] - buf[i - 1]) * (buf[i] - buf[i - 1]);
        s += buf[i] * buf[i];
    }
    return d / (s + 1e-12f);
}
static float hz(uint32_t n)                           /* from the rising zero crossings */
{
    float first = -1.0f, last = 0.0f;
    uint32_t i, rises = 0;
    for (i = 1; i < n; i++)
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) {
            last = (float)(i - 1) + buf[i - 1] / (buf[i - 1] - buf[i]);
            first = first < 0.0f ? last : first;
            rises++;
        }
    return rises > 1 ? (float)(rises - 1) * WV_SR / (last - first) : 0.0f;
}
static float cents(float a, float b) { return 1200.0f * fm_log2f(a / b); }

static void test_app(void)
{
    float dark, bright, f;
    int i;
    wa_init();
    play(SR / 10);
    CHECK(wa_state.level == 0.0f && wa_state.gate == 0.0f, "sound before a note: %g", (double)wa_state.level);
    midi(0x90, 57, 100);                              /* a note, no breath yet: the cutoff stays shut */
    midi(0xB0, 2, 0);
    play(SR / 4);
    CHECK(wa_state.gate == 1.0f && NEAR(wa_state.cutoff, 0.15f, 1e-6f), "gate %g cutoff %g", (double)wa_state.gate,
          (double)wa_state.cutoff);
    dark = brightness(SR / 8, SR / 8);
    midi(0xB0, 2, 127);                               /* full breath: CC2 opens it */
    play(SR / 4);
    CHECK(NEAR(wa_state.cutoff, 0.9f, 1e-3f) && wa_state.breath > 0.99f, "breath: cutoff %g", (double)wa_state.cutoff);
    bright = brightness(SR / 8, SR / 8);
    CHECK(bright > 4.0f * dark, "breath doesn't brighten it: %g -> %g", (double)dark, (double)bright);
    CHECK(wa_state.level > 0.05f && wa_state.level < 1.0f, "level %g", (double)wa_state.level);

    /* in tune, and the bite bends +-2: a square (Morph 50) under the 24 dB filter near the note */
    wa_set(P_MORPH, 50);
    wa_set(P_M1AMT, 0);
    wa_set(P_CUTOFF, 30);
    midi(0x90, 69, 100);
    play(SR / 2);
    play(SR);
    f = hz(SR);
    CHECK(fm_fabsf(cents(f, 440.0f)) < 1.0f, "A4 at %g Hz", (double)f);
    midi(0xE0, 0x7F, 0x7F);
    play(SR / 2);
    play(SR);
    f = hz(SR);
    CHECK(fm_fabsf(cents(f, 440.0f) - 200.0f) < 1.0f, "bite up: %g Hz, %g cents", (double)f, (double)cents(f, 440.0f));
    midi(0xE0, 0x00, 0x00);
    play(SR / 2);
    play(SR);
    f = hz(SR);
    CHECK(fm_fabsf(cents(f, 440.0f) + 200.0f) < 1.0f, "bite down: %g Hz", (double)f);
    midi(0xE0, 0x00, 0x40);

    /* CC5 sets the glide; the next legato note slides */
    midi(0xB0, 5, 64);
    play(64);
    CHECK(wa_take_glide() == 50, "CC5 not seen by the app");
    midi(0x90, 81, 100);
    play(SR / 4);                                     /* halfway along 0.5 s */
    CHECK(NEAR(wa_state.note, 75.0f, 0.2f), "glide halfway: %g", (double)wa_state.note);
    play(SR / 3);
    CHECK(wa_state.note == 81.0f, "glide end: %g", (double)wa_state.note);

    for (i = 0; i < 128; i++)                         /* everything up: the gate shuts, it falls quiet */
        midi(0x80, (uint32_t)i, 0), play(1);
    play(SR / 4);
    CHECK(wa_state.gate == 0.0f && wa_state.level < 0.001f, "release: gate %g level %g", (double)wa_state.gate,
          (double)wa_state.level);
    wa_key(60, 1);                                    /* the panel's keys play it too */
    play(SR / 10);
    CHECK(wa_state.gate == 1.0f && wa_state.note == 60.0f && wa_state.level > 0.05f, "panel key: level %g",
          (double)wa_state.level);
    wa_panic();
    play(SR / 4);
    CHECK(wa_state.gate == 0.0f && wa_state.level < 0.001f, "panic: level %g", (double)wa_state.level);
    CHECK(wa_state.midi_seen > 100u, "MIDI counted: %u", (unsigned)wa_state.midi_seen);
}

int main(void)
{
    test_notes();
    test_breath();
    test_bend();
    test_glide();
    test_matrix();
    test_app();
    if (fails)
        printf("%d check(s) failed\n", fails);
    else
        printf("  wind control, matrix and instrument: all checks passed\n");
    return fails ? 1 : 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* The USB audio input (usb.c, FELUCCA_UAC) against clocks that do not agree: the FM-1's I2S renders
 * 512-frame halves (two 256-frame blocks back to back) at RATE_IN while the host takes a 1 ms packet
 * of the 44.1 pattern by its own clock. A 1 kHz sine goes in; after 20 s of settling it must come
 * out at 1 kHz by the host's clock (the resampler took up the difference), clean, with no packet
 * short of frames and the ring's low point near UA_MID. X0X's I2S runs at ~44,145..44,180 Hz. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_UAC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"

static int16_t outL[44100 * 40];

static int run(double rate_in)
{
    double t_in = 0, t_out = 0, ph = 0, err = 0, sig = 0, f, w;
    static int32_t blk[512];
    long nout = 0, a = 44100L * 20, i, zc = 0;
    uint32_t under_settled = 0;
    memset(&uac, 0, sizeof uac);
    memset(&usb, 0, sizeof usb);
    ua_w = ua_r = 0;
    usb.config = 1;
    uac.alt = 1;
    uac.flowing = 1;
    uac.fill_lo = 0xFFFFFFFFu;
    while (t_out < 40.0) {
        if (t_in <= t_out) {                             /* a half: two blocks, back to back */
            int b, k;
            uac_render_start();
            for (b = 0; b < 2; b++) {
                for (k = 0; k < 256; k++, ph += 1000.0 / rate_in)
                    blk[2 * k] = blk[2 * k + 1] = (int32_t)(sin(2 * M_PI * ph) * 0.5 * 8388607.0) & ~0xFF;
                uac_tap(blk, 256);
            }
            t_in += 512.0 / rate_in;
        } else {                                         /* the host's 1 ms frame */
            uint32_t d[UA_MAXF], n, u = uac.underruns, k;
            n = uac_packet(d);
            if (t_out > 10.0 && uac.underruns != u)
                under_settled++;
            for (k = 0; k < n && nout < 44100L * 40; k++)
                outL[nout++] = (int16_t)(d[k] & 0xFFFFu);
            t_out += 0.001;
        }
    }
    for (i = a + 1; i < nout; i++)
        zc += (outL[i - 1] < 0) != (outL[i] < 0);
    f = zc / 2.0 / ((nout - a) / 44100.0);
    w = 2 * M_PI * f / 44100.0;
    for (i = a + 2; i < nout; i++) {                     /* a sine's next sample from the two before */
        double pred = 2 * cos(w) * outL[i - 1] - outL[i - 2];
        err += (outL[i] - pred) * (outL[i] - pred);
        sig += (double)outL[i] * outL[i];
    }
    printf("  in %.0f Hz: out %.3f Hz, residual %.1f dB, %u short packets after 10 s, low point %u (UA_MID %d)\n",
           rate_in, f, 10 * log10(err / sig), under_settled, uac.fill_min, UA_MID);
    return fabs(f - 1000.0) < 0.05 && 10 * log10(err / sig) < -70.0 && under_settled == 0 &&
           uac.fill_min > UA_MID / 2u && uac.fill_min < 2u * UA_MID;
}

/* the FM-1's own output while the host records (Speaker Auto): after uac_tap, so the host keeps every
 * sample; silent after one block's ramp, the ramp without a step, back the same way when the host stops,
 * and bit-exact whenever it isn't muting (On, or nobody recording) */
static int local(void)
{
    static int32_t blk[512], ref[512];
    int ok = 1, b, k, mute;
    double ph = 0, maxstep = 0;
    memset(&uac, 0, sizeof uac);
    memset(&usb, 0, sizeof usb);
    ua_w = ua_r = 0;
    uac_local_g = 1.0f;
    usb.config = 1;
    uac.alt = 1;
    uac.flowing = 1;
    for (mute = 0; mute < 2; mute++)
        for (b = 0; b < 8; b++) {                         /* recording: blocks 0..5; the host stops at 6 */
            uac.alt = b < 6;
            uac_render_start();
            for (k = 0; k < 256; k++, ph += 1000.0 / 44100.0)
                blk[2 * k] = blk[2 * k + 1] = (int32_t)(sin(2 * M_PI * ph) * 0.5 * 8388607.0) * 256;
            memcpy(ref, blk, sizeof ref);
            {
                uint32_t w0 = ua_w, i, peak = 0;
                uac_tap(blk, 256);
                for (i = w0; i != ua_w; i++) {            /* what the host gets of this block */
                    int16_t v = (int16_t)(ua_ring[i & (UA_N - 1u)] & 0xFFFFu);
                    if ((uint32_t)abs(v) > peak)
                        peak = (uint32_t)abs(v);
                }
                if (b < 6 && peak < 10000u)              /* (the sine at half scale: ~16,000) */
                    ok = 0, printf("  local: block %d not sent to the host (peak %u)\n", b, peak);
                ua_r = ua_w;                              /* (the host took it) */
            }
            uac_local_out(blk, 256, mute);
            if (!mute || b == 7) {                        /* not muting (and after the ramp back): untouched */
                if (memcmp(blk, ref, sizeof blk))
                    ok = 0, printf("  local: block %d changed (mute %d)\n", b, mute);
            } else if (b >= 1 && b <= 5) {                /* muted, after the first block's ramp: silent */
                for (k = 0; k < 512; k++)
                    if (blk[k])
                        ok = 0;
            } else {                                      /* a ramp: the gain moves by at most a step a frame */
                for (k = 0; k < 256; k++) {
                    double g = ref[2 * k] ? (double)blk[2 * k] / ref[2 * k] : -1;
                    if (g >= 0 && (g < -0.001 || g > 1.001))
                        ok = 0;
                    if (k && ref[2 * k] && ref[2 * k - 2]) {
                        double g0 = (double)blk[2 * k - 2] / ref[2 * k - 2], d = fabs(g - g0);
                        if (fabs(ref[2 * k]) > 1e6 && fabs(ref[2 * k - 2]) > 1e6 && d > maxstep)
                            maxstep = d;
                    }
                }
            }
        }
    printf("  local out: the largest gain step in a ramp %.4f (one block: %.4f)\n", maxstep, 1.0 / 256);
    if (maxstep > 2.0 / 256 || maxstep == 0)
        ok = 0;
    if (uac.alt == 0 && uac_recording())
        ok = 0;
    return ok;
}

int main(void)
{
    int ok = local() & run(44145.0) & run(44100.0) & run(44300.0);
    printf(ok ? "uac: ok\n" : "uac: FAILED\n");
    return !ok;
}

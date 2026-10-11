/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects after the voice: a delay, then a reverb, both at half the FM-1's rate (22,059 Hz, 16-bit
 * lines in the .pool RAM region), mixed back with the dry voice at full rate.
 *
 *   Reverb   one tank, several voicings (Type): pre-delay, a chain of modulated input allpasses, then a
 *            ring of four blocks (a modulated allpass, an allpass, a delay, damping and a low-end
 *            control), after Griesinger and Dattorro, as Mutable's Clouds and Rings reverbs are, widened
 *            to four blocks. Cloud: eight long, moving input diffusers into a big ring, after Strymon's
 *            description of BigSky's Cloud. Shimmer: Cloud with an octave-up pitch shifter in the ring.
 *   Delay    one mono line: Digital, Tape (wow and flutter, darker, saturating), Analog (darker still)
 *            or Ping-pong (left at the time, right at twice it). Time changes glide.
 *
 * Mix (both): 0 dry, 50 dry and wet at full level, 100 wet only. At 0 the effect is off: no CPU, and the
 * output is the voice, bit for bit. Settings come from the UI (wf_cfg), the audio runs in wa_render. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { WF_RV_CLOUD, WF_RV_HALL, WF_RV_PLATE, WF_RV_ROOM, WF_RV_SHIMMER, WF_RV_NTYPE };
enum { WF_DL_DIGITAL, WF_DL_TAPE, WF_DL_ANALOG, WF_DL_PINGPONG, WF_DL_NMODE };
extern const char *const WF_RV_NAME[WF_RV_NTYPE];
extern const char *const WF_DL_NAME[WF_DL_NMODE];

#define WF_PRE_MAX_MS 500
#define WF_TIME_MAX_MS 2000            /* the delay (Ping-pong: 1,100 ms, its right tap at twice it) */

typedef struct {
    uint8_t rv_type;
    float rv_mix, rv_decay, rv_tone, rv_size, rv_mod;   /* 0..1 */
    float rv_low;                      /* -1..1: the bass decays faster .. slower than the rest */
    float rv_pre_ms;
    uint8_t dl_mode;
    float dl_mix, dl_fb;               /* 0..1 */
    float dl_ms;
} wf_cfg_t;
extern volatile wf_cfg_t wf_cfg;

void wf_init(void);
/* in place: n stereo frames, 24-bit in int32 (the voice: L = R) */
void wf_process(int32_t *lr, uint32_t n);
float wf_decay_s(float decay);         /* Decay 0..1 as the reverb time (RT60, s): 0.2 .. 60 */
uint32_t wf_reverb_span(void);         /* the reverb's lines at their longest, in samples (tests) */

#ifdef __cplusplus
}
#endif

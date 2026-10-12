/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects after the voice, in this order: a distortion and a modulation (chorus, flanger, phaser) at
 * the FM-1's rate, then a delay and a reverb at half of it (22,059 Hz, 16-bit lines in the .pool RAM
 * region: the reverb's half floats, the delay's fixed point), mixed back with the dry signal at full rate.
 *
 *   Distortion  Overdrive (soft, a touch asymmetric), Distort (hard clipping), Fuzz (hard on one side,
 *            soft on the other, more gain), Fold (a sine wavefolder) or Crush (fewer bits, a lower rate).
 *            Drive: how hard; Tone: a low-pass after it. Two times oversampled (Crush aliases on
 *            purpose), the level held near the dry signal's as Drive goes up.
 *   Modulation  Chorus (two moving taps, left and right in quadrature), Ensemble (three taps 120 degrees
 *            apart plus a fast vibrato, the string machines' chorus), Flanger (a short swept delay fed
 *            back) or Phaser (six swept allpasses fed back). Rate and Depth. Mono in, stereo out: the
 *            delay and the reverb take the sum.
 *   Reverb   one tank, several voicings (Type): pre-delay, a chain of modulated input allpasses, then a
 *            ring of four blocks (a modulated allpass, an allpass, a delay, damping and a low-end
 *            control; the input goes into each, and the outputs are tapped all round it), after
 *            Griesinger and Dattorro, as Mutable's Clouds and Rings reverbs are, widened to four blocks.
 *            Cloud: eight long, moving input diffusers into a big ring, after Strymon's description of
 *            BigSky's Cloud. Shimmer: Cloud whose tail goes through an octave-up pitch
 *            shifter and back into its input, so it climbs an octave at a time (as BigSky's and
 *            Valhalla's shimmers do), high-passed so the bass stays clear.
 *   Delay    one mono line: Digital, Tape (wow and flutter, darker, saturating), Analog (darker still)
 *            or Ping-pong (left at the time, right at twice it). Time changes glide.
 *
 * Mix (each): 0 dry, 50 dry and wet at full level, 100 wet only. At 0 the effect is off: no CPU, and the
 * output is the voice, bit for bit. Settings come from the UI (wf_cfg), the audio runs in wa_render. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { WF_RV_CLOUD, WF_RV_HALL, WF_RV_PLATE, WF_RV_ROOM, WF_RV_SHIMMER, WF_RV_NTYPE };
enum { WF_DL_DIGITAL, WF_DL_TAPE, WF_DL_ANALOG, WF_DL_PINGPONG, WF_DL_NMODE };
enum { WF_DS_OVERDRIVE, WF_DS_DISTORT, WF_DS_FUZZ, WF_DS_FOLD, WF_DS_CRUSH, WF_DS_NTYPE };
enum { WF_MO_CHORUS, WF_MO_ENSEMBLE, WF_MO_FLANGER, WF_MO_PHASER, WF_MO_NTYPE };
extern const char *const WF_RV_NAME[WF_RV_NTYPE];
extern const char *const WF_DL_NAME[WF_DL_NMODE];
extern const char *const WF_DS_NAME[WF_DS_NTYPE];
extern const char *const WF_MO_NAME[WF_MO_NTYPE];
#define WF_DS_LATENCY 5                /* the distortion's oversampling, in samples (the dry is delayed to match) */

#define WF_PRE_MAX_MS 500
#define WF_TIME_MAX_MS 2000            /* the delay (Ping-pong: 1,100 ms, its right tap at twice it) */

typedef struct {
    uint8_t rv_type;
    float rv_mix, rv_decay, rv_tone, rv_size, rv_mod;   /* 0..1 */
    float rv_low;                      /* -1..1: the bass decays faster .. slower than the rest */
    float rv_pre_ms;
    float rv_shim;                     /* 0..1: how much of the tail climbs an octave (Shimmer) */
    uint8_t dl_mode;
    float dl_mix, dl_fb;               /* 0..1 */
    float dl_ms;
    uint8_t ds_type;
    float ds_mix, ds_drive, ds_tone;   /* 0..1 */
    uint8_t mo_type;
    float mo_mix, mo_depth;            /* 0..1 */
    float mo_hz;                       /* the LFO's rate */
} wf_cfg_t;
extern volatile wf_cfg_t wf_cfg;

void wf_init(void);
/* in place: n stereo frames, 24-bit in int32 (the voice: L = R) */
void wf_process(int32_t *lr, uint32_t n);
float wf_decay_s(float decay);         /* Decay 0..1 as the reverb time (RT60, s): 0.2 .. 60 */
float wf_rate_hz(float rate);          /* the modulation's Rate 0..1 as its LFO's (Hz): 0.05 .. 10 */
uint32_t wf_reverb_span(void);         /* the reverb's lines at their longest, in samples (tests) */

#ifdef __cplusplus
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only */
/* The fixed sweep that upstream Plaits (wind_ref.cc), the wind voice (wind_voice_test.c) and its
 * WebAssembly build (wind_wasm_test.mjs) all render: one engine, SWEEP_STEPS steps in a row of
 * SWEEP_LEN samples each, rendered in Plaits' 12-sample blocks. */
#define SWEEP_STEPS 64
#define SWEEP_LEN 960

static void sweep_params(int s, float *note, float *timbre, float *morph, float *harmonics)
{
    *note = 24.0f + (float)s * 1.125f;              /* C1 up past C6 */
    *timbre = (float)(s % 16) / 15.0f;
    *morph = (float)((s * 5) % 16) / 15.0f;
    *harmonics = (float)((s * 11) % 16) / 15.0f;
}

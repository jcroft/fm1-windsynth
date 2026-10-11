/* SPDX-License-Identifier: GPL-3.0-only */
/* Windsynth in the browser (FoMni's emulator, from X0X's): the host simulator (host/windsynth_host.c: the whole app, UI and instrument,
 * against a simulated FM-1) compiled to WebAssembly and run in an AudioWorklet. The worklet asks
 * for audio; producing it advances the device's clock one millisecond at a time, which runs the
 * engine every 256 samples (the I2S half buffer) and the UI every 16 ms, as on the FM-1. The page
 * sends input (and MIDI from Web MIDI), and reads the screen, the lights and the saved objects back
 * through these exports.
 *
 *   web/emu/build.sh   ->  build/emu/windsynth.wasm (+ the page) */
#include <stdint.h>

static void web_audio(const int32_t *blk, uint32_t n);
#include "../../host/windsynth_host.c"

#undef __attribute__

/* output ring, stereo float, filled by run_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r;
static void web_audio(const int32_t *blk, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)blk[2 * k] / 8388608.0f;
        ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
    }
}

static float out_l[1024], out_r[1024];
__attribute__((used, visibility("default"))) float *web_out_l(void) { return out_l; }
__attribute__((used, visibility("default"))) float *web_out_r(void) { return out_r; }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
__attribute__((used, visibility("default"))) void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        run_ms(1);
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

__attribute__((used, visibility("default"))) void web_boot(void) { boot(); }
__attribute__((used, visibility("default"))) void web_buttons(uint32_t m) { held_btn = m; }
__attribute__((used, visibility("default"))) void web_keys(uint32_t m) { held_keys = m; }
__attribute__((used, visibility("default"))) void web_enc(int role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += n;
}
/* one MIDI message from the page (Web MIDI), as a USB-MIDI packet into the same queue the device's USB
 * and TRS jack fill */
__attribute__((used, visibility("default"))) void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >= 0xF0u ? 0x0Fu : st >> 4;
    if (mi_w - mi_r < MQ)
        min_q[mi_w++ % MQ] = cin | (st & 0xFFu) << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
}
__attribute__((used, visibility("default"))) void web_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
__attribute__((used, visibility("default"))) uint32_t web_lit_buttons(void) { return lit_btn; }
__attribute__((used, visibility("default"))) uint32_t web_lit_keys(void) { return lit_keys; }
__attribute__((used, visibility("default"))) uint32_t web_dim_keys(void) { return dim_keys; }
__attribute__((used, visibility("default"))) uint16_t *web_fb(void) { return fb; }
__attribute__((used, visibility("default"))) uint32_t web_blits(void) { return blits; }
__attribute__((used, visibility("default"))) uint32_t web_now(void) { return now_ms; }
__attribute__((used, visibility("default"))) uint32_t web_cpu(void) { return cpu_pct; }

/* saved objects: the page keeps them (localStorage) and gives them back before boot */
__attribute__((used, visibility("default"))) uint8_t *web_store(uint32_t obj) { return obj < OBJ_NOBJ ? store[obj] : 0; }
__attribute__((used, visibility("default"))) int web_store_len(uint32_t obj) { return obj < OBJ_NOBJ ? store_len[obj] : -1; }
__attribute__((used, visibility("default"))) void web_store_set_len(uint32_t obj, int n)
{
    if (obj < OBJ_NOBJ)
        store_len[obj] = n < 0 ? -1 : n > (int)PLAT_STORE_MAX ? (int)PLAT_STORE_MAX : n;
}
__attribute__((used, visibility("default"))) uint32_t web_store_writes(void) { return store_writes; }
__attribute__((used, visibility("default"))) uint32_t web_nobj(void) { return OBJ_NOBJ; }
__attribute__((used, visibility("default"))) uint32_t web_store_max(void) { return PLAT_STORE_MAX; }

/* the instrument's state for the page's monitor: note, gate, breath, bend, cutoff, res, amp, morph,
 * level, the sounding note (-1 none) and the MIDI packets received */
static float state[11];
__attribute__((used, visibility("default"))) float *web_state(void)
{
    state[0] = wa_state.note;
    state[1] = wa_state.gate;
    state[2] = wa_state.breath;
    state[3] = wa_state.bend;
    state[4] = wa_state.cutoff;
    state[5] = wa_state.res;
    state[6] = wa_state.amp;
    state[7] = wa_state.morph;
    state[8] = wa_state.level;
    state[9] = (float)wa_state.top;
    state[10] = (float)wa_state.midi_seen;
    return state;
}

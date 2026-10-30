/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample layer's player (wind_sample), on the host, linked without libm:
 *
 *   crc        ws_crc32 is zlib's
 *   bank       a good bank is taken; a bad CRC, a sample past the bank's end, a loop past the sample's
 *              end, an odd 16-bit offset are not (and then nothing plays)
 *   pitch      a 441 Hz sample (root 69) plays at 441 x 2^((note - 69) / 12) within a cent, from three
 *              octaves down to two up; Tune adds; Track off ignores the note; fine tune counts
 *   loop       Loop: it plays on past its end, with no click at the seam; Off: it ends, and stays ended
 *              until a phrase starts; Ping-pong: forward, back, forward, inside the loop
 *   start      Start 50% begins half way; Trigger Note restarts on a new note, Phrase doesn't
 *   mu-law     an 8-bit sample's level is its 16-bit twin's, within 4% (its step near the top)
 *   declick    a restart mid-sound (Trigger Note), Slot Off and a slot change crossfade: no step bigger than
 *              the wave's own; a one-shot's end fades
 *   long       far into a long sample, ping-pong goes back as fast as it goes forward; a loop start that
 *              would wrap a sum is refused
 */
#include <stdio.h>
#include <string.h>
#include "wind/wind_sample.h"
#include "wind/wind_voice.h"
#include "wind/wind_math.h"

#define SR WV_SR
#define BLK 64
#define LEN 4400                         /* ~0.1 s at 44.1 kHz: 441 Hz, 44 whole periods of 100 frames */
static uint8_t bank[WS_DATA + 3 * LEN * 2 + 16];
static float out[44118 * 2];
static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok" : "FAIL", what);
    fails += !ok;
}

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

static float sine(uint32_t k) { return 0.5f * fm_sinf(6.2831853f * (float)(k % 100u) / 100.0f); }

static uint8_t ulaw_of(float v)          /* G.711 */
{
    int s = (int)(v * 32768.0f), sign = s < 0 ? 0x80 : 0, e = 7;
    s = (s < 0 ? -s : s);
    s = (s > 32635 ? 32635 : s) + 0x84;
    while (e > 0 && !(s & (0x4000 >> (7 - e))))
        e--;
    return (uint8_t)~(sign | e << 4 | ((s >> (e + 3)) & 15));
}

/* slot 1: the sine, 16-bit, loop ls..le; slot 2: empty; slot 3: the same as mu-law, no loop points */
static uint32_t make(uint32_t ls, uint32_t le)
{
    uint8_t *e;
    uint32_t k, off = WS_DATA, size;
    memset(bank, 0, sizeof bank);
    wr32(bank, WS_MAGIC);
    wr16(bank + 4, WS_VERSION);
    wr16(bank + 6, WS_NSLOT);
    for (k = 0; k < LEN; k++)
        wr16(bank + off + 2 * k, (uint32_t)(int32_t)(sine(k) * 32767.0f));
    for (k = 0; k < LEN; k++)
        bank[off + 2 * LEN + k] = ulaw_of(sine(k));
    e = bank + WS_HDR;
    wr32(e, off);
    wr32(e + 4, LEN);
    wr32(e + 8, ls);
    wr32(e + 12, le);
    wr16(e + 16, 44100);
    e[18] = WS_FMT_S16;
    e[19] = 69;
    memcpy(e + 24, "SINE", 4);
    e = bank + WS_HDR + 2 * WS_ENT;
    wr32(e, off + 2 * LEN);
    wr32(e + 4, LEN);
    wr16(e + 16, 44100);
    e[18] = WS_FMT_ULAW;
    e[19] = 69;
    size = off + 3 * LEN;
    wr32(bank + 8, size);
    wr32(bank + 12, ws_crc32(bank + WS_HDR, size - WS_HDR, 0));
    return size;
}

/* n frames at note, the first block with trig */
static int render(float note, uint32_t trig, uint32_t n)
{
    uint32_t i;
    int any = 0;
    memset(out, 0, sizeof out);
    for (i = 0; i < n; i += BLK) {
        any |= ws_render(note, i ? 0u : trig, out + i, BLK);
    }
    return any;
}

/* the frequency of out[from..to), from its rising zero crossings */
static float freq(uint32_t from, uint32_t to)
{
    float first = -1.0f, last = 0.0f;
    int n = 0;
    uint32_t i;
    for (i = from; i + 1u < to; i++)
        if (out[i] < 0.0f && out[i + 1] >= 0.0f) {
            float t = (float)i + out[i] / (out[i] - out[i + 1]);
            if (first < 0.0f)
                first = t;
            else
                n++;
            last = t;
        }
    return n ? (float)n * SR / (last - first) : 0.0f;
}

static float cents(float got, float want) { return 1731.234f * (got / want - 1.0f); }   /* (near 1: exact enough) */

int main(void)
{
    char m[160];
    uint32_t size, i;
    int k;
    float lo, hi, maxd;
    ws_init();

    /* crc */
    check(ws_crc32((const uint8_t *)"123456789", 9, 0) == 0xCBF43926u, "crc: zlib's check value");

    /* bank */
    size = make(0, LEN);
    check(ws_bank(bank, sizeof bank) == (int32_t)size && ws_used() == 2, "bank: a good one, two slots");
    {
        ws_info_t in;
        check(ws_slot_info(1, &in) && !strcmp(in.name, "SINE") && in.root == 69 && in.rate == 44100 &&
              in.len == LEN, "bank: slot 1's info");
        check(!ws_slot_info(2, &in) && ws_slot_info(3, &in) && in.format == WS_FMT_ULAW, "bank: 2 empty, 3 mu-law");
    }
    check(ws_bank(bank, size - 1u) < 0 && ws_used() == 0, "bank: bigger than its room");
    bank[size - 1u] ^= 1u;
    check(ws_bank(bank, sizeof bank) < 0, "bank: a bad CRC");
    bank[size - 1u] ^= 1u;
#define BAD(field, v, what)                                                       \
    do {                                                                          \
        make(0, LEN);                                                             \
        wr32(bank + WS_HDR + (field), (v));                                       \
        wr32(bank + 12, ws_crc32(bank + WS_HDR, size - WS_HDR, 0));               \
        check(ws_bank(bank, sizeof bank) < 0, "bank: " what);                     \
    } while (0)
    BAD(0, size - 100u, "a sample past its end");
    BAD(0, WS_DATA + 1u, "a 16-bit sample at an odd offset");
    BAD(12, LEN + 1u, "a loop past the sample's end");
    BAD(8, LEN - 1u, "a loop's start past its end");
    BAD(4, 2u, "a sample too short");
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 1);
    check(!render(69.0f, WS_START_PHRASE, 256) && ws_where() < 0.0f, "bank: none, nothing plays");

    /* pitch */
    make(0, LEN);
    ws_bank(bank, sizeof bank);
    for (k = -36; k <= 24; k += 12) {
        float want = 441.0f * fm_exp2f((float)k / 12.0f), got;
        render(69.0f + (float)k, WS_START_PHRASE, 44118);
        got = freq(1000, 44118);
        snprintf(m, sizeof m, "pitch: note %d plays %.2f Hz (%.2f), %.3f cents", 69 + k, (double)got, (double)want,
                 (double)cents(got, want));
        check(fm_fabsf(cents(got, want)) < 1.0f, m);
    }
    ws_settings(1, 7.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 1);
    render(69.0f, WS_START_PHRASE, 44118);
    check(fm_fabsf(cents(freq(1000, 44118), 441.0f * fm_exp2f(7.0f / 12.0f))) < 1.0f, "pitch: Tune +7");
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 0);
    render(81.0f, WS_START_PHRASE, 44118);
    check(fm_fabsf(cents(freq(1000, 44118), 441.0f)) < 1.0f, "pitch: Track off, its own pitch at any note");
    bank[WS_HDR + 20] = (uint8_t)(int8_t)-50;    /* (the entries are under the CRC) */
    wr32(bank + 12, ws_crc32(bank + WS_HDR, size - WS_HDR, 0));
    ws_bank(bank, sizeof bank);
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 1);
    render(69.0f, WS_START_PHRASE, 44118);
    check(fm_fabsf(cents(freq(1000, 44118), 441.0f * fm_exp2f(0.5f / 12.0f))) < 1.0f,
          "pitch: fine -50 cents plays it 50 cents up at its root");

    /* loop: on past its end, no click at the seam (the loop is 20 whole periods) */
    make(1000, 3000);
    ws_bank(bank, sizeof bank);
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 1);
    render(69.0f, WS_START_PHRASE, 44118);
    maxd = 0.0f;
    lo = 1.0f;
    for (i = 1; i < 44118; i++)
        maxd = fm_maxf(maxd, fm_fabsf(out[i] - out[i - 1]));
    for (i = 40000; i < 44118; i++)
        lo = fm_minf(lo, out[i]);
    snprintf(m, sizeof m, "loop: on for a second (low %.3f), steps at most %.4f", (double)lo, (double)maxd);
    check(lo < -0.45f && maxd < 0.035f && ws_where() >= 1000.0f / LEN && ws_where() < 3000.0f / LEN, m);
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_OFF, WS_TRIG_PHRASE, 1);
    check(render(69.0f, WS_START_PHRASE, 4096) && ws_where() > 0.0f, "loop: Off, playing");
    render(69.0f, 0, 1024);
    check(ws_where() < 0.0f && !ws_render(69.0f, 0, out, BLK), "loop: Off, ended after its 0.1 s");
    check(ws_render(69.0f, WS_START_PHRASE, out, BLK) && ws_where() >= 0.0f, "loop: Off, again with a phrase");
    {   /* ping-pong: where it plays goes up to le, down to ls, and up again */
        int turns = 0, dir = 1;
        float prev = -1.0f, minw = 1.0f, maxw = 0.0f;
        ws_settings(1, 0.0f, 0.0f, WS_LOOP_PINGPONG, WS_TRIG_PHRASE, 1);
        ws_render(81.0f, WS_START_PHRASE, out, BLK);
        for (i = 0; i < 400; i++) {
            float w;
            ws_render(81.0f, 0, out, BLK);
            w = ws_where();
            if (prev >= 0.0f && (w - prev) * (float)dir < 0.0f) {
                turns++;
                dir = -dir;
            }
            if (i > 100) {
                minw = fm_minf(minw, w);
                maxw = fm_maxf(maxw, w);
            }
            prev = w;
        }
        snprintf(m, sizeof m, "loop: ping-pong turns %d times, inside %.3f..%.3f", turns, (double)minw, (double)maxw);
        check(turns >= 4 && minw >= 1000.0f / LEN - 0.001f && maxw <= 3000.0f / LEN + 0.001f, m);
    }

    /* start, trigger */
    make(0, LEN);
    ws_bank(bank, sizeof bank);
    ws_settings(1, 0.0f, 0.5f, WS_LOOP_OFF, WS_TRIG_PHRASE, 0);
    ws_render(69.0f, WS_START_PHRASE, out, 1);
    check(fm_fabsf(ws_where() - 0.5f) < 0.01f, "start: 50% starts half way");
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 0);
    render(69.0f, WS_START_PHRASE, 2048);
    ws_render(69.0f, WS_START_NOTE, out, 1);
    check(ws_where() > 0.4f, "start: Trigger Phrase, a new note goes on");
    ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_NOTE, 0);
    render(69.0f, WS_START_PHRASE, 2048);
    ws_render(69.0f, WS_START_NOTE, out, 1);
    check(ws_where() < 0.01f, "start: Trigger Note, a new note starts it again");

    /* mu-law: the same sine's level as 16-bit's */
    ws_settings(3, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_PHRASE, 0);
    render(69.0f, WS_START_PHRASE, 8192);
    hi = lo = 0.0f;
    for (i = 0; i < 8192; i++) {
        hi = fm_maxf(hi, out[i]);
        lo = fm_minf(lo, out[i]);
    }
    snprintf(m, sizeof m, "mu-law: peaks %.4f %.4f", (double)hi, (double)lo);
    check(fm_fabsf(hi - 0.5f) < 0.02f && fm_fabsf(lo + 0.5f) < 0.02f, m);
    {
        float plo[8], phi[8];
        check(ws_peaks(3, 8, plo, phi) && phi[0] > 0.45f && plo[7] < -0.45f && !ws_peaks(2, 8, plo, phi),
              "peaks: the view's columns");
    }

    /* declick: the biggest step between frames, across a restart, Slot Off, a slot change, a one-shot's end */
    {
        float wave, worst;
        make(0, LEN);
        ws_bank(bank, sizeof bank);
        ws_settings(1, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_NOTE, 0);
        ws_render(69.0f, WS_START_PHRASE, out, 1025);
        wave = 0.0f;
        for (i = 1; i < 1025; i++)
            wave = fm_maxf(wave, fm_fabsf(out[i] - out[i - 1]));
        worst = 0.0f;
        for (k = 0; k < 4; k++) {               /* (a new note, a quarter period in: the read jumps) */
            float last = out[1024];
            ws_render(69.0f, WS_START_NOTE, out, 1025);
            worst = fm_maxf(worst, fm_fabsf(out[0] - last));
            for (i = 1; i < 1025; i++)
                worst = fm_maxf(worst, fm_fabsf(out[i] - out[i - 1]));
        }
        snprintf(m, sizeof m, "declick: a restart steps at most %.4f (the wave %.4f)", (double)worst, (double)wave);
        check(worst <= wave * 1.05f, m);
        {
            float last = out[1024];
            ws_settings(3, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_NOTE, 0);   /* another slot */
            ws_render(69.0f, 0, out, 256);
            worst = fm_fabsf(out[0] - last);
            for (i = 1; i < 256; i++)
                worst = fm_maxf(worst, fm_fabsf(out[i] - out[i - 1]));
            snprintf(m, sizeof m, "declick: another slot steps at most %.4f", (double)worst);
            check(worst <= wave * 1.1f, m);
            last = out[255];
            ws_settings(0, 0.0f, 0.0f, WS_LOOP_ON, WS_TRIG_NOTE, 0);   /* Off */
            check(ws_render(69.0f, 0, out, 256), "declick: Slot Off plays its fade");
            worst = fm_fabsf(out[0] - last);
            for (i = 1; i < 256; i++)
                worst = fm_maxf(worst, fm_fabsf(out[i] - out[i - 1]));
            snprintf(m, sizeof m, "declick: Slot Off fades (steps at most %.4f, ends at %.4f)", (double)worst, (double)out[255]);
            check(worst <= wave * 1.1f && out[255] == 0.0f && !ws_render(69.0f, 0, out, 64), m);
        }
        ws_settings(1, 0.0f, 0.0f, WS_LOOP_OFF, WS_TRIG_PHRASE, 0);
        render(69.0f, WS_START_PHRASE, 8192);
        lo = 1.0f;
        for (i = 4300; i < 4400; i++)
            if (out[i] != 0.0f)
                lo = fm_fabsf(out[i]);
        snprintf(m, sizeof m, "declick: a one-shot's last frame %.4f", (double)lo);
        check(lo < 0.01f, m);
    }

    /* long: a 200000-frame mu-law sample at 8 kHz, ping-pong over its last 100000, an octave down */
    {
        static uint8_t big[WS_DATA + 200000];
        uint8_t *e = big + WS_HDR;
        float fwd = 0.0f, back = 0.0f, prev;
        int nf = 0, nb = 0;
        memset(big, 0, WS_DATA);
        for (i = 0; i < 200000u; i++)
            big[WS_DATA + i] = ulaw_of(sine(i));
        wr32(big, WS_MAGIC);
        wr16(big + 4, WS_VERSION);
        wr16(big + 6, WS_NSLOT);
        wr32(e, WS_DATA);
        wr32(e + 4, 200000u);
        wr32(e + 8, 100000u);
        wr32(e + 12, 199000u);
        wr16(e + 16, 8000);
        e[18] = WS_FMT_ULAW;
        e[19] = 69;
        wr32(big + 8, sizeof big);
        wr32(big + 12, ws_crc32(big + WS_HDR, sizeof big - WS_HDR, 0));
        check(ws_bank(big, sizeof big) > 0, "long: the bank");
        ws_settings(1, 0.0f, 0.6f, WS_LOOP_PINGPONG, WS_TRIG_PHRASE, 1);
        ws_render(57.0f, WS_START_PHRASE, out, BLK);
        prev = ws_where();
        for (k = 0; k < 40000; k++) {
            float w;
            ws_render(57.0f, 0, out, BLK);
            w = ws_where();
            if (w > prev && w - prev < 0.001f)
                fwd += w - prev, nf++;
            else if (w < prev && prev - w < 0.001f)
                back += prev - w, nb++;
            prev = w;
        }
        fwd /= (float)(nf ? nf : 1);
        back /= (float)(nb ? nb : 1);
        snprintf(m, sizeof m, "long: ping-pong back %.3f cents from forward (%d, %d blocks)",
                 (double)cents(back, fwd), nf, nb);
        check(nf > 100 && nb > 100 && fm_fabsf(cents(back, fwd)) < 1.0f, m);
        make(0, LEN);
        wr32(bank + WS_HDR + 8, 0xFFFFFFFFu);
        wr32(bank + WS_HDR + 12, 8u);
        wr32(bank + 12, ws_crc32(bank + WS_HDR, size - WS_HDR, 0));
        check(ws_bank(bank, sizeof bank) < 0, "long: a loop start that would wrap is refused");
    }

    printf(fails ? "%d FAILED\n" : "wind_sample ok\n", fails);
    return fails != 0;
}

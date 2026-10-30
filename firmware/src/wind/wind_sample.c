/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
/* The sample layer: see wind_sample.h. Float only, no libm (wind_math.h). */
#include "wind_sample.h"
#include "wind_voice.h"
#include "wind_math.h"

static const uint8_t *volatile bank;   /* a checked bank, or 0 */
static uint32_t gen;
static float ulaw[256];                /* mu-law to -1..1 */
static struct {
    int slot, loop, trig, track;
    float tune, start;
} cfg;
static struct {
    int slot, on, dir;                 /* the slot it plays (0: none), playing, +1 forward / -1 back (ping-pong) */
    uint32_t ip;                       /* where it is: frame ip, and fr of the way to the next */
    float fr;
    float where;                       /* for the view */
} pl;

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

uint32_t ws_crc32(const uint8_t *p, uint32_t n, uint32_t crc)   /* zlib's, 4 bits a step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = ~crc;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

void ws_init(void)
{
    int i;
    for (i = 0; i < 256; i++) {                     /* G.711: the byte inverted, sign, 3 bits exponent, 4 mantissa */
        int u = ~i & 0xFF, e = (u >> 4) & 7, m = u & 15;
        int v = (((m << 3) + 0x84) << e) - 0x84;
        ulaw[i] = (float)(u & 0x80 ? -v : v) * (1.0f / 32768.0f);
    }
    bank = 0;
    cfg.slot = 0;
    cfg.loop = WS_LOOP_ON;
    cfg.trig = WS_TRIG_PHRASE;
    cfg.track = 1;
    cfg.tune = cfg.start = 0.0f;
    pl.slot = pl.on = 0;
    pl.dir = 1;
    pl.where = -1.0f;
}

static uint32_t frame_bytes(uint32_t fmt) { return fmt == WS_FMT_S16 ? 2u : 1u; }

/* entry e of a bank that is `size` bytes: 1 a usable sample, 0 an empty slot, -1 a bad entry */
static int entry_ok(const uint8_t *e, uint32_t size)
{
    uint32_t off = rd32(e), len = rd32(e + 4), ls = rd32(e + 8), le = rd32(e + 12), rate = rd16(e + 16), fmt = e[18];
    if (fmt == WS_FMT_NONE)
        return 0;
    if (fmt >= WS_NFMT || len < 4u || len > size || rate < 4000u || rate > 48000u || e[19] > 127u)
        return -1;
    if (off < WS_DATA || off > size || (size - off) / frame_bytes(fmt) < len || (fmt == WS_FMT_S16 && (off & 1u)))
        return -1;
    if (le && (le > len || ls + 2u > le))
        return -1;
    return 1;
}

int32_t ws_check(const uint8_t *p, uint32_t cap)
{
    uint32_t size, i;
    if (!p || cap < WS_DATA || rd32(p) != WS_MAGIC || rd16(p + 4) != WS_VERSION || rd16(p + 6) != WS_NSLOT)
        return -1;
    size = rd32(p + 8);
    if (size < WS_DATA || size > cap || ws_crc32(p + WS_HDR, size - WS_HDR, 0) != rd32(p + 12))
        return -1;
    for (i = 0; i < (uint32_t)WS_NSLOT; i++)
        if (entry_ok(p + WS_HDR + i * WS_ENT, size) < 0)
            return -1;
    return (int32_t)size;
}

int32_t ws_bank(const uint8_t *p, uint32_t cap)
{
    int32_t n = ws_check(p, cap);
    bank = n > 0 ? p : 0;
    gen++;
    return n;
}

uint32_t ws_gen(void) { return gen; }

static const uint8_t *entry(const uint8_t *b, int slot)
{
    const uint8_t *e;
    if (!b || slot < 1 || slot > WS_NSLOT)
        return 0;
    e = b + WS_HDR + (uint32_t)(slot - 1) * WS_ENT;
    return e[18] != WS_FMT_NONE ? e : 0;
}

int ws_slot_info(int slot, ws_info_t *o)
{
    const uint8_t *e = entry(bank, slot);
    int i;
    if (!e)
        return 0;
    o->len = rd32(e + 4);
    o->loop_start = rd32(e + 8);
    o->loop_end = rd32(e + 12);
    o->rate = rd16(e + 16);
    o->format = e[18];
    o->root = e[19];
    o->fine = (int8_t)e[20];
    for (i = 0; i < WS_NAME; i++)
        o->name[i] = e[24 + i] >= ' ' && e[24 + i] < 0x7F ? (char)e[24 + i] : 0;
    o->name[WS_NAME] = 0;
    for (i = 0; i < WS_NAME && o->name[i]; i++)
        ;
    while (i > 0 && o->name[i - 1] == ' ')
        o->name[--i] = 0;
    return 1;
}

int ws_used(void)
{
    int s, n = 0;
    for (s = 1; s <= WS_NSLOT; s++)
        n += entry(bank, s) != 0;
    return n;
}

void ws_settings(int slot, float tune, float start, int loop, int trig, int track)
{
    cfg.slot = slot >= 0 && slot <= WS_NSLOT ? slot : 0;
    cfg.tune = tune;
    cfg.start = fm_clampf(start, 0.0f, 1.0f);
    cfg.loop = loop >= 0 && loop < WS_NLOOP ? loop : WS_LOOP_ON;
    cfg.trig = trig == WS_TRIG_NOTE ? WS_TRIG_NOTE : WS_TRIG_PHRASE;
    cfg.track = track != 0;
}

float ws_where(void) { return pl.where; }

/* one sample of the slot's audio: frame j of len (format fmt at d), held at the ends */
static float frame_at(const uint8_t *d, uint32_t fmt, uint32_t len, int32_t j)
{
    j = j < 0 ? 0 : j >= (int32_t)len ? (int32_t)len - 1 : j;
    if (fmt == WS_FMT_S16) {
        const uint8_t *q = d + 2u * (uint32_t)j;
        return (float)(int16_t)(uint16_t)(q[0] | q[1] << 8) * (1.0f / 32768.0f);
    }
    return ulaw[d[j]];
}

int ws_peaks(int slot, int n, float *lo, float *hi)
{
    const uint8_t *b = bank, *e = entry(b, slot);
    uint32_t len, fmt, c;
    if (!e || n < 1)
        return 0;
    len = rd32(e + 4);
    fmt = e[18];
    for (c = 0; c < (uint32_t)n; c++) {
        float per = (float)len / (float)n;
        uint32_t j = (uint32_t)(per * (float)c), end = c + 1u == (uint32_t)n ? len : (uint32_t)(per * (float)(c + 1u)),
                 st = end - j > 256u ? (end - j) / 256u : 1u;   /* (at most 256 reads a column) */
        float a = 0.0f, z = 0.0f;
        for (; j < end; j += st) {
            float v = frame_at(b + rd32(e), fmt, len, (int32_t)j);
            a = fm_minf(a, v);
            z = fm_maxf(z, v);
        }
        lo[c] = a;
        hi[c] = z;
    }
    return 1;
}

int ws_render(float note, uint32_t trig, float *out, uint32_t n)
{
    const uint8_t *b = bank, *e = entry(b, cfg.slot), *d;
    uint32_t len, ls, le, fmt, i;
    int loop = cfg.loop;
    float step, semis;
    if (!e) {
        pl.slot = pl.on = 0;
        pl.where = -1.0f;
        return 0;
    }
    len = rd32(e + 4);
    ls = rd32(e + 8);
    le = rd32(e + 12);
    fmt = e[18];
    d = b + rd32(e);
    if (!le) {                                      /* no loop points: the whole of it */
        ls = 0u;
        le = len;
    }
    if (pl.slot != cfg.slot || (trig & WS_START_PHRASE) || ((trig & WS_START_NOTE) && cfg.trig == WS_TRIG_NOTE)) {
        uint32_t s = (uint32_t)(cfg.start * (float)(len - 1u));
        if (loop != WS_LOOP_OFF && s >= le - 1u)    /* (past the loop: from its start) */
            s = ls;
        pl.slot = cfg.slot;
        pl.on = 1;
        pl.dir = 1;
        pl.ip = s;
        pl.fr = 0.0f;
    }
    if (!pl.on) {
        pl.where = -1.0f;
        return 0;
    }
    semis = cfg.tune + (cfg.track ? note - (float)e[19] - (float)(int8_t)e[20] * 0.01f : 0.0f);
    step = (float)rd16(e + 16) * (1.0f / WV_SR) * fm_exp2f(fm_clampf(semis, -96.0f, 48.0f) * (1.0f / 12.0f));
    step = fm_minf(step, 16.0f);
    for (i = 0; i < n; i++) {
        /* 4-point Hermite (as stmlib's InterpolateHermite); across a loop's end the frames after it are
         * the ones from its start */
        int32_t j = (int32_t)pl.ip;
        float xm, x0, x1, x2, c, v, w, a, bb, t = pl.fr;
        if (loop == WS_LOOP_ON && j + 2 >= (int32_t)le) {
            int32_t k, span = (int32_t)(le - ls);
            float y[4];
            for (k = 0; k < 4; k++) {
                int32_t q = j - 1 + k;
                while (q >= (int32_t)le)
                    q -= span;
                y[k] = frame_at(d, fmt, len, q);
            }
            xm = y[0], x0 = y[1], x1 = y[2], x2 = y[3];
        } else {
            xm = frame_at(d, fmt, len, j - 1);
            x0 = frame_at(d, fmt, len, j);
            x1 = frame_at(d, fmt, len, j + 1);
            x2 = frame_at(d, fmt, len, j + 2);
        }
        c = (x1 - xm) * 0.5f;
        v = x0 - x1;
        w = c + v;
        a = w + v + (x2 - x0) * 0.5f;
        bb = w + a;
        out[i] = ((((a * t) - bb) * t + c) * t + x0);
        if (pl.dir > 0) {                           /* on by step */
            uint32_t k;
            pl.fr += step;
            k = (uint32_t)pl.fr;
            pl.ip += k;
            pl.fr -= (float)k;
            if (loop == WS_LOOP_ON) {
                while (pl.ip >= le)
                    pl.ip -= le - ls;
            } else if (loop == WS_LOOP_PINGPONG && pl.ip >= le - 1u) {
                float p = (float)(2u * (le - 1u)) - ((float)pl.ip + pl.fr);   /* back from the end */
                p = fm_maxf(p, (float)ls);
                pl.ip = (uint32_t)p;
                pl.fr = p - (float)pl.ip;
                pl.dir = -1;
            } else if (loop == WS_LOOP_OFF && pl.ip >= len - 1u) {
                for (i++; i < n; i++)               /* the end: silence after it */
                    out[i] = 0.0f;
                pl.on = 0;
                break;
            }
        } else {                                    /* ping-pong, on its way back */
            float p = (float)pl.ip + pl.fr - step;
            if (p <= (float)ls) {
                p = fm_minf(2.0f * (float)ls - p, (float)(le - 1u));
                pl.dir = 1;
            }
            pl.ip = (uint32_t)p;
            pl.fr = p - (float)pl.ip;
        }
    }
    pl.where = pl.on ? ((float)pl.ip + pl.fr) / (float)len : -1.0f;
    return 1;
}

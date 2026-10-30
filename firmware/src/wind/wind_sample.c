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
typedef struct {
    const uint8_t *d;                  /* a slot's sample as it plays: its frames, */
    uint32_t len, ls, le, fmt;         /* length, loop (le: the whole of it when it has none), format */
    int loop;                          /* WS_LOOP_* */
} src_t;
typedef struct {
    uint32_t ip;                       /* where it is: frame ip, and fr of the way to the next */
    float fr;
    int dir, on;                       /* +1 forward / -1 back (ping-pong); playing */
} pos_t;
#define WS_XF 64                       /* frames a restart crossfades over (no click) */
static struct {
    int slot, on, in;                  /* the slot it plays (0: none), playing, frames of fading in left */
    const uint8_t *b, *e;              /* the bank and the entry it plays from */
    pos_t p;
    float where;                       /* for the view */
} pl;
static struct {
    const uint8_t *e;                  /* what played before a restart (or Slot Off), fading out: */
    pos_t p;
    int n;                             /* frames of it left */
} fade;

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
    pl.slot = pl.on = pl.in = 0;
    pl.where = -1.0f;
    fade.n = 0;
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
    if (le && (le > len || le < 2u || ls > le - 2u))   /* (no ls + 2: it could wrap) */
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

static void src_of(const uint8_t *b, const uint8_t *e, int loop, src_t *s)
{
    s->d = b + rd32(e);
    s->len = rd32(e + 4);
    s->ls = rd32(e + 8);
    s->le = rd32(e + 12);
    s->fmt = e[18];
    s->loop = loop;
    if (!s->le) {                                   /* no loop points: the whole of it */
        s->ls = 0u;
        s->le = s->len;
    }
}

/* frames a step at `note`: the entry's rate to ours, and its pitch (or its own) */
static float step_of(const uint8_t *e, float note)
{
    float semis = cfg.tune + (cfg.track ? note - (float)e[19] - (float)(int8_t)e[20] * 0.01f : 0.0f);
    float step = (float)rd16(e + 16) * (1.0f / WV_SR) * fm_exp2f(fm_clampf(semis, -96.0f, 48.0f) * (1.0f / 12.0f));
    return fm_minf(step, 16.0f);
}

/* the sample at p: 4-point Hermite (as stmlib's InterpolateHermite); across a loop's end the frames after it
 * are the ones from its start */
static float read_at(const src_t *s, const pos_t *p)
{
    int32_t j = (int32_t)p->ip;
    float xm, x0, x1, x2, c, v, w, a, bb, t = p->fr;
    if (s->loop == WS_LOOP_ON && j + 2 >= (int32_t)s->le) {
        int32_t k, span = (int32_t)(s->le - s->ls);
        float y[4];
        for (k = 0; k < 4; k++) {
            int32_t q = j - 1 + k;
            while (q >= (int32_t)s->le)
                q -= span;
            y[k] = frame_at(s->d, s->fmt, s->len, q);
        }
        xm = y[0], x0 = y[1], x1 = y[2], x2 = y[3];
    } else {
        xm = frame_at(s->d, s->fmt, s->len, j - 1);
        x0 = frame_at(s->d, s->fmt, s->len, j);
        x1 = frame_at(s->d, s->fmt, s->len, j + 1);
        x2 = frame_at(s->d, s->fmt, s->len, j + 2);
    }
    c = (x1 - xm) * 0.5f;
    v = x0 - x1;
    w = c + v;
    a = w + v + (x2 - x0) * 0.5f;
    bb = w + a;
    return (((a * t) - bb) * t + c) * t + x0;
}

/* p on by step; the whole frames and the fraction kept apart, so a long sample keeps its pitch */
static void advance(const src_t *s, pos_t *p, float step)
{
    uint32_t k;
    if (p->dir > 0) {
        p->fr += step;
        k = (uint32_t)p->fr;
        p->ip += k;
        p->fr -= (float)k;
        if (s->loop == WS_LOOP_ON) {
            while (p->ip >= s->le)
                p->ip -= s->le - s->ls;
        } else if (s->loop == WS_LOOP_PINGPONG && p->ip >= s->le - 1u) {
            int32_t q = (int32_t)(s->le - 1u) - (int32_t)(p->ip - (s->le - 1u));   /* back from the end */
            if (p->fr > 0.0f) {
                q--;
                p->fr = 1.0f - p->fr;
            }
            if (q < (int32_t)s->ls) {
                q = (int32_t)s->ls;
                p->fr = 0.0f;
            }
            p->ip = (uint32_t)q;
            p->dir = -1;
        } else if (s->loop == WS_LOOP_OFF && p->ip >= s->len - 1u)
            p->on = 0;
    } else {                                        /* ping-pong, on its way back */
        int32_t q = (int32_t)p->ip;
        p->fr -= step;
        if (p->fr < 0.0f) {
            k = (uint32_t)(-p->fr) + 1u;
            q -= (int32_t)k;
            p->fr += (float)k;
        }
        if (q < (int32_t)s->ls) {                   /* past the loop's start: forward again, as far past it */
            int32_t over = (int32_t)s->ls - q;      /* (ls - (q + fr), in whole frames and a fraction) */
            float f = p->fr;
            q = (int32_t)s->ls + over;
            if (f > 0.0f) {
                q--;
                f = 1.0f - f;
            }
            if (q > (int32_t)(s->le - 1u))
                q = (int32_t)(s->le - 1u), f = 0.0f;
            p->fr = f;
            p->dir = 1;
        }
        p->ip = (uint32_t)q;
    }
}

int ws_render(float note, uint32_t trig, float *out, uint32_t n)
{
    const uint8_t *b = bank, *e = entry(b, cfg.slot);
    src_t s, fs;
    float step = 0.0f, fstep = 0.0f;
    uint32_t i;
    int restart = e && (pl.slot != cfg.slot || pl.b != b || (trig & WS_START_PHRASE) ||
                        ((trig & WS_START_NOTE) && cfg.trig == WS_TRIG_NOTE));
    if (pl.on && (restart || !e) && pl.b == b) {    /* what plays now fades out under what comes */
        fade.e = pl.e;
        fade.p = pl.p;
        fade.n = WS_XF;
    }
    if (fade.n && pl.b != b)                        /* (its bank is gone: nothing to read) */
        fade.n = 0;
    if (!e) {
        pl.slot = pl.on = 0;
        pl.b = b;
    } else if (restart) {
        int crossfade = pl.on && pl.b == b;
        src_of(b, e, cfg.loop, &s);
        {
            uint32_t at = (uint32_t)(cfg.start * (float)(s.len - 1u));
            if (s.loop != WS_LOOP_OFF && at >= s.le - 1u)   /* (past the loop: from its start) */
                at = s.ls;
            pl.p.ip = at;
        }
        pl.p.fr = 0.0f;
        pl.p.dir = 1;
        pl.p.on = 1;
        pl.slot = cfg.slot;
        pl.on = 1;
        pl.in = crossfade ? WS_XF : 0;
        pl.b = b;
        pl.e = e;
    }
    if (!pl.on && !fade.n) {
        pl.where = -1.0f;
        return 0;
    }
    if (pl.on) {
        src_of(b, pl.e, cfg.loop, &s);
        step = step_of(pl.e, note);
    }
    if (fade.n) {
        src_of(b, fade.e, cfg.loop, &fs);
        fstep = step_of(fade.e, note);
    }
    for (i = 0; i < n; i++) {
        float v = 0.0f;
        if (pl.on) {
            float g = 1.0f;
            v = read_at(&s, &pl.p);
            if (pl.in)
                g = 1.0f - (float)pl.in-- * (1.0f / WS_XF);
            if (s.loop == WS_LOOP_OFF)              /* once through: its last frames fade, not stop */
                g *= fm_minf(1.0f, ((float)(s.len - 1u - pl.p.ip) - pl.p.fr) / (step * (float)WS_XF));
            v *= g;
            advance(&s, &pl.p, step);
            pl.on = pl.p.on;
        }
        if (fade.n) {
            v += read_at(&fs, &fade.p) * (float)fade.n * (1.0f / WS_XF);
            fade.n--;
            advance(&fs, &fade.p, fstep);
            if (!fade.p.on)
                fade.n = 0;
        }
        out[i] = v;
    }
    pl.where = pl.on ? ((float)pl.p.ip + pl.p.fr) / (float)s.len : -1.0f;
    return 1;
}

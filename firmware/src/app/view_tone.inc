/* SPDX-License-Identifier: GPL-3.0-only */
/* The tone pages' views, in the middle band (MAIN_Y, MAIN_H): included by ui.c, above draw_main.
 *
 *   Filter    the response, log frequency by dB, at the knobs' Cutoff, Reso and Slope (tracking the
 *             note); and, in the accent, where the mod matrix has it now when something moves it
 *   Noise     the noise filter's response for its Type at Freq and Reso, with the Level as bars
 *   Sample    (and Sample 2) the sample chosen: its wave, its loop, Start, and where it plays now
 *   Breath    the curve, with the breath on it now; the last 1.3 s of breath, in and out (the gate
 *             envelope's Attack and Release)
 *   Pitch     the phrase: the last 2.8 s of pitch, each note's start named, breath under it; the note,
 *             the bite and the glide time now
 *   Pitch 2   the bite on a ruler of the Bend range; the glide time, at Glide, with all of the CC
 *             depth, and now
 *   LFO       one cycle of the shape, where the LFO is in it, the fade, its destination now
 *   LFO 2     the last 2.9 s of the LFO in its fade, marked where a note synced it
 *
 * The views that move redraw every other frame (30 a second) while something sounds and stand still
 * when it's quiet. tn_tick keeps a history of the voice while it sounds, an entry each TN_MS by the
 * clock (so its time holds whatever the frame rate), TN_N entries. */
#include "../dsp/fastmath.h"

static uint16_t blend565(uint16_t a, uint16_t b, uint32_t n);   /* (ui.c's, defined further down) */

#define TN_N 184                       /* the history: entries, */
#define TN_MS 16u                      /* ...one each 16 ms of sound (2.9 s; frames come at most this often) */
#define TN_GATE 1u                     /* a note held */
#define TN_ON 2u                       /* a note starts here: from silence, or the stack's top moved */
#define TN_SYNC 4u                     /* the LFO started again here (Sync Note) */
#define TN_FMAX 11029.0f               /* our filter's top, a quarter of the sample rate (wind_voice.cc) */
typedef struct {
    uint8_t br, out, raw;              /* breath (curved, smoothed), it through the gate envelope, as it came in (curved) */
    uint8_t fl, top, fade;             /* TN_*, the note sounding, the LFO's fade 0..255 */
    int8_t lfo;                        /* the LFO, faded, -127..127 */
    int16_t p;                         /* the pitch, semitones x 100 (glide and bite in it) */
} tn_ent_t;
static tn_ent_t tn_h[TN_N + 2];        /* (+2: the newest, odd entry is written but not yet shown) */
static uint32_t tn_n;                  /* entries written, ever */
static uint32_t tn_frame = 0xFFFFFFFFu, tn_ticks, tn_starts, tn_ms, tn_ms_ok;
static int tn_top = -1;
/* what the views show of the live state, taken every other frame (so a redraw from scratch draws the
 * same): the breath, the filter and the LFO while something sounds (and once more as it stops: the LFO
 * runs on in the quiet, and its views stand still); the bite and the glide always */
static struct {
    float raw, cut, res;
    float ph, lfo, fade, dv;           /* the LFO: phase, value (faded), fade, its destination now */
    float bend, glide;                 /* the bite -1..1, the glide time (ms) */
    int gliding, top, gate, active, taken;
} tn_s = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, -1, 0, 0, 0};

static uint8_t tn_u8(float x) { return (uint8_t)(x <= 0.0f ? 0.0f : x >= 1.0f ? 255.0f : x * 255.0f + 0.5f); }

/* breath through the Breath page's Curve (wind_ctl's) */
static float tn_curve(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    if (x > 1.0f)
        x = 1.0f;
    if (!proj.par[P_CURVE])
        return x;
    return fm_powf(x, fm_exp2f((float)-proj.par[P_CURVE] * (1.0f / 50.0f)));
}

/* the LFO's destination now: x 100 (Pitch: cents) */
static float tn_dest_now(void)
{
    switch (proj.par[P_LFODST]) {
    case WM_DST_CUTOFF: return wa_state.cutoff * 100.0f;
    case WM_DST_RES: return wa_state.res * 100.0f;
    case WM_DST_AMP: return wa_state.amp * 100.0f;
    case WM_DST_MORPH: return wa_state.morph * 100.0f;
    case WM_DST_TIMBRE: return wa_state.timbre * 100.0f;
    case WM_DST_NOISE: return wa_state.noise * 100.0f;
    case WM_DST_HARM: return wa_state.harmonics * 100.0f;
    case WM_DST_AUX: return wa_state.aux * 100.0f;
    case WM_DST_SAMPLE: return wa_state.sample * 100.0f;
    default: return (wa_state.pitch - wa_state.note) * 100.0f;
    }
}

/* once a frame, from ui_frame before anything is drawn and whatever the page (Stage too): the history
 * and the live snapshot. (Not from a redraw from scratch: that draws what the last frame drew.) */
static void tn_tick(void)
{
    int active, top = wa_state.top, gate = wa_state.gate > 0.5f;
    uint32_t st = wa_state.lfo_starts, k;
    if (ui.frame == tn_frame)
        return;
    tn_frame = ui.frame;
    tn_ticks++;
    active = gate || wa_state.env > 0.001f;      /* the voice sounding (effects' tails, a breath at rest aside) */
    k = (plat_ms() - tn_ms) / TN_MS;            /* entries due (TN_MS each), whatever the frame rate */
    if (!tn_ms_ok)
        tn_ms_ok = 1, k = 1, tn_ms = plat_ms();
    tn_ms += k * TN_MS;
    if (k > 4u)                                 /* (frames held up: a short catch-up, not a copy of now) */
        k = 4u;
    for (; active && k; k--) {
        tn_ent_t *e = &tn_h[tn_n % (TN_N + 2)];
        float p = wa_state.note * 100.0f;
        e->br = tn_u8(wa_state.breath);
        e->out = tn_u8(wa_state.breath * wa_state.env);
        e->raw = tn_u8(tn_curve(wa_state.breath_in));
        e->top = (uint8_t)(top < 0 ? 0 : top);
        e->fade = tn_u8(wa_state.lfo_fade);
        e->lfo = (int8_t)(wa_state.lfo < -1.0f ? -127 : wa_state.lfo > 1.0f ? 127 : (int)(wa_state.lfo * 127.0f));
        e->p = (int16_t)(p < -32000.0f ? -32000 : p > 32000.0f ? 32000 : (int)p);
        e->fl = (uint8_t)((gate ? TN_GATE : 0u) | (gate && (top != tn_top || st != tn_starts) ? TN_ON : 0u) |
                          (st != tn_starts && proj.par[P_LFOSYNC] ? TN_SYNC : 0u));
        tn_n++;
        tn_top = top;                           /* (an onset marks the first of them) */
        tn_starts = st;
    }
    if (!active) {                              /* (sounding, none due yet: the onset waits for its entry) */
        tn_top = top;
        tn_starts = st;
    }
    if (tn_ticks & 1u)
        return;
    tn_s.bend = wa_state.bend;
    tn_s.glide = wa_state.glide_ms;
    tn_s.gliding = wa_state.gliding > 0.5f;
    tn_s.top = top;
    tn_s.gate = gate;
    if (active || tn_s.active || !tn_s.taken) {
        tn_s.taken = 1;
        tn_s.raw = wa_state.breath_in;
        tn_s.cut = wa_state.cutoff;
        tn_s.res = wa_state.res;
        tn_s.ph = wa_state.lfo_ph;
        tn_s.lfo = wa_state.lfo;
        tn_s.fade = wa_state.lfo_fade;
        tn_s.dv = tn_dest_now();
        tn_s.active = active;
    }
}

/* the history as drawn: up to the last even entry (a redraw every other frame), the last *cnt of it
 * (fewer while it fills); tn_at(i, cnt) the i-th of those, oldest first */
static uint32_t tn_shown(uint32_t *cnt)
{
    uint32_t s = tn_n & ~1u;
    if (*cnt > s)
        *cnt = s;
    return s;
}
static const tn_ent_t *tn_at(uint32_t s, uint32_t cnt, uint32_t i) { return &tn_h[(s - cnt + i) % (TN_N + 2)]; }

/* ---------------------------------------------------------- drawing --- */
/* a line w px wide (1 or 2) */
static void tn_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int w, uint16_t c)
{
    cv_line(x0, y0, x1, y1, c);
    if (w > 1) {
        int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
        if (dy > dx)
            cv_line(x0 + 1, y0, x1 + 1, y1, c);
        else
            cv_line(x0, y0 + 1, x1, y1 + 1, c);
    }
}

static void tn_poly(const int16_t *x, const int16_t *y, int n, int w, uint16_t c)
{
    int i;
    for (i = 1; i < n; i++)
        tn_line(x[i - 1], y[i - 1], x[i], y[i], w, c);
}

static void tn_vdash(int32_t x, int32_t y0, int32_t y1, uint16_t c)
{
    for (; y0 < y1; y0 += 5)
        cv_rect(x, y0, 1, y1 - y0 < 2 ? y1 - y0 : 2, c);
}

static void tn_hdash(int32_t x0, int32_t x1, int32_t y, uint16_t c)
{
    for (; x0 < x1; x0 += 5)
        cv_rect(x0, y, x1 - x0 < 2 ? x1 - x0 : 2, 1, c);
}

static void tn_dot(int32_t x, int32_t y, int32_t r, uint16_t c) { cv_round(x - r, y - r, 2 * r + 1, 2 * r + 1, r, c); }

/* text with a ring of the panel's colour round it, so it reads over a curve; al: 0 left, 1 centre, 2 right */
static void tn_text_o(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, int al)
{
    int32_t dx, dy;
    x -= al == 1 ? text_w(f, s) / 2 : al == 2 ? text_w(f, s) : 0;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx || dy)
                cv_text(x + dx, y + dy, f, s, K_PANEL);
    cv_text(x, y, f, s, c);
}

static void tn_text_r(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(x - text_w(f, s), y, f, s, c);
}

/* the start of a view's hash: the page and the theme */
static uint32_t tn_hash0(int v) { return hash(hash(2166136261u, 0x7E00u | (uint32_t)v), (uint32_t)proj.par[P_THEME]); }

static int tn_begin(uint32_t h)
{
    if (h == ui.sig[1])
        return 0;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(6, 4, 228, 136, 6, K_PANEL);
    return 1;
}

/* the frequency axis, 20 Hz to 20 kHz, log: x of f, and f of a step of n */
#define TN_FX0 16
#define TN_FW 210
#define TN_NPT 52                      /* points on a response */
static int32_t tn_fx(float hz) { return TN_FX0 + (int32_t)((float)TN_FW * fm_log2f(hz * (1.0f / 20.0f)) * (1.0f / 9.965784f) + 0.5f); }
static float tn_fstep(int i) { return 20.0f * fm_exp2f((float)i * (9.965784f / (float)(TN_NPT - 1))); }
#define TN_SR 44117.647f               /* the sample rate (wind_voice.h) */

/* the digital filters' frequency warp: a filter at fc answers at f as its analog prototype does at
 * tan(pi f / SR) / tan(pi fc / SR) (the bilinear transform; stmlib's state-variable filters) */
static float tn_warp(float hz) { return fm_tanf(FM_PI * fm_minf(hz, 0.49f * TN_SR) * (1.0f / TN_SR)); }

/* the warp at the 52 points, worked out once */
static const float *tn_wpts(void)
{
    static float w[TN_NPT];
    static int done;
    int i;
    if (!done) {
        for (i = 0; i < TN_NPT; i++)
            w[i] = tn_warp(tn_fstep(i));
        done = 1;
    }
    return w;
}

static void tn_fgrid(int32_t top, int32_t bot)
{
    static const float F[3] = {100.0f, 1000.0f, 10000.0f};
    static const char *const L[3] = {"100", "1k", "10k"};
    int i;
    for (i = 0; i < 3; i++) {
        int32_t x = tn_fx(F[i]);
        tn_vdash(x, top, bot, K_LINE);
        text_c(x, bot + 2, &FONT_XS, L[i], K_DIM);
    }
}

/* "185", "1.8k", "12k" */
static void tn_hz_text(float hz, char *t)
{
    uint32_t v = (uint32_t)(hz + 0.5f);
    if (v < 1000u) {
        itoa_u(v, t);
        return;
    }
    if (v < 10000u) {
        v = (v + 50u) / 100u;
        itoa_u(v / 10u, t);
        t += s_len(t);
        *t++ = '.';
        *t++ = (char)('0' + v % 10u);
    } else {
        itoa_u((v + 500u) / 1000u, t);
        t += s_len(t);
    }
    *t++ = 'k';
    *t = 0;
}

/* the note a key-tracked filter follows: the one sounding, or C4 before any */
static int tn_note(void) { return (wa_state.top >= 0 ? wa_state.top : 60) + proj.par[P_TRANSPOSE]; }
static float tn_note_hz(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* ------------------------------------------------------------ Filter --- */
/* our filter (VA-VCF's) as wind_voice.cc sets it: the cutoff tracks the note, 10 octaves a unit from
 * 0.2 at it; a 2-pole low-pass of Q 0.5 + 48 r^4, then (24 dB) a second of Q 0.5 + 1.2 r^4 */
static float tn_filter_fc(float cut, int n)
{
    float fc = tn_note_hz((float)n) * fm_exp2f((fm_clampf(cut, 0.0f, 1.0f) - 0.2f) * 10.0f);
    return fm_clampf(fc, 5.0f, TN_FMAX);
}

/* its level in dB at x, the warped frequency over the warped cutoff */
static float tn_filter_db(float x, float res, int s24)
{
    float r = fm_clampf(res, 0.0f, 1.0f), q = r * r * r * r * 48.0f, x2 = x * x, d = (1.0f - x2) * (1.0f - x2);
    float q1 = 0.5f + q, q2 = 0.5f + 0.025f * q, g = 1.0f / (d + x2 / (q1 * q1) + 1e-12f);
    if (s24)
        g /= d + x2 / (q2 * q2) + 1e-12f;
    return 3.0103f * fm_log2f(g + 1e-12f);           /* 10 log10 */
}

#define TN_FTOP 18
#define TN_FBOT 122
static int16_t tn_dby(float db)
{
    float y = (float)TN_FTOP + (12.0f - db) * (float)(TN_FBOT - TN_FTOP) * (1.0f / 60.0f);
    return (int16_t)fm_clampf(y + 0.5f, (float)(TN_FTOP - 4), (float)TN_FBOT);
}

/* where the cutoff's label goes (its left x, top y), w wide: over the dot, or under it, or beside, so
 * it stays clear of the slope's label (top left, sw wide) and "0 dB" (right, zw wide) and in the plot */
static int tn_box_hit(int32_t x, int32_t y, int32_t w, int32_t bx, int32_t by, int32_t bw)
{
    return x < bx + bw + 2 && x + w + 2 > bx && y < by + 14 && y + 14 > by;
}

static void tn_label_place(int32_t dx, int32_t dy, int32_t w, int32_t sw, int32_t zw, int32_t *lx, int32_t *ly)
{
    const int32_t zx = TN_FX0 + TN_FW - zw, zy = tn_dby(0.0f) - 14, lo = TN_FX0 + 2, hi = TN_FX0 + TN_FW - w;
    int32_t xs[3], k, j;
    xs[0] = dx - w / 2;
    xs[1] = 18 + sw + 6;                       /* right of the slope's label */
    xs[2] = zx - 6 - w;                        /* left of "0 dB" */
    for (k = 0; k < 3; k++)
        for (j = 0; j < 2; j++) {
            int32_t x = xs[k] < lo ? lo : xs[k] > hi ? hi : xs[k], y = j ? dy + 6 : dy - 17;
            if (y < 2 || y > TN_FBOT - 14 || tn_box_hit(x, y, w, 18, 5, sw) || tn_box_hit(x, y, w, zx, zy, zw))
                continue;
            *lx = x;
            *ly = y;
            return;
        }
    *lx = xs[0] < lo ? lo : xs[0] > hi ? hi : xs[0];
    *ly = dy + 6 > TN_FBOT - 14 ? dy - 17 : dy + 6;
}

/* a row no higher than the top of a 2-px polyline (n points, x rising, tn_poly's) in column px: per
 * segment that reaches it (tn_line puts a steep one's second pixel at x + 1), its line there, plus one */
static int32_t tn_poly_y(const int16_t *x, const int16_t *y, int n, int32_t px)
{
    int i;
    int32_t m = 0x7FFF;
    for (i = 1; i < n; i++)
        if (px >= x[i - 1] && px <= x[i] + 1) {
            int32_t c = px > x[i] ? x[i] : px, v = y[i - 1] + (y[i] - y[i - 1]) * (c - x[i - 1]) / (x[i] - x[i - 1]);
            v += 1 + (y[i] > y[i - 1]);                    /* (truncated towards y[i - 1]: one more, rising) */
            m = v < m ? v : m;
        }
    return m == 0x7FFF ? 0 : m;
}

/* a response's 52 points, and the column fill under it */
static void tn_curve_fill(const int16_t *x, const int16_t *y, int32_t bot, uint16_t c)
{
    int i;
    int32_t px;
    for (i = 1; i < TN_NPT; i++)
        for (px = x[i - 1]; px < x[i]; px++) {
            int32_t yy = y[i - 1] + (y[i] - y[i - 1]) * (px - x[i - 1]) / (x[i] - x[i - 1]);
            if (yy < bot)
                cv_rect(px, yy, 1, bot - yy, c);
        }
}

static void tn_filter(void)
{
    int n = tn_note(), s24 = proj.par[P_SLOPE] != 0, i, live;
    float cut = (float)proj.par[P_CUTOFF] * 0.01f, res = (float)proj.par[P_RESO] * 0.01f, fc, lc, lr;
    int16_t x[TN_NPT], y[TN_NPT], ly[TN_NPT];
    uint32_t h;
    char t[12];
    const char *sl = s24 ? "24 dB/oct" : "12 dB/oct";
    lc = (float)(int)(tn_s.cut * 1000.0f + 0.5f) * 0.001f;     /* (as hashed) */
    lr = (float)(int)(tn_s.res * 1000.0f + 0.5f) * 0.001f;
    live = tn_s.active && (fm_fabsf(lc - cut) > 0.004f || fm_fabsf(lr - res) > 0.004f);   /* (quiet: the knobs') */
    h = hash(hash(tn_hash0(V_FILTER), (uint32_t)proj.par[P_CUTOFF] | (uint32_t)proj.par[P_RESO] << 8 |
                                          (uint32_t)s24 << 16 | (uint32_t)(n + 64) << 17),
             live ? (uint32_t)(tn_s.cut * 1000.0f + 0.5f) | (uint32_t)(tn_s.res * 1000.0f + 0.5f) << 12 | 1u << 30 : 0u);
    if (!tn_begin(h))
        return;
    fc = tn_filter_fc(cut, n);
    {
        const float *w = tn_wpts();
        float iw = 1.0f / tn_warp(fc), il = 1.0f / tn_warp(tn_filter_fc(lc, n));
        for (i = 0; i < TN_NPT; i++) {
            x[i] = (int16_t)(TN_FX0 + i * TN_FW / (TN_NPT - 1));
            y[i] = tn_dby(tn_filter_db(w[i] * iw, res, s24));
            if (live)
                ly[i] = tn_dby(tn_filter_db(w[i] * il, lr, s24));
        }
    }
    tn_curve_fill(x, live ? ly : y, TN_FBOT,
                  live ? blend565(K_ACC, K_PANEL, proj.par[P_THEME] ? 56u : 40u) : blend565(K_BLUE_T, K_PANEL, 128u));
    tn_fgrid(TN_FTOP, TN_FBOT);
    cv_rect(TN_FX0, tn_dby(0.0f), TN_FW, 1, K_LINE);
    tn_poly(x, y, TN_NPT, live ? 1 : 2, K_BLUE);
    {   /* the cutoff: a dot on the curve that sounds, and its frequency */
        float hz = live ? tn_filter_fc(lc, n) : fc;
        int32_t dx = tn_fx(hz), dy = tn_dby(tn_filter_db(1.0f, live ? lr : res, s24)), lx, ly0;
        uint16_t c = live ? K_ACC : K_BLUE;
        if (live)
            tn_poly(x, ly, TN_NPT, 2, K_ACC);
        if (dx >= TN_FX0)                       /* (below the plot's 20 Hz: no dot, the label says it) */
            tn_dot(dx, dy, 3, c);
        tn_text_r(TN_FX0 + TN_FW, tn_dby(0.0f) - 14, &FONT_XS, "0 dB", K_DIM);   /* (over the curves) */
        cv_text(18, 5, &FONT_XS, sl, K_DIM);
        tn_hz_text(hz, t);
        tn_label_place(dx, dy, text_w(&FONT_S, t), text_w(&FONT_XS, sl), text_w(&FONT_XS, "0 dB"), &lx, &ly0);
        tn_text_o(lx, ly0, &FONT_S, t, c, 0);
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- Noise --- */
/* the noise layer's filter (wind_voice.cc): Plaits' state-variable one, LP .. BP .. HP as Type goes
 * 0 .. 100 (the outputs mixed in between), at the note plus Freq, Q 0.5 x 32^Reso; its magnitude */
static float tn_noise_mag(float x, float type, float q)
{
    float x2 = x * x, lp = fm_maxf(1.0f - 2.0f * type, 0.0f), bp = 1.0f - 2.0f * fm_fabsf(type - 0.5f),
          hp = fm_minf(1.0f - 2.0f * type, 0.0f), re = lp - hp * x2, im = bp * x;
    float d = (1.0f - x2) * (1.0f - x2) + x2 / (q * q);
    return fm_sqrtf((re * re + im * im) / (d + 1e-12f));
}

static void tn_noise(void)
{
    int n = tn_note(), i, type = proj.par[P_NZTYPE], semis = proj.par[P_NZFREQ], lvl = proj.par[P_NZLEVEL];
    const int32_t top = 16, bot = 122, hh = bot - top - 8;
    float fc, q, mag[TN_NPT + 1], mx = 1e-9f, nhz, iw;
    const float *w = tn_wpts();
    int16_t x[TN_NPT + 1], y[TN_NPT + 1];
    int np = 0;
    int32_t fx;
    char t[12];
    uint32_t h = hash(hash(tn_hash0(V_NOISE), (uint32_t)type | (uint32_t)(semis + 64) << 8 | (uint32_t)lvl << 16),
                      (uint32_t)proj.par[P_NZRESO] | (uint32_t)(n + 64) << 8);
    if (!tn_begin(h))
        return;
    nhz = tn_note_hz((float)n);
    fc = fm_clampf(tn_note_hz((float)(n + semis)), 5.0f, 0.4f * TN_SR);
    q = 0.5f * fm_exp2f((float)proj.par[P_NZRESO] * 0.05f);
    iw = 1.0f / tn_warp(fc);
    fx = tn_fx(fc);
    for (i = 0; i < TN_NPT; i++) {                /* the curve's points, and fc among them (a narrow peak) */
        int32_t px = TN_FX0 + i * TN_FW / (TN_NPT - 1);
        if (fx > (np ? x[np - 1] : TN_FX0 - 1) && fx < px && fx <= TN_FX0 + TN_FW) {
            x[np] = (int16_t)fx;
            mag[np++] = tn_noise_mag(1.0f, (float)type * 0.01f, q);
        }
        x[np] = (int16_t)px;
        mag[np++] = tn_noise_mag(w[i] * iw, (float)type * 0.01f, q);
    }
    for (i = 0; i < np; i++)
        mx = mag[i] > mx ? mag[i] : mx;
    for (i = 0; i < np; i++)
        y[i] = (int16_t)(bot - (int32_t)((float)hh * mag[i] / mx + 0.5f));
    tn_fgrid(top, bot);
    for (i = 0; i < 70; i++) {                    /* the level: the noise through it, as bars, never over the curve */
        int32_t bx = TN_FX0 + i * 3, bt, c;
        float m = tn_noise_mag(tn_warp(20.0f * fm_exp2f((float)(i * 3) * (9.965784f / (float)TN_FW))) * iw,
                               (float)type * 0.01f, q);
        bt = bot - (int32_t)((float)hh * fm_minf(m / mx, 1.0f) * (float)lvl * 0.01f + 0.5f);
        for (c = bx; c < bx + 2; c++) {
            int32_t cy = tn_poly_y(x, y, np, c);
            if (bt < cy)
                bt = cy;
        }
        if (bt < bot)
            cv_rect(bx, bt, 2, bot - bt, K_BLUE);
    }
    tn_poly(x, y, np, 2, K_ACC);
    if (nhz >= 20.0f && nhz <= 20000.0f) {        /* the note, and where Freq puts the filter from it */
        int32_t nx = tn_fx(nhz);
        tn_vdash(nx, top, bot, K_TEXT);
        note_text(n, t);
        tn_text_o(nx + 3, top - 2, &FONT_XS, t, K_TEXT, 0);
    }
    wa_param_text(P_NZFREQ, semis, t);
    {   /* (on the side away from the note, clear of the peak) */
        int al = semis < 0 ? fx > 50 : fx > 190;
        int32_t lx = al ? fx - 7 : fx + 7;
        lx = !al && lx < TN_FX0 + 2 ? TN_FX0 + 2 : lx;  /* (fc below the plot: the label still in it) */
        tn_text_o(lx, top + 12, &FONT_XS, t, K_ACC, al ? 2 : 0);
    }
    wa_param_text(P_NZTYPE, type, t);
    cv_text(18, 5, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------ Sample --- */
#define TN_SX0 16                      /* the wave: from here, a column a pixel, */
#define TN_SW 208
static int32_t tn_round(float x);
static void tn_sample_note(const char *a, const char *b)   /* no sample to show: why, and what to do */
{
    text_c(120, 44, &FONT_B, a, K_TEXT);
    text_c(120, 70, &FONT_XS, b, K_DIM);
    cv_blit(0, MAIN_Y);
}

static void tn_sample(void)
{
    static float lo[TN_SW], hi[TN_SW];
    static uint32_t got_gen = 0xFFFFFFFFu;
    static int got_slot = -1;
    const int32_t mid = 74, a = 34;
    int slot = proj.par[P_SMSLOT], i, has;
    ws_info_t in;
    float at = wa_state.sample_at;
    int32_t px = at >= 0.0f ? TN_SX0 + tn_round(at * (float)(TN_SW - 1)) : -1, sx, lx0, lx1;
    char t[24];
    uint32_t h;
    has = slot > 0 && ws_slot_info(slot, &in);
    h = hash(hash(hash(tn_hash0(V_SAMPLE), (uint32_t)slot | (uint32_t)has << 6 | (uint32_t)proj.par[P_SMSTART] << 8 |
                                               (uint32_t)proj.par[P_SMLOOP] << 16 | (uint32_t)(proj.par[P_SMTUNE] + 24) << 20),
                  ws_gen() ^ (uint32_t)(px + 1) << 16),
             (uint32_t)proj.par[P_SMTRACK] | (uint32_t)ws_used() << 8);
    if (!tn_begin(h))
        return;
    if (!slot) {
        tn_sample_note(ws_used() ? "Off" : "No samples yet", ws_used() ? "Knob 1 picks a sample" : "Load them from the web page");
        return;
    }
    if (!has) {
        tn_sample_note("Empty slot", "Load a sample into it from the web page");
        return;
    }
    if (got_gen != ws_gen() || got_slot != slot) {   /* (read from flash once a sample) */
        got_gen = ws_gen();
        got_slot = slot;
        if (!ws_peaks(slot, TN_SW, lo, hi))
            for (i = 0; i < TN_SW; i++)
                lo[i] = hi[i] = 0.0f;
    }
    wa_param_text(P_SMSLOT, slot, t);                /* "3 SAX", and its root, rate and length */
    cv_text(18, 8, &FONT_B, t, K_TEXT);
    {
        char r[24], n[8];
        int k = 0, j;
        uint32_t ds = (in.len * 10u + in.rate / 2u) / in.rate;   /* tenths of a second */
        note_text(in.root, n);
        for (j = 0; n[j]; j++)
            r[k++] = n[j];
        r[k++] = ' ';
        r[k++] = ' ';
        itoa_u((in.rate + 500u) / 1000u, r + k);
        while (r[k])
            k++;
        r[k++] = 'k';
        r[k++] = ' ';
        r[k++] = ' ';
        itoa_u(ds / 10u, r + k);
        while (r[k])
            k++;
        r[k++] = '.';
        r[k++] = (char)('0' + ds % 10u);
        r[k++] = 's';
        r[k] = 0;
        tn_text_r(226, 11, &FONT_XS, r, K_DIM);
    }
    lx0 = TN_SX0 + (int32_t)((float)(in.loop_end ? in.loop_start : 0u) * (float)TN_SW / (float)in.len);
    lx1 = TN_SX0 + (int32_t)((float)(in.loop_end ? in.loop_end : in.len) * (float)TN_SW / (float)in.len) - 1;
    if (proj.par[P_SMLOOP] != WS_LOOP_OFF) {         /* the loop, shaded, with its ends */
        cv_rect(lx0, mid - a - 4, lx1 - lx0 + 1, 2 * a + 9, blend565(K_BLUE_T, K_PANEL, 96u));
        cv_rect(lx0, mid - a - 4, 1, 2 * a + 9, K_BLUE);
        cv_rect(lx1, mid - a - 4, 1, 2 * a + 9, K_BLUE);
    }
    tn_hdash(TN_SX0, TN_SX0 + TN_SW, mid, K_LINE);
    for (i = 0; i < TN_SW; i++) {
        int32_t y0 = mid - tn_round(hi[i] * (float)a), y1 = mid - tn_round(lo[i] * (float)a);
        cv_rect(TN_SX0 + i, y0, 1, y1 - y0 + 1, K_BLUE);
    }
    sx = TN_SX0 + tn_round((float)proj.par[P_SMSTART] * 0.01f * (float)(TN_SW - 1));
    tn_vdash(sx, mid - a - 4, mid + a + 5, K_TEXT);   /* Start */
    if (px >= 0) {                                    /* where it plays */
        cv_rect(px, mid - a - 4, 1, 2 * a + 9, K_ACC);
        tn_dot(px, mid + a + 9, 3, K_ACC);
    }
    wa_param_text(P_SMLOOP, proj.par[P_SMLOOP], t);   /* the loop and how it's pitched */
    cv_text(18, 120, &FONT_XS, t, K_DIM);
    if (proj.par[P_SMTRACK]) {
        tn_text_r(226, 120, &FONT_XS, "follows the note", K_DIM);
    } else {
        wa_param_text(P_SMTUNE, proj.par[P_SMTUNE], t);
        tn_text_r(226, 120, &FONT_XS, proj.par[P_SMTUNE] ? t : "its own pitch", K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------ Breath --- */
static void tn_breath(void)
{
    const int32_t px = 18, py = 26, pw = 102, ph = 92, x0 = 142, base = 120, hh = 88;
    uint32_t cnt = 84, s = tn_shown(&cnt), i;
    float raw = fm_clampf(tn_s.raw, 0.0f, 1.0f);
    int32_t ix = px + (int32_t)(raw * (float)pw + 0.5f), iy = py + ph - (int32_t)(tn_curve(raw) * (float)ph + 0.5f);
    int16_t cx[TN_NPT], cy[TN_NPT];
    char t[16];
    uint32_t h = hash(hash(hash(tn_hash0(V_BREATH), (uint32_t)(proj.par[P_CURVE] + 100) | (uint32_t)proj.par[P_BSRC] << 8),
                           (uint32_t)ix | (uint32_t)iy << 8 | (uint32_t)(raw * 127.0f + 0.5f) << 16),
                      s);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(6, 4, 124, 136, 6, K_PANEL);
    cv_round(134, 4, 100, 136, 6, K_PANEL);
    /* the curve: in across, out up; straight is the dashed diagonal */
    cv_rect(px, py + ph, pw, 1, K_LINE);
    cv_rect(px, py, 1, ph, K_LINE);
    for (i = 0; i < (uint32_t)pw; i += 4)
        cv_rect(px + (int32_t)i, py + ph - (int32_t)i * ph / pw, 2, 1, K_LINE);
    for (i = 0; i < TN_NPT; i++) {
        float xin = (float)i / (float)(TN_NPT - 1);
        cx[i] = (int16_t)(px + (int32_t)(xin * (float)pw + 0.5f));
        cy[i] = (int16_t)(py + ph - (int32_t)(tn_curve(xin) * (float)ph + 0.5f));
    }
    tn_poly(cx, cy, TN_NPT, 2, K_BLUE);
    {
        uint16_t d = blend565(K_ACC, K_PANEL, 160u);
        int32_t k;
        for (k = iy + 4; k < py + ph; k += 4)
            cv_rect(ix, k, 1, 2, d);
        for (k = px + 2; k < ix - 4; k += 4)
            cv_rect(k, iy, 2, 1, d);
        tn_dot(ix, iy, 4, K_ACC);
    }
    tn_text_r(px + pw, py + ph + 3, &FONT_XS, "IN", K_DIM);
    cv_text(px + 4, 4, &FONT_XS, "OUT", K_DIM);
    wa_param_text(P_BSRC, proj.par[P_BSRC], t);
    i = (uint32_t)s_len(t);
    t[i++] = ' ';
    itoa_u((uint32_t)(raw * 127.0f + 0.5f), t + i);
    tn_text_r(124, 4, &FONT_XS, t, K_TEXT);
    /* smoothing: the breath as it came in (dim) and out of the gate envelope (Attack, Release) */
    cv_text(142, 8, &FONT_XS, "SMOOTHING", K_DIM);
    cv_rect(x0, base + 1, 84, 1, K_LINE);
    for (i = 1; i < cnt; i++) {
        const tn_ent_t *a = tn_at(s, cnt, i - 1), *b = tn_at(s, cnt, i);
        int32_t xa = x0 + 84 - (int32_t)cnt + (int32_t)i - 1;
        tn_line(xa, base - a->raw * hh / 255, xa + 1, base - b->raw * hh / 255, 1, K_DIM);
    }
    for (i = 1; i < cnt; i++) {
        const tn_ent_t *a = tn_at(s, cnt, i - 1), *b = tn_at(s, cnt, i);
        int32_t xa = x0 + 84 - (int32_t)cnt + (int32_t)i - 1;
        tn_line(xa, base - a->out * hh / 255, xa + 1, base - b->out * hh / 255, 2, K_ACC);
    }
    cv_text(142, 123, &FONT_XS, "raw", K_DIM);
    tn_text_r(226, 123, &FONT_XS, "out", K_ACC);
    cv_blit(0, MAIN_Y);
}

/* "+1.25", "-0.01", "0.00": hundredths, signed */
static void tn_fix2(int v, char *t)
{
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    if (v)
        *t++ = v < 0 ? '-' : '+';
    itoa_u(a / 100u, t);
    t += s_len(t);
    *t++ = '.';
    *t++ = (char)('0' + a / 10u % 10u);
    *t++ = (char)('0' + a % 10u);
    *t = 0;
}

/* "2.8 s", bottom left: the span of n entries */
static void tn_span(int32_t y, uint32_t n)
{
    char t[12];
    uint32_t ds = (n * TN_MS + 50u) / 100u, i;
    itoa_u(ds / 10u, t);
    i = (uint32_t)s_len(t);
    t[i++] = '.', t[i++] = (char)('0' + ds % 10u), t[i++] = ' ', t[i++] = 's', t[i] = 0;
    cv_text(14, y, &FONT_XS, t, K_DIM);
}

/* ------------------------------------------------------------- Pitch --- */
/* the phrase: the pitch over the last 2.8 s (glide and bite in it), each note's start marked and named,
 * breath under it; the note, the bite and the glide time now */
static int32_t tn_py(int p, float center)
{
    return (int32_t)fm_clampf(64.0f - ((float)p - center) * 0.036f + 0.5f, 20.0f, 108.0f);
}

static void tn_pitch(void)
{
    const int32_t now = 184, base = 132;
    uint32_t cnt = 172, s = tn_shown(&cnt), i, ng = 0;
    int bq = (int)(tn_s.bend * (float)proj.par[P_BEND] * 100.0f + (tn_s.bend < 0.0f ? -0.5f : 0.5f));
    int gms = (int)(tn_s.glide + 0.5f), top = tn_s.top >= 0 ? tn_s.top + proj.par[P_TRANSPOSE] : -1000;
    int32_t x0 = now - (int32_t)cnt + 1, prev = -1, py;
    float center = 0.0f;
    uint16_t fill = blend565(K_ACC, K_PANEL, proj.par[P_THEME] ? 41u : 31u);
    char t[16];
    uint32_t h = hash(hash(hash(tn_hash0(V_PITCH), s), (uint32_t)(bq + 5000) | (uint32_t)(top + 2000) << 16),
                      (uint32_t)gms | (uint32_t)tn_s.gliding << 16 | (uint32_t)tn_s.gate << 31 | (uint32_t)(proj.par[P_TRANSPOSE] + 64) << 17);
    if (!tn_begin(h))
        return;
    for (i = 0; i < cnt; i++) {
        const tn_ent_t *e = tn_at(s, cnt, i);
        int32_t bh = e->br * 34 / 255;
        if (bh > 0)
            cv_rect(x0 + (int32_t)i, base - bh, 1, bh, fill);
        if (e->fl & TN_GATE) {
            center += (float)e->p;
            ng++;
        }
    }
    center = ng ? center / (float)ng : 6600.0f;
    for (i = 0; i < cnt; i++) {                   /* the pitch, where a note sounds */
        const tn_ent_t *e = tn_at(s, cnt, i);
        if (!(e->fl & TN_GATE)) {
            prev = -1;
            continue;
        }
        py = tn_py(e->p, center);
        if (prev >= 0)
            tn_line(x0 + (int32_t)i - 1, prev, x0 + (int32_t)i, py, 2, K_BLUE);
        else
            cv_rect(x0 + (int32_t)i, py, 1, 2, K_BLUE);
        prev = py;
    }
    prev = -100;
    for (i = 0; i < cnt; i++)                     /* each note's start */
        if (tn_at(s, cnt, i)->fl & TN_ON)
            cv_rect(x0 + (int32_t)i, tn_py(tn_at(s, cnt, i)->p, center) - 3, 1, 7, K_TEXT);
    for (i = 0; i < cnt; i++) {                   /* ...named, unless one was just named */
        const tn_ent_t *e = tn_at(s, cnt, i);
        if ((e->fl & TN_ON) && x0 + (int32_t)i - prev >= 20) {
            py = tn_py(e->p, center);
            prev = x0 + (int32_t)i;
            note_text(e->top + proj.par[P_TRANSPOSE], t);
            tn_text_o(x0 + (int32_t)i + 2, py - 18, &FONT_XS, t, K_TEXT, 0);
        }
    }
    tn_vdash(now, 10, 134, K_LINE);
    if (cnt && (tn_at(s, cnt, cnt - 1)->fl & TN_GATE))
        tn_dot(now, tn_py(tn_at(s, cnt, cnt - 1)->p, center), 3, K_BLUE);
    tn_span(122, 172);
    /* now: the note, the bite (semitones), the glide time */
    text_c(209, 10, &FONT_XS, "NOTE", K_DIM);
    if (top > -1000)
        note_text(top, t);
    else
        t[0] = '-', t[1] = 0;
    text_c(209, 22, &FONT_B, t, tn_s.gate ? K_TEXT : K_DIM);   /* (let go: dim) */
    text_c(209, 44, &FONT_XS, "BITE", K_DIM);
    tn_fix2(bq, t);
    text_c(209, 56, &FONT_S, t, K_BLUE);
    text_c(209, 78, &FONT_XS, "GLIDE", K_DIM);
    itoa_u((uint32_t)gms, t);
    text_c(209, 90, &FONT_S, t, tn_s.gliding ? K_ACC : K_TEXT);
    text_c(209, 104, &FONT_XS, "ms", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* ----------------------------------------------------------- Pitch 2 --- */
/* the bite's ruler, Bend semitones each way, with the bite now; the glide: how the pitch gets to a new
 * note over time (a straight line, a constant time), at Glide alone, at Glide plus all of the CC depth
 * (dashed), and now */
static void tn_pitch2(void)
{
    const int32_t rx = 22, rw = 196, ry = 38, gx = 20, gw = 200, gy0 = 118, gy1 = 88;
    static const int SPAN[6] = {100, 250, 500, 1000, 2000, 4000};
    int r = proj.par[P_BEND], gl = proj.par[P_GLIDE], gd = proj.par[P_GDEPTH], i, step, span = 4000;
    int bq = (int)(tn_s.bend * (float)r * 100.0f + (tn_s.bend < 0.0f ? -0.5f : 0.5f));
    int gms = (int)(tn_s.glide + 0.5f), cc = gms - gl;
    int32_t mid = rx + rw / 2, bx;
    char t[24];
    uint32_t h = hash(hash(tn_hash0(V_PITCH2), (uint32_t)r | (uint32_t)gl << 8 | (uint32_t)gd << 20),
                      (uint32_t)(bq + 5000) | (uint32_t)gms << 16);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(6, 4, 228, 60, 6, K_PANEL);
    cv_round(6, 68, 228, 72, 6, K_PANEL);
    /* bite */
    cv_text(14, 8, &FONT_XS, "BITE", K_DIM);
    tn_fix2(bq, t);
    i = s_len(t);
    t[i++] = ' ', t[i++] = 's', t[i++] = 't', t[i] = 0;
    tn_text_r(226, 6, &FONT_S, t, K_TEXT);
    cv_rect(rx, ry, rw, 2, K_LINE);
    step = r <= 4 ? 1 : r <= 8 ? 2 : 4;               /* a label every step semitones */
    for (i = r <= 2 ? -4 * r : -r; r > 0 && i <= (r <= 2 ? 4 * r : r); i++) {
        int st4 = r <= 2 ? i : i * 4;                 /* in quarter semitones */
        int32_t x = mid + (int32_t)st4 * (rw / 2) / (4 * r);
        int v = st4 / 4, end = v == r || v == -r;
        int major = st4 % 4 == 0 && (end || (v % step == 0 && (r - (v < 0 ? -v : v)) * (rw / 2) >= 18 * r));
        cv_rect(x, ry - (major ? 6 : 3), 1, major ? 8 : 5, major ? K_DIM : K_LINE);
        if (major) {
            t[0] = v > 0 ? '+' : '-';
            itoa_u((uint32_t)(v < 0 ? -v : v), t + (v != 0));
            text_c(x, ry + 6, &FONT_XS, t, K_DIM);
        }
    }
    if (!r)
        text_c(mid, ry + 6, &FONT_XS, "0", K_DIM);
    bx = r ? mid + (int32_t)bq * (rw / 2) / (100 * r) : mid;
    bx = bx < rx ? rx : bx > rx + rw ? rx + rw : bx;
    cv_round(bx < mid ? bx : mid, ry - 2, (bx < mid ? mid - bx : bx - mid) + 3, 6, 3, K_BLUE);
    for (i = 0; i < 6; i++)                           /* the pointer */
        cv_rect(bx - 5 + i, ry - 13 + i, 11 - 2 * i, 1, K_BLUE);
    /* glide */
    cv_text(14, 72, &FONT_XS, "GLIDE", K_DIM);
    itoa_u((uint32_t)gl, t);
    i = s_len(t);
    t[i++] = ' ', t[i++] = '+', t[i++] = ' ';
    itoa_u((uint32_t)(cc > 0 ? cc : 0), t + i);
    i = s_len(t);
    t[i++] = ' ', t[i++] = 'm', t[i++] = 's', t[i] = 0;
    tn_text_r(226, 70, &FONT_S, t, cc > 0 ? K_BLUE : K_TEXT);
    for (i = 0; i < 6; i++)
        if (SPAN[i] >= gl + gd) {
            span = SPAN[i];
            break;
        }
    cv_rect(gx, gy0 + 2, gw, 1, K_LINE);
    {
        int32_t xa = gx + gl * gw / span, xb = gx + (gl + gd > span ? span : gl + gd) * gw / span,
                xn = gx + (gms > span ? span : gms) * gw / span, k;
        tn_line(gx, gy0, xa, gy1, 1, K_DIM);           /* Glide alone */
        cv_rect(xa, gy1, gx + gw - xa, 1, K_DIM);
        for (k = 0; k < 32; k += 2)                    /* with all the CC adds: dashed */
            cv_line(gx + (xb - gx) * k / 32, gy0 - (gy0 - gy1) * k / 32, gx + (xb - gx) * (k + 1) / 32,
                    gy0 - (gy0 - gy1) * (k + 1) / 32, K_DIM);
        for (k = xb; k < gx + gw; k += 5)
            cv_rect(k, gy1, 2, 1, K_DIM);
        tn_line(gx, gy0, xn, gy1, 2, K_BLUE);          /* now */
        cv_rect(xn, gy1, gx + gw - xn, 2, K_BLUE);
        tn_dot(xn, gy1, 3, K_ACC);
    }
    cv_text(gx, gy0 + 4, &FONT_XS, "0", K_DIM);
    itoa_u((uint32_t)span, t);
    i = s_len(t);
    t[i++] = ' ', t[i++] = 'm', t[i++] = 's', t[i] = 0;
    tn_text_r(gx + gw, gy0 + 4, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* --------------------------------------------------------------- LFO --- */
/* the LFO's shape over one cycle (wind_app's): S&H holds one value a cycle, the one it has now */
static float tn_lfo_shape(int sh, float ph)
{
    switch (sh) {
    case WA_LFO_TRI: return 1.0f - 4.0f * fm_fabsf(ph + 0.25f - fm_floorf(ph + 0.25f) - 0.5f);
    case WA_LFO_SAW: return 2.0f * ph - 1.0f;
    case WA_LFO_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    case WA_LFO_SH: return tn_s.fade > 0.01f ? fm_clampf(tn_s.lfo / tn_s.fade, -1.0f, 1.0f) : 0.0f;
    default: return fm_sin_turns(ph);
    }
}

static int32_t tn_round(float x) { return (int32_t)(x + (x < 0.0f ? -0.5f : 0.5f)); }

/* one cycle of the shape, where it is in it now (the dot: as faded in; the small one: in full), the
 * fade, and the destination's value now */
static void tn_lfo(void)
{
    const int32_t x0 = 16, w = 208, mid = 48, a = 28;
    int sh = proj.par[P_LFOSHAPE], i, dst = proj.par[P_LFODST];
    float ph = fm_clampf(tn_s.ph, 0.0f, 0.999f), v = tn_lfo_shape(sh, ph), fade = fm_clampf(tn_s.fade, 0.0f, 1.0f);
    int32_t px = x0 + tn_round(ph * (float)w), yf = mid - tn_round(v * fade * (float)a), yv = mid - tn_round(v * (float)a),
            fw = tn_round(70.0f * fade), dv = tn_round(tn_s.dv);
    int16_t x[105], y[105];
    char t[16];
    uint32_t h = hash(hash(hash(tn_hash0(V_LFO), (uint32_t)sh | (uint32_t)dst << 4 | (uint32_t)proj.par[P_LFORATE] << 8 |
                                                 (uint32_t)proj.par[P_LFOFADE] << 16),
                           (uint32_t)px | (uint32_t)(yf + 100) << 8 | (uint32_t)(yv + 100) << 16 | (uint32_t)fw << 24),
                      (uint32_t)(dv + 100000) | (uint32_t)proj.par[P_ENGINE] << 24);
    if (!tn_begin(h))
        return;
    tn_hdash(x0, x0 + w, mid, K_LINE);
    for (i = 0; i < 105; i++) {
        x[i] = (int16_t)(x0 + i * 2);
        y[i] = (int16_t)(mid - tn_round(tn_lfo_shape(sh, (float)i * (0.9999f / 104.0f)) * (float)a));
    }
    tn_poly(x, y, 105, 2, K_BLUE);
    cv_rect(px, mid - a - 6, 1, 2 * a + 11, blend565(K_ACC, K_PANEL, 128u));
    tn_dot(px, yf, 4, K_ACC);
    tn_dot(px, yv, 2, K_BLUE);
    wa_param_text(P_LFORATE, proj.par[P_LFORATE], t);
    tn_text_r(226, 6, &FONT_XS, t, K_DIM);
    cv_text(14, 104, &FONT_XS, "FADE", K_DIM);
    cv_round(48, 109, 70, 6, 3, K_LINE);
    cv_round(48, 109, fw < 6 ? 6 : fw, 6, 3, K_BLUE);
    wa_param_text(P_LFOFADE, proj.par[P_LFOFADE], t);
    cv_text(122, 104, &FONT_XS, t, K_DIM);
    wa_param_text(P_LFODST, dst, t);                  /* the destination, and its value now */
    tn_text_r(226, 84, &FONT_XS, t, K_DIM);
    if (dst == WM_DST_PITCH) {                        /* cents, signed */
        t[0] = dv > 0 ? '+' : '-';
        itoa_u((uint32_t)(dv < 0 ? -dv : dv), t + (dv != 0));
    } else {
        itoa_u((uint32_t)(dv < 0 ? 0 : dv), t);
    }
    tn_text_r(226, 100, &FONT_M, t, K_ACC);
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- LFO 2 --- */
/* the LFO over the last 2.9 s, inside its fade (dim), with where a note started it again (Sync Note) */
static void tn_lfo2(void)
{
    const int32_t now = 222, mid = 72, a = 46;
    uint32_t cnt = TN_N, s = tn_shown(&cnt), i;
    int32_t x0 = now - (int32_t)cnt + 1;
    uint16_t sc = blend565(K_TEXT, K_PANEL, 140u);
    if (!tn_begin(hash(tn_hash0(V_LFO2), s)))
        return;
    tn_hdash(14, now, mid, K_LINE);
    for (i = 0; i < cnt; i++)
        if (tn_at(s, cnt, i)->fl & TN_SYNC) {
            cv_rect(x0 + (int32_t)i, 16, 1, 112, sc);
            if (x0 + (int32_t)i + 3 + text_w(&FONT_XS, "sync") <= 228)
                cv_text(x0 + (int32_t)i + 3, 6, &FONT_XS, "sync", K_DIM);
            else
                tn_text_r(x0 + (int32_t)i - 3, 6, &FONT_XS, "sync", K_DIM);
        }
    for (i = 1; i < cnt; i++) {
        const tn_ent_t *p = tn_at(s, cnt, i - 1), *e = tn_at(s, cnt, i);
        int32_t x = x0 + (int32_t)i;
        cv_line(x - 1, mid - p->fade * a / 255, x, mid - e->fade * a / 255, K_DIM);
        cv_line(x - 1, mid + p->fade * a / 255, x, mid + e->fade * a / 255, K_DIM);
    }
    for (i = 1; i < cnt; i++) {
        const tn_ent_t *p = tn_at(s, cnt, i - 1), *e = tn_at(s, cnt, i);
        int32_t x = x0 + (int32_t)i;
        tn_line(x - 1, mid - p->lfo * a / 127, x, mid - e->lfo * a / 127, 2, K_BLUE);
    }
    if (cnt)
        tn_dot(now, mid - tn_at(s, cnt, cnt - 1)->lfo * a / 127, 3, K_ACC);
    tn_span(122, TN_N);
    cv_blit(0, MAIN_Y);
}

/* the band, on one of these pages: 1 (drawn, or nothing changed), 0 not one of them */
static int vw_tone(int v)
{
    int mine = 1;
    switch (v) {
    case V_FILTER: tn_filter(); break;
    case V_NOISE: tn_noise(); break;
    case V_SAMPLE: case V_SAMPLE2: tn_sample(); break;
    case V_BREATH: tn_breath(); break;
    case V_PITCH: tn_pitch(); break;
    case V_PITCH2: tn_pitch2(); break;
    case V_LFO: tn_lfo(); break;
    case V_LFO2: tn_lfo2(); break;
    default: mine = 0; break;
    }
    return mine;
}

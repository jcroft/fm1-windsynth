/* SPDX-License-Identifier: GPL-3.0-only */
/* The FX pages' views (Distortion, Modulation, Delay, Reverb, Reverb 2), in the middle band. Included by
 * ui.c above draw_main, so it uses ui.c's statics. Each draws from the page's settings (proj.par) and the
 * effects' own numbers (wind_fx.c's wf_ds_shape, wf_rv_rt_at and the rest), so the pictures are the sound's.
 *
 *   the chain     along the top on all five: DIST > MOD > DELAY > VERB, the page's lit, each with a bar
 *                 under its name while its Mix is up (a stub at 1, full at 50, where the wet is)
 *   Distortion    the shaper's curve for the Type at this Drive (in across, out up), a dot where the note
 *                 is on it; a sine before it and after it, Tone's low-pass included
 *   Modulation    the two sides' delay times over the last few seconds (Phaser: the notches' frequency),
 *                 moving while a note plays (not through the delay's or the reverb's tail after)
 *   Delay         the echoes over time from Time and Feedback (Ping-pong: left and right in turn; Tape and
 *                 Analog: darker each repeat), the dry first; each lights as it sounds
 *   Reverb        the tail's fall in dB over a log time axis (50 ms .. 80 s): Decay moves where it reaches
 *                 -60 dB, Pre-delay where it starts, Tone the highs' faster fall, Mix the fill's strength;
 *                 feeding while a note plays, a marker running down it after
 *   Reverb 2      the room: its size from Size and the Type, its walls moving with Mod, the pre-delay on
 *                 the direct path, Shimmer's octaves climbing; Low end as the bass's decay against the rest
 *
 * A page whose Mix is 0 (the effect off) draws in grey. Moving parts (and the levels they follow) step at
 * most every 32 ms, only while a note sounds through an effect that's on, and stop where they are when it's
 * quiet; what's hashed is what's drawn (an echo lit, a marker's column), so a still picture isn't redrawn. */
#include "../dsp/fastmath.h"

static uint16_t blend565(uint16_t a, uint16_t b, uint32_t n);   /* (ui.c, below) */

static uint32_t fx_t;                  /* the time the moving parts are drawn at (ms, 32 ms steps; held when quiet) */
static uint32_t fx_on_t, fx_off_t;     /* when the last note began, and ended (32 ms steps) */
static uint8_t fx_gate, fx_seen;       /* the gate last frame; seen: 1 a note has begun, 2 one has ended */
static float fx_mph, fx_fph, fx_rph;   /* phases (turns): the modulation's LFO, its fast one, the reverb's ring */
static float fx_lvl, fx_amp;           /* the output's level and the voice's, as of the last step */

static uint16_t fx_mix(uint16_t a, uint16_t b, float t)       /* a .. b, t 0..1 */
{
    return blend565(b, a, (uint32_t)((t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t) * 256.0f));
}

static int32_t fx_px(float v) { return (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f); }

static void fx_line2(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* 2 px thick */
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}

static void fx_dash(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)    /* 2 on, 3 off */
{
    int32_t dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx)
                                                                                      : (dy < 0 ? -dy : dy), i;
    for (i = 0; i <= n; i++)
        if (i % 5 < 2)
            cv_pset(x0 + (n ? dx * i / n : 0), y0 + (n ? dy * i / n : 0), c);
}

static void fx_dot(int32_t x, int32_t y, int32_t r, uint16_t c) { cv_round(x - r, y - r, 2 * r + 1, 2 * r + 1, r, c); }

static void fx_text_r(int32_t x, int32_t y, const char *s, uint16_t c) { cv_text(x - text_w(&FONT_XS, s), y, &FONT_XS, s, c); }

/* text on a patch of the panel, over whatever a curve drew there */
static void fx_label(int32_t x, int32_t y, const char *s)
{
    cv_rect(x - 2, y + 2, text_w(&FONT_XS, s) + 3, 11, K_PANEL);
    cv_text(x, y, &FONT_XS, s, K_DIM);
}

/* v as text with dec decimals (v >= 0): "2.5", "10" */
static char *fx_num(char *b, float v, int dec)
{
    uint32_t sc = dec == 2 ? 100u : dec == 1 ? 10u : 1u, q = (uint32_t)(v * (float)sc + 0.5f);
    itoa_u(q / sc, b);
    b += s_len(b);
    if (dec) {
        *b++ = '.';
        if (dec == 2)
            *b++ = (char)('0' + q / 10u % 10u);
        *b++ = (char)('0' + q % 10u);
        *b = 0;
    }
    return b;
}

static char *fx_cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}

static float fx_wet(int mix) { return mix >= 50 ? 1.0f : (float)mix * 0.02f; }   /* (wind_fx.c's wet_of) */
static float fx_dry(int mix) { return mix <= 50 ? 1.0f : (float)(100 - mix) * 0.02f; }

/* the time span to draw, from a list (ms): the shortest at least need */
static uint32_t fx_span(float need)
{
    static const uint32_t SPAN[7] = {200, 400, 800, 1600, 3200, 6400, 12800};
    uint32_t i;
    for (i = 0; i < 6u && (float)SPAN[i] < need; i++)
        ;
    return SPAN[i];
}

static void fx_ms_text(char *b, uint32_t ms)    /* "800 ms", "1.6 s", "12.8 s" */
{
    if (ms < 1000u) {
        itoa_u(ms, b);
        fx_cat(b + s_len(b), " ms");
    } else {
        fx_cat(fx_num(b, (float)ms * 0.001f, 1), " s");
    }
}

/* ---------------------------------------------------------- the chain --- */
static const uint8_t FX_MIXP[4] = {P_DSMIX, P_MOMIX, P_DLMIX, P_RVMIX};

static void fx_chain(int cur)
{
    static const char *const NM[4] = {"DIST", "MOD", "DELAY", "VERB"};
    int i;
    for (i = 0; i < 4; i++) {
        int32_t x = 6 + i * 58, w = 52, m = proj.par[FX_MIXP[i]];
        int on = i == cur;
        if (i)
            cv_rect(x - 5, 11, 4, 1, K_LINE);
        cv_round(x, 2, w, 20, 5, on ? K_ACC : K_PANEL);
        text_c(x + w / 2, 1, &FONT_S, NM[i], on ? K_WHITE : K_DIM);
        if (m > 0)                       /* the wet's level: a stub that reads as on, full at 50 */
            cv_rect(x + 4, 17, 8 + (w - 16) * (m > 50 ? 50 : m) / 50, 2, on ? K_WHITE : K_ACC);
    }
}

/* --------------------------------------------------------- Distortion --- */
#define FX_DS_S 46                     /* the curve's half size */
#define FX_DS_W 84                     /* the waves' width */

/* a wave's columns, each from its lowest to its highest (and on from the last), 2 px thick */
static void fx_wave(int32_t x0, int32_t mid, const float *mn, const float *mx, const float *last, uint16_t c)
{
    int32_t i;
    for (i = 0; i < FX_DS_W; i++) {
        float lo = mn[i], hi = mx[i];
        int32_t a, b;
        if (i) {
            lo = lo < last[i - 1] ? lo : last[i - 1];
            hi = hi > last[i - 1] ? hi : last[i - 1];
        }
        a = mid - fx_px(hi * 17.0f);
        b = mid - fx_px(lo * 17.0f);
        cv_rect(x0 + i, a, 1, b - a + 2, c);
    }
}

static void fx_dist(int on, int32_t amp_px)
{
    uint32_t type = (uint32_t)proj.par[P_DSTYPE], hold;
    float drive = (float)proj.par[P_DSDRIVE] * 0.01f, tone = wf_ds_tone_hz((float)proj.par[P_DSTONE] * 0.01f);
    static float yv[2 * FX_DS_S + 1], bmn[FX_DS_W], bmx[FX_DS_W], blast[FX_DS_W], amn[FX_DS_W], amx[FX_DS_W],
        alast[FX_DS_W];                  /* (off the UI's stack) */
    float norm = 1e-6f, ph = 0.0f, a, l1 = 0.0f, l2 = 0.0f, held = 0.0f;
    float per = WV_SR / 440.0f;
    int32_t cx = 68, cy = 84, s = FX_DS_S, i, k, warm = (int32_t)(3.0f * per), total = (int32_t)(5.0f * per);
    uint32_t n = 0;
    uint16_t cc = on ? K_BLUE : K_DIM;
    cv_round(6, 24, 124, 117, 6, K_PANEL);
    cv_rect(cx - s, cy, 2 * s + 1, 1, K_LINE);
    cv_rect(cx, cy - s, 1, 2 * s + 1, K_LINE);
    fx_dash(cx - s, cy + s, cx + s, cy - s, K_LINE);
    for (i = -s; i <= s; i++) {         /* the shaper, scaled so its loudest is the box's edge */
        float y = wf_ds_shape(type, drive, (float)i / (float)s, 0);
        yv[i + s] = y;
        norm = fm_maxf(norm, fm_fabsf(y));
    }
    for (i = -s; i < s; i++)
        fx_line2(cx + i, cy - fx_px(yv[i + s] / norm * (float)s), cx + i + 1, cy - fx_px(yv[i + s + 1] / norm * (float)s), cc);
    fx_label(cx + s - text_w(&FONT_XS, "IN"), cy + 1, "IN");
    fx_label(cx - 4 - text_w(&FONT_XS, "OUT"), cy - s - 9, "OUT");
    if (amp_px > 0) {                   /* the note's level into it, and where that lands */
        int32_t yd = cy - fx_px(yv[amp_px + s] / norm * (float)s);
        fx_dash(cx + amp_px, cy + s, cx + amp_px, yd, K_ACC);
        fx_dot(cx + amp_px, yd, 3, K_ACC);
    }
    /* a full-scale 440 Hz sine through it: the shaper, Crush's held samples, Tone's two poles; each
     * column's lowest and highest, so a wave folded many times over fills in rather than aliasing */
    wf_ds_shape(type, drive, 0.0f, &hold);
    a = tone > 0.0f ? 1.0f - fm_expf(-FM_TWO_PI * tone / WV_SR) : 1.0f;
    for (i = 0; i < FX_DS_W; i++) {
        bmn[i] = amn[i] = 9.0f;
        bmx[i] = amx[i] = -9.0f;
    }
    for (k = 0; k <= total; k++) {
        float x = fm_sin_turns(ph), y;
        if (n == 0u)
            held = wf_ds_shape(type, drive, x, 0);
        n = n + 1u >= hold ? 0u : n + 1u;
        y = held / norm;
        l1 += a * (y - l1);
        l2 += a * (l1 - l2);
        ph += 440.0f / WV_SR;
        ph = ph >= 1.0f ? ph - 1.0f : ph;
        if (k >= warm) {
            int32_t col = (int32_t)((float)(k - warm) * (float)FX_DS_W / (2.0f * per));
            if (col < FX_DS_W) {
                bmn[col] = fm_minf(bmn[col], x), bmx[col] = fm_maxf(bmx[col], x), blast[col] = x;
                amn[col] = fm_minf(amn[col], l2), amx[col] = fm_maxf(amx[col], l2), alast[col] = l2;
            }
        }
    }
    cv_round(134, 24, 100, 117, 6, K_PANEL);
    cv_text(142, 27, &FONT_XS, "BEFORE", K_DIM);
    cv_text(142, 83, &FONT_XS, "AFTER", K_DIM);
    fx_wave(142, 60, bmn, bmx, blast, K_DIM);
    fx_wave(142, 116, amn, amx, alast, on ? K_ACC : K_DIM);
}

/* --------------------------------------------------------- Modulation --- */
static void fx_modfx(int on)
{
    int type = proj.par[P_MOTYPE], j;
    float dp = (float)proj.par[P_MODEPTH] * 0.01f, hz = wf_rate_hz((float)proj.par[P_MORATE] * 0.01f);
    float win = hz <= 2.0f ? 4.0f : 8.0f / hz;   /* seconds shown: 4, or eight cycles when faster */
    int32_t now_x = 214, w = 196, i;
    char t[24], *p;
    cv_round(6, 24, 228, 117, 6, K_PANEL);
    p = fx_cat(t, type == WF_MO_PHASER ? "NOTCHES, LAST " : "DELAY, LAST ");
    fx_cat(fx_num(p, win, win < 3.95f ? 1 : 0), " s");
    cv_text(14, 27, &FONT_XS, t, K_DIM);
    /* the sweep's range, as wind_fx.c's mo_sample sets it */
    if (type == WF_MO_PHASER) {          /* 150 Hz, up 1 + 5 x Depth octaves */
        float top = 150.0f * fm_exp2f(1.0f + dp * 5.0f);
        p = fx_cat(t, "150 Hz-");
        if (top >= 1000.0f)
            fx_cat(fx_num(p, top * 0.001f, top < 9950.0f ? 1 : 0), " kHz");
        else
            fx_cat(fx_num(p, top, 0), " Hz");
    } else if (type == WF_MO_FLANGER) {  /* 0.3 ms, + 6 x Depth */
        fx_cat(fx_num(fx_cat(t, "0.3-"), 0.3f + 6.0f * dp, 1), " ms");
    } else {                             /* Chorus 10 ms +-4 x Depth, Ensemble 8 ms +-3 x Depth */
        p = fx_num(fx_cat(t, type == WF_MO_ENSEMBLE ? "8 ms +/-" : "10 ms +/-"), (type == WF_MO_ENSEMBLE ? 3.0f : 4.0f) * dp, 1);
        if (type == WF_MO_ENSEMBLE)      /* (and its vibrato, +-0.35 x Depth) */
            p = fx_num(fx_cat(p, "+/-"), 0.35f * dp, 1);
        fx_cat(p, " ms");
    }
    fx_text_r(now_x, 27, t, K_DIM);
    for (j = 0; j < 2; j++) {
        int32_t mid = j ? 114 : 64, py = 0;
        for (i = 14; i <= now_x; i += 5)
            cv_rect(i, mid, 2, 1, K_LINE);
        for (i = 0; i <= w; i++) {
            float back = (float)(w - i) / (float)w * win, u = fx_mph - back * hz, c = fm_sin_turns(u + 0.25f),
                  s = fm_sin_turns(u), v;
            switch (type) {
            case WF_MO_ENSEMBLE: {     /* the left tap at 0 degrees, the right at 120, each with the vibrato */
                float uf = fx_fph - back * 6.3f, cf = fm_sin_turns(uf + 0.25f), sf = fm_sin_turns(uf);
                if (j) {
                    c = -0.5f * c + 0.8660254f * s;
                    cf = -0.5f * cf + 0.8660254f * sf;
                }
                v = (3.0f * c + 0.35f * cf) * (1.0f / 3.35f) * dp;
                break;
            }
            case WF_MO_FLANGER: v = (j ? -c : c) * dp; break;            /* the sides swept opposite ways */
            case WF_MO_PHASER: v = (j ? s : c) * (1.0f + dp * 5.0f) * (1.0f / 6.0f); break;   /* octaves */
            default: v = (j ? s : c) * dp; break;                       /* Chorus: in quadrature */
            }
            {
                int32_t y = mid - fx_px(v * 22.0f);
                if (i)
                    fx_line2(now_x - w + i - 1, py, now_x - w + i, y, on ? K_BLUE : K_DIM);
                py = y;
            }
        }
        fx_dot(now_x, py, 3, on ? K_ACC : K_DIM);
        cv_text(221, mid - 8, &FONT_XS, j ? "R" : "L", K_DIM);
    }
}

/* -------------------------------------------------------------- Delay --- */
#define FX_DL_X0 22
#define FX_DL_W 192

static float fx_delay_ms(void)          /* Time, as far as the mode reaches (Ping-pong: about 1.1 s) */
{
    float tms = (float)proj.par[P_DLTIME], lim = wf_dl_max_ms((uint32_t)proj.par[P_DLMODE]);
    return tms > lim ? lim : tms;
}

static uint32_t fx_delay_span(void) { return fx_span(fx_delay_ms() * 4.4f); }

/* the echo sounding since ms after the note began (1 the first), 0 none */
static int fx_delay_lit(uint32_t since)
{
    float tms = fx_delay_ms(), at;
    int n = (int)(((float)since + 0.5f * tms) / tms);
    at = (float)n * tms;
    return n >= 1 && at <= (float)fx_delay_span() && fm_fabsf((float)since - at) < 90.0f ? n : 0;
}

static void fx_delay(int on, int lit)
{
    int mode = proj.par[P_DLMODE], mix = proj.par[P_DLMIX], ping = mode == WF_DL_PINGPONG, n;
    float tms = fx_delay_ms(), fb = (float)proj.par[P_DLFB] * 0.01f, wet = on ? fx_wet(mix) : 1.0f;
    float pg = wf_dl_pass_gain((uint32_t)mode, 3000.0f);
    int32_t x0 = FX_DL_X0, w = FX_DL_W, mid = 72, H = 40, hd, i, tx;
    uint32_t span = fx_delay_span();
    char t[16];
    cv_round(6, 24, 228, 117, 6, K_PANEL);
    cv_rect(x0, mid, w, 1, K_LINE);
    fx_text_r(226, 27, "L", K_DIM);
    fx_text_r(226, 103, "R", K_DIM);
    if (fb > 0.0f)                       /* the repeats' fall, through their tops */
        for (i = 0; i <= w; i += 3) {
            float ms = (float)i * (float)span / (float)w, eu = (ms - tms) / (ping ? 2.0f * tms : tms),
                  ed = (ms - (ping ? 2.0f * tms : tms)) / (ping ? 2.0f * tms : tms);
            if (eu >= 0.0f)
                cv_pset(x0 + i, mid - fx_px((float)H * wet * fm_powf(fb, eu)), K_LINE);
            if (ed >= 0.0f)
                cv_pset(x0 + i, mid + 1 + fx_px((float)H * wet * fm_powf(fb, ed)), K_LINE);
        }
    hd = on ? fx_px((float)H * fx_dry(mix)) : H;
    if (hd > 0)
        cv_round(x0 - 3, mid - hd, 6, 2 * hd + 1, 3, K_TEXT);
    for (n = 1; n < 400; n++) {          /* the echoes: n x Time; Ping-pong left, right, left... */
        int passes = ping ? (n - 1) / 2 : n - 1, up = !ping || (n & 1), dn = !ping || !(n & 1);
        float at = (float)n * tms, g = fb > 0.0f ? fm_powf(fb, (float)passes) : (passes ? 0.0f : 1.0f);
        int32_t x, hp;
        uint16_t c;
        if (at > (float)span)
            break;
        hp = fx_px((float)H * g * wet);
        if (hp < 1)
            break;
        x = x0 + fx_px(at * (float)w / (float)span);
        c = !on ? K_LINE : n == lit ? K_ACC : fx_mix(K_BLUE, K_DIM, passes ? 1.0f - fm_powf(pg, (float)passes) : 0.0f);
        if (up)
            cv_round(x - 3, mid - hp, 6, hp < 3 ? 3 : hp, 3, c);
        if (dn)
            cv_round(x - 3, mid + 1, 6, hp < 3 ? 3 : hp, 3, c);
    }
    tx = x0 + fx_px(tms * (float)w / (float)span);
    if (hd > 0 && tx - x0 >= 26)
        cv_text(x0 + 6, mid - H - 2, &FONT_XS, "dry", K_DIM);
    cv_rect(x0, 122, tx - x0 + 1, 1, K_DIM);
    cv_rect(x0, 119, 1, 7, K_DIM);
    cv_rect(tx, 119, 1, 7, K_DIM);
    wa_param_text(P_DLTIME, (int)(tms + 0.5f), t);
    if ((float)proj.par[P_DLTIME] > tms)     /* (Ping-pong past its longest: what it plays) */
        fx_cat(t + s_len(t), " max");
    {
        int32_t lw = text_w(&FONT_XS, t), lx = (x0 + tx) / 2 - lw / 2;
        cv_text(lx < x0 ? x0 : lx, 124, &FONT_XS, t, K_DIM);
    }
    fx_ms_text(t, span);
    fx_text_r(x0 + w, 124, t, K_DIM);
}

/* ------------------------------------------------------------- Reverb --- */
#define FX_RV_X0 18
#define FX_RV_W 206
#define FX_RV_OCT 10.644f              /* the time axis: 50 ms .. 80 s, log2(1600) octaves */

static int32_t fx_rv_x(float ms)
{
    return FX_RV_X0 + (ms <= 50.0f ? 0 : fx_px(fm_log2f(ms * (1.0f / 50.0f)) * ((float)FX_RV_W / FX_RV_OCT)));
}

static float fx_rv_db(float ms, float pre, float rt)   /* the tail's level at ms, 0 .. -60 dB (and below) */
{
    return ms < pre ? 1.0f : -60.0f * (ms - pre) / rt;
}

static float fx_rv_end(void)            /* when the tail is 60 dB down (ms) */
{
    float e = (float)proj.par[P_RVPRE] + wf_decay_s((float)proj.par[P_RVDECAY] * 0.01f) * 1000.0f;
    return e > 80000.0f ? 80000.0f : e;
}

static void fx_reverb(int on, int32_t feed_px, int32_t mark)
{
    uint32_t type = (uint32_t)proj.par[P_RVTYPE];
    float size = (float)proj.par[P_RVSIZE] * 0.01f, decay = (float)proj.par[P_RVDECAY] * 0.01f,
          tone = (float)proj.par[P_RVTONE] * 0.01f, pre = (float)proj.par[P_RVPRE];
    float rt = wf_decay_s(decay) * 1000.0f, rh = wf_rv_rt_at(type, size, decay, tone, 4000.0f) * 1000.0f;
    int32_t x0 = FX_RV_X0, w = FX_RV_W, base = 122, top = 42, H = base - top, i, py = 0, ph = 0, xr, lx = 8, rx;
    uint16_t fill = fx_mix(K_PANEL, K_BLUE_T, 0.4f + 0.6f * fx_wet(proj.par[P_RVMIX]));
    int highs = rh < rt * 0.98f;
    char t[24];
    static const uint16_t TICK[3] = {100, 1000, 10000};
    static const char *const TICK_S[3] = {"0.1s", "1s", "10s"};
    cv_round(6, 24, 228, 117, 6, K_PANEL);
    for (i = 0; i <= w; i++) {          /* the tail in dB, then the highs' (4 kHz) */
        float ms = 50.0f * fm_exp2f((float)i * (FX_RV_OCT / (float)FX_RV_W)), db = fx_rv_db(ms, pre, rt),
              dh = fx_rv_db(ms, pre, rh);
        int32_t y = db > 0.0f ? base : top + fx_px((db < -60.0f ? 60.0f : -db) * (float)H / 60.0f),
                yh = dh > 0.0f ? base : top + fx_px((dh < -60.0f ? 60.0f : -dh) * (float)H / 60.0f);
        if (on && y < base)
            cv_rect(x0 + i, y, 1, base - y, fill);
        if (i) {
            if (db <= 0.0f)
                fx_line2(x0 + i - 1, py, x0 + i, y, on ? K_BLUE : K_DIM);
            if ((i / 3) % 2 == 0 && highs && dh <= 0.0f && yh < base)
                cv_line(x0 + i - 1, ph, x0 + i, yh, K_DIM);
        }
        py = y;
        ph = yh;
    }
    cv_rect(x0, base, w, 1, K_LINE);
    for (i = 0; i < 3; i++) {
        int32_t x = fx_rv_x((float)TICK[i]);
        cv_rect(x, base, 1, 3, K_DIM);
        text_c(x, base + 2, &FONT_XS, TICK_S[i], K_DIM);
    }
    fx_text_r(x0 + w, base + 2, "80s", K_DIM);
    if (feed_px) {                       /* the top left: a note going in, or the dashed line's name */
        lx = cv_text(14, 27, &FONT_XS, "feeding", K_ACC);
    } else if (highs) {
        for (i = 0; i < 12; i += 6)
            cv_rect(14 + i, 33, 3, 1, K_DIM);
        lx = cv_text(30, 27, &FONT_XS, "highs", K_DIM);
    }
    xr = fx_rv_x(pre + rt);              /* where the tail is 60 dB down: Decay's time */
    fx_dash(xr, top - 2, xr, base, K_DIM);
    {
        char *p = fx_cat(t, "-60 dB at ");
        int32_t tw;
        wa_param_text(P_RVDECAY, proj.par[P_RVDECAY], p);
        tw = text_w(&FONT_XS, t);
        if (xr - 3 - tw >= lx + 6) {
            fx_text_r(xr - 3, 27, t, K_DIM);
            rx = xr;
        } else {
            cv_text(xr + 3, 27, &FONT_XS, t, K_DIM);
            rx = xr + 3 + tw;
        }
    }
    {
        const char *nm = WF_RV_NAME[type < WF_RV_NTYPE ? type : 0];
        if (228 - text_w(&FONT_XS, nm) > rx + 6)
            fx_text_r(228, 27, nm, K_DIM);
    }
    if (feed_px) {                       /* a note going in */
        cv_round(x0 - 2, base - feed_px, 5, feed_px, 2, K_ACC);
    } else if (mark >= 0) {              /* ...and its tail since: the column, the level there */
        float ms = 50.0f * fm_exp2f((float)(mark - x0) * (FX_RV_OCT / (float)FX_RV_W)), db = fx_rv_db(ms, pre, rt);
        cv_rect(mark, top, 2, H, K_ACC);
        fx_dot(mark, db > 0.0f ? top : top + fx_px((db < -60.0f ? 60.0f : -db) * (float)H / 60.0f), 3, K_ACC);
    }
}

/* ----------------------------------------------------------- Reverb 2 --- */
static void fx_reverb2(int on, int run, uint32_t since)
{
    uint32_t type = (uint32_t)proj.par[P_RVTYPE];
    float k = wf_rv_scale(type, (float)proj.par[P_RVSIZE] * 0.01f) * (1.0f / 1.5f), mod = (float)proj.par[P_RVMOD] * 0.01f;
    float wob = 2.0f * mod, low = (float)proj.par[P_RVLOW] * 0.02f;
    int32_t cx = 81, cy = 86, hw = 18 + fx_px(46.0f * k), hh = 12 + fx_px(28.0f * k), L, R, T, B, i, j;
    int32_t gx = 166, gw = 62, mid = 82;
    char t[24];
    cv_round(6, 24, 150, 117, 6, K_PANEL);
    wa_param_text(P_RVSIZE, proj.par[P_RVSIZE], fx_cat(t, "SIZE "));
    cv_text(12, 27, &FONT_XS, t, K_DIM);
    /* the walls, each moving with the ring's modulation (Mod) */
    L = cx - hw + fx_px(wob * fm_sin_turns(fx_rph));
    R = cx + hw + fx_px(wob * fm_sin_turns(fx_rph + 0.25f));
    T = cy - hh + fx_px(wob * fm_sin_turns(fx_rph + 0.5f));
    B = cy + hh + fx_px(wob * fm_sin_turns(fx_rph + 0.75f));
    {
        int32_t sx = L + 12, sy = cy + 6, lx = R - 12, ly = cy - 4, tw;
        int32_t P[4][4][2] = {{{sx, sy}, {cx - 8, T}, {lx, ly}, {lx, ly}},
                              {{sx, sy}, {cx + 6, B}, {lx, ly}, {lx, ly}},
                              {{sx, sy}, {L, cy - 6}, {cx, T}, {lx, ly}},
                              {{sx, sy}, {R, cy + 10}, {lx, ly}, {lx, ly}}};
        for (i = 0; i < 4; i++)          /* reflections: source to listener by the walls */
            for (j = 0; j < 3; j++)
                cv_line(P[i][j][0], P[i][j][1], P[i][j + 1][0], P[i][j + 1][1], K_LINE);
        cv_rect(L, T, R - L, 2, K_DIM);
        cv_rect(L, B - 1, R - L, 2, K_DIM);
        cv_rect(L, T, 2, B - T, K_DIM);
        cv_rect(R - 1, T, 2, B - T, K_DIM);
        if (run)                         /* the sound going round them */
            for (i = 0; i < 4; i++) {
                float u = (float)(since % 1200u) * (0.9f / 1200.0f) + (float)i * 0.108f, tot = 0.0f, d[3], f;
                u -= fm_floorf(u);
                for (j = 0; j < 3; j++) {
                    float ex = (float)(P[i][j + 1][0] - P[i][j][0]), ey = (float)(P[i][j + 1][1] - P[i][j][1]);
                    d[j] = fm_sqrtf(ex * ex + ey * ey);
                    tot += d[j];
                }
                f = u * tot;
                for (j = 0; j < 2 && f > d[j]; j++)
                    f -= d[j];
                f = d[j] > 0.0f ? f / d[j] : 0.0f;
                fx_dot(P[i][j][0] + fx_px(f * (float)(P[i][j + 1][0] - P[i][j][0])),
                       P[i][j][1] + fx_px(f * (float)(P[i][j + 1][1] - P[i][j][1])), 2, K_ACC);
            }
        fx_dash(sx, sy, lx, ly, K_DIM);   /* the direct path: the pre-delay (under the room when it's small) */
        wa_param_text(P_RVPRE, proj.par[P_RVPRE], t);
        tw = text_w(&FONT_XS, t);
        if (hh < 24 || lx - sx < tw + 12)
            text_c(cx, B + 3, &FONT_XS, t, K_TEXT);
        else
            text_c((sx + lx) / 2, (sy + ly) / 2 + 2, &FONT_XS, t, K_TEXT);
        fx_dot(sx, sy, 4, on ? K_ACC : K_DIM);
        fx_dot(lx, ly, 4, on ? K_BLUE : K_DIM);
        if (type == WF_RV_SHIMMER && proj.par[P_RVSHIM] > 0)   /* Shimmer: octaves climbing out of the room */
            for (i = 0; i < 1 + proj.par[P_RVSHIM] * 3 / 100; i++) {
                int32_t y = T - 6 - i * 5;
                if (y < 26)                  /* (inside the panel, however big the room) */
                    break;
                cv_line(lx - 3, y + 3, lx, y, on ? K_ACC : K_DIM);
                cv_line(lx, y, lx + 3, y + 3, on ? K_ACC : K_DIM);
            }
    }
    /* Low end: the bass's decay time against the rest's, x0.4 .. x2.5, below about 250 Hz */
    cv_round(160, 24, 74, 117, 6, K_PANEL);
    cv_text(gx, 27, &FONT_XS, "LOW END", K_DIM);
    fx_text_r(gx + gw, 40, "longer", K_DIM);
    fx_text_r(gx + gw, 115, "shorter", K_DIM);
    for (i = gx; i <= gx + gw; i += 5)
        cv_rect(i, mid, 2, 1, K_LINE);
    {
        int32_t py = mid;
        for (i = 0; i <= gw; i++) {      /* 20 Hz .. 10 kHz across; the low end's share, a one-pole's */
            float f = 20.0f * fm_exp2f((float)i * (8.966f / (float)gw)), r = f * (1.0f / 250.0f);
            int32_t y = mid - fx_px(low * 32.0f * fm_rsqrtf(1.0f + r * r));
            if (i)
                fx_line2(gx + i - 1, py, gx + i, y, on ? K_BLUE : K_DIM);
            py = y;
        }
    }
    if (proj.par[P_RVLOW])
        fx_num(fx_cat(t, "bass x"), fm_exp2f(low * 1.3219f), 1);
    else
        fx_cat(t, "flat");
    fx_text_r(gx + gw, low >= 0.0f ? mid + 3 : mid - 14, t, K_DIM);
}

/* ------------------------------------------------------------ the view --- */
/* when the note began and ended: every frame (ui_frame), whatever is showing, Stage too */
static void fx_edges(void)
{
    int gate = wa_state.gate > 0.5f;
    if (gate != fx_gate) {
        if (gate)
            fx_on_t = plat_ms() & ~31u, fx_seen |= 1u;
        else
            fx_off_t = plat_ms() & ~31u, fx_seen |= 2u;
        fx_gate = (uint8_t)gate;
    }
}

static int vw_fx(int v)
{
    int gate = wa_state.gate > 0.5f, cur, mix, on, anim = 0, feed = 0, lit = 0;
    uint32_t now = plat_ms(), h, since = 0, now_q = now & ~31u;
    int32_t amp_px = 0, feed_px = 0, mark = -1, i;
    if (v < V_DIST || v > V_REVERB2)
        return 0;
    cur = v == V_REVERB2 ? 3 : v - V_DIST;
    mix = proj.par[FX_MIXP[cur]];
    on = mix > 0;
    /* what moves, and whether it moves now: only through an effect that's on, while it has sound */
    switch (v) {
    case V_DIST:
        anim = on && gate;
        break;
    case V_MODFX:                        /* (while the note plays: not through another effect's tail) */
        anim = on && gate;
        break;
    case V_REVERB2:
        anim = on && (gate || wa_state.level > 0.002f);
        break;
    case V_DELAY:
        anim = on && (fx_seen & 1u) && now - fx_on_t < fx_delay_span() + 100u;
        break;
    case V_REVERB:
        feed = on && gate;
        anim = feed || (on && (fx_seen & 2u) && (float)(now - fx_off_t) < fx_rv_end() + 100.0f);
        break;
    }
    if (anim && now_q != fx_t) {         /* the clock, the phases and the levels forward, in 32 ms steps */
        uint32_t dt = now_q - fx_t;
        float s = (float)(dt > 1000u ? 1000u : dt) * 0.001f;
        fx_mph += s * wf_rate_hz((float)proj.par[P_MORATE] * 0.01f);
        fx_fph += s * 6.3f;
        fx_rph += s * (0.11f + (float)proj.par[P_RVMOD] * 0.003f);
        fx_mph -= fm_floorf(fx_mph);
        fx_fph -= fm_floorf(fx_fph);
        fx_rph -= fm_floorf(fx_rph);
        fx_lvl = wa_state.level;
        fx_amp = wa_state.amp;
        fx_t = now_q;
    }
    switch (v) {                         /* (the edges are in the same steps; a note begun since the last
                                          * step, on another page, counts from 0) */
    case V_DIST:
        if (anim && fx_amp > 0.01f)
            amp_px = fx_px((fx_amp > 1.0f ? 1.0f : fx_amp) * (float)FX_DS_S);
        break;
    case V_DELAY:
        if (anim)
            lit = fx_delay_lit((int32_t)(fx_t - fx_on_t) > 0 ? fx_t - fx_on_t : 0u);
        break;
    case V_REVERB2:
        if (anim)
            since = (int32_t)(fx_t - fx_on_t) > 0 ? fx_t - fx_on_t : 0u;
        break;
    case V_REVERB:
        if (feed)
            feed_px = 4 + fx_px((fx_lvl * 1.5f > 1.0f ? 1.0f : fx_lvl * 1.5f) * 76.0f);
        else if (anim)
            mark = fx_rv_x((float)((int32_t)(fx_t - fx_off_t) > 0 ? fx_t - fx_off_t : 0u));
        break;
    }
    /* what's drawn: the moving pages' time; the others' dot, lit echo, bar or marker */
    h = hash(hash(2166136261u, (uint32_t)v | (uint32_t)proj.par[P_THEME] << 8 | (uint32_t)amp_px << 16),
             (uint32_t)lit | (uint32_t)feed_px << 12 | (uint32_t)(mark + 1) << 20);
    h = hash(hash(h, v == V_MODFX || v == V_REVERB2 ? fx_t : 0u), since);
    for (i = P_RVTYPE; i <= P_DLFB; i++)
        h = hash(h, (uint32_t)(uint16_t)proj.par[i]);
    for (i = P_DSTYPE; i <= P_MODEPTH; i++)
        h = hash(h, (uint32_t)(uint16_t)proj.par[i]);
    h = hash(h, (uint32_t)(uint16_t)proj.par[P_RVSHIM]);
    if (h == ui.sig[1])
        return 1;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    fx_chain(cur);
    switch (v) {
    case V_DIST: fx_dist(on, amp_px); break;
    case V_MODFX: fx_modfx(on); break;
    case V_DELAY: fx_delay(on, lit); break;
    case V_REVERB: fx_reverb(on, feed_px, mark); break;
    default: fx_reverb2(on, anim, since); break;
    }
    cv_blit(0, MAIN_Y);
    return 1;
}

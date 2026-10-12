/* SPDX-License-Identifier: GPL-3.0-only */
/* The routing views, in the screen's middle band (included by ui.c, above draw_main):
 *
 *   Mod 1-4   the slot as a cable: its source's live level on the left, the amount on the cable, the
 *             destination on the right (the knob's value, its live value after all the modulation, a
 *             mark at it); under it, everything modulating the sound now: the Mod slots in use, the
 *             LFO, the Filter page's Breath, each with its source's live level, and the slots that are off
 *   Setup     a tuner (the note sounding, cents off it, Transpose, Tune and the bite applied) and the
 *             controller monitor: the breath source, pitch bend, the glide CC and the last note as they
 *             came in, a dot lit as each arrives */

/* the audio side's state as of this frame: what moves on these views (the LFO, a dot fading) is drawn
 * as it was when the frame began, and a redraw from scratch (ui_repaint, ui.sig cleared) draws the same */
static wa_state_t rt_ws;
static uint8_t rt_have;
static void rt_take(void)
{
    if (ui.sig[1] || !rt_have) {
        rt_ws = wa_state;
        rt_have = 1;
    }
}

/* a over b, t 0..16 */
static uint16_t rt_mix(uint16_t a, uint16_t b, int32_t t)
{
    uint32_t r = ((uint32_t)(a >> 11) * (uint32_t)t + (uint32_t)(b >> 11) * (uint32_t)(16 - t)) / 16u,
             g = ((uint32_t)(a >> 5 & 63u) * (uint32_t)t + (uint32_t)(b >> 5 & 63u) * (uint32_t)(16 - t)) / 16u,
             bl = ((uint32_t)(a & 31u) * (uint32_t)t + (uint32_t)(b & 31u) * (uint32_t)(16 - t)) / 16u;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

static int32_t rt_round(float v) { return (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f); }
static float rt_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* "+40", "-7", "0" */
static void rt_int(char *b, int32_t v, int plus)
{
    if (v < 0)
        *b++ = '-';
    else if (plus && v > 0)
        *b++ = '+';
    itoa_u((uint32_t)(v < 0 ? -v : v), b);
}

/* a small arrow, x to x + 8, at the middle of a line of FONT_XS/S text at y; returns its end */
static int32_t rt_arrow(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x + 1, y + 7, 7, 1, c);
    cv_line(x + 5, y + 4, x + 8, y + 7, c);
    cv_line(x + 5, y + 10, x + 8, y + 7, c);
    return x + 12;
}

/* ------------------------------------------------------------- Mod --- */
/* a destination's name, the engine's own for Timbre, Morph and Harmonics ("Square" on VA) */
static const char *rt_dst_name(int d)
{
    switch (d) {
    case WM_DST_TIMBRE: return wa_param_name(P_INDEX);
    case WM_DST_MORPH: return wa_param_name(P_MORPH);
    case WM_DST_HARM: return wa_harm_smooth(proj.par[P_ENGINE]) ? wa_param_name(P_RATIO) : "Harm off";
    default: return WM_DST_NAME[(unsigned)d < WM_NDST ? d : 0];
    }
}

/* a destination's knob value and its live value after the matrix, 0..1 (Amp: a gain from 1) */
static void rt_dst_value(int d, float *base, float *live)
{
    static const uint8_t KNOB[WM_NDST] = {P_CUTOFF, P_RESO, 255, P_MORPH, P_INDEX, P_NZLEVEL, P_RATIO, P_AUX};
    switch (d) {
    case WM_DST_CUTOFF: *live = rt_ws.cutoff; break;
    case WM_DST_RES: *live = rt_ws.res; break;
    case WM_DST_AMP: *live = rt_ws.amp; break;
    case WM_DST_MORPH: *live = rt_ws.morph; break;
    case WM_DST_TIMBRE: *live = rt_ws.timbre; break;
    case WM_DST_NOISE: *live = rt_ws.noise; break;
    case WM_DST_HARM: *live = rt_ws.harmonics; break;
    default: *live = rt_ws.aux; break;
    }
    d = (unsigned)d < WM_NDST ? d : 0;
    *base = KNOB[d] == 255 ? 1.0f : (float)proj.par[KNOB[d]] * 0.01f;
    *live = rt_clamp(*live, 0.0f, 1.0f);
}

/* a source's level now: 0..1, Bite -1..1 */
static float rt_src_level(int s)
{
    return (unsigned)s < WM_NSRC && s != WM_SRC_OFF ? rt_ws.src[s] : 0.0f;
}

/* upper case, for the cable's labels */
static void rt_upper(char *d, const char *s, int n)
{
    int i;
    for (i = 0; s[i] && i < n - 1; i++)
        d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}

static void rt_copy(char *d, const char *s, int n)
{
    int i;
    for (i = 0; s[i] && i < n - 1; i++)
        d[i] = s[i];
    d[i] = 0;
}

/* a row of the live list: what, source -> destination, the amount, the source's level now */
typedef struct {
    char key[8], src[10], dst[12], amt[8];
    float lvl;
    uint8_t cur;
} rt_row_t;

#define RT_ROWS 6                      /* the Mod slots, the LFO, the Filter's Breath */
static const char *const RT_LFO_SHAPE[WA_LFO_NSHAPE] = {"Sine", "Tri", "Saw", "Square", "S&H"};

static int rt_slot_off(int i) { return proj.par[P_M1SRC + 3 * i] == WM_SRC_OFF || !proj.par[P_M1AMT + 3 * i]; }

/* the rows; off: the slots that are off, "2-4", "1,3" ("" none) */
static int rt_rows(int cur_slot, rt_row_t *r, char *off)
{
    int n = 0, i, k = 0;
    for (i = 0; i < 4; i++) {
        int s = proj.par[P_M1SRC + 3 * i], d = proj.par[P_M1DST + 3 * i];
        if (rt_slot_off(i)) {                    /* a run of them: its ends */
            int j = i;
            while (j + 1 < 4 && rt_slot_off(j + 1))
                j++;
            if (k)
                off[k++] = ',';
            off[k++] = (char)('1' + i);
            if (j > i) {
                off[k++] = '-';
                off[k++] = (char)('1' + j);
            }
            i = j;
            continue;
        }
        r[n].key[0] = (char)('1' + i);
        r[n].key[1] = 0;
        rt_copy(r[n].src, WM_SRC_NAME[(unsigned)s < WM_NSRC ? s : 0], sizeof r[n].src);
        rt_copy(r[n].dst, rt_dst_name(d), sizeof r[n].dst);
        rt_int(r[n].amt, proj.par[P_M1AMT + 3 * i], 1);
        r[n].lvl = rt_src_level(s);
        r[n].cur = (uint8_t)(i == cur_slot);
        n++;
    }
    off[k] = 0;
    if (proj.par[P_LFOAMT]) {
        int d = proj.par[P_LFODST], sh = proj.par[P_LFOSHAPE];
        rt_copy(r[n].key, "LFO", sizeof r[n].key);
        rt_copy(r[n].src, RT_LFO_SHAPE[(unsigned)sh < WA_LFO_NSHAPE ? sh : 0], sizeof r[n].src);
        rt_copy(r[n].dst, d == WM_DST_PITCH ? "Pitch" : rt_dst_name(d), sizeof r[n].dst);
        wa_param_text(P_LFOAMT, proj.par[P_LFOAMT], r[n].amt);
        r[n].lvl = rt_ws.lfo;
        r[n].cur = 0;
        n++;
    }
    if (proj.par[P_BRCUT]) {
        rt_copy(r[n].key, "Filter", sizeof r[n].key);
        rt_copy(r[n].src, WM_SRC_NAME[WM_SRC_BREATH], sizeof r[n].src);
        rt_copy(r[n].dst, WM_DST_NAME[WM_DST_CUTOFF], sizeof r[n].dst);
        rt_int(r[n].amt, proj.par[P_BRCUT], 1);
        r[n].lvl = rt_ws.breath;
        r[n].cur = 0;
        n++;
    }
    return n;
}

/* what the Mod page shows that moves, as drawn (the hash's) */
typedef struct {
    int16_t src_px, src_num, live_px, live_num, cable_w, cable_t;
    int16_t row_px[RT_ROWS];
} rt_modq_t;

/* the cable: a curve from the source to the destination, thicker and stronger with the source */
static void rt_cable(int32_t w, uint16_t c)
{
    int i;
    for (i = 0; i <= 128; i++) {
        float t = (float)i * (1.0f / 128.0f), u = 1.0f - t;
        float x = u * u * u * 78.0f + 3.0f * u * u * t * 110.0f + 3.0f * u * t * t * 130.0f + t * t * t * 162.0f,
              y = u * u * u * 30.0f + 3.0f * u * u * t * 30.0f + 3.0f * u * t * t * 56.0f + t * t * t * 30.0f;
        int32_t px = rt_round(x) - w / 2, py = rt_round(y) - w / 2;
        if (w <= 2)
            cv_rect(px, py, w, w, c);
        else
            cv_round(px, py, w, w, w / 2, c);
    }
}

static void rt_mod(int slot)
{
    int s = proj.par[P_M1SRC + 3 * slot], d = proj.par[P_M1DST + 3 * slot], a = proj.par[P_M1AMT + 3 * slot];
    int on = s != WM_SRC_OFF && a, bi = s == WM_SRC_BITE, n, i, lines, crowd;
    float lvl = on ? rt_src_level(s) : 0.0f, mag = lvl < 0.0f ? -lvl : lvl, base, live;
    rt_row_t row[RT_ROWS];
    rt_modq_t q;
    char off[12], t[24];
    uint32_t h, st;
    const uint8_t *qb;
    uint16_t pill;
    rt_dst_value(d, &base, &live);
    n = rt_rows(slot, row, off);
    memset(&q, 0, sizeof q);
    q.src_px = (int16_t)(bi ? rt_round(rt_clamp(lvl, -1.0f, 1.0f) * 28.0f) : rt_round(rt_clamp(lvl, 0.0f, 1.0f) * 56.0f));
    q.src_num = (int16_t)rt_round(lvl * 100.0f);
    q.live_px = (int16_t)rt_round(live * 58.0f);
    q.live_num = (int16_t)rt_round(live * 100.0f);
    q.cable_w = (int16_t)(on ? 2 + rt_round(mag * 4.0f) : 1);
    q.cable_t = (int16_t)(on ? rt_round(16.0f * (0.35f + 0.65f * rt_clamp(mag, 0.0f, 1.0f))) : 0);
    for (i = 0; i < n; i++) {
        float v = row[i].lvl < 0.0f ? -row[i].lvl : row[i].lvl;
        row[i].lvl = rt_clamp(v, 0.0f, 1.0f);
        q.row_px[i] = (int16_t)rt_round(row[i].lvl * 56.0f);
    }
    st = hash(hash(2166136261u, (uint32_t)ui.view | (uint32_t)proj.par[P_THEME] << 8), (uint32_t)proj.par[P_ENGINE]);
    for (i = 0; i < P_NPARAMS; i++)
        st = hash(st, (uint32_t)(uint16_t)proj.par[i]);
    h = st;
    for (qb = (const uint8_t *)&q, i = 0; i < (int)sizeof q; i++)
        h = hash(h, qb[i]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;

    cv_begin(240, MAIN_H, K_BG);
    /* the slot */
    lines = n + (off[0] != 0);
    crowd = lines > 4;                 /* 5 or 6 lines: the list takes 8 px of the slot's panel */
    cv_round(6, 4, 228, crowd ? 66 : 74, 6, K_PANEL);
    rt_upper(t, on ? WM_SRC_NAME[(unsigned)s < WM_NSRC ? s : 0] : "Off", sizeof t);
    cv_text(16, 10, &FONT_XS, t, K_DIM);
    cv_round(16, 26, 56, 8, 4, K_LINE);
    if (bi) {
        int32_t m = 44, e = m + q.src_px;
        cv_rect(m, 23, 1, 14, K_DIM);
        if (on)
            cv_round(e < m ? e : m, 26, (e < m ? m - e : e - m) + 2, 8, 4, K_ACC);
    } else if (on && q.src_px > 0) {
        cv_round(16, 26, q.src_px < 8 ? 8 : q.src_px, 8, 4, K_ACC);
    }
    if (on) {
        rt_int(t, q.src_num, bi);
        cv_text(16, 40, &FONT_B, t, K_TEXT);
    } else {
        cv_text(16, 40, &FONT_B, "-", K_DIM);
    }
    rt_upper(t, rt_dst_name(d), sizeof t);
    cv_text(168, 10, &FONT_XS, t, K_DIM);
    cv_round(168, 26, 58, 8, 4, K_LINE);
    if (base > 0.0f)
        cv_round(168, 26, rt_round(58.0f * base) < 8 ? 8 : rt_round(58.0f * base), 8, 4, K_BLUE);
    cv_round(168 + q.live_px - 1, 22, 3, 16, 1, K_ACC);
    {   /* "40 -> 67": the knob, the value now */
        int32_t x;
        itoa_u((uint32_t)rt_round(base * 100.0f), t);
        x = cv_text(168, 40, &FONT_B, t, K_TEXT);
        x = rt_arrow(x + 3, 41, K_DIM);
        itoa_u((uint32_t)q.live_num, t);
        cv_text(x, 40, &FONT_B, t, K_TEXT);
    }
    rt_cable(q.cable_w, on ? rt_mix(K_ACC, K_PANEL, q.cable_t) : K_LINE);
    pill = on ? K_ACC : K_LINE;
    cv_round(105, 36, 32, 15, 6, pill);
    if (on)
        rt_int(t, a, 1);
    else
        rt_copy(t, "Off", sizeof t);
    text_c(121, 36, &FONT_XS, t, on ? K_WHITE : K_DIM);

    /* everything modulating now */
    cv_round(6, crowd ? 74 : 82, 228, crowd ? 66 : 58, 6, K_PANEL);
    {
        int pitch = crowd ? 11 : 13;
        int32_t y = crowd ? 78 : 87;
        for (i = 0; i < n; i++, y += pitch) {
            int32_t x, bw = q.row_px[i];
            if (row[i].cur)
                cv_round(10, y - 1, 220, pitch, 4, K_BG);
            cv_text(16, y - 2, &FONT_XS, row[i].key, row[i].cur ? K_ACC : K_DIM);
            x = cv_text(50, y - 2, &FONT_XS, row[i].src, row[i].cur ? K_TEXT : K_DIM);
            x = rt_arrow(x + 2, y - 3, K_DIM);
            cv_text(x, y - 2, &FONT_XS, row[i].dst, row[i].cur ? K_TEXT : K_DIM);
            cv_text(160 - text_w(&FONT_XS, row[i].amt), y - 2, &FONT_XS, row[i].amt, K_DIM);
            cv_round(166, y + 3, 56, 5, 2, K_LINE);
            if (bw > 0)
                cv_round(166, y + 3, bw < 5 ? 5 : bw, 5, 2, row[i].cur ? K_ACC : rt_mix(K_ACC, K_PANEL, 10));
        }
        if (off[0]) {
            int32_t x = cv_text(16, y - 2, &FONT_XS, off, K_DIM);
            cv_text(x + 6, y - 2, &FONT_XS, "Off", K_DIM);
        }
        if (!lines)
            text_c(120, 102, &FONT_XS, "Nothing modulates the sound", K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

/* ----------------------------------------------------------- Setup --- */
/* what the Setup page shows that moves, as drawn */
typedef struct {
    int16_t note, cents;               /* the note (-1000: none yet) and the cents off it */
    uint8_t gate, fresh[4], never[4];  /* fresh: 4 just in .. 1, 0 older than RT_FRESH_MS */
    int16_t val[4], bar[4];
} rt_setq_t;
#define RT_FRESH_MS 300u

static void rt_setup(void)
{
    wc_mon_t m = rt_ws.mon;
    const uint32_t *ts[4];
    rt_setq_t q;
    uint32_t h, st;
    const uint8_t *qb;
    float note = rt_ws.note;
    char t[16];
    int i;
    ts[0] = &m.t_breath, ts[1] = &m.t_bend, ts[2] = &m.t_glide, ts[3] = &m.t_note;
    memset(&q, 0, sizeof q);
    q.note = -1000;
    if (rt_ws.top >= 0) {
        int n = (int)(note + 0.5f + 1200.0f) - 1200;
        q.note = (int16_t)n;
        q.cents = (int16_t)rt_round((note - (float)n) * 100.0f);
        q.gate = (uint8_t)(rt_ws.gate > 0.5f);
    }
    for (i = 0; i < 4; i++) {
        uint32_t d = m.clock - *ts[i], age = d > 1000u ? 1000000u : d * 1451u / 1000u;   /* blocks of 1.451 ms */
        q.never[i] = (uint8_t)(*ts[i] == 0u);
        q.fresh[i] = (uint8_t)(!q.never[i] && age < RT_FRESH_MS ? 1u + (RT_FRESH_MS - 1u - age) * 4u / RT_FRESH_MS : 0u);
    }
    q.val[0] = (int16_t)(m.breath >> 7);
    q.bar[0] = (int16_t)(m.breath * 76u / 16383u);
    q.val[1] = m.bend;
    q.bar[1] = (int16_t)(m.bend * 37 / 8192);
    q.val[2] = m.glide;
    q.bar[2] = (int16_t)(m.glide * 76 / 127);
    q.val[3] = (int16_t)(m.on ? m.velocity : 0);
    q.bar[3] = (int16_t)(m.on ? m.velocity * 76 / 127 : 0);
    q.val[3] = (int16_t)(q.val[3] | m.note << 8 | m.on << 7);
    st = hash(hash(2166136261u, (uint32_t)ui.view | (uint32_t)proj.par[P_THEME] << 8),
              (uint32_t)proj.par[P_BSRC] | (uint32_t)proj.par[P_GSRC] << 8);
    h = st;
    for (qb = (const uint8_t *)&q, i = 0; i < (int)sizeof q; i++)
        h = hash(h, qb[i]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;

    cv_begin(240, MAIN_H, K_BG);
    /* the tuner */
    cv_round(6, 4, 228, 64, 6, K_PANEL);
    {
        const int32_t rx = 84, rw = 140, ry = 42;
        int ct;
#define RT_X(c) (rx + rw / 2 + (c) * (rw / 2) / 50)
        cv_rect(rx, ry, rw, 2, K_LINE);
        for (ct = -50; ct <= 50; ct += 10) {
            int major = ct % 50 == 0;
            cv_rect(RT_X(ct), ry - (major ? 7 : 4), 1, major ? 9 : 6, ct == 0 ? K_TEXT : major ? K_DIM : K_LINE);
        }
        text_c(RT_X(-50), ry + 5, &FONT_XS, "-50", K_DIM);
        text_c(RT_X(50), ry + 5, &FONT_XS, "+50", K_DIM);
        if (q.note == -1000) {
            text_c(40, 16, &FONT_L, "--", K_DIM);
            text_c(RT_X(0), 8, &FONT_S, "Blow a note", K_DIM);
        } else {
            int ok = q.cents >= -5 && q.cents <= 5, c = q.cents < -50 ? -50 : q.cents > 50 ? 50 : q.cents;
            uint16_t col = ok ? K_BLUE : K_ACC;
            note_text(q.note, t);
            text_c(40, 16, &FONT_L, t, q.gate ? K_TEXT : K_DIM);
            cv_round(RT_X(c) - 2, ry - 14, 4, 22, 2, col);
            if (q.cents) {
                rt_int(t, q.cents, 1);
                i = s_len(t);
                t[i++] = 'c';
                t[i] = 0;
            } else {
                rt_copy(t, "in tune", sizeof t);
            }
            text_c(RT_X(0), 8, &FONT_S, t, col);
        }
#undef RT_X
    }
    /* the controller monitor: what came in, and a dot lit as it comes */
    cv_round(6, 72, 228, 68, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t y = 76 + i * 16;
        char lab[8], nm[12];
        uint16_t dot = q.fresh[i] ? rt_mix(K_ACC, K_PANEL, 4 + 3 * q.fresh[i]) : K_LINE;
        switch (i) {
        case 0:
            if (proj.par[P_BSRC] >= WC_SRC_AT)
                rt_copy(lab, "AT", sizeof lab);
            else
                lab[0] = 'C', lab[1] = 'C', itoa_u((uint32_t)proj.par[P_BSRC], lab + 2);
            rt_copy(nm, "Breath", sizeof nm);
            break;
        case 1: rt_copy(lab, "PB", sizeof lab); rt_copy(nm, "Bite", sizeof nm); break;
        case 2:
            if (proj.par[P_GSRC] >= WC_SRC_OFF)
                rt_copy(lab, "Off", sizeof lab);
            else
                lab[0] = 'C', lab[1] = 'C', itoa_u((uint32_t)proj.par[P_GSRC], lab + 2);
            rt_copy(nm, "Glide", sizeof nm);
            break;
        default: {
            int32_t k;
            rt_copy(lab, "NOTE", sizeof lab);
            if (q.never[3]) {
                rt_copy(nm, "none", sizeof nm);
            } else {
                note_text(m.note, nm);
                k = s_len(nm);
                rt_copy(nm + k, m.on ? " on" : " off", (int)sizeof nm - k);
            }
            break;
        }
        }
        cv_round(12, y + 4, 6, 6, 3, dot);
        cv_text(24, y, &FONT_XS, lab, K_TEXT);
        cv_text(56, y, &FONT_XS, nm, K_DIM);
        cv_round(110, y + 4, 76, 6, 3, K_LINE);
        if (i == 1) {                              /* the bend: from the middle */
            int32_t mid = 148, e = mid + q.bar[1];
            cv_rect(mid, y + 1, 1, 12, K_DIM);
            if (!q.never[1])
                cv_round(e < mid ? e : mid, y + 4, (e < mid ? mid - e : e - mid) + 3, 6, 3, K_BLUE);
        } else if (q.bar[i] > 0) {
            cv_round(110, y + 4, q.bar[i] < 6 ? 6 : q.bar[i], 6, 3, i == 0 ? K_ACC : K_BLUE);
        }
        if (q.never[i])
            rt_copy(t, "-", sizeof t);
        else if (i == 3)
            itoa_u(m.on ? m.velocity : 0u, t);
        else
            rt_int(t, q.val[i], 0);
        cv_text(226 - text_w(&FONT_S, t), y - 1, &FONT_S, t, q.fresh[i] ? K_ACC : q.never[i] ? K_DIM : K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}

static int vw_route(int v)
{
    if ((v >= V_MOD1 && v <= V_MOD4) || v == V_SETUP)
        rt_take();
    if (v >= V_MOD1 && v <= V_MOD4)
        rt_mod(v - V_MOD1);
    else if (v == V_SETUP)
        rt_setup();
    else
        return 0;
    return 1;
}

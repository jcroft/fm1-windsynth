/* SPDX-License-Identifier: GPL-3.0-only */
/* The routing views, in the screen's middle band (included by ui.c, above draw_main):
 *
 *   Mod 1-4   the slot as a cable: its source's live level on the left, the amount on the cable, the
 *             destination on the right (the knob's value, then its value now, after every route, and a
 *             mark at it); a destination that does nothing on this engine is drawn inert, as an off slot.
 *             Under it, everything modulating the sound now: the Mod slots in use, the LFO, the Filter
 *             page's Breath, each with its source's live level, and the slots that are off. With no
 *             note the LFO rests (its bar empty, the value now without it), so the screen
 *             holds still.
 *   Setup     a tuner (the note sounding, cents off it, Transpose, Tune and the bite applied) and the
 *             controller monitor: the breath source, pitch bend, the glide CC and the last note as they
 *             came in, on any channel (the channel shown, in the accent when the MIDI ch setting doesn't
 *             hear it), a dot lit as each arrives */

/* the audio side's state as of this frame: what moves on these views (the LFO, a dot fading) is drawn
 * as it was when the frame began, and a redraw from scratch (ui_repaint, ui.sig cleared) draws the same.
 * (The views' hashes are never 0: 0 is the redraw's.) */
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

static void rt_copy(char *d, const char *s, int n)
{
    int i;
    for (i = 0; s[i] && i < n - 1; i++)
        d[i] = s[i];
    d[i] = 0;
}

/* upper case, for the cable's labels */
static void rt_upper(char *d, const char *s, int n)
{
    int i;
    for (i = 0; s[i] && i < n - 1; i++)
        d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}

/* s cut to w pixels of FONT_XS, a '.' where it's cut ("Harm o.") */
static void rt_fit(char *s, int32_t w)
{
    char t[24];
    int32_t n = s_len(s), k, j;
    if (text_w(&FONT_XS, s) <= w)
        return;
    for (k = n - 1 < 22 ? n - 1 : 22; k > 1; k--) {
        for (j = k; j > 1 && s[j - 1] == ' '; j--)
            ;
        memcpy(t, s, (size_t)j);
        t[j] = '.';
        t[j + 1] = 0;
        if (text_w(&FONT_XS, t) <= w)
            break;
    }
    if (k <= 1) {
        t[0] = s[0];
        t[1] = '.';
        t[2] = 0;
    }
    rt_copy(s, t, n + 1);
}

/* a small arrow, x to x + 8, at the middle of a line of FONT_XS text at y; returns its end */
#define RT_ARROW_W 14                  /* (with the gap before it) */
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

/* a destination that does nothing on this engine: Harmonics where it's stepped, Classic's Timbre (it has
 * none), 6-op's Aux (its main output) */
static int rt_dst_inert(int d)
{
    int e = proj.par[P_ENGINE];
    return (d == WM_DST_HARM && !wa_harm_smooth(e)) || (d == WM_DST_TIMBRE && e == WV_CLASSIC) ||
           (d == WM_DST_AUX && e == WV_SIXOP);
}

static const uint8_t RT_KNOB[WM_NDST] = {P_CUTOFF, P_RESO, 255, P_MORPH, P_INDEX, P_NZLEVEL, P_RATIO, P_AUX};

/* the knobs' values, 0..1, as the matrix takes them (wa_set) */
static void rt_bases(float base[WM_NDST])
{
    int d;
    for (d = 0; d < WM_NDST; d++)
        base[d] = RT_KNOB[d] == 255 ? 1.0f : (float)proj.par[RT_KNOB[d]] * 0.01f;
}

/* the quiet: no note. The LFO then rests: the values now are worked out without it */
static int rt_quiet(void) { return rt_ws.gate <= 0.5f; }

/* a destination's value now, 0..1 (Amp: the gain, which the matrix keeps 0..1 too) */
static float rt_dst_live(int d)
{
    if (rt_quiet()) {                  /* the matrix as the audio side runs it, the LFO's slot left out */
        wm_slot_t sl[WM_NSLOT];
        float base[WM_NDST], out[WM_NDST], src[WM_NSRC];
        int i;
        for (i = 0; i < 4; i++) {
            sl[i].src = (uint8_t)proj.par[P_M1SRC + 3 * i];
            sl[i].dst = (uint8_t)proj.par[P_M1DST + 3 * i];
            sl[i].amount = (int8_t)proj.par[P_M1AMT + 3 * i];
        }
        sl[4].src = WM_SRC_OFF, sl[4].dst = 0, sl[4].amount = 0;
        sl[5].src = WM_SRC_BREATH, sl[5].dst = WM_DST_CUTOFF, sl[5].amount = (int8_t)proj.par[P_BRCUT];
        for (i = 0; i < WM_NSRC; i++)
            src[i] = rt_ws.src[i];
        rt_bases(base);
        wm_apply(sl, base, src, out);
        return out[(unsigned)d < WM_NDST ? d : 0];
    }
    switch (d) {
    case WM_DST_CUTOFF: return rt_clamp(rt_ws.cutoff, 0.0f, 1.0f);
    case WM_DST_RES: return rt_clamp(rt_ws.res, 0.0f, 1.0f);
    case WM_DST_AMP: return rt_clamp(rt_ws.amp, 0.0f, 1.0f);
    case WM_DST_MORPH: return rt_clamp(rt_ws.morph, 0.0f, 1.0f);
    case WM_DST_TIMBRE: return rt_clamp(rt_ws.timbre, 0.0f, 1.0f);
    case WM_DST_NOISE: return rt_clamp(rt_ws.noise, 0.0f, 1.0f);
    case WM_DST_HARM: return rt_clamp(rt_ws.harmonics, 0.0f, 1.0f);
    default: return rt_clamp(rt_ws.aux, 0.0f, 1.0f);
    }
}

/* the knob's value as its page shows it ("Amp": 100, the gain it starts from) */
static void rt_dst_text(int d, char *t)
{
    d = (unsigned)d < WM_NDST ? d : 0;
    if (RT_KNOB[d] == 255)
        rt_copy(t, "100", 12);
    else
        wa_param_text(RT_KNOB[d], proj.par[RT_KNOB[d]], t);
}

/* a source's level now: 0..1, Bite -1..1 */
static float rt_src_level(int s)
{
    return (unsigned)s < WM_NSRC && s != WM_SRC_OFF ? rt_ws.src[s] : 0.0f;
}

/* a row of the live list: what, source -> destination (cut to fit left of the amount), the amount, the
 * source's level now */
typedef struct {
    char key[8], src[16], dst[16], amt[8];
    float lvl;
    uint8_t cur, inert;
} rt_row_t;

#define RT_ROWS 6                      /* the Mod slots, the LFO, the Filter's Breath */
#define RT_NAME_X 50                   /* the rows' names start here (with no key: at the key's place)... */
#define RT_KEY_X 16
#define RT_ROW_NAME_X(r) ((r)->key[0] ? RT_NAME_X : RT_KEY_X)
#define RT_AMT_R 172                   /* ...the amounts end here */
#define RT_LVL_X 178                   /* the level bars: from here, RT_LVL_W long */
#define RT_LVL_W 44
static const char *const RT_LFO_SHAPE[WA_LFO_NSHAPE] = {"Sine", "Tri", "Saw", "Square", "S&H"};

static int rt_slot_off(int i) { return proj.par[P_M1SRC + 3 * i] == WM_SRC_OFF || !proj.par[P_M1AMT + 3 * i]; }

/* the names cut to the room left of the amount: the destination keeps half of it at least */
static void rt_row_fit(rt_row_t *r)
{
    int32_t room = RT_AMT_R - text_w(&FONT_XS, r->amt) - 6 - RT_ROW_NAME_X(r) - RT_ARROW_W, ws = text_w(&FONT_XS, r->src),
            wd = text_w(&FONT_XS, r->dst);
    if (ws + wd <= room)
        return;
    rt_fit(r->dst, room - ws > room / 2 ? room - ws : room / 2);
    rt_fit(r->src, room - text_w(&FONT_XS, r->dst));
}

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
        r[n].inert = (uint8_t)rt_dst_inert(d);
        n++;
    }
    off[k] = 0;
    if (proj.par[P_LFOAMT]) {
        int d = proj.par[P_LFODST], sh = proj.par[P_LFOSHAPE];
        r[n].key[0] = 0;                         /* (its name says it: "LFO Sine") */
        rt_copy(r[n].src, "LFO ", sizeof r[n].src);
        rt_copy(r[n].src + 4, RT_LFO_SHAPE[(unsigned)sh < WA_LFO_NSHAPE ? sh : 0], (int)sizeof r[n].src - 4);
        rt_copy(r[n].dst, d == WM_DST_PITCH ? "Pitch" : rt_dst_name(d), sizeof r[n].dst);
        wa_param_text(P_LFOAMT, proj.par[P_LFOAMT], r[n].amt);
        r[n].lvl = rt_quiet() ? 0.0f : rt_ws.lfo;
        r[n].cur = 0;
        r[n].inert = (uint8_t)(d != WM_DST_PITCH && rt_dst_inert(d));
        n++;
    }
    if (proj.par[P_BRCUT]) {
        rt_copy(r[n].key, "Filter", sizeof r[n].key);
        rt_copy(r[n].src, WM_SRC_NAME[WM_SRC_BREATH], sizeof r[n].src);
        rt_copy(r[n].dst, WM_DST_NAME[WM_DST_CUTOFF], sizeof r[n].dst);
        rt_int(r[n].amt, proj.par[P_BRCUT], 1);
        r[n].lvl = rt_ws.breath;
        r[n].cur = 0;
        r[n].inert = 0;
        n++;
    }
    for (i = 0; i < n; i++)
        rt_row_fit(&r[i]);
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

/* the layout: four lines of list or fewer, as the design; five or six, the slot's panel gives up 20 px */
#define RT_SLOT_H(crowd) ((crowd) ? 54 : 74)
#define RT_LIST_Y(crowd) ((crowd) ? 62 : 82)
#define RT_ROW_Y(crowd) ((crowd) ? 63 : 85)      /* the first row's text */
#define RT_PITCH(crowd) ((crowd) ? 12 : 13)

static void rt_mod(int slot)
{
    int s = proj.par[P_M1SRC + 3 * slot], d = proj.par[P_M1DST + 3 * slot], a = proj.par[P_M1AMT + 3 * slot];
    int inert = rt_dst_inert(d), on = s != WM_SRC_OFF && a && !inert, bi = s == WM_SRC_BITE, n, i, lines, crowd;
    float lvl = s != WM_SRC_OFF && a ? rt_src_level(s) : 0.0f, mag = lvl < 0.0f ? -lvl : lvl, base, live;
    rt_row_t row[RT_ROWS];
    rt_modq_t q;
    char off[12], t[24];
    uint32_t h, st;
    const uint8_t *qb;
    {
        float b[WM_NDST];
        rt_bases(b);
        base = b[(unsigned)d < WM_NDST ? d : 0];
    }
    live = inert ? base : rt_dst_live(d);
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
        q.row_px[i] = (int16_t)(row[i].inert ? 0 : rt_round(rt_clamp(v, 0.0f, 1.0f) * (float)RT_LVL_W));
    }
    st = hash(hash(2166136261u, (uint32_t)ui.view | (uint32_t)proj.par[P_THEME] << 8), (uint32_t)proj.par[P_ENGINE]);
    for (i = 0; i < P_NPARAMS; i++)
        st = hash(st, (uint32_t)(uint16_t)proj.par[i]);
    h = st;
    for (qb = (const uint8_t *)&q, i = 0; i < (int)sizeof q; i++)
        h = hash(h, qb[i]);
    h |= 1u;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;

    lines = n + (off[0] != 0);
    crowd = lines > 4;
    cv_begin(240, MAIN_H, K_BG);
    /* the slot */
    cv_round(6, 4, 228, RT_SLOT_H(crowd), 6, K_PANEL);
    rt_upper(t, s != WM_SRC_OFF && a ? WM_SRC_NAME[(unsigned)s < WM_NSRC ? s : 0] : "Off", sizeof t);
    cv_text(16, 10, &FONT_XS, t, K_DIM);
    cv_round(16, 26, 56, 8, 4, K_LINE);
    if (bi) {
        int32_t m = 44, e = m + q.src_px;
        cv_rect(m, 23, 1, 14, K_DIM);
        if (a)
            cv_round(e < m ? e : m, 26, (e < m ? m - e : e - m) + 2, 8, 4, on ? K_ACC : K_DIM);
    } else if (s != WM_SRC_OFF && a && q.src_px > 0) {
        cv_round(16, 26, q.src_px < 8 ? 8 : q.src_px, 8, 4, on ? K_ACC : K_DIM);
    }
    if (s != WM_SRC_OFF && a) {
        rt_int(t, q.src_num, bi);
        cv_text(16, 40, &FONT_B, t, on ? K_TEXT : K_DIM);
    } else {
        cv_text(16, 40, &FONT_B, "-", K_DIM);
    }
    rt_upper(t, rt_dst_name(d), sizeof t);
    rt_fit(t, 62);
    cv_text(168, 10, &FONT_XS, t, K_DIM);
    cv_round(168, 26, 58, 8, 4, K_LINE);
    if (base > 0.0f)
        cv_round(168, 26, rt_round(58.0f * base) < 8 ? 8 : rt_round(58.0f * base), 8, 4, inert ? K_DIM : K_BLUE);
    rt_dst_text(d, t);
    if (inert) {                       /* it does nothing here: the knob's value alone */
        cv_text(168, 40, &FONT_B, t, K_DIM);
    } else {                           /* "40 -> 67": the knob, the value now */
        int32_t x;
        cv_round(168 + q.live_px - 1, 22, 3, 16, 1, K_ACC);
        x = cv_text(168, 40, &FONT_B, t, K_TEXT);
        x = rt_arrow(x + 3, 41, K_DIM);
        itoa_u((uint32_t)q.live_num, t);
        cv_text(x, 40, &FONT_B, t, K_ACC);
        if (!crowd)                    /* (the value now: every route's doing, not this slot's alone) */
            cv_text(226 - text_w(&FONT_XS, "now, all routes"), 58, &FONT_XS, "now, all routes", K_DIM);
    }
    rt_cable(q.cable_w, on ? rt_mix(K_ACC, K_PANEL, q.cable_t) : K_LINE);
    cv_round(105, 36, 32, 15, 6, on ? K_ACC : K_LINE);
    if (a && s != WM_SRC_OFF)
        rt_int(t, a, 1);
    else
        rt_copy(t, "Off", sizeof t);
    text_c(121, 36, &FONT_XS, t, on ? K_WHITE : K_DIM);

    /* everything modulating now: the highlight first, so no row's text is painted over */
    cv_round(6, RT_LIST_Y(crowd), 228, 140 - RT_LIST_Y(crowd), 6, K_PANEL);
    {
        int pitch = RT_PITCH(crowd);
        int32_t y;
        for (i = 0, y = RT_ROW_Y(crowd); i < n; i++, y += pitch)
            if (row[i].cur)
                cv_round(10, y + 1, 220, pitch, 4, K_BG);
        for (i = 0, y = RT_ROW_Y(crowd); i < n; i++, y += pitch) {
            int32_t x, bw = q.row_px[i];
            uint16_t c = row[i].cur && !row[i].inert ? K_TEXT : K_DIM;
            cv_text(RT_KEY_X, y, &FONT_XS, row[i].key, row[i].cur ? K_ACC : K_DIM);
            x = cv_text(RT_ROW_NAME_X(&row[i]), y, &FONT_XS, row[i].src, c);
            x = rt_arrow(x + 2, y - 1, K_DIM);
            cv_text(x, y, &FONT_XS, row[i].dst, c);
            cv_text(RT_AMT_R - text_w(&FONT_XS, row[i].amt), y, &FONT_XS, row[i].amt, K_DIM);
            cv_round(RT_LVL_X, y + 5, RT_LVL_W, 5, 2, K_LINE);
            if (bw > 0)
                cv_round(RT_LVL_X, y + 5, bw < 5 ? 5 : bw, 5, 2, row[i].cur ? K_ACC : rt_mix(K_ACC, K_PANEL, 10));
        }
        if (off[0]) {
            int32_t x = cv_text(16, y, &FONT_XS, off, K_DIM);
            cv_text(x + 6, y, &FONT_XS, "Off", K_DIM);
        }
        if (!lines)
            text_c(120, 102, &FONT_XS, "Nothing modulates the sound", K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

/* ----------------------------------------------------------- Setup --- */
/* a monitor row's text: its label (the source as its knob says it: "CC2", "Press"), name, the channel it
 * came on ("ch 3", "key", "" nothing yet) and value ("-": nothing yet, or not since the source changed);
 * returns 1 when there is a value */
static int rt_mon_text(int i, const wc_mon_t *m, char *lab, char *nm, char *ch, char *val)
{
    int ok;
    uint32_t c;
    switch (i) {
    case 0:
        wa_param_text(P_BSRC, proj.par[P_BSRC], lab);
        rt_copy(nm, "Breath", 12);
        ok = m->t_breath && m->breath_src == proj.par[P_BSRC];
        if (ok)
            itoa_u((uint32_t)m->breath >> 7, val);
        break;
    case 1:
        rt_copy(lab, "PB", 8);
        rt_copy(nm, "Bite", 12);
        ok = m->t_bend != 0u;
        if (ok)
            rt_int(val, m->bend, 0);
        break;
    case 2:
        wa_param_text(P_GSRC, proj.par[P_GSRC], lab);
        rt_copy(nm, "Glide", 12);
        ok = proj.par[P_GSRC] < WC_SRC_OFF && m->t_glide && m->glide_src == proj.par[P_GSRC];
        if (ok)
            itoa_u(m->glide, val);
        break;
    default:
        rt_copy(lab, "NOTE", 8);
        ok = m->t_note != 0u;
        if (ok) {
            int32_t k;
            note_text(m->note, nm);
            k = s_len(nm);
            rt_copy(nm + k, m->on ? " on" : " off", 12 - k);
            itoa_u(m->on ? m->velocity : 0u, val);
        } else {
            rt_copy(nm, "none", 12);
        }
        break;
    }
    c = m->ch[i];
    if (!ok) {
        rt_copy(val, "-", 8);
        ch[0] = 0;
    } else if (!c) {
        rt_copy(ch, "key", 8);
    } else {
        rt_copy(ch, "ch", 8);
        itoa_u(c, ch + 2);
    }
    return ok;
}

/* the MIDI ch setting hears it (a panel key: always) */
static int rt_heard(uint32_t ch) { return !ch || !proj.par[P_MIDICH] || ch == (uint32_t)proj.par[P_MIDICH]; }

/* what the Setup page shows, as drawn */
typedef struct {
    int16_t note, cents;               /* the note (-1000: none yet) and the cents off it */
    uint8_t gate, fresh[4], ok[4], heard[4];   /* fresh: 4 just in .. 1, 0 older than RT_FRESH_MS */
    int16_t bar[4];
    char lab[4][8], nm[4][12], ch[4][8], val[4][8];
} rt_setq_t;
#define RT_FRESH_MS 300u
#define RT_CH_R 114                    /* the channel ends here, before the bars */
#define RT_BAR_X 118                   /* the bars: from here, RT_BAR_W long */
#define RT_BAR_W 68

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
        q.ok[i] = (uint8_t)rt_mon_text(i, &m, q.lab[i], q.nm[i], q.ch[i], q.val[i]);
        q.heard[i] = (uint8_t)rt_heard(m.ch[i]);
        q.fresh[i] = (uint8_t)(q.ok[i] && age < RT_FRESH_MS ? 1u + (RT_FRESH_MS - 1u - age) * 4u / RT_FRESH_MS : 0u);
        rt_fit(q.nm[i], RT_CH_R - text_w(&FONT_XS, q.ch[i]) - 4 - 56);
    }
    q.bar[0] = (int16_t)(q.ok[0] ? m.breath * (uint32_t)RT_BAR_W / 16383u : 0);
    q.bar[1] = (int16_t)(q.ok[1] ? m.bend * (RT_BAR_W / 2 - 1) / 8192 : 0);
    q.bar[2] = (int16_t)(q.ok[2] ? m.glide * RT_BAR_W / 127 : 0);
    q.bar[3] = (int16_t)(q.ok[3] && m.on ? m.velocity * RT_BAR_W / 127 : 0);
    st = hash(hash(2166136261u, (uint32_t)ui.view | (uint32_t)proj.par[P_THEME] << 8), (uint32_t)proj.par[P_MIDICH]);
    h = st;
    for (qb = (const uint8_t *)&q, i = 0; i < (int)sizeof q; i++)
        h = hash(h, qb[i]);
    h |= 1u;
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
    /* the controller monitor: what came in, on which channel, and a dot lit as it comes */
    cv_round(6, 72, 228, 68, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t y = 76 + i * 16;
        uint16_t dot = q.fresh[i] ? rt_mix(K_ACC, K_PANEL, 4 + 3 * q.fresh[i]) : K_LINE;
        cv_round(12, y + 4, 6, 6, 3, dot);
        cv_text(24, y, &FONT_XS, q.lab[i], K_TEXT);
        cv_text(56, y, &FONT_XS, q.nm[i], K_DIM);
        cv_text(RT_CH_R - text_w(&FONT_XS, q.ch[i]), y, &FONT_XS, q.ch[i], q.heard[i] ? K_DIM : K_ACC);
        cv_round(RT_BAR_X, y + 4, RT_BAR_W, 6, 3, K_LINE);
        if (i == 1) {                              /* the bend: from the middle */
            int32_t mid = RT_BAR_X + RT_BAR_W / 2, e = mid + q.bar[1];
            cv_rect(mid, y + 1, 1, 12, K_DIM);
            if (q.ok[1])
                cv_round(e < mid ? e : mid, y + 4, (e < mid ? mid - e : e - mid) + 3, 6, 3, K_BLUE);
        } else if (q.bar[i] > 0) {
            cv_round(RT_BAR_X, y + 4, q.bar[i] < 6 ? 6 : q.bar[i], 6, 3, i == 0 ? K_ACC : K_BLUE);
        }
        cv_text(226 - text_w(&FONT_S, q.val[i]), y - 1, &FONT_S, q.val[i],
                q.fresh[i] ? K_ACC : q.ok[i] ? K_TEXT : K_DIM);
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

#ifdef OM_HOST
/* the views' text, for the host's checks (tests/scenarios/view_route.scn): rtrowN, a Mod page's list row N
 * (0..) as "key|source>destination|amount" ("-" none); rtfitN, "ok" when its names clear its amount; rtoff, its off slots ("none"); rtdst, the cable's
 * destination; rtmonN, Setup's monitor row N as "label|name|channel|value" (the channel
 * "ch3!" when the MIDI ch setting doesn't hear it); as of the frame drawn. Returns 0: not one
 * of these. */
static const char *rt_text(const char *what, char *b)
{
    if (!strncmp(what, "rtrow", 5) || !strncmp(what, "rtfit", 5) || !strcmp(what, "rtoff")) {
        rt_row_t r[RT_ROWS];
        char off[12];
        int n, i = atoi(what + 5);
        n = rt_rows(ui.view >= V_MOD1 && ui.view <= V_MOD4 ? ui.view - V_MOD1 : -1, r, off);
        if (what[2] == 'o')
            snprintf(b, 64, "%s", off[0] ? off : "none");
        else if (what[2] == 'f' && i < n)       /* rtfitN: row N's names clear of its amount, as drawn */
            snprintf(b, 64, "%s", RT_ROW_NAME_X(&r[i]) + text_w(&FONT_XS, r[i].src) + RT_ARROW_W +
                                          text_w(&FONT_XS, r[i].dst) + 4 <= RT_AMT_R - text_w(&FONT_XS, r[i].amt)
                                      ? "ok" : "overlap");
        else if (i < n)
            snprintf(b, 64, "%s|%s>%s|%s", r[i].key, r[i].src, r[i].dst, r[i].amt);
        else
            snprintf(b, 64, "-");
        return b;
    }
    if (!strcmp(what, "rtdst")) {
        int s = ui.view >= V_MOD1 && ui.view <= V_MOD4 ? ui.view - V_MOD1 : 0;
        rt_upper(b, rt_dst_name(proj.par[P_M1DST + 3 * s]), 24);
        rt_fit(b, 62);
        return b;
    }
    if (!strncmp(what, "rtmon", 5)) {
        char lab[8], nm[12], ch[8], val[8];
        wc_mon_t m;
        int i = atoi(what + 5) & 3;
        m = rt_ws.mon;
        rt_mon_text(i, &m, lab, nm, ch, val);
        rt_fit(nm, RT_CH_R - text_w(&FONT_XS, ch) - 4 - 56);
        snprintf(b, 64, "%s|%s|%s%s|%s", lab, nm, ch, ch[0] && !rt_heard(m.ch[i]) ? "!" : "", val);
        return b;
    }
    return 0;
}
#endif

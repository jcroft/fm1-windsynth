/* SPDX-License-Identifier: GPL-3.0-only */
/* The Engine page's view: a scope of the voice, locked to the note's period so a held note's wave stands
 * still: two periods across the panel, the newest trace over the two before it, fainter (a phosphor
 * trail). Read at most every other frame (~30 a second), redrawn only when what it shows changes (a
 * held note's trace, locked: seldom); quiet, a flat line and a hint. Included by ui.c (its canvas, theme and state). */

#define VS_X 14                        /* the trace: x 14..226, centred on y 72 of the band */
#define VS_N 213                       /* points: two periods, so point i and i + VS_N/2 are one apart */
#define VS_MID 72
#define VS_AMP 125.0f                  /* px for full scale, before MASTER (clipped to the panel) */
#define VS_TOP 7
#define VS_BOT 137
#define VS_TRAIL 3                     /* traces kept: the newest and two fading */
#define VS_QUIET 0.0003f               /* a trace's peak under this (-70 dB): quiet */

static struct {
    int16_t y[VS_TRAIL][VS_N];         /* y[0] the newest */
    uint8_t n;                         /* traces held */
    int16_t note;                      /* the note they show (MIDI, rounded) */
    uint32_t frame;                    /* the frame of the last read */
    uint32_t sig;                      /* what they look like (the redraw's hash: a held note's, unchanged) */
    float per;                         /* the period the last read locked to, in samples (0: none) */
    int16_t last[VS_N];                /* the last trace read, drawn or not */
    uint32_t reads;                    /* traces read since a clear */
    uint16_t still;                    /* tenths of a px the last read moved from the one before, on average */
} vs;

static uint16_t blend565(uint16_t a, uint16_t b, uint32_t n);   /* (ui.c's, defined further down) */

static void vs_clear(void)
{
    vs.n = 0;
    vs.reads = 0u;
    vs.per = 0.0f;
    vs.sig = 0u;
}

/* a new trace from the audio's ring, the older ones moved down the trail (a new note: the trail goes);
 * one within half a px of the newest on average is left out (a held note: nothing to redraw); a silent
 * one clears them */
static void vs_capture(void)
{
    float y[VS_N], per = wa_scope(y, VS_N, 2), pk = 0.0f;
    int i, j, d, moved = 0, still = 0, note = (int)(wa_state.note + 0.5f + 1200.0f) - 1200;
    int16_t t[VS_N];
    if (per <= 0.0f)
        return;                        /* (none yet, or not of this note yet, or the audio moved on while it
                                        * read: the next frame's) */
    for (i = 0; i < VS_N; i++)
        pk = y[i] > pk ? y[i] : -y[i] > pk ? -y[i] : pk;
    if (pk < VS_QUIET) {
        vs_clear();
        return;
    }
    for (i = 0; i < VS_N; i++) {
        float p = (float)VS_MID - y[i] * VS_AMP;
        p = p < (float)VS_TOP ? (float)VS_TOP : p > (float)VS_BOT ? (float)VS_BOT : p;
        t[i] = (int16_t)(p + 0.5f);
        d = t[i] - vs.y[0][i];
        moved += d < 0 ? -d : d;
        d = t[i] - vs.last[i];
        still += d < 0 ? -d : d;
    }
    vs.still = vs.reads++ ? (uint16_t)((still * 10 + VS_N / 2) / VS_N) : 999u;
    memcpy(vs.last, t, sizeof t);
    vs.per = per;
    if (note == vs.note && vs.n && moved * 2 < VS_N)
        return;                        /* (as drawn, to half a px on average: a held note's; no redraw) */
    if (note != vs.note)
        vs.n = 0;
    vs.note = (int16_t)note;
    for (i = VS_TRAIL - 1; i > 0; i--)
        memcpy(vs.y[i], vs.y[i - 1], sizeof vs.y[0]);   /* (no memmove on the device) */
    memcpy(vs.y[0], t, sizeof vs.y[0]);
    if (vs.n < VS_TRAIL)
        vs.n++;
    vs.sig = 2166136261u;
    for (j = 0; j < vs.n; j++)
        for (i = 0; i < VS_N; i++)
            vs.sig = hash(vs.sig, (uint32_t)vs.y[j][i]);
}

/* the label on the right: the engine's tone as it is now (breath, the Mod slots), Timbre's knob, or
 * Morph's on Classic: "Square 67"; 0 if it has neither */
static int vs_label(char *t)
{
    int lk = knob_of(1), live, i = 0;
    const char *p;
    if (lk == P_INDEX)
        live = (int)(wa_state.timbre * 100.0f + 0.5f);
    else if ((lk = knob_of(2)) == P_MORPH)
        live = (int)(wa_state.morph * 100.0f + 0.5f);
    else
        return 0;
    for (p = wa_param_name(lk); *p && i < 14; p++)
        t[i++] = *p;
    t[i++] = ' ';
    itoa_u((uint32_t)(live < 0 ? 0 : live > 100 ? 100 : live), t + i);
    return 1;
}

/* text with a panel-coloured edge, readable over a trace */
static void vs_text(int32_t x, int32_t y, const char *s, uint16_t c)
{
    int dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx || dy)
                cv_text(x + dx, y + dy, &FONT_XS, s, K_PANEL);
    cv_text(x, y, &FONT_XS, s, c);
}

static void vs_trace(const int16_t *y, uint16_t c, int thick)
{
    int i;
    for (i = 1; i < VS_N; i++) {
        cv_line(VS_X + i - 1, y[i - 1], VS_X + i, y[i], c);
        if (thick)
            cv_line(VS_X + i - 1, y[i - 1] + 1, VS_X + i, y[i] + 1, c);
    }
}

static int vw_scope(int v)
{
    int j;
    uint32_t h;
    char lb[24];
    if (v != V_ENGINE)
        return 0;
    /* every other frame, a trace (a redraw from scratch, ui.sig[1] cleared, draws what was drawn) */
    if (ui.sig[1] && ui.frame - vs.frame >= 2u) {
        if (ui.frame - vs.frame > 4u)                /* back on the page: no trail from before */
            vs_clear();
        vs.frame = ui.frame;
        vs_capture();
    }
    h = hash(hash(2166136261u, (uint32_t)v | (uint32_t)proj.par[P_THEME] << 8 | (uint32_t)proj.par[P_ENGINE] << 12 |
                                   (uint32_t)vs.n << 20),
             vs.sig);
    if (vs.n) {                                      /* (the labels: only over a trace) */
        h = hash(h, (uint32_t)(vs.note + 2000));
        if (vs_label(lb))
            for (j = 0; lb[j]; j++)
                h = hash(h, (uint8_t)lb[j]);
    }
    if (h == ui.sig[1])
        return 1;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(6, 4, 228, 136, 6, K_PANEL);
    if (!vs.n) {                                     /* quiet: a flat line */
        cv_rect(VS_X, VS_MID, VS_N - 1, 1, K_DIM);
        text_c(120, VS_MID - 30, &FONT_S, "Blow a note", K_DIM);
        cv_blit(0, MAIN_Y);
        return 1;
    }
    for (j = 0; j < VS_N - 1; j += 5)                /* the axes, dotted */
        cv_rect(VS_X + j, VS_MID, 2, 1, K_LINE);
    for (j = 14; j < 132; j += 5)
        cv_rect(VS_X + (VS_N - 1) / 2, j, 1, 2, K_LINE);
    for (j = vs.n - 1; j >= 0; j--)                  /* the oldest first, fainter */
        vs_trace(vs.y[j], j ? blend565(K_ACC, K_PANEL, j == 1 ? 130u : 85u) : K_ACC, !j);
    {
        char t[24], nt[8];
        int i = 0;
        const char *p = "2 cycles of ";
        while (*p)
            t[i++] = *p++;
        note_text(vs.note, nt);
        for (p = nt; *p; p++)
            t[i++] = *p;
        t[i] = 0;
        vs_text(VS_X, 9, t, K_DIM);
        if (vs_label(lb))
            vs_text(VS_X + VS_N - 1 - text_w(&FONT_XS, lb), 9, lb, K_ACC);
    }
    cv_blit(0, MAIN_Y);
    return 1;
}

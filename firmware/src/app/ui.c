/* SPDX-License-Identifier: GPL-3.0-only */
/* Windsynth UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. (The frame of it, knobs, bands and autosave, is FoMni's and FiMba's ui.c.)
 *
 *   the screen    the note playing and the bite's bend; breath, cutoff and level as bars; the page's
 *                 four values along the bottom, each with a ghost (an accent tick) at its live value
 *                 when something moves it: a Mod slot, the LFO, the Filter's Breath, the glide CC
 *   HOME ENV LFO SEL SEQ GLO FX   the pages (pressed again: the button's next page): HOME Filter,
 *                 Engine (named for the engine: ALGORITHM picks it) and Noise, ENV Breath, LFO LFO and LFO 2, SEL Mod 1-4, SEQ Pitch and Pitch 2, GLO Setup, FX Distortion,
 *                 Modulation, Delay, Reverb and Reverb 2 (the order the sound goes through them). EDIT and
 *                 SELECT step through them all.
 *   PLAY          Stage: the controllers (note, breath, bite, glide, output) over twelve knobs in three
 *                 rows (Filter, the engine, the effects' mixes); KNOB 1-4 turn the lit row, SELECT moves
 *                 it; the keys light dim from the left with the breath. PLAY again: back to the page
 *   KNOB 1-4      the page's values      PRESETS the preset (1-64)      OCT- / OCT+ the panel keys' octave
 *   the keys      play notes (F3 up, chromatic), at full breath (they sense no pressure)
 *   SAVE          the sound into a preset: "SAVE TO P12", PRESETS picks another, SAVE again saves; held
 *                 1.5 s: the init sound (SAVE again to confirm). What you play is kept by itself, a few
 *                 seconds after a change, when quiet, with the preset it came from.
 *
 * MIDI never passes through here: wind_app reads it in the audio interrupt, every 1.45 ms. */

#include "../dsp/fastmath.h"            /* (Stage's dials: no libm on the device) */

enum { V_FILTER, V_ENGINE, V_NOISE, V_BREATH, V_LFO, V_LFO2, V_MOD1, V_MOD2, V_MOD3, V_MOD4, V_PITCH, V_PITCH2, V_SETUP,
       V_DIST, V_MODFX, V_DELAY, V_REVERB, V_REVERB2, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Filter", "Engine", "Noise", "Breath", "LFO", "LFO 2", "Mod 1", "Mod 2", "Mod 3",
                                              "Mod 4", "Pitch", "Pitch 2", "Setup", "Distortion", "Modulation", "Delay",
                                              "Reverb", "Reverb 2"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_HOME, B_HOME, B_ENV, B_LFO, B_LFO, B_SEL, B_SEL, B_SEL, B_SEL, B_SEQ, B_SEQ,
                                         B_GLO, B_FX, B_FX, B_FX, B_FX, B_FX};
#define K_NONE 255                     /* a knob with nothing on it */
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_CUTOFF, P_RESO, P_SLOPE, P_BRCUT},
    {P_RATIO, P_INDEX, P_MORPH, P_AUX},          /* (the engine itself: ALGORITHM, and the header shows it) */
    {P_NZLEVEL, P_NZTYPE, P_NZFREQ, P_NZRESO},
    {P_BSRC, P_CURVE, P_ATTACK, P_RELEASE},
    {P_LFOSHAPE, P_LFORATE, P_LFODST, P_LFOAMT},
    {P_LFOFADE, P_LFOSYNC, K_NONE, K_NONE},
    {P_M1SRC, P_M1DST, P_M1AMT, K_NONE},
    {P_M2SRC, P_M2DST, P_M2AMT, K_NONE},
    {P_M3SRC, P_M3DST, P_M3AMT, K_NONE},
    {P_M4SRC, P_M4DST, P_M4AMT, K_NONE},
    {P_BEND, P_GLIDE, P_GSRC, P_GMODE},
    {P_GDEPTH, K_NONE, K_NONE, K_NONE},
    {P_TRANSPOSE, P_TUNE, P_MIDICH, P_THEME},
    {P_DSTYPE, P_DSMIX, P_DSDRIVE, P_DSTONE},
    {P_MOTYPE, P_MOMIX, P_MORATE, P_MODEPTH},
    {P_DLMODE, P_DLMIX, P_DLTIME, P_DLFB},
    {P_RVTYPE, P_RVMIX, P_RVDECAY, P_RVTONE},
    {P_RVPRE, P_RVSIZE, P_RVMOD, P_RVLOW},
};
#define SAVE_HOLD_MS 1500u             /* SAVE held this long: the init sound? (a tap: save to a preset) */
#define RESET_ASK_MS 5000u             /* ...asked this long: SAVE again resets, anything else keeps */
#define SAVE_ASK_MS 8000u              /* "SAVE TO": this long without a turn or a press, and it's off */
#define KEY_F3 53                      /* the lowest key's MIDI note */

/* the screen's colours (Setup: Theme): Light and Dark */
typedef struct {
    uint16_t bg, panel, line, dim, text, white, acc, acc_t, blue, blue_t;
} theme_t;
static const theme_t THEMES[THEME_N] = {
    {RGB(244, 241, 234), RGB(228, 223, 212), RGB(204, 198, 186), RGB(140, 134, 124), RGB(40, 38, 34),
     RGB(255, 255, 255), RGB(214, 92, 48), RGB(244, 204, 186), RGB(40, 128, 150), RGB(190, 224, 230)},
    {RGB(20, 20, 22), RGB(34, 34, 38), RGB(70, 70, 76), RGB(140, 140, 148), RGB(228, 226, 220),
     RGB(20, 20, 22), RGB(232, 120, 52), RGB(84, 46, 24), RGB(96, 196, 214), RGB(30, 58, 66)},
};
#define THEME (&THEMES[(unsigned)proj.par[P_THEME] < THEME_N ? proj.par[P_THEME] : 0])
#define K_BG (THEME->bg)
#define K_PANEL (THEME->panel)
#define K_LINE (THEME->line)
#define K_DIM (THEME->dim)
#define K_TEXT (THEME->text)
#define K_WHITE (THEME->white)                 /* text on the accent */
#define K_ACC (THEME->acc)
#define K_ACC_T (THEME->acc_t)
#define K_BLUE (THEME->blue)
#define K_BLUE_T (THEME->blue_t)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[4];                   /* the bands' hashes as drawn; [3]: the strip's, without its ghosts */
    uint32_t ghost_t;                  /* when the strip last drew for a ghost alone */
    int16_t ghost[4];                  /* each knob's ghost, the pixel it sits at (-1: none) */
    uint8_t stage;                     /* 1: Stage is showing (PLAY) */
    float br_pk, out_pk;               /* Stage's breath and output peaks, falling */
    uint32_t pk_t;                     /* ...as of then */
    uint32_t save_t;                   /* when SAVE went down (held: reset to the defaults?) */
    uint32_t reset_ask;                /* until when "RESET?" waits for SAVE (0: not asking) */
    uint32_t save_ask;                 /* until when "SAVE TO" waits for SAVE (0: not asking) */
    uint8_t save_slot;                 /* ...and the preset it would save to */
    uint32_t midi_seen;                /* wa_state.midi_seen at the last frame (activity) */
    uint8_t key_note[NKEYS];           /* the note each held key plays (released as it was) */
} ui;

/* a knob's value on this page: Reverb 2's Mod is Shimmer's amount while the type is Shimmer; the Engine
 * page's Ratio is 6-op's patch, and Classic has no Ratio or Index */
static int knob_sub(int k)
{
    if (k == P_RVMOD && proj.par[P_RVTYPE] == WF_RV_SHIMMER)
        return P_RVSHIM;
    if ((k == P_RATIO || k == P_INDEX) && proj.par[P_ENGINE] == WV_CLASSIC)
        return K_NONE;
    if (k == P_AUX && proj.par[P_ENGINE] == WV_SIXOP)   /* (6-op's AUX is its main output) */
        return K_NONE;
    if (k == P_RATIO && proj.par[P_ENGINE] == WV_SIXOP)
        return P_DXPATCH;
    return k;
}

/* Stage (PLAY): twelve knobs, a row of four on K1-K4 at a time; SELECT moves the row */
#define STAGE_ROWS 3
static const uint8_t STAGE_KNOB[STAGE_ROWS][4] = {
    {P_BRCUT, P_CUTOFF, P_RESO, P_NZLEVEL},
    {P_RATIO, P_INDEX, P_MORPH, P_AUX},          /* (named for the engine, as the Engine page) */
    {P_DSMIX, P_MOMIX, P_DLMIX, P_RVMIX},
};
static const char *const STAGE_NAME[2][4] = {    /* the Filter and FX rows' names (the engine's: its own) */
    {"Breath", "Cutoff", "Reso", "Noise"}, {"Dist", "Mod", "Delay", "Verb"}};
static int stage_row(void) { return (unsigned)proj.par[P_STAGEROW] < STAGE_ROWS ? proj.par[P_STAGEROW] : 0; }
static int stage_knob(int r, int i) { return knob_sub(STAGE_KNOB[r][i]); }

static int knob_of(int i)
{
    return ui.stage ? stage_knob(stage_row(), i) : knob_sub(VIEW_KNOB[ui.view][i]);
}

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
int ui_dirty(void) { return ui.dirty != 0; }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void mark_dirty(void) { ui.dirty = 1; }
void ui_mark_dirty(void) { mark_dirty(); }

/* (no C library on the device: libc.c has only mem*) */
static int32_t s_len(const char *s)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* ------------------------------------------------------------ values --- */
static void say_param(int k)
{
    char t[16];
    wa_param_text(k, proj.par[k], t);
    say(wa_param_name(k), t);
}

static void knob_set(int k, int v)
{
    const wa_param_t *p = wa_param_info(k);
    v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
    if (v == proj.par[k])
        return;
    proj.par[k] = (int16_t)v;
    wa_set(k, v);
    mark_dirty();
}

/* knob acceleration by speed: a slow turn is one step a detent, a quick one up to 8 (Release: 2000 steps) */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (range <= 24 || !a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 8u : dt < 30u ? 5u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    const wa_param_t *p;
    if (!e || k == K_NONE)
        return;
    p = wa_param_info(k);
    knob_set(k, proj.par[k] + accel(role, e, p->hi - p->lo));
    if (k == P_DXPATCH)                /* (the knob shows its number: the name, in full, here) */
        say_param(k);
}

/* ------------------------------------------------------------- keys --- */
static int key_note(int k) { return KEY_F3 + k + 12 * proj.par[P_OCTAVE]; }

static void key_down(int k)
{
    int n = key_note(k);
    n = n < 0 ? 0 : n > 127 ? 127 : n;
    ui.key_note[k] = (uint8_t)n;
    wa_key(n, 1);
}

static void key_up(int k) { wa_key(ui.key_note[k], 0); }

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

/* "P12" */
static void preset_name(int slot, char *t)
{
    t[0] = 'P';
    t[1] = (char)('0' + (slot + 1) / 10);
    t[2] = (char)('0' + (slot + 1) % 10);
    t[3] = 0;
}

/* back to the init sound: every sound setting. The rig's (breath source and curve, the glide CC, the
 * keys, Setup) stays, and so does the preset it would be saved to. Kept as any change is. */
static void reset_defaults(void)
{
    int k;
    for (k = 0; k < P_NPARAMS; k++)
        if (!wa_param_global(k))
            knob_set(k, wa_param_info(k)->def);
    mark_dirty();
    say("INIT", "SOUND");
}

static void save_ask(int on)
{
    char t[4];
    ui.save_ask = on ? plat_ms() + SAVE_ASK_MS : 0u;
    if (on) {
        preset_name(ui.save_slot, t);
        say("SAVE TO", t);
        ui.msg_until = ui.save_ask;
    }
}

static void reset_ask(int on)
{
    ui.reset_ask = on ? plat_ms() + RESET_ASK_MS : 0u;
    if (on) {
        say("RESET?", "SAVE: YES");
        ui.msg_until = ui.reset_ask;
    } else {
        say("KEPT", 0);
    }
}

static void button(int b)
{
    int v;
    if (ui.save_ask) {                              /* "SAVE TO": SAVE saves, any other button doesn't */
        char t[4];
        save_ask(0);
        preset_name(ui.save_slot, t);
        if (b != B_SAVE)
            say("NOT SAVED", 0);
        else if (preset_store(ui.save_slot) == 0) {
            mark_dirty();                           /* (the project now names this preset) */
            say("SAVED", t);
        } else
            say("NOT SAVED", "FLASH ERROR");
        return;
    }
    if (ui.reset_ask) {                             /* "RESET?": SAVE resets, any other button keeps */
        ui.reset_ask = 0u;
        if (b == B_SAVE)
            reset_defaults();
        else
            say("KEPT", 0);
        return;
    }
    if (b == B_PLAY) {                              /* Stage, and back to the page it came from */
        ui.stage ^= 1u;
        ui.touched = -1;
        return;
    }
    if (ui.stage) {                                 /* a page button or EDIT: off Stage, to that page */
        for (v = 0; v < NVIEWS && VIEW_BTN[v] != b; v++)
            ;
        if (v < NVIEWS || b == B_EDIT) {
            ui.stage = 0u;
            ui.touched = -1;
            if (b == B_EDIT || VIEW_BTN[ui.view] == b)
                return;                             /* (the page it was on) */
        }
    }
    if (VIEW_BTN[ui.view] == b) {                   /* pressed again: its next page, or round to its first */
        v = ui.view + 1;
        if (v >= NVIEWS || VIEW_BTN[v] != b)
            for (v = ui.view; v > 0 && VIEW_BTN[v - 1] == b; v--)
                ;
        set_view(v);
        return;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_EDIT: set_view((ui.view + 1) % NVIEWS); break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_OCTAVE, proj.par[P_OCTAVE] + (b == B_OCTUP ? 1 : -1));
        say_param(P_OCTAVE);
        break;
    case B_SAVE:
        ui.save_slot = proj.preset;
        save_ask(1);
        break;
    default: break;
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_SAVE)
                ui.save_t = plat_ms();
        } else if (!(ui.btn_used & m)) {
            button((int)i);
        }
    }
    if ((btn >> B_SAVE & 1u) && !(ui.btn_used >> B_SAVE & 1u) && !ui.reset_ask && !ui.save_ask &&
        plat_ms() - ui.save_t >= SAVE_HOLD_MS) {
        reset_ask(1);                                /* SAVE held: ask (no save on release) */
        ui.btn_used |= 1u << B_SAVE;
    }
    if (ui.reset_ask && (int32_t)(plat_ms() - ui.reset_ask) >= 0)
        reset_ask(0);                                /* not answered: kept */
    if (ui.save_ask && (int32_t)(plat_ms() - ui.save_ask) >= 0) {
        save_ask(0);                                 /* not answered: not saved */
        say("NOT SAVED", 0);
    }
    ch = keys ^ ui.keys;
    if (ui.reset_ask && (ch & keys))                 /* a key while "RESET?" asks: kept (and it plays) */
        reset_ask(0);
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u)
                key_down((int)i);
            else
                key_up((int)i);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    if (wa_state.midi_seen != ui.midi_seen) {         /* playing from MIDI counts as activity (autosave waits) */
        ui.midi_seen = wa_state.midi_seen;
        ui.act_t = plat_ms();
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.stage) {                              /* Stage: the row */
            knob_set(P_STAGEROW, stage_row() + (e > 0 ? 1 : -1));
            ui.touched = -1;
        } else {
            set_view((ui.view + (e > 0 ? 1 : NVIEWS - 1)) % NVIEWS);
        }
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {           /* the preset; while "SAVE TO" asks, where to */
        int s = (ui.save_ask ? ui.save_slot : proj.preset) + (int)e;
        s = s < 0 ? 0 : s >= NPRESETS ? NPRESETS - 1 : s;
        if (ui.save_ask) {
            ui.save_slot = (uint8_t)s;
            save_ask(1);
        } else if (s != proj.preset) {
            char t[4];
            preset_load(s);
            mark_dirty();
            preset_name(s, t);
            say("PRESET", t);
        }
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {             /* the engine */
        knob_set(P_ENGINE, proj.par[P_ENGINE] + (e > 0 ? 1 : -1));
        say_param(P_ENGINE);
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, knob_of((int)i), e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = plat_ms();
        }
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the instrument (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68
static void draw_stage_mon(void);     /* (Stage: below) */
static void draw_stage_grid(void);

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* a page's name in the header: the Engine page shows the engine ("VA", "Waveshape") */
static const char *engine_name(void) { return WA_ENGINE_NAME[(unsigned)proj.par[P_ENGINE] < WV_NENGINE ? proj.par[P_ENGINE] : 0]; }
static const char *view_name(int v) { return v == V_ENGINE ? engine_name() : VIEW_NAME[v]; }
static const char *title_now(void) { return ui.stage ? "Stage" : view_name(ui.view); }
static const char *stage_row_name(int r) { return r == 0 ? "Filter" : r == 1 ? engine_name() : "FX"; }

static void draw_header(void)
{
    char t[24];
    int msg = ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0, midi = wa_state.midi_seen != 0u, edited = preset_edited();
    if (!msg)
        ui.msg_until = 0u;              /* (over: 0, so a wrap of the ms count can't bring it back) */
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)proj.par[P_THEME] << 8 | (uint32_t)ui.dirty << 10 |
                                            (uint32_t)msg << 11 | (uint32_t)midi << 12),
                      (uint32_t)proj.par[P_MIDICH] | (uint32_t)(proj.par[P_TRANSPOSE] + 24) << 8 |
                          (uint32_t)proj.preset << 16 | (uint32_t)edited << 24);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (ui.view == V_SETUP && !ui.stage)
        h = hash(h, plat_cpu_pct() | plat_xruns() << 8);
    h = hash(h, (uint32_t)proj.par[P_ENGINE] | (uint32_t)ui.stage << 8 | (uint32_t)stage_row() << 9);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 232, w;
        cv_text(10, 6, &FONT_B, title_now(), K_TEXT);
        if (ui.view == V_SETUP && !ui.stage) {                     /* the audio load and the version, for a check on the hardware */
            itoa_u(plat_cpu_pct(), t);
            w = s_len(t);
            t[w++] = '%';
            t[w] = 0;
            if (plat_xruns()) {
                t[w++] = ' ';
                t[w++] = 'X';
                itoa_u(plat_xruns(), t + w);
            }
            cv_text(x - text_w(&FONT_S, t), 6, &FONT_S, t, K_DIM);
            x -= text_w(&FONT_S, t) + 6;
            x -= text_w(&FONT_S, "CPU");
            cv_text(x, 6, &FONT_S, "CPU", K_DIM);
            for (w = 0; OM_VERSION[w] && OM_VERSION[w] != ' ' && w < 15; w++)   /* "0.0.3", "DEV" */
                t[w] = OM_VERSION[w];
            t[w] = 0;
            x -= text_w(&FONT_S, t) + 12;
            cv_text(x, 6, &FONT_S, t, K_DIM);
        } else {                                      /* the MIDI channel: "MIDI Omni", "MIDI 3" */
            char v[12];
            wa_param_text(P_MIDICH, proj.par[P_MIDICH], v);
            x -= text_w(&FONT_S, v);
            cv_text(x, 6, &FONT_S, v, midi ? K_TEXT : K_DIM);
            x -= text_w(&FONT_S, "MIDI ");
            cv_text(x, 6, &FONT_S, "MIDI ", K_DIM);
            preset_name(proj.preset, v);             /* the preset: "P12", "P12*" when the sound has changed */
            if (edited)
                v[3] = '*', v[4] = 0;
            x -= text_w(&FONT_B, v) + 10;
            cv_text(x, 5, &FONT_B, v, K_TEXT);
        }
        x = 18 + text_w(&FONT_B, title_now());
        if (ui.stage) {                               /* Stage: a dot per row, and the row's name */
            int r;
            for (r = 0; r < STAGE_ROWS; r++)
                cv_round(x - 4 + r * 9, 12, 6, 6, 3, r == stage_row() ? K_TEXT : K_LINE);
            x += STAGE_ROWS * 9 + 2;
            x = cv_text(x, 7, &FONT_S, stage_row_name(stage_row()), K_ACC) + 6;
        } else {   /* the button's pages, when it has more than one: a dot each, this one filled */
            int first = ui.view, n = 0, v;
            while (first > 0 && VIEW_BTN[first - 1] == VIEW_BTN[ui.view])
                first--;
            for (v = first; v < NVIEWS && VIEW_BTN[v] == VIEW_BTN[ui.view]; v++)
                n++;
            if (n > 1) {
                for (v = 0; v < n; v++)
                    cv_round(x - 4 + v * 9, 12, 6, 6, 3, first + v == ui.view ? K_TEXT : K_LINE);
                x += n * 9 + 2;
            }
        }
        if (ui.dirty)
            cv_round(x, 12, 5, 5, 2, K_DIM);
    }
    cv_blit(0, 0);
}

/* a vertical bar 0..1 with its name under it */
#define BAR_H 100
static int32_t bar_px(float v) { return (int32_t)((v > 1.0f ? 1.0f : v) * (float)(BAR_H - 4) + 0.5f); }

static void bar(int32_t x, int32_t y, int32_t h, float v, const char *name, uint16_t c)
{
    int32_t f = bar_px(v);
    cv_round(x, y, 22, h, 5, K_PANEL);
    if (f > 0)
        cv_round(x + 3, y + h - 2 - f, 16, f, 4, c);
    text_c(x + 11, y + h + 4, &FONT_XS, name, K_DIM);
}

/* a note's name, "C4", "F#-2", "G11": MIDI 60 is C4 */
static void note_text(int n, char t[8])
{
    int oct = (n >= 0 ? n / 12 : (n - 11) / 12) - 1, i = 0;     /* (rounded down below 0: C-2 is -12) */
    const char *nm = WA_NOTE_NAME[((n % 12) + 12) % 12];
    t[i++] = nm[0];
    if (nm[1])
        t[i++] = nm[1];
    if (oct < 0)
        t[i++] = '-', oct = -oct;
    if (oct >= 10)
        t[i++] = (char)('0' + oct / 10 % 10);
    t[i++] = (char)('0' + oct % 10);
    t[i] = 0;
}

/* the instrument: the note (and how far the bite bends it) on the left; breath, cutoff and level */
static void draw_main(void)
{
    if (ui.stage) {
        draw_stage_mon();
        return;
    }
    float note = wa_state.note, bend = wa_state.bend;
    int top = wa_state.top, gate = wa_state.gate > 0.5f;
    /* the note, its cents and the bite bar's end, as drawn; the bars by the pixel they fill to: so any
     * change that shows is drawn */
    int n = (int)(note + 0.5f + 1200.0f) - 1200, c = (int)((note - (float)n) * 100.0f);
    int32_t cx = 73 + (int32_t)(bend * 54.0f);
    uint32_t qb = (uint32_t)bar_px(wa_state.breath), qc = (uint32_t)bar_px(wa_state.cutoff),
             ql = (uint32_t)bar_px(wa_state.level);
    uint32_t h = hash(hash(hash(hash(2166136261u, qb | qc << 8 | ql << 16 | (uint32_t)gate << 24 |
                                                      (uint32_t)proj.par[P_THEME] << 25),
                                (uint32_t)(n + 2000) | (uint32_t)(c + 100) << 16),
                           (uint32_t)(cx + 1000)),
                      (uint32_t)(top + 1));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(6, 4, 134, 120, 6, K_PANEL);
    if (top < 0) {
        text_c(73, 40, &FONT_B, "Blow a note", K_DIM);
        text_c(73, 60, &FONT_S, "or press a key", K_DIM);
    } else {
        char t[8];
        int32_t i;
        note_text(n, t);
        text_c(73, 18, &FONT_L, t, gate ? K_TEXT : K_DIM);
        if (c) {                                     /* off the note: "+37c" */
            char d[8];
            i = 0;
            d[i++] = c > 0 ? '+' : '-';
            itoa_u((uint32_t)(c > 0 ? c : -c), d + i);
            i = s_len(d);
            d[i++] = 'c';
            d[i] = 0;
            text_c(73, 54, &FONT_S, d, K_ACC);
        }
        /* the bite: a bar from the middle, a semitone to a tick */
        cv_rect(18, 92, 110, 2, K_LINE);
        cv_rect(72, 86, 2, 14, K_DIM);
        cv_round(cx < 73 ? cx : 73, 89, (cx < 73 ? 73 - cx : cx - 73) + 2, 8, 3, K_BLUE);
        text_c(73, 102, &FONT_XS, "BITE", K_DIM);
        if (gate)
            cv_round(122, 12, 10, 10, 5, K_ACC);
    }
    bar(150, 8, BAR_H, wa_state.breath, "BRTH", K_ACC);
    bar(180, 8, BAR_H, wa_state.cutoff, "CUT", K_BLUE);
    bar(210, 8, BAR_H, wa_state.level > 1.0f ? 1.0f : wa_state.level, "OUT", K_TEXT);
    cv_blit(0, MAIN_Y);
}

/* a knob's ghost: the tick at its live value, when something moves it (wa_live); -1: none */
static int32_t ghost_px(int k, int32_t cx)
{
    const wa_param_t *p;
    float v, f;
    if (k == K_NONE || !wa_live(k, &v))
        return -1;
    p = wa_param_info(k);
    if (p->hi <= p->lo)
        return -1;
    f = (v - (float)p->lo) / (float)(p->hi - p->lo);
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    return cx - 22 + (int32_t)(44.0f * f);         /* (truncated, as the bar is) */
}

/* a over b, a's share n/256 (RGB565) */
static uint16_t blend565(uint16_t a, uint16_t b, uint32_t n)
{
    uint32_t r = ((a >> 11) * n + (b >> 11) * (256u - n)) >> 8, g = (((a >> 5) & 63u) * n + ((b >> 5) & 63u) * (256u - n)) >> 8,
             bl = ((a & 31u) * n + (b & 31u) * (256u - n)) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

#define GHOST_MS 30u                   /* a ghost alone moves the strip at most this often: every other frame */
static void draw_knobs(void)
{
    if (ui.stage) {
        draw_stage_grid();
        return;
    }
    /* (each in its own hash: touched is -1 untouched, and shifted in with the rest it hid the engine) */
    uint32_t h = hash(hash(hash(2166136261u, ui.view), (uint32_t)(uint8_t)ui.touched),
                      (uint32_t)proj.par[P_THEME] | (uint32_t)proj.par[P_ENGINE] << 8), i;
    int now_touch = ui.touch_until && (int32_t)(ui.touch_until - plat_ms()) > 0;
    if (!now_touch)
        ui.touch_until = 0u;
    for (i = 0; i < 4; i++) {
        int k = knob_of((int)i);
        h = hash(h, k == K_NONE ? 0xFFFFu : (uint32_t)(uint16_t)proj.par[k] | (uint32_t)k << 16);
    }
    h = hash(h, (uint32_t)now_touch);
    {   /* the ghosts: a change that moves only them draws at most every GHOST_MS */
        uint32_t hg = h, now = plat_ms();
        int16_t g[4];
        for (i = 0; i < 4; i++) {
            g[i] = (int16_t)ghost_px(knob_of((int)i), (int32_t)i * 60 + 30);
            hg = hash(hg, (uint32_t)(g[i] + 1));
        }
        if (hg == ui.sig[2])
            return;
        if (h == ui.sig[3] && now - ui.ghost_t < GHOST_MS)
            return;
        ui.sig[2] = hg;
        ui.sig[3] = h;
        ui.ghost_t = now;
        for (i = 0; i < 4; i++)
            ui.ghost[i] = g[i];                  /* (as drawn) */
    }
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int k = knob_of((int)i), lo, hi, v;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        int hot = now_touch && ui.touched == (int)i;
        char t[16];
        if (i)
            cv_rect(x, 14, 1, KNB_H - 30, K_LINE);
        if (k == K_NONE)
            continue;
        lo = wa_param_info(k)->lo;
        hi = wa_param_info(k)->hi;
        v = proj.par[k];
        text_c(cx, 6, &FONT_XS, wa_param_name(k), hot ? K_TEXT : K_DIM);
        if (k == P_DXPATCH)
            itoa_u((uint32_t)v + 1u, t);
        else
            wa_param_text(k, v, t);
        {   /* the value: big, unless it has lower case (the big face has none) or is long */
            const char *q = t;
            int lower = 0;
            for (; *q; q++)
                lower |= *q >= 'a' && *q <= 'z';
            if (lower || text_w(&FONT_M, t) > 56)
                text_c(cx, 26, &FONT_B, t, hot ? K_ACC : K_TEXT);
            else
                text_c(cx, 22, &FONT_M, t, hot ? K_ACC : K_TEXT);
        }
        bw = hi > lo ? (int32_t)(44 * (v - lo) / (hi - lo)) : 0;
        cv_round(cx - 22, 50, 44, 6, 3, K_LINE);
        if (lo < 0) {                                /* centred values: a bar from the middle */
            int32_t m = cx - 22 + 44 * (0 - lo) / (hi - lo), e = cx - 22 + bw;
            cv_round(e < m ? e : m, 50, (e < m ? m - e : e - m) + 6, 6, 3, hot ? K_ACC : K_BLUE);
        } else if (bw) {
            cv_round(cx - 22, 50, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACC : K_BLUE);
        }
        if (ui.ghost[i] >= 0) {                      /* the ghost: where modulation has the value now */
            int32_t gx = ui.ghost[i], sx = cx - 22 + bw;  /* (the knobs it shows on start at 0) */
            if (gx - sx > 1 || sx - gx > 1)
                cv_rect(gx < sx ? gx : sx, 52, gx < sx ? sx - gx : gx - sx, 2, blend565(K_ACC, K_LINE, 115u));
            cv_round(gx - 1, 46, 3, 14, 1, hot ? K_TEXT : K_ACC);
        }
    }
    cv_blit(0, KNB_Y);
}


/* ------------------------------------------------------------- stage --- */
/* Stage: the controllers (84 px) over the twelve knobs (128 px), in place of the band and the strip */
#define STG_MON_H 84
#define STG_GRID_Y (MAIN_Y + STG_MON_H)
#define STG_GRID_H (240 - STG_GRID_Y)

/* the angle of (x, y) from the centre, in turns 0..1: 0 is right, clockwise (the screen's y is down) */
static float turns_of(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a, q, r;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    a = ax > ay ? ay / ax : ax / ay;           /* atan on 0..1, within 0.0001 rad */
    q = a * a;
    r = ((-0.0464964749f * q + 0.15931422f) * q - 0.327622764f) * q * a + a;
    if (ay > ax)
        r = 1.57079633f - r;
    if (x < 0.0f)
        r = 3.14159265f - r;
    r *= 0.159154943f;
    return y < 0.0f ? 1.0f - r : r;
}

/* a ring's arc: radius r, w thick, from turn a0 clockwise for span turns; its round edges blended */
static void cv_arc(int32_t cx, int32_t cy, float r, float w, float a0, float span, uint16_t c)
{
    int32_t R = (int32_t)(r + w) + 1, x, y;
    for (y = cy - R; y <= cy + R; y++) {
        int32_t py = y + cv_oy;
        if (py < cv_y0 || py >= cv_y1)
            continue;
        for (x = cx - R; x <= cx + R; x++) {
            float dx = (float)(x - cx), dy = (float)(y - cy), cov, t;
            uint16_t *q;
            if ((uint32_t)x >= cv_w)
                continue;
            cov = w * 0.5f + 0.5f - fm_fabsf(fm_sqrtf(dx * dx + dy * dy) - r);
            if (cov <= 0.0f)
                continue;
            t = turns_of(dy, dx) - a0;
            t = t < 0.0f ? t + 1.0f : t >= 1.0f ? t - 1.0f : t;
            if (t > span)
                continue;
            q = &cv_px[(uint32_t)py * cv_w + (uint32_t)x];
            *q = swap16(blend565(c, swap16(*q), cov >= 1.0f ? 256u : (uint32_t)(cov * 256.0f)));
        }
    }
}

/* the glide time on a log scale, 2 ms .. 2 s, as 0..1 */
static float glide_pos(float ms)
{
    float p = ms > 2.0f ? fm_log2f(ms * 0.5f) * (1.0f / 9.9657843f) : 0.0f;
    return p > 1.0f ? 1.0f : p;
}

/* the controllers: the note and its cents; breath (with its peak), bite, glide and output as bars */
static void draw_stage_mon(void)
{
    uint32_t now = plat_ms(), h;
    float dt = (float)(now - ui.pk_t) * 0.001f, br = wa_state.breath, lv = wa_state.level > 1.0f ? 1.0f : wa_state.level;
    float note = wa_state.note;
    int top = wa_state.top, gate = wa_state.gate > 0.5f, n = (int)(note + 0.5f + 1200.0f) - 1200,
        c = (int)((note - (float)n) * 100.0f);
    int32_t pb, pbk, pe, pg, pgf, po, pok, i;
    ui.pk_t = now;
    dt = dt > 1.0f ? 1.0f : dt;
    ui.br_pk = br > ui.br_pk - 0.5f * dt ? br : ui.br_pk - 0.5f * dt;   /* peaks fall at half a bar a second */
    ui.out_pk = lv > ui.out_pk - 0.5f * dt ? lv : ui.out_pk - 0.5f * dt;
    pb = (int32_t)(84.0f * br + 0.5f);
    pbk = (int32_t)(84.0f * ui.br_pk + 0.5f);
    pe = (int32_t)(42.0f * (wa_state.bend < -1.0f ? -1.0f : wa_state.bend > 1.0f ? 1.0f : wa_state.bend) + (wa_state.bend < 0.0f ? -0.5f : 0.5f));
    pg = (int32_t)(84.0f * glide_pos(wa_state.glide_ms) + 0.5f);
    pgf = (int32_t)(84.0f * glide_pos((float)proj.par[P_GLIDE]) + 0.5f);
    po = (int32_t)(84.0f * lv + 0.5f);
    pok = (int32_t)(84.0f * ui.out_pk + 0.5f);
    h = hash(hash(hash(hash(2166136261u, (uint32_t)pb | (uint32_t)pbk << 8 | (uint32_t)(pe + 64) << 16 | (uint32_t)pg << 24),
                       (uint32_t)pgf | (uint32_t)po << 8 | (uint32_t)pok << 16 | (uint32_t)gate << 24 | (uint32_t)proj.par[P_THEME] << 25),
                  (uint32_t)(n + 2000) | (uint32_t)(c + 100) << 16),
             (uint32_t)(top + 1) | 0x80000000u);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, STG_MON_H, K_BG);
    cv_round(6, 2, 228, STG_MON_H - 4, 6, K_PANEL);
    if (top < 0) {
        text_c(52, 26, &FONT_B, "Blow a note", K_DIM);
    } else {
        char t[8];
        note_text(n, t);
        text_c(52, 10, &FONT_L, t, gate ? K_TEXT : K_DIM);
        if (c) {
            char d[8];
            i = 0;
            d[i++] = c > 0 ? '+' : '-';
            itoa_u((uint32_t)(c > 0 ? c : -c), d + i);
            i = s_len(d);
            d[i++] = 'c';
            d[i] = 0;
            text_c(52, 52, &FONT_S, d, K_ACC);
        }
        if (gate)
            cv_round(86, 10, 8, 8, 4, K_ACC);
    }
    {
        static const char *const NM[4] = {"BREATH", "BITE", "GLIDE", "OUT"};
        for (i = 0; i < 4; i++) {
            int32_t y = 12 + i * 17;
            cv_text(104, y - 3, &FONT_XS, NM[i], K_DIM);
            cv_round(142, y, 84, 6, 3, K_LINE);
        }
    }
    if (pb > 0)
        cv_round(142, 12, pb < 6 ? 6 : pb, 6, 3, K_ACC);
    cv_rect(142 + pbk - 1, 10, 2, 10, K_TEXT);
    cv_rect(183, 27, 2, 10, K_DIM);
    cv_round(pe < 0 ? 184 + pe : 184, 29, (pe < 0 ? -pe : pe) + 1 < 4 ? 4 : (pe < 0 ? -pe : pe) + 1, 6, 3, K_BLUE);
    if (pg > 0)
        cv_round(142, 46, pg < 6 ? 6 : pg, 6, 3, K_BLUE);
    cv_rect(142 + pgf, 44, 2, 10, K_TEXT);
    if (po > 0)
        cv_round(142, 63, po < 6 ? 6 : po, 6, 3, K_TEXT);
    cv_rect(142 + pok - 1, 61, 2, 10, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* a knob's dial: a 270-degree ring, filled to its value (from the top when it's centred), and its ghost
 * as a dot on the ring */
#define DIAL_A0 0.375f                         /* (135 degrees: the ring opens at the bottom) */
#define DIAL_SPAN 0.75f
static void dial(int32_t x, int32_t y, int k, int on, int hot, int g)
{
    const wa_param_t *p = wa_param_info(k);
    float f = p->hi > p->lo ? (float)(proj.par[k] - p->lo) / (float)(p->hi - p->lo) : 0.0f;
    uint16_t col = hot ? K_ACC : on ? K_BLUE : K_DIM;
    cv_arc(x, y, 9.0f, 3.0f, DIAL_A0, DIAL_SPAN, K_LINE);
    if (p->lo < 0) {
        if (f >= 0.5f)
            cv_arc(x, y, 9.0f, 3.0f, DIAL_A0 + DIAL_SPAN * 0.5f, DIAL_SPAN * (f - 0.5f), col);
        else
            cv_arc(x, y, 9.0f, 3.0f, DIAL_A0 + DIAL_SPAN * f, DIAL_SPAN * (0.5f - f), col);
    } else if (f > 0.0f) {
        cv_arc(x, y, 9.0f, 3.0f, DIAL_A0, DIAL_SPAN * f, col);
    }
    if (g >= 0) {                              /* g: the ghost's place, 0..255 along the ring */
        float a = (DIAL_A0 + DIAL_SPAN * (float)g * (1.0f / 255.0f)) * 6.28318531f;
        int32_t gx = x + (int32_t)(9.0f * fm_cosf(a) + (fm_cosf(a) < 0.0f ? -0.5f : 0.5f)),
                gy = y + (int32_t)(9.0f * fm_sinf(a) + (fm_sinf(a) < 0.0f ? -0.5f : 0.5f));
        cv_round(gx - 2, gy - 2, 5, 5, 2, K_ACC);
    }
}

/* a dial's ghost: its place along the ring, 0..255, or -1 */
static int dial_ghost(int k)
{
    const wa_param_t *p;
    float v, f;
    if (k == K_NONE || !wa_live(k, &v))
        return -1;
    p = wa_param_info(k);
    if (p->hi <= p->lo)
        return -1;
    f = (v - (float)p->lo) / (float)(p->hi - p->lo);
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    return (int)(f * 255.0f + 0.5f);
}

/* the twelve knobs: the lit row is the one K1-K4 turn; the columns line up with the knobs */
static void draw_stage_grid(void)
{
    int row = stage_row(), r, i, now_touch = ui.touch_until && (int32_t)(ui.touch_until - plat_ms()) > 0;
    int16_t g[STAGE_ROWS][4];
    uint32_t h = hash(hash(2166136261u, (uint32_t)row | (uint32_t)proj.par[P_THEME] << 4 | (uint32_t)proj.par[P_ENGINE] << 8 |
                                             0x40000000u),
                      (uint32_t)(uint8_t)ui.touched | (uint32_t)now_touch << 8), hg, now = plat_ms();
    if (!now_touch)
        ui.touch_until = 0u;
    for (r = 0; r < STAGE_ROWS; r++)
        for (i = 0; i < 4; i++) {
            int k = stage_knob(r, i);
            h = hash(h, k == K_NONE ? 0xFFFFu : (uint32_t)(uint16_t)proj.par[k] | (uint32_t)k << 16);
        }
    hg = h;
    for (r = 0; r < STAGE_ROWS; r++)
        for (i = 0; i < 4; i++) {
            g[r][i] = (int16_t)dial_ghost(stage_knob(r, i));
            hg = hash(hg, (uint32_t)(g[r][i] + 1));
        }
    if (hg == ui.sig[2])
        return;
    if (h == ui.sig[3] && now - ui.ghost_t < GHOST_MS)
        return;
    ui.sig[2] = hg;
    ui.sig[3] = h;
    ui.ghost_t = now;
    for (i = 0; i < 4; i++)                     /* (as drawn; the host's ghostN: the lit row's) */
        ui.ghost[i] = g[row][i];
    cv_begin(240, STG_GRID_H, K_BG);
    for (r = 0; r < STAGE_ROWS; r++) {
        int32_t ry = 2 + r * 42;
        int on = r == row;
        if (on)
            cv_round(4, ry, 232, 40, 6, K_PANEL);
        for (i = 0; i < 4; i++) {
            int k = stage_knob(r, i), hot = on && now_touch && ui.touched == i;
            int32_t x = i * 60;
            char t[16];
            if (on && i)
                cv_rect(x, ry + 8, 1, 24, K_LINE);
            if (k == K_NONE)
                continue;
            dial(x + 16, ry + 22, k, on, hot, g[r][i]);
            cv_text(x + 30, ry + 5, &FONT_XS, r == 1 ? wa_param_name(k) : STAGE_NAME[r / 2][i], on ? (hot ? K_TEXT : K_DIM) : blend565(K_DIM, K_BG, 170u));
            if (k == P_DXPATCH)
                itoa_u((uint32_t)proj.par[k] + 1u, t);
            else
                wa_param_text(k, proj.par[k], t);
            cv_text(x + 30, ry + 16, &FONT_B, t, hot ? K_ACC : on ? K_TEXT : K_DIM);
        }
    }
    cv_blit(0, STG_GRID_Y);
}

/* the page's button lit; the keys held, and the key of the note sounding when it is on the panel */
static void leds(void)
{
    uint32_t b = 1u << (ui.stage ? B_PLAY : VIEW_BTN[ui.view]), k = ui.keys, d = 0u;
    int n = wa_state.top - KEY_F3 - 12 * proj.par[P_OCTAVE];
    if (wa_state.gate > 0.5f && n >= 0 && n < NKEYS)
        k |= 1u << n;
    if (ui.stage) {                                  /* Stage: the keys, dim, a breath meter from the left */
        int m = (int)(wa_state.breath * (float)NKEYS + 0.5f);
        m = m < 0 ? 0 : m > NKEYS ? NKEYS : m;
        d = m >= 32 ? 0xFFFFFFFFu : (1u << m) - 1u;
    }
    plat_leds(b, k, d);
}

/* AUTOSAVE: a few seconds after a change, when nothing sounds and nothing is touched: a flash erase
 * silences the audio for a moment, so it waits for the output itself to be quiet */
#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    if (!ui.dirty || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || wa_state.gate > 0.5f ||
        wa_state.level > 0.0005f)
        return;
    if (project_save() == 0)
        ui.dirty = 0;
    else
        ui.act_t = now;                 /* failed: the next try after another quiet spell, not every frame */
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    memset(ui.ghost, 0xFF, sizeof ui.ghost);
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }

#ifdef OM_HOST
/* the screen drawn from scratch, for the host's check that nothing on it is stale */
void ui_repaint(void)
{
    memset(ui.sig, 0, sizeof ui.sig);
    draw_header();
    draw_main();
    draw_knobs();
}
#endif

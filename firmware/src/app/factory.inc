/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory presets: the first slots, until something is saved over them. Each is the init sound with
 * the settings it changes; all keep the init sound's playing (breath to cutoff +75, attack, release,
 * bend, glide), so the tone changes and the feel doesn't. Part of project.c. */

typedef struct {
    uint8_t k;
    int16_t v;
} fac_set_t;
typedef struct {
    const char *name;                    /* as the screen shows it (at most 15) */
    const fac_set_t *set;                /* ...ended by P_NPARAMS */
} factory_t;

#define END {P_NPARAMS, 0}
#define BREATH_TO(d, a) {P_M1SRC, WM_SRC_BREATH}, {P_M1DST, d}, {P_M1AMT, a}

/* Classic's Morph is its waveform: 0 saw and sub-octave, 25 saw, 50 square, 65 a narrow pulse */
static const fac_set_t F_SQUARE[] = {{P_MORPH, 50}, END};
static const fac_set_t F_PULSE[] = {{P_MORPH, 65}, END};
static const fac_set_t F_SUB[] = {{P_MORPH, 0}, END};
/* VA's saw oscillator at its triangle end (Square off, Saw 0): a triangle, very nearly symmetric */
static const fac_set_t F_TRI[] = {{P_ENGINE, WV_VA}, {P_RATIO, 50}, {P_INDEX, 0}, {P_MORPH, 0},
                                  {P_CUTOFF, 25}, END};
/* T-Square, the Lyricon years (Takeshi Itoh): the Lyricon into an Oberheim SEM on sawtooth, an enhancer,
 * two delays and reverb. VA's AUX at Square 0 is its two saws, here about 10 cents apart; 12 dB like
 * the SEM; a touch of overdrive for the enhancer; Ping-pong for the two delays. */
static const fac_set_t F_TRUTH[] = {{P_ENGINE, WV_VA}, {P_RATIO, 52}, {P_INDEX, 0}, {P_MORPH, 33}, {P_AUX, 100},
                                    {P_SLOPE, 0}, {P_RESO, 15},
                                    {P_DSMIX, 8}, {P_DSDRIVE, 15},
                                    {P_DLMODE, WF_DL_PINGPONG}, {P_DLMIX, 18}, {P_DLTIME, 330}, {P_DLFB, 30},
                                    {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 12}, {P_RVDECAY, 45}, END};
/* The late-80s EWI sound (Akai's EWI4000s manual): two detuned saws from a darker start, a 28 ms
 * doubling delay and a touch of chorus */
static const fac_set_t F_DOUBLER[] = {{P_ENGINE, WV_VA}, {P_RATIO, 52}, {P_INDEX, 0}, {P_MORPH, 33}, {P_AUX, 100},
                                      {P_CUTOFF, 8},
                                      {P_DLMIX, 30}, {P_DLTIME, 28}, {P_DLFB, 0},
                                      {P_MOMIX, 20}, END};
/* Michael Brecker's EWI into an Oberheim Xpander ("Original Rays"): between saw and square, breath on the
 * filter, a little drive, tape delay and hall */
static const fac_set_t F_RAYS[] = {{P_MORPH, 40}, {P_RESO, 20},
                                   {P_DSMIX, 12}, {P_DSDRIVE, 20},
                                   {P_DLMODE, WF_DL_TAPE}, {P_DLMIX, 15}, {P_DLTIME, 375}, {P_DLFB, 35},
                                   {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 12}, {P_RVDECAY, 45}, END};
/* Brecker's "In a Sentimental Mood" (Steps Ahead): a square, breath on the filter and level, more drive
 * (richer the harder it's pushed), a longer delay and a ballad's hall */
static const fac_set_t F_MOOD[] = {{P_MORPH, 50}, {P_RESO, 10},
                                   {P_DSMIX, 12}, {P_DSDRIVE, 25},
                                   {P_DLMODE, WF_DL_ANALOG}, {P_DLMIX, 12}, {P_DLTIME, 430}, {P_DLFB, 25},
                                   {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 18}, {P_RVDECAY, 60}, END};
/* Brecker's Xpander stacked intervals under the EWI; with one voice, the octave below */
static const fac_set_t F_OCTAVE[] = {{P_MORPH, 0}, {P_RESO, 10},
                                     {P_DSMIX, 10}, {P_DSDRIVE, 20},
                                     {P_DLMODE, WF_DL_TAPE}, {P_DLMIX, 12}, {P_DLTIME, 375}, {P_DLFB, 35},
                                     {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 12}, {P_RVDECAY, 45}, END};
/* the mellow 70s Lyricon: VA's two oscillators between triangle and saw, 12 dB, some resonance */
static const fac_set_t F_LYRICON[] = {{P_ENGINE, WV_VA}, {P_RATIO, 52}, {P_INDEX, 0}, {P_MORPH, 15}, {P_AUX, 100},
                                      {P_SLOPE, 0}, {P_RESO, 25}, {P_CUTOFF, 12},
                                      {P_MOTYPE, WF_MO_ENSEMBLE}, {P_MOMIX, 25},
                                      {P_RVTYPE, WF_RV_PLATE}, {P_RVMIX, 15}, END};
/* FM at 2:1, odd harmonics like a clarinet; blowing harder raises the index */
static const fac_set_t F_REED[] = {{P_ENGINE, WV_FM}, {P_RATIO, 48}, {P_INDEX, 25}, {P_MORPH, 50},
                                   {P_CUTOFF, 30}, BREATH_TO(WM_DST_TIMBRE, 30),
                                   {P_RVTYPE, WF_RV_ROOM}, {P_RVMIX, 12}, END};
/* the DX7's breath-controller brass; and its CS-80, after Yamaha's (Vangelis' lead) */
static const fac_set_t F_DXBRASS[] = {{P_ENGINE, WV_SIXOP}, {P_DXPATCH, 94}, {P_CUTOFF, 30},
                                      {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 12}, {P_RVDECAY, 45}, END};
static const fac_set_t F_DXCS80[] = {{P_ENGINE, WV_SIXOP}, {P_DXPATCH, 16}, {P_INDEX, 55}, {P_CUTOFF, 40},
                                    {P_DLMIX, 12}, {P_DLTIME, 250},
                                    {P_RVTYPE, WF_RV_PLATE}, {P_RVMIX, 12}, END};
/* a sung "aah": the vowel opens as you blow */
static const fac_set_t F_VOX[] = {{P_ENGINE, WV_GRAIN}, {P_RATIO, 40}, {P_INDEX, 45}, {P_MORPH, 60},
                                  BREATH_TO(WM_DST_TIMBRE, 20),
                                  {P_MOTYPE, WF_MO_ENSEMBLE}, {P_MOMIX, 20},
                                  {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 20}, END};
/* Casio CZ: a resonant sweep from phase distortion, breath driving it */
static const fac_set_t F_CZ[] = {{P_ENGINE, WV_PD}, {P_RATIO, 24}, {P_INDEX, 30}, {P_MORPH, 50},
                                 {P_CUTOFF, 35}, BREATH_TO(WM_DST_TIMBRE, 35),
                                 {P_DLMIX, 15}, {P_DLTIME, 300}, END};
/* a breathy pipe: a few harmonics, and the noise layer for the air, more of it the harder you blow */
static const fac_set_t F_PIPE[] = {{P_ENGINE, WV_HARM}, {P_RATIO, 0}, {P_INDEX, 2}, {P_MORPH, 60},
                                   {P_NZLEVEL, 20}, {P_NZRESO, 20}, BREATH_TO(WM_DST_NOISE, 25),
                                   {P_RVTYPE, WF_RV_HALL}, {P_RVMIX, 20}, END};
/* Waveshape's wavefolder, west-coast style: blowing harder folds the wave into brighter harmonics */
static const fac_set_t F_FOLD[] = {{P_ENGINE, WV_SHAPE}, {P_RATIO, 50}, {P_INDEX, 15}, {P_MORPH, 0},
                                   {P_CUTOFF, 30}, BREATH_TO(WM_DST_TIMBRE, 45),
                                   {P_DLMIX, 12}, {P_DLTIME, 300}, END};
static const fac_set_t F_INIT[] = {END};

static const factory_t FACTORY[] = {
    {"Simple", F_INIT},
    {"Simple Square", F_SQUARE},
    {"Simple Pulse", F_PULSE},
    {"Simple Sub", F_SUB},
    {"Simple Triangle", F_TRI},
    {"Truth", F_TRUTH},
    {"EWI Doubler", F_DOUBLER},
    {"Brecker Rays", F_RAYS},
    {"Sentimental", F_MOOD},
    {"Brecker Octave", F_OCTAVE},
    {"Lyricon", F_LYRICON},
    {"FM Reed", F_REED},
    {"DX Brass", F_DXBRASS},
    {"DX CS-80", F_DXCS80},
    {"Formant Vox", F_VOX},
    {"CZ Lead", F_CZ},
    {"Air Pipe", F_PIPE},
    {"Fold Lead", F_FOLD},
};
#define NFACTORY ((int)(sizeof FACTORY / sizeof FACTORY[0]))
typedef char factory_fits[NFACTORY <= NPRESETS ? 1 : -1];

#undef END
#undef BREATH_TO

/* a slot's factory sound: the init sound, and what its preset changes (past the factory ones: init) */
static void factory_sound(int slot, int16_t *par)
{
    const fac_set_t *s;
    int k;
    for (k = 0; k < P_NPARAMS; k++)
        par[k] = wa_param_info(k)->def;
    if (slot < 0 || slot >= NFACTORY)
        return;
    for (s = FACTORY[slot].set; s->k < P_NPARAMS; s++)
        par[s->k] = s->v;
}

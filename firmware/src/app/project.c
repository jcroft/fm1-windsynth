/* SPDX-License-Identifier: GPL-3.0-only */
/* Windsynth project: one flash object (OBJ_PROJ). A project of another format, or none (or FoMni's),
 * gives the defaults. Part of the unity build, after app.h. */
project_t proj;

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = wa_param_info(i)->def;
}

/* A project of this format loads. Each value out of its range (a setting this version added, that an older
 * project has no room for, or one whose range has changed) takes its default; the rest are kept. */
static int project_valid(int bytes)
{
    int i, have = (bytes - 8) / 2, at;           /* the values the saved project holds */
    if (proj.magic != PROJ_MAGIC || proj.format < 1u || proj.format > PROJ_FORMAT)
        return 0;
    if (have > PROJ_NPAR_OF(proj.format))        /* (its spare bytes, zeros, are not settings) */
        have = PROJ_NPAR_OF(proj.format);
    at = 8 + 2 * PROJ_NPAR_OF(proj.format);      /* the preset follows its format's values */
    proj.preset = at < bytes ? ((const uint8_t *)&proj)[at] : 0u;
    if (proj.format == 4u && have > P_GDEPTH)    /* 0.0.8 kept CC depth as a Glide value: now it's ms */
        proj.par[P_GDEPTH] = (int16_t)((proj.par[P_GDEPTH] * proj.par[P_GDEPTH] * 2000 + 5000) / 10000);
    if (proj.format <= 6u) {                     /* Glide was where the glide CC last left it: now it's */
        proj.par[P_GLIDE] = wa_param_info(P_GLIDE)->def;   /* the least time, and the CC adds to it */
        if (have > P_ENGINE && proj.par[P_ENGINE] == 6)    /* (and 6 was Swarm, now gone: Classic) */
            proj.par[P_ENGINE] = WV_CLASSIC;
    }
    proj.format = PROJ_FORMAT;
    if (proj.preset >= NPRESETS)
        proj.preset = 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const wa_param_t *p = wa_param_info(i);
        if (i >= have || proj.par[i] < p->lo || proj.par[i] > p->hi)
            proj.par[i] = p->def;
    }
    return 1;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n >= 8 && project_valid(n)) {
        preset_peek();
        return 0;
    }
    project_defaults();
    preset_peek();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        wa_set(i, proj.par[i]);
}

/* ------------------------------------------------------------ presets --- */
#define BANK_MAGIC 0x4B4E4257u           /* "WBNK" */
#define BANK_N 16                        /* presets per bank object */
typedef struct {
    uint32_t magic;
    uint16_t format, npar;               /* npar: the values each preset holds (P_NPARAMS when saved) */
    struct {
        char name[12];                   /* (unused yet: zeros) */
        int16_t par[PRESET_NPAR];
    } p[BANK_N];
} bank_t;
typedef char bank_fits[sizeof(bank_t) <= PLAT_STORE_MAX ? 1 : -1];

static bank_t bank;
static int bank_at = -1;                 /* the bank in `bank`, -1 none */
static int16_t snd[P_NPARAMS];           /* the project's preset as stored: what "edited" compares with */

/* one bank into `bank`: from flash, or (never saved, or not ours) every preset the init sound */
static void bank_read(int b)
{
    int n, i, k;
    if (bank_at == b)
        return;
    memset(&bank, 0, sizeof bank);
    n = plat_store_load(OBJ_BANK0 + (uint32_t)b, &bank, sizeof bank);
    if (n < 8 || bank.magic != BANK_MAGIC || bank.npar > PRESET_NPAR) {
        memset(&bank, 0, sizeof bank);
        bank.npar = 0;
    }
    if (bank.format < 2u)                /* (to 0.0.11: Glide as the glide CC left it, 6 Swarm; see project_valid) */
        for (i = 0; i < BANK_N; i++) {
            bank.p[i].par[P_GLIDE] = wa_param_info(P_GLIDE)->def;
            if (bank.p[i].par[P_ENGINE] == 6)
                bank.p[i].par[P_ENGINE] = WV_CLASSIC;
        }
    for (i = 0; i < BANK_N; i++)          /* what a preset lacks (newer settings), or holds out of range: defaults */
        for (k = 0; k < P_NPARAMS; k++) {
            const wa_param_t *p = wa_param_info(k);
            if (k >= bank.npar || bank.p[i].par[k] < p->lo || bank.p[i].par[k] > p->hi)
                bank.p[i].par[k] = p->def;
        }
    bank.magic = BANK_MAGIC;
    bank.format = 2;
    bank.npar = P_NPARAMS;
    bank_at = b;
}

static const int16_t *preset_par(int slot)
{
    bank_read(slot / BANK_N);
    return bank.p[slot % BANK_N].par;
}

/* the project's preset, as stored, for preset_edited() (boot: the project names it) */
void preset_peek(void)
{
    memcpy(snd, preset_par(proj.preset), sizeof snd);
}

void preset_load(int slot)
{
    const int16_t *par;
    int k;
    slot = slot < 0 ? 0 : slot >= NPRESETS ? NPRESETS - 1 : slot;
    par = preset_par(slot);
    proj.preset = (uint8_t)slot;
    for (k = 0; k < P_NPARAMS; k++)
        if (!wa_param_global(k) && proj.par[k] != par[k]) {
            proj.par[k] = par[k];
            wa_set(k, par[k]);
        }
    memcpy(snd, par, sizeof snd);
}

int preset_store(int slot)
{
    int k;
    if (slot < 0 || slot >= NPRESETS)
        return -1;
    bank_read(slot / BANK_N);
    for (k = 0; k < P_NPARAMS; k++)
        bank.p[slot % BANK_N].par[k] = wa_param_global(k) ? wa_param_info(k)->def : proj.par[k];
    if (plat_store_save(OBJ_BANK0 + (uint32_t)(slot / BANK_N), &bank, sizeof bank) != 0) {
        bank_at = -1;                    /* (re-read: what flash holds) */
        return -1;
    }
    proj.preset = (uint8_t)slot;
    memcpy(snd, bank.p[slot % BANK_N].par, sizeof snd);
    return 0;
}

int preset_edited(void)
{
    int k;
    for (k = 0; k < P_NPARAMS; k++)
        if (!wa_param_global(k) && proj.par[k] != snd[k])
            return 1;
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN, the brain: a rave that writes itself. Customized for Dark Outrun / French Electro ("Power Glove - Clutch"). */
#include "siren_brain.h"

/* ------------------------------------------------------------------------------------------------- the dice --- */
typedef struct { uint32_t s; } sb_rng_t;
static sb_rng_t sb_rng(uint32_t seed) { sb_rng_t r; r.s = seed ? seed : 0x9E3779B9u; return r; }
static uint32_t sb_rnext(sb_rng_t *r) { uint32_t s = r->s; s ^= s << 13; s ^= s >> 17; s ^= s << 5; r->s = s; return s; }
static uint32_t sb_ri(sb_rng_t *r, uint32_t n) { return sb_rnext(r) % n; }
static int sb_rchance(sb_rng_t *r, int pm) { return (int)(sb_rnext(r) % 1000u) < pm; }
#define SB_PICK(r, a) ((a)[sb_rnext(r) % (sizeof(a) / sizeof((a)[0]))])
static uint32_t sb_hash(uint32_t a, uint32_t b)
{
    uint32_t h = a ^ ((b + 0x7F4A7C15u) * 0x85EBCA6Bu);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static int32_t sb_fdiv(int32_t a, int32_t b) { int32_t q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
static int32_t sb_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static int32_t sb_abs(int32_t x) { return x < 0 ? -x : x; }
static const char SB_B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
void sb_code(uint32_t seed, char out[9])
{
    char o[7]; uint32_t s = seed; int i;
    for (i = 6; i >= 0; i--) { o[i] = SB_B32[s & 31u]; s >>= 5; }
    out[0] = o[0]; out[1] = o[1]; out[2] = o[2]; out[3] = '-'; out[4] = o[3]; out[5] = o[4]; out[6] = o[5]; out[7] = o[6]; out[8] = 0;
}
uint32_t sb_uncode(const char *c)
{
    uint32_t s = 0;
    for (; *c; c++) {
        char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c; int v;
        for (v = 0; v < 32 && SB_B32[v] != ch; v++) {}
        if (v < 32) s = s * 32u + (uint32_t)v;
    }
    return s;
}

/* ------------------------------------------------------------------------------------------------ the music --- */
enum { SB_MINOR, SB_DORIAN, SB_PHRYGIAN, SB_HARMONIC, SB_MAJOR };
static const uint8_t SB_SCALES[5][7] = { {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 4, 5, 7, 9, 11} };
enum { GR_FOUR, GR_BREAKS, GR_DNB, GR_HALF };
enum { SBBK_OFFBEAT, SBBK_ROLLING, SBBK_RUMBLE, SBBK_PULSE, SBBK_REESE, SBBK_ROLLER, SBBK_WOBBLE, SBBK_RIDDIM, SBBK_GROWL, SBBK_ACID };
enum { SBHM_16, SBHM_OFF8, SBHM_8, SBHM_16GAP };
/* the drum lanes: GM notes */
enum { SBD_KICK = 36, SBD_KICK2 = 35, SBD_SNARE = 38, SBD_CLAP = 39, SBD_HAT = 42, SBD_OPEN = 46, SBD_RIM = 37, SBD_SNARE2 = 40, SBD_TOML = 43,
       SBD_TOMH = 48, SBD_CRASH = 49, SBD_RIDE = 51, SBD_SHAKE = 70, SBD_CONGA = 63, SBD_BELL = 56, SBD_CLAVE = 75 };

/* Map OUTRUN into index 5 */
#ifndef SB_OUTRUN
#define SB_OUTRUN 5
#endif
#ifndef SB_HARD
#define SB_HARD 5
#endif

const char *const SB_STYLE_NAMES[SB_NSTYLES] = { "TECHNO", "TRANCE", "PSY", "ACID", "RAVE", "OUTRUN", "DNB", "HOUSE" };
const char *const SB_SEC_NAMES[7] = { "INTRO", "GROOVE", "BUILD", "DROP", "BREAK", "PEAK", "OUTRO" };
static const int16_t SB_SEC_E[7] = { 250, 550, 750, 1000, 300, 920, 320 };
static const uint8_t SB_STYLE_ORDER[SB_NSTYLES] = { SB_HOUSE, SB_TECHNO, SB_ACID, SB_TRANCE, SB_PSY, SB_OUTRUN, SB_RAVE, SB_DNB };

typedef struct { uint8_t n, c[4]; } sb_prog_t;
typedef struct {
    uint8_t break_drums, groove; uint16_t rumble; uint8_t kit_tune; uint16_t sub; uint8_t drop_bass; uint16_t gallop;
    uint8_t bpm_lo, bpm_hi; uint8_t nscales, scales[3];
    uint8_t nprogs; sb_prog_t progs[5]; uint8_t chord_bars;
    uint8_t nbass, bass[4]; uint8_t swing; uint16_t layers[6]; uint8_t hats_lo, hats_hi; uint16_t ride; uint8_t roll;
    uint8_t nperc, perc[4]; uint8_t nhm, hm[3];
} sb_style_t;

static const sb_style_t SB_STY[SB_NSTYLES] = {
    /* TECHNO */ { 1, GR_FOUR, 550, 1, 0, 0, 0, 126, 132, 3, {SB_MINOR, SB_PHRYGIAN, SB_DORIAN}, 4, {{1, {0}}, {4, {0, 0, 0, 5}}, {2, {0, 3}}, {4, {0, 0, 6, 0}}}, 4,
                 3, {SBBK_RUMBLE, SBBK_OFFBEAT, SBBK_PULSE}, 40, {350, 450, 450, 550, 1000, 0}, 5, 11, 600, SBD_SNARE, 4, {SBD_RIM, SBD_CONGA, SBD_TOML, SBD_RIM}, 3, {SBHM_16, SBHM_OFF8, SBHM_16GAP} },
    /* TRANCE */ { 0, GR_FOUR, 0, 1, 0, 0, 0, 136, 140, 2, {SB_MINOR, SB_HARMONIC}, 5, {{4, {0, 5, 2, 6}}, {4, {0, 6, 5, 4}}, {4, {0, 3, 5, 4}}, {4, {5, 3, 0, 4}}, {4, {0, 5, 3, 4}}}, 2,
                 3, {SBBK_OFFBEAT, SBBK_OFFBEAT, SBBK_ROLLING}, 0, {950, 850, 950, 200, 1000, 0}, 4, 8, 800, SBD_SNARE, 2, {SBD_SHAKE, SBD_RIM}, 2, {SBHM_16, SBHM_16GAP} },
    /* PSY */    { 1, GR_FOUR, 0, 1, 0, 0, 0, 142, 146, 2, {SB_PHRYGIAN, SB_HARMONIC}, 3, {{1, {0}}, {2, {0, 1}}, {4, {0, 0, 0, 1}}}, 8,
                 1, {SBBK_ROLLING}, 0, {600, 700, 350, 100, 1000, 0}, 6, 10, 300, SBD_SNARE, 3, {SBD_SHAKE, SBD_CONGA, SBD_RIM}, 2, {SBHM_16, SBHM_16} },
    /* ACID */   { 1, GR_FOUR, 150, 0, 0, 0, 0, 128, 134, 2, {SB_MINOR, SB_PHRYGIAN}, 2, {{1, {0}}, {4, {0, 0, 5, 0}}}, 8,
                 1, {SBBK_ACID}, 80, {200, 500, 300, 300, 1000, 0}, 6, 12, 500, SBD_CLAP, 3, {SBD_BELL, SBD_RIM, SBD_CLAVE}, 2, {SBHM_16, SBHM_16GAP} },
    /* RAVE */   { 0, GR_BREAKS, 0, 0, 0, 0, 0, 140, 150, 2, {SB_MINOR, SB_HARMONIC}, 3, {{4, {0, 5, 6, 4}}, {4, {0, 3, 6, 5}}, {4, {0, 6, 5, 6}}}, 2,
                 2, {SBBK_REESE, SBBK_OFFBEAT}, 0, {400, 600, 600, 950, 1000, 600}, 6, 12, 400, SBD_SNARE, 2, {SBD_RIM, SBD_SHAKE}, 2, {SBHM_8, SBHM_16GAP} },
    /* OUTRUN (Zenith/Clutch profile: 102-112 BPM, Minor/Phrygian, Sidechained 16th Octaves) */
                 { 0, GR_FOUR, 250, 1, 800, 0, 0, 102, 112, 2, {SB_MINOR, SB_PHRYGIAN}, 5,
                 {{4, {0, 5, 3, 6}}, {4, {0, 5, 6, 4}}, {4, {0, 6, 3, 4}}, {4, {0, 1, 5, 4}}, {2, {0, 5}}}, 4,
                 1, {SBBK_ROLLING}, 0, {800, 1000, 950, 750, 1000, 0}, 6, 10, 0, SBD_TOML, 3, {SBD_RIM, SBD_SNARE2, SBD_SNARE2}, 2, {SBHM_8, SBHM_16} },
    /* DNB */    { 0, GR_DNB, 0, 1, 850, 0, 0, 170, 176, 2, {SB_MINOR, SB_DORIAN}, 4, {{4, {0, 5, 3, 4}}, {2, {0, 3}}, {4, {0, 6, 5, 4}}, {4, {0, 0, 5, 6}}}, 2,
                 3, {SBBK_ROLLER, SBBK_REESE, SBBK_ROLLER}, 0, {500, 500, 900, 450, 1000, 0}, 6, 10, 700, SBD_SNARE, 2, {SBD_RIM, SBD_SHAKE}, 2, {SBHM_8, SBHM_16GAP} },
    /* HOUSE */  { 0, GR_FOUR, 0, 1, 0, 0, 0, 122, 126, 2, {SB_MINOR, SB_DORIAN}, 4, {{2, {0, 3}}, {4, {0, 5, 2, 6}}, {4, {0, 3, 6, 4}}, {4, {0, 0, 3, 3}}}, 2,
                 3, {SBBK_OFFBEAT, SBBK_PULSE, SBBK_OFFBEAT}, 70, {350, 800, 800, 950, 900, 0}, 4, 9, 500, SBD_CLAP, 3, {SBD_SHAKE, SBD_CONGA, SBD_CLAVE}, 3, {SBHM_OFF8, SBHM_16, SBHM_16GAP} },
};

enum { IK_DRUMS, IK_ATMOS, IK_HOOK, IK_FILTER, IK_BASS, IK_DIRECT, IK_BREAK, IK_N };
static const uint16_t SB_INTRO_W[SB_NSTYLES][IK_N] = {
    {500, 100, 50, 100, 200, 50, 0},      /* TECHNO */
    {200, 250, 150, 100, 0, 50, 250},     /* TRANCE */
    {330, 200, 50, 100, 250, 70, 0},      /* PSY */
    {330, 50, 50, 150, 370, 50, 0},       /* ACID */
    {330, 100, 200, 100, 50, 150, 70},    /* RAVE */
    {0,   750, 250, 0,   0,   0, 0},      /* OUTRUN (Strictly Atmosphere & Hook intro, zero drums/bass) */
    {330, 150, 100, 100, 150, 70, 100},   /* DNB */
    {500, 50, 200, 150, 100, 0, 0},       /* HOUSE */
};

typedef struct { uint8_t n, v[4]; } sb_opt_t;
typedef struct {
    sb_opt_t intro, intro2, groove1, midg, brks, brkl, build, drop, tail, outro;
    uint16_t p_intro2, p_groove1, p_mid, p_break1, p_build, p_fake, p_pause, p_peak, p_peaklast, p_tail;
    uint8_t cyc_lo, cyc_hi; uint16_t max_bars;
} sb_form_t;

static const sb_form_t SB_FORM[SB_NSTYLES] = {
    /* TECHNO */ { {2, {16, 32}}, {2, {16, 32}}, {3, {16, 32, 32}}, {2, {16, 32}}, {1, {8}}, {2, {8, 16}}, {1, {8}}, {3, {16, 32, 32}}, {2, {16, 32}}, {2, {16, 32}},
                   300, 900, 700, 400, 350, 0, 100, 500, 500, 600, 2, 3, 200 },
    /* TRANCE */ { {2, {16, 32}}, {1, {16}}, {1, {16}}, {2, {8, 16}}, {2, {8, 16}}, {3, {16, 32, 32}}, {3, {8, 16, 16}}, {2, {16, 32}}, {2, {8, 16}}, {2, {16, 32}},
                   600, 700, 200, 500, 1000, 150, 250, 100, 800, 300, 2, 3, 208 },
    /* PSY */    { {1, {16}}, {2, {16, 32}}, {2, {32, 48}}, {2, {16, 32}}, {1, {8}}, {2, {8, 16}}, {1, {8}}, {2, {32, 48}}, {2, {16, 32}}, {2, {8, 16}},
                   300, 900, 600, 200, 700, 100, 200, 200, 400, 400, 2, 4, 216 },
    /* ACID */   { {2, {8, 16}}, {2, {16, 32}}, {2, {32, 48}}, {2, {16, 32}}, {1, {8}}, {2, {8, 16}}, {1, {8}}, {2, {32, 48}}, {2, {16, 32}}, {2, {8, 16}},
                   400, 1000, 700, 500, 600, 50, 100, 200, 300, 500, 1, 3, 200 },
    /* RAVE */   { {2, {8, 16}}, {2, {8, 16}}, {1, {16}}, {2, {8, 16}}, {1, {8}}, {2, {8, 16}}, {2, {8, 16}}, {2, {16, 32}}, {2, {8, 16}}, {2, {8, 16}},
                   300, 600, 400, 500, 900, 200, 300, 200, 600, 300, 2, 3, 200 },
    /* OUTRUN (Expanded arrangement variety) */ { {3, {8, 16, 32}}, {2, {8, 16}}, {3, {16, 32, 48}}, {3, {16, 32, 32}}, {1, {8}}, {3, {8, 16, 24}}, {2, {4, 8}}, {3, {16, 32, 48}}, {2, {8, 16}}, {3, {16, 32, 48}},
                   400, 900, 600, 600, 900, 150, 250, 600, 850, 500, 2, 4, 224 },
    /* DNB */    { {3, {16, 32, 32}}, {1, {16}}, {1, {8}}, {1, {8}}, {1, {8}}, {3, {16, 24, 32}}, {2, {8, 16}}, {3, {32, 32, 48}}, {1, {8}}, {2, {16, 32}},
                   300, 200, 100, 300, 1000, 150, 300, 100, 400, 200, 2, 3, 256 },
    /* HOUSE */  { {3, {16, 32, 32}}, {2, {16, 32}}, {3, {32, 48, 64}}, {2, {16, 32}}, {1, {8}}, {2, {8, 16}}, {1, {8}}, {3, {32, 32, 48}}, {2, {16, 32}}, {2, {16, 32}},
                   600, 900, 700, 700, 700, 100, 200, 200, 300, 600, 1, 3, 192 },
};

static int sb_opt(sb_rng_t *r, const sb_opt_t *o) { return o->v[sb_ri(r, o->n)]; }
static void sb_sec(sb_brain_t *b, int sec, int len)
{
    if (b->plan_n && b->plan_sec[b->plan_n - 1] == sec) { b->plan_len[b->plan_n - 1] = (uint16_t)(b->plan_len[b->plan_n - 1] + len); return; }
    if (b->plan_n >= SB_PLAN_MAX) return;
    b->plan_sec[b->plan_n] = (uint8_t)sec; b->plan_len[b->plan_n] = (uint16_t)len; b->plan_n++;
}

static void sb_form(sb_brain_t *b, sb_rng_t *r)
{
    const sb_form_t *f = &SB_FORM[b->style];
    int c, ncyc, sec, len, ik = 0, w = (int)sb_ri(r, 1000);
    static const uint8_t DIRECT[2] = {4, 8};
    while (ik < IK_N - 1 && w >= SB_INTRO_W[b->style][ik]) w -= SB_INTRO_W[b->style][ik++];
    b->intro_kind = (uint8_t)ik;
    b->plan_n = 0;
    if (ik == IK_DIRECT) sb_sec(b, SEC_GROOVE, SB_PICK(r, DIRECT));
    else if (ik == IK_BREAK) sb_sec(b, SEC_BREAK, sb_opt(r, &f->brkl));
    else {
        sb_sec(b, SEC_INTRO, sb_opt(r, &f->intro));
        if (sb_rchance(r, f->p_intro2)) sb_sec(b, SEC_GROOVE, sb_opt(r, &f->intro2));
    }
    ncyc = f->cyc_lo + (int)sb_ri(r, (uint32_t)(f->cyc_hi - f->cyc_lo + 1));
    for (c = 0; c < ncyc; c++) {
        int last = c == ncyc - 1;
        if (!c && ik == IK_BREAK) sb_sec(b, SEC_BUILD, sb_opt(r, &f->build));
        else if (!c && ik == IK_DIRECT) { }
        else {
            if (sb_rchance(r, c ? f->p_mid : f->p_groove1)) sb_sec(b, SEC_GROOVE, sb_opt(r, c ? &f->midg : &f->groove1));
            if (c || sb_rchance(r, f->p_break1)) sb_sec(b, SEC_BREAK, sb_opt(r, c ? &f->brkl : &f->brks));
            if (sb_rchance(r, f->p_build)) {
                sb_sec(b, SEC_BUILD, sb_opt(r, &f->build));
                if (sb_rchance(r, f->p_fake)) { len = 4 * (1 + (int)sb_ri(r, 2)); sb_sec(b, SEC_BREAK, len); sb_sec(b, SEC_BUILD, sb_opt(r, &f->build)); }
                if (sb_rchance(r, f->p_pause)) sb_sec(b, SEC_BREAK, 1);
            }
        }
        sec = sb_rchance(r, last ? f->p_peaklast : f->p_peak) ? SEC_PEAK : SEC_DROP;
        sb_sec(b, sec, sb_opt(r, &f->drop));
        if (!last && sb_rchance(r, f->p_tail)) sb_sec(b, SEC_GROOVE, sb_opt(r, &f->tail));
    }
    if (sb_rchance(r, f->p_tail)) sb_sec(b, SEC_GROOVE, sb_opt(r, &f->tail));
    sb_sec(b, SEC_OUTRO, sb_opt(r, &f->outro));
}

static int sb_euclid(int k, int n, int rot, int i) { return (((i + n - (rot % n)) * k) % n) < k; }
static int sb_has(const uint8_t *a, int n, int x) { int i; for (i = 0; i < n; i++) if (a[i] == x) return 1; return 0; }

/* ------------------------------------------------------------------------------------------------ the brain --- */
static void sb_motifs(sb_brain_t *b);
static void sb_pick_sounds(sb_brain_t *b);
static const sb_style_t *SB_ST(const sb_brain_t *b) { return &SB_STY[b->style]; }

static void sb_new_track(sb_brain_t *b, uint32_t seed, int style, int has_prev, int prev_key, int prev_bpm)
{
    const sb_style_t *st = &SB_STY[style];
    static const uint8_t KEYSTEP[6] = {0, 0, 7, 5, 3, 9};
    sb_rng_t r = sb_rng(sb_hash(seed, 1));
    int i;
    b->seed = seed; b->style = (uint8_t)style;
    b->scale = st->scales[sb_rnext(&r) % st->nscales];
    /* Bias OUTRUN toward dark D minor (key = 2) or A minor (key = 9) */
    if (style == SB_OUTRUN && !has_prev) {
        static const uint8_t DARK_KEYS[4] = {2, 2, 9, 0};
        b->key = SB_PICK(&r, DARK_KEYS);
    } else {
        b->key = has_prev ? (uint8_t)((prev_key + SB_PICK(&r, KEYSTEP)) % 12) : (uint8_t)sb_ri(&r, 12);
    }
    if (has_prev) b->bpm_target = (uint8_t)sb_clamp(prev_bpm + (int)sb_ri(&r, 5) - 2, st->bpm_lo, st->bpm_hi);
    else b->bpm_target = (uint8_t)(st->bpm_lo + sb_ri(&r, (uint32_t)(st->bpm_hi - st->bpm_lo + 1)));
    b->bpm = has_prev && sb_abs(prev_bpm - b->bpm_target) <= 12 ? (uint8_t)prev_bpm : b->bpm_target;
    { const sb_prog_t *p = &st->progs[sb_rnext(&r) % st->nprogs]; b->prog = p->c; b->nprog = p->n; }
    b->chord_bars = st->chord_bars;
    b->kit_pick = sb_rnext(&r); b->swing = st->swing;
    b->bass_kind = st->bass[sb_rnext(&r) % st->nbass];
    {
        sb_rng_t pr = sb_rng(sb_hash(seed, 7));
        uint8_t was[SB_PLAN_MAX], wn = has_prev ? b->plan_n : 0;
        int t;
        for (i = 0; i < wn; i++) was[i] = b->plan_sec[i];
        for (t = 0; t < 5; t++) {
            int same;
            sb_form(b, &pr);
            same = has_prev && b->plan_n == wn;
            for (i = 0; same && i < wn; i++) if (was[i] != b->plan_sec[i]) same = 0;
            if (!same) break;
        }
        {
            int bars = 0, k;
            for (i = 0; i < b->plan_n; i++) bars += b->plan_len[i];
            while (bars > SB_FORM[style].max_bars) {
                for (k = -1, i = 0; i < b->plan_n; i++)
                    if (b->plan_sec[i] != SEC_BUILD && b->plan_sec[i] != SEC_BREAK && b->plan_len[i] > 16 && (k < 0 || b->plan_len[i] > b->plan_len[k])) k = i;
                if (k < 0) break;
                b->plan_len[k] = (uint16_t)(b->plan_len[k] - 8); bars -= 8;
            }
        }
    }
    b->plan_at = 0; b->sec_bar = 0; b->phrase_bar = 0;
    for (i = 0; i < 6; i++) b->layer_on[i] = (uint8_t)sb_rchance(&r, st->layers[i]);
    if (!b->layer_on[1] && !b->layer_on[0]) b->layer_on[1] = 1;
    sb_motifs(b); sb_pick_sounds(b);
    b->track_start_bar = b->bar;
}

void sb_init(sb_brain_t *b, const sb_palette_t *pal)
{
    int i;
    uint8_t *z = (uint8_t *)b;
    for (i = 0; i < (int)sizeof *b; i++) z[i] = 0;
    b->style = SB_OUTRUN;
    b->knobs[K_ENERGY] = 650; b->knobs[K_DENSITY] = 650; b->knobs[K_TENSION] = 650; b->knobs[K_MUTATION] = 450;
    for (i = 0; i < SB_NLAYERS; i++) b->snd[i] = -1;
    b->kit_force = -1;
    b->pal = pal;
    sb_new_track(b, 0x51A3u, SB_OUTRUN, 0, 0, 0);
}

/* the sounds of this track */
static void sb_pick_sounds(sb_brain_t *b)
{
    const sb_palette_t *p = b->pal; const sb_style_t *st = SB_ST(b); int L;
    if (!p) return;
    for (L = L_BASS; L <= L_FX; L++) {
        sb_rng_t r; int n;
        if (b->locks[L] && b->snd[L] >= 0) continue;
        r = sb_rng(sb_hash(b->seed, (uint32_t)(100 + L * 7 + b->rerolls[L] * 131)));
        n = p->ncur[b->style][L];
        if (n) b->snd[L] = (int16_t)sb_ri(&r, (uint32_t)n);
    }
    {
        int nk = p->nkits[b->style], ki = nk ? (int)(b->kit_pick % (uint32_t)nk) : 0, w, kx;
        if (b->locks[L_DRUMS] && b->kit_force >= 0 && p->nkall) { ki = b->kit_force % p->nkall; nk = p->nkall; }
        kx = p->kit_at[b->style] ? p->kit_at[b->style][ki] : p->kit_first[b->style] + ki;
        b->snd[L_DRUMS] = (int16_t)ki;
        for (w = 0; w < 2; w++) {
            int n16 = nk ? p->kick_n16[kx][w] : 0, best = n16;
            if (st->kit_tune && n16) {
                int bd = 1 << 30, pcs[2], q, t;
                pcs[0] = b->key; pcs[1] = b->key + 7;
                for (q = 0; q < 2; q++) for (t = pcs[q] % 12; t < 128; t += 12) if (sb_abs(t * 16 - n16) < bd) { bd = sb_abs(t * 16 - n16); best = t * 16; }
            }
            if (w) b->kick2_n16 = (uint16_t)best; else b->kick_n16 = (uint16_t)best;
        }
    }
    b->has_alt = 0;
    if (st->drop_bass && !b->locks[L_BASS]) {
        sb_rng_t r2 = sb_rng(sb_hash(b->seed, (uint32_t)(177 + b->rerolls[L_BASS] * 131)));
        int n = p->ncur[b->style][L_BASS];
        if (n) { b->bass_alt[0] = b->snd[L_BASS]; b->bass_alt[1] = (int16_t)sb_ri(&r2, (uint32_t)n); b->bass_alt[2] = (int16_t)sb_ri(&r2, (uint32_t)n); b->has_alt = 1; }
    }
    b->sounds_changed = 1;
}
static sb_bstep_t sb_bs(int d, int o, int len, int v) { sb_bstep_t s; s.d = (int8_t)d; s.o = (int8_t)o; s.len = (uint8_t)len; s.v = (uint8_t)v; s.slide = 0; s.on = 1; s.has_wob = 0; s.wob.rate3 = 0; s.wob.shape = 0; s.wob.depth = 0; s.wob.growl = 0; return s; }

/* the sb_motifs */
static void sb_motifs(sb_brain_t *b)
{
    const sb_style_t *st = SB_ST(b); int G = st->groove, i, j;
#define SB_RL(L) sb_rng(sb_hash(b->seed, (uint32_t)(1000 + (L) * 17 + b->rerolls[L] * 977 + b->motif_ver[L] * 313)))
#define SB_KEEP(L) ((b->locks[L] || (b->motif_only && !((b->motif_only >> (L)) & 1u))) && b->pat_init)
    if (!SB_KEEP(L_DRUMS)) {
        sb_rng_t r = SB_RL(L_DRUMS);
        if (b->style == SB_OUTRUN) {
                /* Kavinsky-style pumping bass: 16th-note syncopation with rhythmic rests and accent ducking */
                for (i = 0; i < 16; i++) {
                    int oct = (i == 3 || i == 7 || i == 11 || i == 15) ? 1 : 0;
                    int vel = (i % 2 == 0) ? 120 : 90;
                    int active = !(i == 2 || i == 6 || i == 10 || i == 14);
                    uint8_t gate_len = (i % 4 == 0) ? 2 : 1;
                    s[i] = sb_bs(0, oct, gate_len, active ? vel : 0);
                    s[i].on = active;
                }
            }
            } else {
                for (i = 0; i < 16; i++) if (i % 4) { int d = (i % 4 == 3 && sb_rchance(&r, 250)) ? 4 : 0; s[i] = sb_bs(d, 0, 1, i % 4 == 1 ? 110 : 90); }
            }
        }
        else if (kind == SBBK_RUMBLE) { for (i = 2; i < 16; i += 4) s[i] = sb_bs(0, 0, 2, 90); }
        else if (kind == SBBK_PULSE) { for (i = 0; i < 16; i += 2) if (i % 4) s[i] = sb_bs(0, 0, 1, 95); }
        else if (kind == SBBK_REESE) { static const int8_t D3[3] = {4, 6, 2}; s[0] = sb_bs(0, 0, 8, 100); s[8] = sb_bs(0, 0, 6, 95); s[14] = sb_bs(SB_PICK(&r, D3), 0, 2, 90); }
        else if (kind == SBBK_ROLLER) {
            static const uint8_t H0[] = {0, 3, 6, 10, 14}, H1[] = {0, 6, 10, 11, 14}, H2[] = {0, 3, 7, 10}, H3[] = {0, 2, 6, 10, 13};
            static const struct { const uint8_t *a; uint8_t n; } HS[4] = { {H0, 5}, {H1, 5}, {H2, 4}, {H3, 5} };
            static const int8_t D3[3] = {4, 6, 7}; static const uint8_t LEN[4] = {1, 2, 2, 3}, RATE[3] = {48, 96, 24};
            int h = (int)(sb_rnext(&r) % 4);
            for (j = 0; j < HS[h].n; j++) {
                int at = HS[h].a[j], d, o, len;
                d = sb_rchance(&r, 750) ? 0 : SB_PICK(&r, D3); o = sb_rchance(&r, 200) ? 1 : 0; len = SB_PICK(&r, LEN);
                s[at] = sb_bs(d, o, len, at == 0 ? 112 : 96);
                s[at].has_wob = 1; s[at].wob.rate3 = SB_PICK(&r, RATE); s[at].wob.shape = 0; s[at].wob.depth = 350; s[at].wob.growl = 0;
            }
        }
        else if (two) {
            static const uint8_t RATES_R[4] = {4, 8, 2, 4}, RATES_W[7] = {12, 6, 4, 3, 24, 2, 6}, SH_R[3] = {2, 1, 2}, SH_W[5] = {0, 1, 2, 3, 0};
            static const uint8_t DEG_P[8] = {0, 0, 0, 0, 1, 4, 6, 7}, DEG_M[8] = {0, 0, 0, 0, 4, 5, 6, 7};
            static const uint8_t LR[3] = {2, 3, 3}, LW[6] = {2, 3, 4, 4, 6, 8}, GR[4] = {1, 1, 2, 3}, GW[4] = {0, 0, 1, 2}, L2[5] = {1, 2, 2, 3, 4};
            const uint8_t *degs = b->scale == SB_PHRYGIAN ? DEG_P : DEG_M;
#define SB_WOB(st_, lo, span, gch, gval) do { \
            (st_).has_wob = 1; \
            (st_).wob.rate3 = kind == SBBK_RIDDIM ? SB_PICK(&r, RATES_R) : SB_PICK(&r, RATES_W); \
            (st_).wob.shape = kind == SBBK_RIDDIM ? SB_PICK(&r, SH_R) : SB_PICK(&r, SH_W); \
            (st_).wob.depth = (uint16_t)((lo) + sb_ri(&r, (span) + 1)); \
            (st_).wob.growl = (uint16_t)(kind == SBBK_GROWL ? 1000 : sb_rchance(&r, gch) ? (gval) : 0); } while (0)
            i = 0;
            while (i < 16) {
                int len = kind == SBBK_RIDDIM ? SB_PICK(&r, LR) : SB_PICK(&r, LW);
                int gap = kind == SBBK_RIDDIM ? SB_PICK(&r, GR) : SB_PICK(&r, GW);
                int d = i == 0 ? 0 : degs[sb_rnext(&r) % 8];
                s[i] = sb_bs(d == 7 ? 0 : d, d == 7 ? 1 : 0, len < 16 - i ? len : 16 - i, 115); SB_WOB(s[i], 600, 400, 300, 600);
                i += len + gap;
            }
            for (j = 0; j < 16; j++) s[16 + j] = s[j];
            for (j = 24; j < 32; j++) s[j].on = 0;
            for (j = 24; j < 32;) {
                int len = SB_PICK(&r, L2);
                if (sb_rchance(&r, 750)) { int d = degs[sb_rnext(&r) % 8] % 7, o = sb_rchance(&r, 300) ? 1 : 0; s[j] = sb_bs(d, o, len < 32 - j ? len : 32 - j, 120); SB_WOB(s[j], 700, 300, 500, 700); }
                j += len + (sb_rchance(&r, 400) ? 1 : 0);
            }
#undef SB_WOB
        }
        else if (kind == SBBK_ACID) {
            static const int8_t DA[7] = {0, 0, 0, 2, 4, 6, 7};
            for (i = 0; i < 16; i++) if (sb_rchance(&r, 700)) {
                int d = SB_PICK(&r, DA), o = sb_rchance(&r, 250) ? 1 : 0, len = sb_rchance(&r, 250) ? 2 : 1, v = sb_rchance(&r, 300) ? 127 : 90;
                s[i] = sb_bs(d, o, len, v); s[i].slide = (uint8_t)(sb_rchance(&r, 200) ? 1 : 0);
            }
        }
    }
    if (!SB_KEEP(L_HOOK)) {
        if (b->style == SB_OUTRUN) {
            static const int8_t CLUTCH_D[6] = {0, 1, 2, 4, 5, 7};
            static const int8_t CLUTCH_STEP[8] = {-2, -1, 0, 1, 2, 3, 5, -5};
            static const uint8_t CLUTCH_MASK[16] = {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0};
            sb_rng_t r = SB_RL(L_HOOK); int len = 32, d = 0;
            for (i = 0; i < len; i++) {
                sb_hstep_t *h = &b->hook[i];
                int step16 = i % 16;
                int on = CLUTCH_MASK[step16] ? sb_rchance(&r, 850) : sb_rchance(&r, 150);
                if (!on) { h->on = 0; continue; }
                if (step16 == 0) d = SB_PICK(&r, CLUTCH_D);
                else d += SB_PICK(&r, CLUTCH_STEP);
                if (d < 0) d += 7;
                if (d > 9) d -= 5;
                h->d = (int8_t)d;
                h->o = (int8_t)((step16 == 10 || step16 == 14) && sb_rchance(&r, 450) ? 2 : 1);
                h->len = (uint8_t)((step16 == 0 || step16 == 6) ? 4 : 2);
                h->v = (uint8_t)(step16 == 0 || step16 == 6 ? 127 : 114);
                if (step16 > 0 && sb_abs(h->d - (h - 1)->d) >= 2) h->slide = 1;
                h->on = 1;
            }
            b->hook_n = (uint8_t)len;
            for (i = 0; i < len; i++) b->theme[i] = b->hook[i];
        } else {
            static const int8_t D0[4] = {0, 2, 4, 7}, STEP[9] = {-2, -1, -1, 0, 1, 1, 2, 3, -3};
            sb_rng_t r = SB_RL(L_HOOK); int len = sb_rchance(&r, 500) ? 32 : 16, dens = 350 + 350 * b->knobs[K_DENSITY] / 1000, d;
            d = SB_PICK(&r, D0);
            for (i = 0; i < len; i++) {
                int strong = i % 4 == 0, on = strong ? sb_rchance(&r, 800) : sb_rchance(&r, i % 2 ? dens * 7 / 10 : dens);
                sb_hstep_t *h = &b->hook[i];
                if (!on) { h->on = 0; continue; }
                d += SB_PICK(&r, STEP);
                if (d < -2) d += 4;
                if (d > 9) d -= 5;
                if (strong) { int bb = 0, c; for (c = 0; c < 4; c++) if (sb_abs(D0[c] - d) < sb_abs(bb - d)) bb = D0[c]; d = bb; }
                h->d = (int8_t)d; h->o = 1; h->len = (uint8_t)(sb_rchance(&r, 300) ? 2 : 1); h->v = (uint8_t)(strong ? 112 : 92); h->on = 1;
            }
            b->hook_n = (uint8_t)len;
            for (i = 0; i < len; i++) b->theme[i] = b->hook[i];
        }
    }
    if (!SB_KEEP(L_ARP)) {
        static const uint8_t RATE[4] = {1, 1, 1, 2}, GATE[3] = {0, 0, 1};
        sb_rng_t r = SB_RL(L_ARP);
        b->arp.order = (uint8_t>sb_ri(&r, 5); b->arp.rate = SB_PICK(&r, RATE); b->arp.oct = (uint8_t)(1 + sb_ri(&r, 2)); b->arp.gate = SB_PICK(&r, GATE);
        b->arp.skip = (int8_t)(sb_rchance(&r, 300) ? (int)sb_ri(&r, 16) : -1); b->arp.seed = sb_rnext(&r);
    }
    if (!SB_KEEP(L_STAB)) { sb_rng_t r = SB_RL(L_STAB); int kk = 3 + (int)sb_ri(&r, 3), rot = (int)sb_ri(&r, 16); for (i = 0; i < 16; i++) b->stab[i] = (uint8_t)(sb_euclid(kk, 16, rot, i) && i % 4 != 0 ? 110 : 0); }
    if (!SB_KEEP(L_PAD)) { sb_rng_t r = SB_RL(L_PAD); b->pad.inv = (uint8_t)sb_ri(&r, 3); b->pad.add9 = (uint8_t)sb_rchance(&r, 400); }
    b->pat_init = 1;
#undef SB_RL
#undef SB_KEEP
}

/* a phrase ends: the sb_motifs develop */
static void sb_mutate(sb_brain_t *b)
{
    int m = b->knobs[K_MUTATION]; sb_rng_t r = sb_rng(sb_hash(b->seed, 7000 + b->bar));
    if (!b->locks[L_HOOK] && sb_rchance(&r, m)) {
        sb_hstep_t *s = b->hook; int n = b->hook_n, j;
        if (sb_rchance(&r, 180)) { for (j = 0; j < n; j++) s[j] = b->theme[j]; }
        else {
            int op = (int)sb_ri(&r, 6), i = (int)sb_ri(&r, (uint32_t)n);
            if (op == 0) { sb_hstep_t t = s[n - 1]; for (j = n - 1; j > 0; j--) s[j] = s[j - 1]; s[0] = t; }
            else if (op == 1 && s[i].on) { static const int8_t M[4] = {-1, 1, 2, -2}; s[i].d = (int8_t)(s[i].d + SB_PICK(&r, M)); }
            else if (op == 2 && !s[i].on && i % 2 == 0) { static const int8_t A[3] = {0, 2, 4}; s[i].d = SB_PICK(&r, A); s[i].o = 1; s[i].len = 1; s[i].v = 90; s[i].on = 1; }
            else if (op == 3 && s[i].on && i % 4) s[i].on = 0;
            else if (op == 4 && s[i].on) s[i].o = (int8_t)(s[i].o ? 0 : 1);
            else if (op == 5) { int k = (int)sb_ri(&r, (uint32_t)n); sb_hstep_t t = s[i]; s[i] = s[k]; s[k] = t; }
        }
    }
    if (!b->locks[L_DRUMS] && sb_rchance(&r, m * 7 / 10)) {
        if (sb_rchance(&r, 500)) b->dr.perc_rot = (uint8_t)((b->dr.perc_rot + 1 + sb_ri(&r, 3)) % 16);
        else { static const int8_t PM[2] = {-1, 1}; b->dr.perc_k = (uint8_t)sb_clamp(b->dr.perc_k + SB_PICK(&r, PM), 2, 9); }
    }
    if (!b->locks[L_BASS] && b->bass_kind == SBBK_ACID && sb_rchance(&r, m)) {
        sb_bstep_t *s = b->bass; int i = (int)sb_ri(&r, 16);
        if (s[i].on) { if (sb_rchance(&r, 500)) s[i].on = 0; else { s[i].slide = (uint8_t)(s[i].slide ? 0 : 1); s[i].v = (uint8_t)(s[i].v > 100 ? 90 : 127); } }
        else { static const int8_t DA[5] = {0, 0, 2, 4, 7}; int d = SB_PICK(&r, DA), o = sb_rchance(&r, 300) ? 1 : 0; s[i] = sb_bs(d, o, 1, 100); }
    }
    if (!b->locks[L_ARP] && sb_rchance(&r, m / 2)) b->arp.order = (uint8_t)sb_ri(&r, 5);
}

static int sb_arc_energy(const sb_brain_t *b)
{
    int sec = b->plan_sec[b->plan_at], len = b->plan_len[b->plan_at];
    return sec == SEC_BUILD ? 550 + 450 * (b->sec_bar * 16 + b->step) / (len * 16) : SB_SEC_E[sec];
}
static int sb_energy(const sb_brain_t *b) { return sb_clamp(sb_arc_energy(b) + (b->knobs[K_ENERGY] - 500) * 7 / 10, 0, 1000); }

static void sb_plan_insert(sb_brain_t *b, int at, int sec, int len)
{
    int i;
    if (b->plan_n >= SB_PLAN_MAX) {
        if (b->plan_n - 1 <= at) return;
        b->plan_n--;
    }
    for (i = b->plan_n; i > at; i--) { b->plan_sec[i] = b->plan_sec[i - 1]; b->plan_len[i] = b->plan_len[i - 1]; }
    b->plan_sec[at] = (uint8_t)sec; b->plan_len[at] = (uint16_t)len; b->plan_n++;
}

static void sb_enter(sb_brain_t *b)
{
    b->plan_at++; b->sec_bar = 0; b->phrase_bar = 0;
    if (b->plan_at >= b->plan_n) sb_next_track(b);
}

static void sb_key_up(sb_brain_t *b)
{
    if (b->key_req && b->plan_at < b->plan_n && b->plan_sec[b->plan_at] == SEC_DROP && b->sec_bar == 0) {
        b->key_req = 0; b->key = (uint8_t)((b->key + 2) % 12); sb_pick_sounds(b);
    }
}

static void sb_next_bar(sb_brain_t *b)
{
    b->sec_bar++; b->phrase_bar++;
    if (b->phrase_bar == 8) { b->phrase_bar = 0; sb_mutate(b); }
    if (b->drop_req) {
        b->drop_req = 0; b->dive = 0;
        if (b->plan_sec[b->plan_at] != SEC_DROP) { sb_plan_insert(b, b->plan_at + 1, SEC_DROP, 32); sb_enter(b); sb_key_up(b); return; }
    }
    if (b->dive) {
        if (b->plan_sec[b->plan_at] == SEC_BUILD) {
            int len = b->plan_len[b->plan_at], lim = b->sec_bar + b->dive;
            len = len < lim ? len : lim; if (len < b->sec_bar + 1) len = b->sec_bar + 1;
            b->plan_len[b->plan_at] = (uint16_t)len;
            if (b->plan_at + 1 >= b->plan_n || b->plan_sec[b->plan_at + 1] != SEC_DROP) sb_plan_insert(b, b->plan_at + 1, SEC_DROP, 32);
            b->dive = 0;
        } else { sb_plan_insert(b, b->plan_at + 1, SEC_BUILD, b->dive); sb_plan_insert(b, b->plan_at + 2, SEC_DROP, 32); b->dive = 0; sb_enter(b); return; }
    }
    if (b->break_req) { b->break_req, b->break_req = 0; sb_plan_insert(b, b->plan_at + 1, SEC_BREAK, 8); sb_enter(b); return; }
    if (b->sec_bar >= b->plan_len[b->plan_at]) {
        sb_enter(b);
    }
}
void sb_rewind(sb_brain_t *b) { b->sec_bar = 0; b->phrase_bar = 0; b->step = 0; }

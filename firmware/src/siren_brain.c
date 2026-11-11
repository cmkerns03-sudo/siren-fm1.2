/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN, the brain: a rave that writes itself. A seed and a grammar make a track; the sb_energy arc makes a set; the
 * mutations make it never repeat yet stay recognisable. This is the prototype's brain.js line for line (integers
 * only: energies, knobs, chances and levels in per mille, a seeded xorshift32), so the FM-1 plays what was heard on
 * the prototype: tests/siren_brain_test.c runs both and compares every event. Every draw from a stream happens in
 * the same order as there, every condition is evaluated in the same order (the draws inside conditions too). */
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
static int32_t sb_clamp(int32_t x, int32_lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
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
    /* OUTRUN */ { 0, GR_FOUR, 0, 1, 0, 0, 0, 96, 114, 2, {SB_MINOR, SB_DORIAN}, 5,
                  {{4, {0, 5, 2, 6}}, {4, {0, 6, 5, 6}}, {4, {0, 5, 6, 0}}, {4, {0, 3, 5, 6}}, {2, {0, 5}}}, 2,
                  3, {SBBK_ROLLING, SBBK_PULSE, SBBK_OFFBEAT}, 0, {850, 900, 950, 400, 1000, 0}, 4, 8, 0, SBD_SNARE, 3, {SBD_TOML, SBD_TOMH, SBD_RIM}, 3, {SBHM_8, SBHM_OFF8, SBHM_16} },
    /* DNB */    { 0, GR_DNB, 0, 1, 850, 0, 0, 170, 176, 2, {SB_MINOR, SB_DORIAN}, 4, {{4, {0, 5, 3, 4}}, {2, {0, 3}}, {4, {0, 6, 5, 4}}, {4, {0, 0, 5, 6}}}, 2,
                  3, {SBBK_ROLLER, SBBK_REESE, SBBK_ROLLER}, 0, {500, 500, 900, 450, 1000, 0}, 6, 10, 700, SBD_SNARE, 2, {SBD_RIM, SBD_SHAKE}, 2, {SBHM_8, SBHM_16GAP} },
    /* HOUSE */  { 0, GR_FOUR, 0, 1, 0, 0, 0, 122, 126, 2, {SB_MINOR, SB_DORIAN}, 4, {{2, {0, 3}}, {4, {0, 5, 2, 6}}, {4, {0, 3, 6, 4}}, {4, {0, 0, 3, 3}}}, 2,
                  3, {SBBK_OFFBEAT, SBBK_PULSE, SBBK_OFFBEAT}, 70, {350, 800, 800, 950, 900, 0}, 4, 9, 500, SBD_CLAP, 3, {SBD_SHAKE, SBD_CONGA, SBD_CLAVE}, 3, {SBHM_OFF8, SBHM_16, SBHM_16GAP} },
};
/* (0.6) the form of a track, generated: an intro (in two parts, now and then), then one to four waves, each a groove,
 * a breakdown (short before the first drop, longer after), a build (now and then a false one: build, a breath, build
 * again; or a bar of silence before the drop), the drop or the peak and a ride after it; then the outro. Each style
 * its proportions (from how its records are made: house's 32-bar intros and long grooves, techno's rides and few
 * builds, trance's long breakdowns, drum & bass's two drops, hard techno's short intros and frequent drops). Lengths
 * in bars; chances per mille. The next track never takes the same order of sections as the one before. */
/* (0.6) how a track begins: the DJ's drum intro, or an atmosphere with no kick (a pad, a riser), the hook alone, the
 * whole groove under a closed low-pass that opens, the bassline and hats before the kick (the first half of the intro;
 * the drums come in for the second), straight into the groove and the drop, or the breakdown first. Each style its
 * share of each (per mille), as its records begin */
enum { IK_DRUMS, IK_ATMOS, IK_HOOK, IK_FILTER, IK_BASS, IK_DIRECT, IK_BREAK, IK_N };
static const uint16_t SB_INTRO_W[SB_NSTYLES][IK_N] = {
    {500, 100, 50, 100, 200, 50, 0},      /* TECHNO */
    {200, 250, 150, 100, 0, 50, 250},     /* TRANCE */
    {330, 200, 50, 100, 250, 70, 0},      /* PSY */
    {330, 50, 50, 150, 370, 50, 0},       /* ACID */
    {330, 100, 200, 100, 50, 150, 70},    /* RAVE */
    {200, 350, 250, 100, 100, 0, 0},      /* OUTRUN */
    {330, 150, 100, 100, 150, 70, 100},   /* DNB */
    {500, 50, 200, 150, 100, 0, 0},       /* HOUSE */
};

typedef struct { uint8_t n, v[4]; } sb_opt_t;
typedef struct {
    sb_opt_t intro, intro2, groove1, midg, brks, brkl, build, drop, tail, outro;
    uint16_t p_intro2, p_groove1, p_mid, p_break1, p_build, p_fake, p_pause, p_peak, p_peaklast, p_tail;
    uint8_t cyc_lo, cyc_hi; uint16_t max_bars;    /* (about six minutes at the style's tempo) */
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
    /* OUTRUN */ { {2, {16, 32}}, {1, {16}}, {2, {16, 32}}, {2, {16, 32}}, {1, {8}}, {2, {8, 16}}, {2, {4, 8}}, {3, {16, 16, 32}}, {2, {8, 16}}, {2, {8, 16}},
                   500, 800, 500, 400, 600, 50, 100, 400, 600, 400, 2, 3, 192 },
    /* DNB */    { {3, {16, 32, 32}}, {1, {16}}, {1, {8}}, {1, {8}}, {1, {8}}, {3, {16, 24, 32}}, {2, {8, 16}}, {3, {32, 32, 48}}, {1, {8}}, {2, {16, 32}},
                   300, 200, 100, 300, 1000, 150, 300, 100, 400, 200, 2, 3, 256 },
    /* HOUSE */  { {3, {16, 32, 32}}, {2, {16, 32}}, {3, {32, 48, 64}}, {2, {16, 32}}, {1, {8}}, {2, {8, 16}}, {1, {8}}, {3, {32, 32, 48}}, {2, {16, 32}}, {2, {16, 32}},
                   600, 900, 700, 700, 700, 100, 200, 200, 300, 600, 1, 3, 192 },
};

static int sb_opt(sb_rng_t *r, const sb_opt_t *o) { return o->v[sb_ri(r, o->n)]; }
static void sb_sec(sb_brain_t *b, int sec, int len)
{
    if (b->plan_n && b->plan_sec[b->plan_n - 1] == sec) { b->plan_len[b->plan_n - 1] = (uint16_t)(b->plan_len[b->plan_n - 1] + len); return; }   /* (one longer) */
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
    if (ik == IK_DIRECT) sb_sec(b, SEC_GROOVE, SB_PICK(r, DIRECT));               /* (straight in: a few bars, the drop) */
    else if (ik == IK_BREAK) sb_sec(b, SEC_BREAK, sb_opt(r, &f->brkl));          /* (the breakdown first) */
    else {
        sb_sec(b, SEC_INTRO, sb_opt(r, &f->intro));
        if (sb_rchance(r, f->p_intro2)) sb_sec(b, SEC_GROOVE, sb_opt(r, &f->intro2));
    }
    ncyc = f->cyc_lo + (int)sb_ri(r, (uint32_t)(f->cyc_hi - f->cyc_lo + 1));
    for (c = 0; c < ncyc; c++) {
        int last = c == ncyc - 1;
        if (!c && ik == IK_BREAK) sb_sec(b, SEC_BUILD, sb_opt(r, &f->build));      /* (the breakdown first: its build, the drop) */
        else if (!c && ik == IK_DIRECT) { }                                         /* (straight in: the drop now) */
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
    b->key = has_prev ? (uint8_t)((prev_key + SB_PICK(&r, KEYSTEP)) % 12) : (uint8_t)sb_ri(&r, 12);
    if (has_prev) b->bpm_target = (uint8_t)sb_clamp(prev_bpm + (int)sb_ri(&r, 5) - 2, st->bpm_lo, st->bpm_hi);
    else b->bpm_target = (uint8_t)(st->bpm_lo + sb_ri(&r, (uint32_t)(st->bpm_hi - st->bpm_lo + 1)));
    b->bpm = has_prev && sb_abs(prev_bpm - b->bpm_target) <= 12 ? (uint8_t)prev_bpm : b->bpm_target;   /* (far apart: the new tempo at once) */
    { const sb_prog_t *p = &st->progs[sb_rnext(&r) % st->nprogs]; b->prog = p->c; b->nprog = p->n; }
    b->chord_bars = st->chord_bars;
    b->kit_pick = sb_rnext(&r); b->swing = st->swing;
    b->bass_kind = st->bass[sb_rnext(&r) % st->nbass];
    {   /* (0.6) the form, from its own stream; never the order of sections the last track had */
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
        {   /* too long for the style: the longest of its long sections 8 bars shorter, until it fits */
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
    if (!b->layer_on[1] && !b->layer_on[0]) b->layer_on[1] = 1;   /* (a track always sings something) */
    sb_motifs(b); sb_pick_sounds(b);
    b->track_start_bar = b->bar;
}

void sb_init(sb_brain_t *b, const sb_palette_t *pal)
{
    int i;
    uint8_t *z = (uint8_t *)b;
    for (i = 0; i < (int)sizeof *b; i++) z[i] = 0;
    b->style = SB_TRANCE;
    b->knobs[K_ENERGY] = 500; b->knobs[K_DENSITY] = 500; b->knobs[K_TENSION] = 500; b->knobs[K_MUTATION] = 350;
    for (i = 0; i < SB_NLAYERS; i++) b->snd[i] = -1;
    b->kit_force = -1;
    b->pal = pal;
    sb_new_track(b, 0x51A3u, SB_TRANCE, 0, 0, 0);
}

/* the sounds of this track: a pick for each layer from the style's selection, by its own stream */
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
    /* the drums: one of the style's own kits, its kick (and boom) tuned to the key */
    {
        int nk = p->nkits[b->style], ki = nk ? (int)(b->kit_pick % (uint32_t)nk) : 0, w, kx;
        if (b->locks[L_DRUMS] && b->kit_force >= 0 && p->nkall) { ki = b->kit_force % p->nkall; nk = p->nkall; }   /* (SIREN 0.10) */
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
    /* dubstep: the bass changes its sound inside a drop */
    b->has_alt = 0;
    if (st->drop_bass && !b->locks[L_BASS]) {
        sb_rng_t r2 = sb_rng(sb_hash(b->seed, (uint32_t)(177 + b->rerolls[L_BASS] * 131)));
        int n = p->ncur[b->style][L_BASS];
        if (n) { b->bass_alt[0] = b->snd[L_BASS]; b->bass_alt[1] = (int16_t)sb_ri(&r2, (uint32_t)n); b->bass_alt[2] = (int16_t)sb_ri(&r2, (uint32_t)n); b->has_alt = 1; }
    }
    b->sounds_changed = 1;
}

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
const char *const SB_STYLE_NAMES[SB_NSTYLES] = { "TECHNO", "TRANCE", "PSY", "ACID", "RAVE", "HARD", "DNB", "DUBSTEP" };
const char *const SB_SEC_NAMES[7] = { "INTRO", "GROOVE", "BUILD", "DROP", "BREAK", "PEAK", "OUTRO" };
static const int16_t SB_SEC_E[7] = { 250, 550, 750, 1000, 300, 920, 320 };
static const uint8_t SB_STYLE_ORDER[SB_NSTYLES] = { SB_TECHNO, SB_ACID, SB_TRANCE, SB_PSY, SB_HARD, SB_RAVE, SB_DNB, SB_DUBSTEP };

typedef struct { uint8_t sec, a, b; } sb_shp_t;
typedef struct { const sb_shp_t *s; uint8_t n; } sb_shape_t;
#define SB_SHAPE(a) { a, sizeof(a) / sizeof((a)[0]) }
static const sb_shp_t SB_SH0[] = { {SEC_INTRO, 1, 2}, {SEC_GROOVE, 2, 4}, {SEC_BUILD, 1, 1}, {SEC_DROP, 3, 4}, {SEC_BREAK, 1, 2}, {SEC_BUILD, 1, 1}, {SEC_DROP, 3, 5}, {SEC_PEAK, 1, 2}, {SEC_GROOVE, 1, 2}, {SEC_OUTRO, 1, 2} };
static const sb_shp_t SB_SH1[] = { {SEC_INTRO, 1, 2}, {SEC_GROOVE, 3, 5}, {SEC_BUILD, 1, 1}, {SEC_DROP, 4, 6}, {SEC_GROOVE, 1, 2}, {SEC_OUTRO, 1, 2} };
static const sb_shp_t SB_SH2[] = { {SEC_INTRO, 1, 1}, {SEC_BUILD, 1, 1}, {SEC_DROP, 2, 3}, {SEC_BREAK, 1, 1}, {SEC_BUILD, 1, 1}, {SEC_DROP, 2, 3}, {SEC_BREAK, 1, 1}, {SEC_BUILD, 1, 1}, {SEC_PEAK, 2, 3}, {SEC_OUTRO, 1, 1} };
static const sb_shp_t SB_SH_TECHNO[] = { {SEC_INTRO, 2, 2}, {SEC_GROOVE, 4, 6}, {SEC_BREAK, 1, 1}, {SEC_GROOVE, 2, 3}, {SEC_PEAK, 2, 4}, {SEC_GROOVE, 1, 2}, {SEC_OUTRO, 1, 2} };
static const sb_shp_t SB_SH_TRANCE[] = { {SEC_INTRO, 2, 2}, {SEC_GROOVE, 2, 3}, {SEC_BUILD, 1, 1}, {SEC_DROP, 3, 4}, {SEC_BREAK, 2, 3}, {SEC_BUILD, 1, 2}, {SEC_PEAK, 4, 5}, {SEC_OUTRO, 1, 2} };
static const sb_shp_t SB_SH_PSY[] = { {SEC_INTRO, 1, 1}, {SEC_GROOVE, 4, 6}, {SEC_BUILD, 1, 1}, {SEC_DROP, 4, 6}, {SEC_BREAK, 1, 1}, {SEC_DROP, 2, 3}, {SEC_OUTRO, 1, 1} };
static const sb_shp_t SB_SH_ACID[] = { {SEC_INTRO, 1, 2}, {SEC_GROOVE, 4, 6}, {SEC_BREAK, 1, 1}, {SEC_BUILD, 1, 1}, {SEC_DROP, 4, 6}, {SEC_OUTRO, 1, 2} };
static const sb_shp_t SB_SH_DNB[] = { {SEC_INTRO, 2, 2}, {SEC_BUILD, 1, 1}, {SEC_DROP, 4, 5}, {SEC_BREAK, 1, 2}, {SEC_BUILD, 1, 1}, {SEC_DROP, 4, 5}, {SEC_OUTRO, 1, 2} };
static const sb_shp_t SB_SH_DUB0[] = { {SEC_INTRO, 2, 2}, {SEC_BUILD, 1, 1}, {SEC_DROP, 2, 3}, {SEC_BREAK, 2, 2}, {SEC_BUILD, 1, 1}, {SEC_DROP, 3, 4}, {SEC_OUTRO, 1, 1} };
static const sb_shp_t SB_SH_DUB1[] = { {SEC_INTRO, 1, 2}, {SEC_GROOVE, 1, 1}, {SEC_BUILD, 1, 1}, {SEC_DROP, 3, 3}, {SEC_BREAK, 1, 2}, {SEC_BUILD, 1, 1}, {SEC_DROP, 3, 3}, {SEC_PEAK, 1, 1}, {SEC_OUTRO, 1, 1} };

typedef struct { uint8_t n, c[4]; } sb_prog_t;
typedef struct {
    uint8_t break_drums, groove; uint16_t rumble; uint8_t kit_tune; uint16_t sub; uint8_t drop_bass; uint16_t gallop;
    uint8_t bpm_lo, bpm_hi; uint8_t nscales, scales[3];
    uint8_t nprogs; sb_prog_t progs[5]; uint8_t chord_bars;
    uint8_t nbass, bass[4]; uint8_t swing; uint16_t layers[6]; uint8_t hats_lo, hats_hi; uint16_t ride; uint8_t roll;
    uint8_t nperc, perc[4]; uint8_t nhm, hm[3]; uint8_t nshapes; sb_shape_t shapes[3];
} sb_style_t;
static const sb_style_t SB_STY[SB_NSTYLES] = {
    /* TECHNO */ { 1, GR_FOUR, 550, 1, 0, 0, 0, 126, 132, 3, {SB_MINOR, SB_PHRYGIAN, SB_DORIAN}, 4, {{1, {0}}, {4, {0, 0, 0, 5}}, {2, {0, 3}}, {4, {0, 0, 6, 0}}}, 4,
                 3, {SBBK_RUMBLE, SBBK_OFFBEAT, SBBK_PULSE}, 40, {350, 450, 450, 550, 1000, 0}, 5, 11, 600, SBD_SNARE, 4, {SBD_RIM, SBD_CONGA, SBD_TOML, SBD_RIM}, 3, {SBHM_16, SBHM_OFF8, SBHM_16GAP},
                 3, {SB_SHAPE(SB_SH1), SB_SHAPE(SB_SH_TECHNO), SB_SHAPE(SB_SH0)} },
    /* TRANCE */ { 0, GR_FOUR, 0, 1, 0, 0, 0, 136, 140, 2, {SB_MINOR, SB_HARMONIC}, 5, {{4, {0, 5, 2, 6}}, {4, {0, 6, 5, 4}}, {4, {0, 3, 5, 4}}, {4, {5, 3, 0, 4}}, {4, {0, 5, 3, 4}}}, 2,
                 3, {SBBK_OFFBEAT, SBBK_OFFBEAT, SBBK_ROLLING}, 0, {950, 850, 950, 200, 1000, 0}, 4, 8, 800, SBD_SNARE, 2, {SBD_SHAKE, SBD_RIM}, 2, {SBHM_16, SBHM_16GAP},
                 2, {SB_SHAPE(SB_SH0), SB_SHAPE(SB_SH_TRANCE)} },
    /* PSY */    { 1, GR_FOUR, 0, 1, 0, 0, 0, 142, 146, 2, {SB_PHRYGIAN, SB_HARMONIC}, 3, {{1, {0}}, {2, {0, 1}}, {4, {0, 0, 0, 1}}}, 8,
                 1, {SBBK_ROLLING}, 0, {600, 700, 350, 100, 1000, 0}, 6, 10, 300, SBD_SNARE, 3, {SBD_SHAKE, SBD_CONGA, SBD_RIM}, 2, {SBHM_16, SBHM_16},
                 3, {SB_SHAPE(SB_SH1), SB_SHAPE(SB_SH2), SB_SHAPE(SB_SH_PSY)} },
    /* ACID */   { 1, GR_FOUR, 150, 0, 0, 0, 0, 128, 134, 2, {SB_MINOR, SB_PHRYGIAN}, 2, {{1, {0}}, {4, {0, 0, 5, 0}}}, 8,
                 1, {SBBK_ACID}, 80, {200, 500, 300, 300, 1000, 0}, 6, 12, 500, SBD_CLAP, 3, {SBD_BELL, SBD_RIM, SBD_CLAVE}, 2, {SBHM_16, SBHM_16GAP},
                 2, {SB_SHAPE(SB_SH1), SB_SHAPE(SB_SH_ACID)} },
    /* RAVE */   { 0, GR_BREAKS, 0, 0, 0, 0, 0, 140, 150, 2, {SB_MINOR, SB_HARMONIC}, 3, {{4, {0, 5, 6, 4}}, {4, {0, 3, 6, 5}}, {4, {0, 6, 5, 6}}}, 2,
                 2, {SBBK_REESE, SBBK_OFFBEAT}, 0, {400, 600, 600, 950, 1000, 600}, 6, 12, 400, SBD_SNARE, 2, {SBD_RIM, SBD_SHAKE}, 2, {SBHM_8, SBHM_16GAP},
                 3, {SB_SHAPE(SB_SH0), SB_SHAPE(SB_SH1), SB_SHAPE(SB_SH2)} },
    /* HARD */   { 1, GR_FOUR, 350, 1, 0, 0, 500, 150, 165, 2, {SB_HARMONIC, SB_PHRYGIAN}, 3, {{4, {0, 5, 6, 4}}, {2, {0, 1}}, {4, {0, 6, 5, 4}}}, 2,
                 2, {SBBK_OFFBEAT, SBBK_PULSE}, 0, {500, 850, 400, 500, 1000, 0}, 6, 12, 600, SBD_SNARE, 2, {SBD_RIM, SBD_TOML}, 2, {SBHM_16, SBHM_16GAP},
                 3, {SB_SHAPE(SB_SH0), SB_SHAPE(SB_SH1), SB_SHAPE(SB_SH2)} },
    /* DNB */    { 0, GR_DNB, 0, 1, 850, 0, 0, 170, 176, 2, {SB_MINOR, SB_DORIAN}, 4, {{4, {0, 5, 3, 4}}, {2, {0, 3}}, {4, {0, 6, 5, 4}}, {4, {0, 0, 5, 6}}}, 2,
                 3, {SBBK_ROLLER, SBBK_REESE, SBBK_ROLLER}, 0, {500, 500, 900, 450, 1000, 0}, 6, 10, 700, SBD_SNARE, 2, {SBD_RIM, SBD_SHAKE}, 2, {SBHM_8, SBHM_16GAP},
                 2, {SB_SHAPE(SB_SH_DNB), SB_SHAPE(SB_SH0)} },
    /* DUBSTEP */{ 0, GR_HALF, 0, 1, 1000, 1, 0, 140, 140, 3, {SB_PHRYGIAN, SB_MINOR, SB_HARMONIC}, 4, {{1, {0}}, {4, {0, 0, 5, 6}}, {2, {0, 1}}, {4, {0, 0, 6, 0}}}, 4,
                 4, {SBBK_WOBBLE, SBBK_RIDDIM, SBBK_GROWL, SBBK_WOBBLE}, 0, {300, 600, 850, 300, 1000, 0}, 4, 6, 200, SBD_SNARE, 2, {SBD_RIM, SBD_SHAKE}, 2, {SBHM_8, SBHM_16GAP},
                 2, {SB_SHAPE(SB_SH_DUB0), SB_SHAPE(SB_SH_DUB1)} },
};
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
    const sb_shape_t *sh; int i;
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
    sh = &st->shapes[sb_rnext(&r) % st->nshapes];
    for (i = 0; i < sh->n; i++) { b->plan_sec[i] = sh->s[i].sec; b->plan_len[i] = (uint16_t)((sh->s[i].a + sb_ri(&r, (uint32_t)(sh->s[i].b - sh->s[i].a + 1))) * 8); }
    b->plan_n = sh->n; b->plan_at = 0; b->sec_bar = 0; b->phrase_bar = 0;
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
        int nk = p->nkits[b->style], ki = nk ? (int)(b->kit_pick % (uint32_t)nk) : 0, w;
        b->snd[L_DRUMS] = (int16_t)ki;
        for (w = 0; w < 2; w++) {
            int n16 = nk ? p->kick_n16[p->kit_first[b->style] + ki][w] : 0, best = n16;
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

static sb_bstep_t sb_bs(int d, int o, int len, int v) { sb_bstep_t s; s.d = (int8_t)d; s.o = (int8_t)o; s.len = (uint8_t)len; s.v = (uint8_t)v; s.slide = 0; s.on = 1; s.has_wob = 0; s.wob.rate3 = 0; s.wob.shape = 0; s.wob.depth = 0; s.wob.growl = 0; return s; }

/* the sb_motifs: each layer's pattern, from its own stream (a locked layer keeps its own) */
static void sb_motifs(sb_brain_t *b)
{
    const sb_style_t *st = SB_ST(b); int G = st->groove, i, j;
#define SB_RL(L) sb_rng(sb_hash(b->seed, (uint32_t)(1000 + (L) * 17 + b->rerolls[L] * 977)))
#define SB_KEEP(L) (b->locks[L] && b->pat_init)
    if (!SB_KEEP(L_DRUMS)) {
        static const uint8_t BK_DNB0[] = {0, 10}, BK_DNB2[] = {0, 7, 10}, BK_DNB3[] = {0, 6, 10}, BK_DNB4[] = {0, 10, 13};
        static const uint8_t BK_H0[] = {0}, BK_H1[] = {0, 10}, BK_H2[] = {0, 6}, BK_H3[] = {0, 11, 14}, BK_H4[] = {0, 3, 10}, BK_H5[] = {0, 7};
        static const uint8_t BK_B0[] = {0, 10}, BK_B1[] = {0, 6, 10}, BK_B2[] = {0, 2, 10}, BK_B3[] = {0, 10, 11}, BK_B4[] = {0, 3, 6, 10};
        static const struct { const uint8_t *a; uint8_t n; } KD[5] = { {BK_DNB0, 2}, {BK_DNB0, 2}, {BK_DNB2, 3}, {BK_DNB3, 3}, {BK_DNB4, 3} },
            KH[6] = { {BK_H0, 1}, {BK_H1, 2}, {BK_H2, 2}, {BK_H3, 3}, {BK_H4, 3}, {BK_H5, 2} },
            KB[5] = { {BK_B0, 2}, {BK_B1, 3}, {BK_B2, 3}, {BK_B3, 3}, {BK_B4, 4} };
        static const uint8_t GH_H1[] = {15}, GH_H2[] = {6, 14}, GH_H3[] = {14};
        static const uint8_t GH0[] = {7, 9, 15}, GH1[] = {7, 15}, GH2[] = {9, 14}, GH3[] = {3, 7, 11, 15}, GH4[] = {7, 9};
        static const struct { const uint8_t *a; uint8_t n; } GHH[4] = { {GH_H1, 0}, {GH_H1, 1}, {GH_H2, 2}, {GH_H3, 1} },
            GHO[5] = { {GH0, 3}, {GH1, 2}, {GH2, 2}, {GH3, 4}, {GH4, 2} };
        sb_rng_t r = SB_RL(L_DRUMS);
        b->dr.hat_k = (uint8_t)(st->hats_lo + sb_ri(&r, (uint32_t)(st->hats_hi - st->hats_lo + 1))); b->dr.hat_rot = (uint8_t)sb_ri(&r, 4);
        b->dr.perc_k = (uint8_t)(3 + sb_ri(&r, 4)); b->dr.perc_rot = (uint8_t)sb_ri(&r, 16);
        b->dr.perc_lane = st->perc[sb_rnext(&r) % st->nperc]; b->dr.clap_groove = (uint8_t)sb_rchance(&r, 600); b->dr.ride = (uint8_t)sb_rchance(&r, st->ride);
        b->dr.hat_mode = st->hm[sb_rnext(&r) % st->nhm]; b->dr.gallop = (uint8_t)sb_rchance(&r, st->gallop);
        if (G == GR_DNB) { i = (int)(sb_rnext(&r) % 5); b->dr.break_k = KD[i].a; b->dr.nbreak_k = KD[i].n; }
        else if (G == GR_HALF) { i = (int)(sb_rnext(&r) % 6); b->dr.break_k = KH[i].a; b->dr.nbreak_k = KH[i].n; }
        else { i = (int)(sb_rnext(&r) % 5); b->dr.break_k = KB[i].a; b->dr.nbreak_k = KB[i].n; }
        if (G == GR_HALF) { i = (int)(sb_rnext(&r) % 4); b->dr.ghost = GHH[i].a; b->dr.nghost = GHH[i].n; }
        else { i = (int)(sb_rnext(&r) % 5); b->dr.ghost = GHO[i].a; b->dr.nghost = GHO[i].n; }
        b->dr.four = (uint8_t)sb_rchance(&r, 500);
    }
    if (!SB_KEEP(L_BASS)) {
        sb_rng_t r = SB_RL(L_BASS); int kind = b->bass_kind, two = kind == SBBK_WOBBLE || kind == SBBK_RIDDIM || kind == SBBK_GROWL;
        sb_bstep_t *s = b->bass;
        b->bass_n = (uint8_t)(two ? 32 : 16);
        for (i = 0; i < 32; i++) s[i].on = 0;
        if (kind == SBBK_OFFBEAT) { for (i = 2; i < 16; i += 4) s[i] = sb_bs(0, 0, 1, 100); }
        else if (kind == SBBK_ROLLING) { for (i = 0; i < 16; i++) if (i % 4) { int d = (i % 4 == 3 && sb_rchance(&r, 250)) ? 4 : 0; s[i] = sb_bs(d, 0, 1, i % 4 == 1 ? 110 : 90); } }
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
        else if (two) {                                  /* dubstep: a 2-bar riff of hits, each its own wobble rate */
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
            for (j = 0; j < 16; j++) s[16 + j] = s[j];           /* the answer: bar 2 repeats bar 1, its end changed */
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
    if (!SB_KEEP(L_ARP)) {
        static const uint8_t RATE[4] = {1, 1, 1, 2}, GATE[3] = {0, 0, 1};
        sb_rng_t r = SB_RL(L_ARP);
        b->arp.order = (uint8_t)sb_ri(&r, 5); b->arp.rate = SB_PICK(&r, RATE); b->arp.oct = (uint8_t)(1 + sb_ri(&r, 2)); b->arp.gate = SB_PICK(&r, GATE);
        b->arp.skip = (int8_t)(sb_rchance(&r, 300) ? (int)sb_ri(&r, 16) : -1); b->arp.seed = sb_rnext(&r);
    }
    if (!SB_KEEP(L_STAB)) { sb_rng_t r = SB_RL(L_STAB); int kk = 3 + (int)sb_ri(&r, 3), rot = (int)sb_ri(&r, 16); for (i = 0; i < 16; i++) b->stab[i] = (uint8_t)(sb_euclid(kk, 16, rot, i) && i % 4 != 0 ? 110 : 0); }
    if (!SB_KEEP(L_PAD)) { sb_rng_t r = SB_RL(L_PAD); b->pad.inv = (uint8_t)sb_ri(&r, 3); b->pad.add9 = (uint8_t)sb_rchance(&r, 400); }
    b->pat_init = 1;
#undef SB_RL
#undef SB_KEEP
}

/* a phrase ends: the sb_motifs develop (a little, as MUTATION says), and the theme comes back now and then */
static void sb_mutate(sb_brain_t *b)
{
    int m = b->knobs[K_MUTATION]; sb_rng_t r = sb_rng(sb_hash(b->seed, 7000 + b->bar));
    if (!b->locks[L_HOOK] && sb_rchance(&r, m)) {
        sb_hstep_t *s = b->hook; int n = b->hook_n, j;
        if (sb_rchance(&r, 180)) { for (j = 0; j < n; j++) s[j] = b->theme[j]; }      /* the theme again */
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
    if (b->plan_n >= SB_PLAN_MAX) return;
    for (i = b->plan_n; i > at; i--) { b->plan_sec[i] = b->plan_sec[i - 1]; b->plan_len[i] = b->plan_len[i - 1]; }
    b->plan_sec[at] = (uint8_t)sec; b->plan_len[at] = (uint16_t)len; b->plan_n++;
}
static void sb_enter(sb_brain_t *b) { b->plan_at++; b->sec_bar = 0; b->phrase_bar = 0; }

/* a bar ends: every 8 bars the sb_motifs develop; DIVE and BREAK change the plan on the next bar; the next section,
 * and at the plan's end the next track, born of this one */
static void sb_next_bar(sb_brain_t *b)
{
    b->sec_bar++; b->phrase_bar++;
    if (b->phrase_bar == 8) { b->phrase_bar = 0; sb_mutate(b); }
    if (b->dive) {
        if (b->plan_sec[b->plan_at] == SEC_BUILD) {
            int len = b->plan_len[b->plan_at], lim = b->sec_bar + b->dive;
            len = len < lim ? len : lim; if (len < b->sec_bar + 1) len = b->sec_bar + 1;
            b->plan_len[b->plan_at] = (uint16_t)len;
            if (b->plan_at + 1 >= b->plan_n || b->plan_sec[b->plan_at + 1] != SEC_DROP) sb_plan_insert(b, b->plan_at + 1, SEC_DROP, 32);
            b->dive = 0;
        } else { sb_plan_insert(b, b->plan_at + 1, SEC_BUILD, b->dive); sb_plan_insert(b, b->plan_at + 2, SEC_DROP, 32); b->dive = 0; sb_enter(b); return; }
    }
    if (b->break_req) { b->break_req = 0; sb_plan_insert(b, b->plan_at + 1, SEC_BREAK, 8); sb_enter(b); return; }
    if (b->sec_bar >= b->plan_len[b->plan_at]) sb_enter(b);
    if (b->plan_at >= b->plan_n) {
        int pk = b->key, pb = b->bpm, style = b->style; uint32_t n = ++b->track_no;
        sb_rng_t r1 = sb_rng(sb_hash(b->seed, 31));
        if (!b->style_lock && sb_rchance(&r1, 250)) {
            int i, at = 0; sb_rng_t r2 = sb_rng(sb_hash(b->seed, 33));
            for (i = 0; i < SB_NSTYLES; i++) if (SB_STYLE_ORDER[i] == style) at = i;
            style = SB_STYLE_ORDER[sb_clamp(at + (sb_rchance(&r2, 500) ? 1 : -1), 0, SB_NSTYLES - 1)];
        }
        sb_new_track(b, sb_hash(b->seed, n), style, 1, pk, pb);
    }
}

/* -------------------------------------------------------------------------------------------- one sixteenth --- */
static int sb_note_of(const sb_brain_t *b, int chord, int deg, int oct, int base)
{
    int n = deg + chord, o = sb_fdiv(n, 7), d = ((n % 7) + 7) % 7;
    return base + b->key + 12 * (o + oct) + SB_SCALES[b->scale][d];
}
static void sb_add(sb_events_t *ev, int L, int n, int v, int len)
{
    sb_note_t *x;
    if (ev->nnotes >= SB_MAX_NOTES) return;
    x = &ev->notes[ev->nnotes++];
    x->layer = (uint8_t)L; x->note = (uint8_t)sb_clamp(n, 0, 127); x->vel = (uint8_t)sb_clamp(v, 1, 127); x->len = (uint16_t)(len ? len : 1);
}
#define SB_DRUM(lane, v) sb_add(ev, L_DRUMS, (lane), (v), 1)

void sb_step16(sb_brain_t *b, sb_events_t *ev)
{
    const sb_style_t *st = SB_ST(b); const int *dummy = 0;
    int sec = b->plan_sec[b->plan_at], e = sb_energy(b), step = b->step, barInPhrase = b->phrase_bar;
    uint32_t bar = b->bar;
    int secLen = b->plan_len[b->plan_at], barInSec = b->sec_bar;
    int lastBarOfBuild = sec == SEC_BUILD && barInSec == secLen - 1;
    sb_rng_t r = sb_rng(sb_hash(b->seed, bar * 16u + (uint32_t)step + 99991u));
    int chord, dens = b->knobs[K_DENSITY], G = st->groove, brk, live, intro0, kickOn, brkD, half, bb, bbOn, offbeat, ohOn, bassDrop, t, L;
    (void)dummy;
    ev->nnotes = 0; ev->has_wob = 0;
    for (L = 0; L < SB_NLAYERS; L++) ev->cut[L] = -1;
    if (step == 0 && b->bpm != b->bpm_target) b->bpm = (uint8_t)(b->bpm + (b->bpm_target > b->bpm ? 1 : -1));
    chord = b->prog[((bar - b->track_start_bar) / b->chord_bars) % b->nprog];
#define SB_NOTE(deg, oct, base) sb_note_of(b, chord, (deg), (oct), (base))

    /* ---- drums: the style's groove */
    brk = sec == SEC_BREAK; live = sec == SEC_DROP || sec == SEC_PEAK; intro0 = sec == SEC_INTRO && barInSec < 2 && b->track_no == 0;
    kickOn = !brk && !(lastBarOfBuild && step >= 8) && !(sec == SEC_BUILD && G != GR_FOUR && barInSec >= secLen - 2 && step >= 8);
    brkD = !brk || st->break_drums;
    half = G == GR_HALF; bb = half ? step == 8 : step == 4 || step == 12;
    if (kickOn) {
        if (G == GR_FOUR) {
            if (step % 4 == 0 && !(intro0 && step % 8)) SB_DRUM(SBD_KICK, 120);
            if (b->dr.gallop && live && step == 14 && barInPhrase % 2 == 1) SB_DRUM(SBD_KICK, 105);
        } else if (sec == SEC_BUILD) { if (step % (2 * barInSec < secLen ? 8 : 4) == 0) SB_DRUM(SBD_KICK, 110); }
        else {
            if (sb_has(b->dr.break_k, b->dr.nbreak_k, step) && !(intro0 && step)) SB_DRUM(SBD_KICK, step ? 108 : 122);
            if (G == GR_BREAKS && b->dr.four && live && step % 4 == 0 && !sb_has(b->dr.break_k, b->dr.nbreak_k, step)) SB_DRUM(SBD_KICK2, 100);
        }
    }
    if (live && step == 0 && (barInSec == 0 || (barInPhrase == 0 && sb_rchance(&r, 500)))) SB_DRUM(SBD_CRASH, barInSec == 0 ? 118 : 95);
    bbOn = sec != SEC_BUILD && !(sec == SEC_INTRO && barInSec < 4) && !brk;
    if (G == GR_FOUR) {
        if (bbOn && e > 330 && bb && (sec != SEC_GROOVE || b->dr.clap_groove || b->style != SB_TECHNO) && !(b->style == SB_PSY && !live && sec != SEC_GROOVE)) SB_DRUM(SBD_CLAP, 108);
        if (bbOn && b->style == SB_HARD && live && bb) SB_DRUM(SBD_SNARE, 96);
        if (bbOn && b->style == SB_ACID && e > 500 && (step == 7 || step == 15) && sb_rchance(&r, 500)) SB_DRUM(SBD_SNARE, 52);
    } else if (bbOn && e > 300) {
        if (bb) { SB_DRUM(SBD_SNARE, 120); if (half || live) SB_DRUM(SBD_CLAP, half ? 115 : 90); }
        if (sb_has(b->dr.ghost, b->dr.nghost, step) && sb_rchance(&r, 500 + 400 * dens / 1000)) SB_DRUM(SBD_SNARE2, 45 + 20 * e / 1000);
    }
    offbeat = step % 4 == 2;
    ohOn = brkD && e > 450 && !brk && (G == GR_FOUR || (G != GR_HALF && live && step == 14) || (half && live && (step == 6 || step == 14) && b->dr.four));
    if (ohOn && (G != GR_FOUR || offbeat)) SB_DRUM(SBD_OPEN, (G == GR_FOUR ? 92 : 80) + 20 * e / 1000);
    if (e > 200 && brkD && !(ohOn && G == GR_FOUR && offbeat)) {
        int m = b->dr.hat_mode, acc = offbeat ? 1000 : step % 2 == 0 ? 820 : 620, hit;
        hit = m == SBHM_16 ? 1 : m == SBHM_OFF8 ? offbeat : m == SBHM_8 ? step % 2 == 0 : sb_euclid((b->dr.hat_k * (600000 + 800 * dens) + 500000) / 1000000, 16, b->dr.hat_rot, step);
        if (G != GR_FOUR && step % 2 == 1 && e > 600 && !hit) hit = sb_rchance(&r, 350 * dens / 1000);
        if (hit && (m != SBHM_16 || dens > 200 || step % 2 == 0)) { int v = 58 + 46 * acc * (600 + 400 * e / 1000) / 1000000; if (brk) v = v * 8 / 10; SB_DRUM(SBD_HAT, v); }
    }
    if (b->dr.ride && live && (G == GR_DNB ? step % 2 == 0 : half ? step % 4 == 0 : offbeat)) SB_DRUM(SBD_RIDE, G == GR_DNB ? 62 + (step % 4 ? 0 : 14) : 72);
    if ((e > 550 || (brk && st->break_drums)) && brkD && sb_euclid(b->dr.perc_k, 16, b->dr.perc_rot, step) && sb_rchance(&r, 600 + 400 * dens / 1000)) SB_DRUM(b->dr.perc_lane, 78 + 30 * e / 1000);
    if (b->style == SB_PSY && e > 600 && !brk && step % 2 == 1 && sb_rchance(&r, 300 + 500 * dens / 1000)) SB_DRUM(SBD_SHAKE, 50 + 20 * e / 1000);
    if ((sec == SEC_GROOVE || live) && barInPhrase == 7 && step >= 12) {
        sb_rng_t rf = sb_rng(sb_hash(b->seed, bar * 3u + 5u));
        if (sb_rchance(&rf, 350 + 400 * dens / 1000)) {
            sb_rng_t rk = sb_rng(sb_hash(b->seed, bar)); int F = (int)sb_ri(&rk, 3);
            if (b->style == SB_PSY) SB_DRUM(step % 2 ? SBD_TOMH : SBD_TOML, 90 + 6 * (step - 12));
            else if (G == GR_DNB || G == GR_BREAKS) SB_DRUM(step == 15 ? SBD_SNARE : SBD_SNARE2, 80 + 10 * (step - 12));
            else if (half) { if (step != 13) SB_DRUM(F ? SBD_SNARE : SBD_TOMH, 90 + 8 * (step - 12)); }
            else SB_DRUM(F == 0 ? SBD_SNARE : F == 1 ? (step < 14 ? SBD_TOMH : SBD_TOML) : SBD_CLAP, 80 + 8 * (step - 12));
        }
    }
    if (sec == SEC_BUILD && !(half && lastBarOfBuild && step >= 8)) {
        int every = 4 * barInSec < secLen ? 4 : 2 * barInSec < secLen ? 2 : 1;
        if (step % every == 0) SB_DRUM(st->roll, 60 + 60 * barInSec / secLen);
        if (4 * barInSec >= 3 * secLen && step % 2 == 1) SB_DRUM(st->roll, 70 + 50 * barInSec / secLen);
    }
    /* ---- bass, its wobble, the sub */
    {
        int bassOn = !brk && sec != SEC_INTRO && !lastBarOfBuild && !(sec == SEC_BUILD && st->sub && barInSec < secLen - 2 && b->bass_kind != SBBK_ROLLER);
        if (b->has_alt && live && step == 0 && barInSec % 4 == 0) {
            int a = (barInSec / 4) % 3;
            if (b->bass_alt[a] != b->snd[L_BASS]) { b->snd[L_BASS] = b->bass_alt[a]; b->sounds_changed = 1; }
        }
        if (bassOn && e > 300) {
            const sb_bstep_t *s = &b->bass[(b->bass_n > 16 ? (int)(bar % 2) * 16 : 0) + step];
            if (s->on && !(sec == SEC_BUILD && b->has_alt)) {
                sb_add(ev, L_BASS, SB_NOTE(s->d, s->o, 36), s->v, s->slide ? s->len + 1 : s->len);
                if (s->has_wob) { ev->has_wob = 1; ev->wob = s->wob; }
                if (st->sub) { int sn = SB_NOTE(s->d, 0, 24); while (sn > 40) sn -= 12; while (sn < 28) sn += 12; sb_add(ev, L_SUB, sn, 110 * st->sub / 1000, s->len); }
            }
        }
    }
    /* ---- arp */
    bassDrop = st->drop_bass && live;
    if (!bassDrop && b->layer_on[0] && (e > 550 || (brk && st->layers[0] > 900)) && sec != SEC_INTRO) {
        if (step % b->arp.rate == 0 && step != b->arp.skip) {
            static const int8_t TONES[4] = {0, 2, 4, 7};
            int i = step / b->arp.rate, span = 4 * b->arp.oct, j = i % span;
            if (b->arp.order == 1) j = span - 1 - j;
            else if (b->arp.order == 2) { int q = i % (2 * span - 2); j = q < span ? q : 2 * span - 2 - q; }
            else if (b->arp.order == 3) { sb_rng_t ra = sb_rng(sb_hash(b->arp.seed, bar * 16u + (uint32_t)step)); j = (int)sb_ri(&ra, (uint32_t)span); }
            else if (b->arp.order == 4) j = (i * 2) % span;
            sb_add(ev, L_ARP, SB_NOTE(TONES[j % 4], 1 + j / 4, 48), 80 + 30 * e / 1000, b->arp.gate ? b->arp.rate : 1);
        }
    }
    /* ---- the hook */
    if (!bassDrop && b->layer_on[1] && (live || brk || (sec == SEC_GROOVE && barInSec >= 4 && e > 500))) {
        const sb_hstep_t *s = &b->hook[(barInPhrase * 16 + step) % b->hook_n];
        if (s->on) sb_add(ev, L_HOOK, SB_NOTE(s->d, s->o, 48), s->v * (750 + 250 * e / 1000) / 1000, s->len);
    }
    /* ---- the pad */
    if ((!bassDrop && b->layer_on[2] && sec != SEC_BUILD && (e < 800 || st->layers[2] > 900)) || brk || sec == SEC_INTRO || sec == SEC_OUTRO) {
        if (step == 0 && (bar - b->track_start_bar) % b->chord_bars == 0) {
            static const int8_t P3[4] = {0, 2, 4, 8}; int len = 16 * b->chord_bars - 1, n = b->pad.add9 ? 4 : 3, q;
            for (q = 0; q < n; q++) { int d = P3[q]; sb_add(ev, L_PAD, SB_NOTE(d + (b->pad.inv == 1 && d == 0 ? 7 : 0), 0, 48), 70 + 20 * e / 1000, len); }
        }
    }
    /* ---- stabs */
    if (!bassDrop && b->layer_on[3] && live && b->stab[step]) { sb_add(ev, L_STAB, SB_NOTE(0, 0, 60), 100, 1); sb_add(ev, L_STAB, SB_NOTE(2, 0, 60), 100, 1); sb_add(ev, L_STAB, SB_NOTE(4, 0, 60), 100, 1); }
    /* ---- fx: the riser */
    if (sec == SEC_BUILD && barInSec == 0 && step == 0) sb_add(ev, L_FX, 60 + b->key, 110, secLen * 16 - 1);
    /* ---- the filters, the master, the duck */
    t = sec == SEC_BUILD ? (barInSec * 16 + step) * 1000 / (secLen * 16) : 0;
    if (!st->sub) ev->cut[L_BASS] = (int16_t)(350 + 500 * e / 1000);
    ev->cut[L_ARP] = (int16_t)(300 + 600 * e / 1000); ev->cut[L_HOOK] = (int16_t)(450 + 500 * e / 1000);
    ev->cut[L_PAD] = (int16_t)(250 + 500 * e * (1000 - b->knobs[K_TENSION] / 2) / 1000000); ev->cut[L_FX] = (int16_t)(100 + 850 * t / 1000);
    ev->hp = (int16_t)(sec == SEC_BUILD ? sb_clamp((t - 500) * 16 / 10, 0, 100000) : 0);
    ev->duck = (int16_t)(550 + 350 * e / 1000);
    { int ee = e * 1000 / 900; ev->gain = (int16_t)(brk ? 580 : 600 + 400 * (ee < 1000 ? ee : 1000) / 1000); }
    ev->verb = (int16_t)(brk ? 600 : sec == SEC_BUILD ? 350 + 400 * t / 1000 : 250);
    ev->bar = bar; ev->step = (uint8_t)step; ev->sec = (uint8_t)sec; ev->chord = (uint8_t)chord; ev->key = b->key; ev->scale = b->scale; ev->e = (int16_t)e; ev->bpm = b->bpm;
    ev->sec_bar = (uint16_t)barInSec; ev->sec_len = (uint16_t)secLen; ev->plan_at = b->plan_at; ev->plan_n = b->plan_n;
    /* the clock */
    b->step++;
    if (b->step == 16) { b->step = 0; b->bar++; sb_next_bar(b); }
#undef SB_NOTE
}

void sb_set_seed(sb_brain_t *b, uint32_t seed, int style) { b->track_no = 0; sb_new_track(b, seed, style < 0 ? b->style : style, 0, 0, 0); }
void sb_reroll(sb_brain_t *b, int L) { b->rerolls[L]++; b->locks[L] = 0; sb_motifs(b); sb_pick_sounds(b); }
void sb_lock(sb_brain_t *b, int L, int on) { b->locks[L] = (uint8_t)(on ? 1 : 0); }
void sb_knob(sb_brain_t *b, int k, int v) { b->knobs[k] = (int16_t)sb_clamp(v, 0, 1000); if (k == K_DENSITY) sb_motifs(b); }

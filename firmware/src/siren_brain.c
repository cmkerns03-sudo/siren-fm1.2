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

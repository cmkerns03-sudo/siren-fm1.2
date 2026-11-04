/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (ABYSS) */
/* ABYSS, the brain: ambient that writes itself. A seed makes a set of pieces; each piece a world's grammar of sections
 * (the dawn, the theme, the rise, the swell, a breath, the peak, the fall, the tail; COSMIC its pulse and its orbit;
 * DARK its void and its decay), a harmony (a loop of chords from the music it comes from), a theme, and the moment it
 * is all for: the climax, where at least three of the things that give people chills come together (Sloboda 1991,
 * Grewe 2007, Guhn 2007, Bannister & Eerola 2018: a crescendo, a breath of silence before it, a new voice entering,
 * an unexpected chord, the register opening at both ends, the filter opening, the theme coming back augmented).
 *
 * The worlds, from the research (tools/abyss/RESEARCH.md):
 *   CINEMATIC  56-72 BPM, a chord a bar or two; Richter's ground bass loops (On the Nature of Daylight i v6 III6 VI
 *              VII), Zimmer's (Interstellar VI7 v6 i over a pedal; One Day i VI III VIIsus4), aeolian (dorian's
 *              lift); a theme of 2-4 note cells, stepwise, a leap answered by a step back, appoggiaturas on the chord
 *              changes, in the voice's register; layers entering from the low end up; the climax at 65-80 %; a felt
 *              piano repeating a chord tone with octave jumps (Arnalds' Stratus); tape hiss; sometimes a clock's tick.
 *   COSMIC     beatless (half), a Berlin sequencer (Tangerine Dream, Roach) or a slow beat (Carbon Based Lifeforms,
 *              86-100 BPM, entering late, out before the end); lydian, major, sus chords, a chord every 20-60 s;
 *              root + fifth drones, a sub at 40-65 Hz; the lift: the key moves a chromatic mediant and a new voice
 *              comes in; wind; the pitch drifting a few cents (Phaedra's Moog).
 *   DARK       no pulse; drones on root and octave, a semitone or a tritone away, changing every minute or two; the
 *              sub at 27-45 Hz swelling slowly; rough clusters (a minor second in the middle register beats at 30-150
 *              Hz: alarm, Arnal 2015) only at the peak; metal struck far away; one looming crescendo of minutes, then
 *              a void of silence and the decay; or Basinski's disintegrating loop: a pastoral phrase played again and
 *              again, its notes' ends falling away first, then the notes, faster and faster.
 *
 * Everything is drawn from seeded streams (ab_hash of the seed, the piece, the bar), so a seed plays the same set. */
#include "abyss_brain.h"

/* ------------------------------------------------------------------------------------------------- the dice --- */
typedef struct { uint32_t s; } ab_rng_t;
static ab_rng_t ab_rng(uint32_t seed) { ab_rng_t r; r.s = seed ? seed : 0x9E3779B9u; return r; }
static uint32_t ab_rnext(ab_rng_t *r) { uint32_t s = r->s; s ^= s << 13; s ^= s >> 17; s ^= s << 5; r->s = s; return s; }
static uint32_t ab_ri(ab_rng_t *r, uint32_t n) { return ab_rnext(r) % n; }
static int ab_chance(ab_rng_t *r, int pm) { return (int)(ab_rnext(r) % 1000u) < pm; }
static int32_t ab_range(ab_rng_t *r, int32_t lo, int32_t hi) { return lo + (int32_t)ab_ri(r, (uint32_t)(hi - lo + 1)); }
#define AB_PICK(r, a) ((a)[ab_rnext(r) % (sizeof(a) / sizeof((a)[0]))])
static uint32_t ab_hash(uint32_t a, uint32_t b)
{
    uint32_t h = a ^ ((b + 0x7F4A7C15u) * 0x85EBCA6Bu);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static int32_t ab_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static int32_t ab_abs(int32_t x) { return x < 0 ? -x : x; }
static int32_t ab_mod(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }
static const char AB_B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
void ab_code(uint32_t seed, char out[9])
{
    char o[7]; uint32_t s = seed; int i;
    for (i = 6; i >= 0; i--) { o[i] = AB_B32[s & 31u]; s >>= 5; }
    out[0] = o[0]; out[1] = o[1]; out[2] = o[2]; out[3] = '-'; out[4] = o[3]; out[5] = o[4]; out[6] = o[5]; out[7] = o[6]; out[8] = 0;
}
uint32_t ab_uncode(const char *c)
{
    uint32_t s = 0;
    for (; *c; c++) {
        char ch = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c; int v;
        for (v = 0; v < 32 && AB_B32[v] != ch; v++) {}
        if (v < 32) s = s * 32u + (uint32_t)v;
    }
    return s;
}

/* ------------------------------------------------------------------------------------------------ the music --- */
const char *const AB_WORLD_NAMES[AB_NWORLDS] = {"CINEMATIC", "COSMIC", "DARK"};
const char *const AB_SEC_NAMES[AB_NSECS] = {"DAWN", "THEME", "RISE", "SWELL", "BREATH", "PEAK", "FALL", "TAIL", "PULSE", "ORBIT", "VOID", "DECAY"};
const char *const AB_MODE_NAMES[7] = {"AEOLIAN", "DORIAN", "PHRYGIAN", "LYDIAN", "IONIAN", "MIXOLYDIAN", "LOCRIAN"};
enum { AM_AEOLIAN, AM_DORIAN, AM_PHRYGIAN, AM_LYDIAN, AM_IONIAN, AM_MIXO, AM_LOCRIAN };
static const uint8_t AB_MODES[7][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11},
                                       {0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}};
/* the chords: their intervals (the first is the root) */
enum { AQ_MIN, AQ_MAJ, AQ_M9, AQ_A9, AQ_MAJ7, AQ_SUS2, AQ_SUS4, AQ_M7, AQ_FIFTH, AQ_CLUS, AQ_TRI, AQ_OCT, AQ_N };
static const int8_t AB_Q[AQ_N][4] = {{0, 3, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, 14}, {0, 4, 7, 14}, {0, 4, 7, 11}, {0, 2, 7, -1},
                                     {0, 5, 7, -1}, {0, 3, 7, 10}, {0, 7, 12, -1}, {0, 1, 12, -1}, {0, 6, 12, -1}, {0, 12, -1, -1}};
static int ab_qn(int q) { int n = 0; while (n < 4 && AB_Q[q][n] >= 0) n++; return n; }

/* the harmonies, each world's: a loop of chords (root over the key, quality, inversion), its mode, a pedal (the
 * drone's note over the key, -1: the chord's root), how light it is (per mille: the LIGHT knob leans to it) */
typedef struct { uint8_t mode, n; int8_t pedal; int16_t light; ab_chord_t c[6]; } ab_prog_t;
static const ab_prog_t AB_CINEMA[] = {
    {AM_AEOLIAN, 5, 0, 300, {{0, AQ_MIN, 0}, {7, AQ_MIN, 1}, {3, AQ_MAJ, 1}, {8, AQ_MAJ, 0}, {10, AQ_MAJ, 0}}},   /* Daylight */
    {AM_AEOLIAN, 4, 7, 400, {{8, AQ_MAJ7, 0}, {7, AQ_MIN, 1}, {0, AQ_MIN, 0}, {7, AQ_MIN, 2}}},                 /* Interstellar */
    {AM_AEOLIAN, 4, 0, 650, {{8, AQ_MAJ, 0}, {10, AQ_MAJ, 0}, {0, AQ_M9, 0}, {10, AQ_SUS4, 0}}},               /* No Time for Caution */
    {AM_AEOLIAN, 5, -1, 450, {{0, AQ_MIN, 0}, {7, AQ_MIN, 0}, {8, AQ_A9, 0}, {3, AQ_A9, 0}, {5, AQ_M9, 0}}},   /* Dona Nobis Pacem */
    {AM_AEOLIAN, 4, 0, 500, {{0, AQ_MIN, 0}, {8, AQ_MAJ, 0}, {3, AQ_MAJ, 0}, {10, AQ_SUS4, 0}}},               /* One Day */
    {AM_AEOLIAN, 4, 0, 150, {{0, AQ_MIN, 0}, {7, AQ_MIN, 1}, {5, AQ_MIN, 1}, {7, AQ_MAJ, 0}}},                 /* the lament */
    {AM_AEOLIAN, 4, 0, 350, {{0, AQ_M9, 0}, {5, AQ_MIN, 0}, {8, AQ_MAJ7, 0}, {0, AQ_MIN, 0}}},                 /* plagal */
    {AM_DORIAN, 4, 0, 700, {{0, AQ_MIN, 0}, {5, AQ_MAJ, 0}, {0, AQ_M9, 0}, {10, AQ_MAJ, 0}}},                  /* dorian's lift */
    {AM_AEOLIAN, 4, -1, 750, {{8, AQ_MAJ, 0}, {0, AQ_MIN, 0}, {10, AQ_MAJ, 0}, {3, AQ_A9, 0}}},                /* hope */
};
static const ab_prog_t AB_COSMOS[] = {
    {AM_LYDIAN, 2, 0, 800, {{0, AQ_A9, 0}, {2, AQ_MAJ, 0}}},                                                    /* I II */
    {AM_LYDIAN, 4, 0, 850, {{0, AQ_MAJ7, 0}, {2, AQ_MAJ, 0}, {7, AQ_MAJ, 1}, {0, AQ_A9, 0}}},
    {AM_IONIAN, 4, -1, 750, {{0, AQ_SUS2, 0}, {5, AQ_A9, 0}, {9, AQ_MIN, 0}, {5, AQ_MAJ7, 0}}},
    {AM_MIXO, 4, 0, 650, {{0, AQ_MAJ, 0}, {10, AQ_MAJ, 0}, {5, AQ_A9, 0}, {0, AQ_SUS2, 0}}},
    {AM_AEOLIAN, 4, 0, 350, {{0, AQ_M9, 0}, {8, AQ_MAJ7, 0}, {3, AQ_A9, 0}, {10, AQ_SUS2, 0}}},                /* Carbon Based */
    {AM_DORIAN, 2, 0, 450, {{0, AQ_M9, 0}, {5, AQ_A9, 0}}},
    {AM_IONIAN, 4, 0, 700, {{0, AQ_SUS2, 0}, {7, AQ_SUS4, 0}, {5, AQ_A9, 0}, {0, AQ_MAJ, 0}}},
    {AM_IONIAN, 4, 0, 600, {{0, AQ_A9, 0}, {8, AQ_MAJ, 0}, {0, AQ_A9, 0}, {5, AQ_A9, 0}}},                     /* the mediant */
};
static const ab_prog_t AB_DARK[] = {
    {AM_PHRYGIAN, 4, 0, 300, {{0, AQ_FIFTH, 0}, {0, AQ_MIN, 0}, {1, AQ_MAJ, 0}, {0, AQ_FIFTH, 0}}},
    {AM_LOCRIAN, 4, 0, 100, {{0, AQ_FIFTH, 0}, {6, AQ_TRI, 0}, {0, AQ_MIN, 0}, {1, AQ_CLUS, 0}}},
    {AM_LOCRIAN, 4, 0, 50, {{0, AQ_CLUS, 0}, {6, AQ_FIFTH, 0}, {0, AQ_MIN, 0}, {1, AQ_MAJ, 0}}},
    {AM_AEOLIAN, 4, 0, 450, {{0, AQ_MIN, 0}, {8, AQ_MAJ, 0}, {0, AQ_MIN, 0}, {1, AQ_MAJ, 0}}},
    {AM_PHRYGIAN, 4, 0, 200, {{0, AQ_OCT, 0}, {0, AQ_MIN, 0}, {11, AQ_OCT, 0}, {0, AQ_CLUS, 0}}},
};
static const ab_prog_t AB_PASTORAL[] = {                 /* (DARK, the disintegrating loop: consonant; the decay darkens it) */
    {AM_IONIAN, 2, 0, 700, {{0, AQ_MAJ, 0}, {5, AQ_A9, 0}}},
    {AM_LYDIAN, 2, 0, 800, {{0, AQ_SUS2, 0}, {7, AQ_SUS4, 0}}},
    {AM_AEOLIAN, 2, 0, 400, {{0, AQ_M9, 0}, {8, AQ_MAJ7, 0}}},
};
#define AB_NP(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* each section's energy, from its first bar to its last (per mille) */
static const int16_t AB_SEC_E[AB_NSECS][2] = {{80, 200}, {250, 380}, {420, 620}, {620, 900}, {40, 40}, {1000, 900}, {650, 300},
                                              {220, 0}, {400, 620}, {650, 820}, {0, 0}, {500, 150}};
/* the layers each section plays (bits AL_*), each world's */
#define AB_B(L) (1u << (L))
#define MDR AB_B(AL_DRUMS)
#define MDN AB_B(AL_DRONE)
#define MPD AB_B(AL_PAD)
#define MLO AB_B(AL_LOW)
#define MME AB_B(AL_MELO)
#define MBE AB_B(AL_BELL)
#define MSW AB_B(AL_SWELL)
#define MSU AB_B(AL_SUB)
static const uint16_t AB_MASK[AB_NWORLDS][AB_NSECS] = {
    /* DAWN THEME RISE SWELL BREATH PEAK FALL TAIL PULSE ORBIT VOID DECAY */
    {MDN | MPD, MDN | MPD | MLO | MBE | MME, MDN | MPD | MLO | MME | MBE | MSU, MDN | MPD | MLO | MME | MBE | MSU, MDN, MDN | MPD | MLO | MME | MBE | MSW | MSU,
     MDN | MPD | MLO | MME | MSU, MDN | MPD, MDN | MPD | MLO | MBE, MDN | MPD | MLO | MBE | MME, 0, MDN | MPD},
    {MDN | MPD, MDN | MPD | MBE | MME, MDN | MPD | MBE | MME | MSU, MDN | MPD | MLO | MBE | MME | MSU, MDN, MDN | MPD | MLO | MBE | MME | MSW | MSU | MDR,
     MDN | MPD | MME | MSU, MDN | MPD, MDN | MPD | MLO | MBE | MSU, MDN | MPD | MLO | MBE | MME | MSU | MDR, 0, MDN | MPD},
    {MDN | MSU, MDN | MPD | MLO | MSU | MBE, MDN | MPD | MLO | MSU | MBE | MME, MDN | MPD | MLO | MSU | MBE | MME, 0, MDN | MPD | MLO | MSU | MBE | MME | MSW,
     MDN | MPD | MSU, MDN, MDN | MPD | MLO | MSU, MDN | MPD | MLO | MSU, 0, MDN | MPD | MSU | MME},
};
/* each layer's octave (its C) */
static const uint8_t AB_BASE[AB_NLAYERS] = {0, 36, 48, 36, 60, 60, 48, 24};

/* --------------------------------------------------------------------------------------------------- a piece --- */
static void ab_plan_add(ab_brain_t *b, int sec, int bars)
{
    int k = b->knobs[AK_TIME];                       /* TIME: slower (longer sections) .. faster */
    bars = bars * (1500 - k) / 1000;
    if (sec == AS_BREATH || sec == AS_VOID) bars = bars < 1 ? 1 : bars;
    if (b->plan_n >= AB_PLAN_MAX) return;
    if (b->plan_n && b->plan_sec[b->plan_n - 1] == sec) { b->plan_len[b->plan_n - 1] = (uint16_t)(b->plan_len[b->plan_n - 1] + bars); return; }
    b->plan_sec[b->plan_n] = (uint8_t)sec; b->plan_len[b->plan_n] = (uint16_t)(bars < 1 ? 1 : bars); b->plan_n++;
}
static const ab_prog_t *ab_pick_prog(ab_brain_t *b, ab_rng_t *r, int *n_out)
{
    const ab_prog_t *t; int n, i, best = 0; int32_t wsum = 0, w[12], x;
    if (b->world == AW_CINEMA) t = AB_CINEMA, n = AB_NP(AB_CINEMA);
    else if (b->world == AW_COSMOS) t = AB_COSMOS, n = AB_NP(AB_COSMOS);
    else if (b->kind == AP_DECAYLOOP) t = AB_PASTORAL, n = AB_NP(AB_PASTORAL);
    else t = AB_DARK, n = AB_NP(AB_DARK);
    for (i = 0; i < n; i++) { w[i] = 1100 - ab_abs(t[i].light - b->knobs[AK_LIGHT]); wsum += w[i]; }   /* (LIGHT leans) */
    x = (int32_t)ab_ri(r, (uint32_t)wsum);
    for (i = 0; i < n; i++) { if (x < w[i]) { best = i; break; } x -= w[i]; }
    *n_out = n;
    return &t[best];
}
static void ab_harmony(ab_brain_t *b, ab_rng_t *r)
{
    int n, i; const ab_prog_t *p = ab_pick_prog(b, r, &n);
    b->mode = p->mode; b->nprog = p->n; b->pedal = p->pedal;
    for (i = 0; i < p->n; i++) b->prog[i] = p->c[i];
    /* the climax's surprise: CINEMATIC VI major or IV major (dorian), COSMIC the flat sixth, DARK the flat second */
    if (b->world == AW_CINEMA) { static const int8_t SR[3] = {8, 5, 3}; b->surprise_root = AB_PICK(r, SR); b->surprise_q = b->surprise_root == 5 ? AQ_MAJ : AQ_A9; }
    else if (b->world == AW_COSMOS) { b->surprise_root = 8; b->surprise_q = AQ_MAJ7; }
    else { b->surprise_root = 1; b->surprise_q = AQ_CLUS; }
}
/* the theme: a cell of 2-4 notes, stepwise with one leap answered by a step back, repeated with a change, in 2 or 4
 * bars; notes of one to four beats; the last the longest */
static void ab_theme(ab_brain_t *b)
{
    ab_rng_t r = ab_rng(ab_hash(b->seed, b->piece_no * 977u + b->theme_ver * 31u + 5u));
    static const uint8_t DUR[6] = {4, 8, 8, 12, 16, 8};
    int cell = ab_range(&r, 2, 4), total = 0, i, d = AB_PICK(&r, ((const int8_t[3]){0, 2, 4})), leap = ab_ri(&r, (uint32_t)cell), lo = d, hi = d;
    int8_t cd[4]; uint8_t cl[4];
    b->theme_bars = (uint8_t)(ab_chance(&r, 550) ? 2 : 4);
    for (i = 0; i < cell; i++) {                     /* the cell */
        cd[i] = (int8_t)d; cl[i] = AB_PICK(&r, DUR);
        if (i == leap) d += ab_chance(&r, 500) ? ab_range(&r, 3, 5) : -ab_range(&r, 3, 4);
        else { int sg = ab_chance(&r, 550) ? -1 : 1, st = ab_chance(&r, 800) ? 1 : 2; d += sg * st; }   /* (draws in order: the C and the web alike) */
        if (i == leap + 1 && i < cell) d += cd[i] > cd[i - 1] ? -1 : 1;   /* (the step back) */
        d = ab_clamp(d, -2, 7);
    }
    b->theme_n = 0;
    while (total < b->theme_bars * 16 && b->theme_n < 16) {   /* the cell again and again, moved a step (or inverted) */
        int shift = b->theme_n == 0 ? 0 : AB_PICK(&r, ((const int8_t[4]){-1, 1, 2, 0})), inv = ab_chance(&r, 250);
        for (i = 0; i < cell && b->theme_n < 16 && total < b->theme_bars * 16; i++) {
            ab_tnote_t *t = &b->theme[b->theme_n++];
            int dd = inv && i ? cd[0] - (cd[i] - cd[0]) : cd[i];
            t->d = (int8_t)ab_clamp(dd + shift, -2, 8); t->len = cl[i]; t->v = (uint8_t)(i == 0 ? 100 : 80 + ab_ri(&r, 20));
            if (t->d < lo) lo = t->d;
            if (t->d > hi) hi = t->d;
            total += t->len;
        }
    }
    if (b->theme_n) { int over = total - b->theme_bars * 16; ab_tnote_t *t = &b->theme[b->theme_n - 1]; t->len = (uint8_t)ab_clamp(t->len - over + 8, 4, 32); }
    (void)lo; (void)hi;
}
static void ab_patterns(ab_brain_t *b)
{
    ab_rng_t r = ab_rng(ab_hash(b->seed, b->piece_no * 131u + b->rerolls[AL_LOW] * 7u + b->rerolls[AL_BELL] * 13u + 77u));
    int i;
    static const uint8_t K[5] = {3, 5, 2, 3, 4};
    b->seq_n = (uint8_t)(ab_chance(&r, 500) ? 8 : 16);
    for (i = 0; i < b->seq_n; i++)                   /* the sequencer: chord tones (0 root, 1 fifth, 2 octave, 3 ninth), rests */
        b->seq[i] = (uint8_t)(i == 0 ? 0 : ab_chance(&r, 180) ? 0xFF : ab_chance(&r, 450) ? 0 : ab_ri(&r, 4));
    b->arp_rate = (uint8_t)(ab_chance(&r, 600) ? 2 : 1);
    b->bell_n = 8; b->bell_k = AB_PICK(&r, K); b->bell_rot = (uint8_t)ab_ri(&r, 8);
    b->loop_bars = (uint8_t)(ab_chance(&r, 600) ? 1 : 2);   /* (the decaying loop: 3-6 notes, a bar or two) */
    b->loop_n = (uint8_t)ab_range(&r, 3, 6);
    for (i = 0; i < b->loop_n; i++) b->loop[i] = (uint8_t)ab_ri(&r, 5);
}
static void ab_new_piece(ab_brain_t *b, int first)
{
    ab_rng_t r = ab_rng(ab_hash(b->seed, b->piece_no * 7919u + 3u));
    int prev_key = b->key, i, w;
    if (b->world_q != 0xFF) { b->world = b->world_q; b->world_q = 0xFF; }
    w = b->world;
    /* the kind */
    b->kind = AP_PLAIN;
    if (w == AW_COSMOS) { int x = (int)ab_ri(&r, 1000); b->kind = x < 500 ? AP_PLAIN : x < 800 ? AP_SEQ : AP_BEAT; }
    else if (w == AW_DARK) b->kind = ab_chance(&r, 250) ? AP_DECAYLOOP : AP_PLAIN;
    else b->kind = ab_chance(&r, 150) ? AP_TICK : AP_PLAIN;
    /* the key: the last one's, a fifth, the relative, or another */
    if (first) b->key = (uint8_t)ab_ri(&r, 12);
    else { int x = (int)ab_ri(&r, 100); b->key = (uint8_t)ab_mod(prev_key + (x < 30 ? 0 : x < 55 ? 7 : x < 80 ? (ab_chance(&r, 500) ? 3 : -3) : (int)ab_ri(&r, 12)), 12); }
    ab_harmony(b, &r);
    /* the tempo and the chords' pace */
    if (w == AW_CINEMA) { b->bpm = (uint8_t)ab_range(&r, 56, 72); b->chord_bars = (uint8_t)AB_PICK(&r, ((const uint8_t[6]){2, 2, 2, 1, 1, 4})); }
    else if (w == AW_COSMOS) {
        b->bpm = (uint8_t)(b->kind == AP_PLAIN ? ab_range(&r, 66, 80) : b->kind == AP_SEQ ? ab_range(&r, 82, 96) : ab_range(&r, 86, 100));
        b->chord_bars = (uint8_t)(b->kind == AP_PLAIN ? AB_PICK(&r, ((const uint8_t[3]){8, 12, 16})) : AB_PICK(&r, ((const uint8_t[3]){4, 8, 8})));
    } else { b->bpm = (uint8_t)ab_range(&r, 56, 64); b->chord_bars = (uint8_t)AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})); }
    if (b->kind == AP_DECAYLOOP) b->chord_bars = 255;   /* (the loop holds its chords: they live in the loop) */
    { int k = b->knobs[AK_TIME]; if (b->chord_bars != 255) b->chord_bars = (uint8_t)ab_clamp(b->chord_bars * (1500 - k) / 1000, 1, 64); }
    /* the plan, the world's grammar */
    b->plan_n = 0;
    if (w == AW_CINEMA) {
        ab_plan_add(b, AS_DAWN, AB_PICK(&r, ((const uint8_t[5]){6, 8, 8, 10, 12})));
        ab_plan_add(b, AS_THEME, AB_PICK(&r, ((const uint8_t[4]){16, 16, 24, 32})));
        if (ab_chance(&r, 300)) ab_plan_add(b, AS_RISE, 8);
        ab_plan_add(b, AS_RISE, AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})));
        if (ab_chance(&r, 200)) { ab_plan_add(b, AS_FALL, 8); ab_plan_add(b, AS_RISE, 16); }   /* (two waves) */
        ab_plan_add(b, AS_SWELL, AB_PICK(&r, ((const uint8_t[3]){8, 12, 16})));
        if (ab_chance(&r, 650)) ab_plan_add(b, AS_BREATH, ab_range(&r, 1, 2));
        ab_plan_add(b, AS_PEAK, AB_PICK(&r, ((const uint8_t[3]){8, 12, 16})));
        ab_plan_add(b, AS_FALL, AB_PICK(&r, ((const uint8_t[3]){8, 12, 16})));
        ab_plan_add(b, AS_TAIL, AB_PICK(&r, ((const uint8_t[3]){6, 8, 10})));
    } else if (w == AW_COSMOS) {
        ab_plan_add(b, AS_DAWN, AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})));
        ab_plan_add(b, AS_THEME, AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})));
        if (b->kind != AP_PLAIN) ab_plan_add(b, AS_PULSE, AB_PICK(&r, ((const uint8_t[3]){32, 48, 64})));
        if (b->kind == AP_BEAT) ab_plan_add(b, AS_ORBIT, AB_PICK(&r, ((const uint8_t[3]){32, 48, 64})));
        if (b->kind == AP_PLAIN) { ab_plan_add(b, AS_RISE, AB_PICK(&r, ((const uint8_t[2]){32, 48}))); ab_plan_add(b, AS_SWELL, AB_PICK(&r, ((const uint8_t[2]){16, 24}))); }
        if (ab_chance(&r, 300)) ab_plan_add(b, AS_BREATH, 1);
        ab_plan_add(b, AS_PEAK, AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})));
        ab_plan_add(b, AS_FALL, AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})));
        ab_plan_add(b, AS_TAIL, AB_PICK(&r, ((const uint8_t[3]){16, 24, 32})));
    } else if (b->kind == AP_DECAYLOOP) {
        ab_plan_add(b, AS_DAWN, AB_PICK(&r, ((const uint8_t[2]){8, 12})));
        ab_plan_add(b, AS_DECAY, AB_PICK(&r, ((const uint8_t[3]){96, 128, 160})));
        ab_plan_add(b, AS_TAIL, AB_PICK(&r, ((const uint8_t[2]){8, 12})));
    } else {
        ab_plan_add(b, AS_DAWN, AB_PICK(&r, ((const uint8_t[2]){16, 24})));
        ab_plan_add(b, AS_THEME, AB_PICK(&r, ((const uint8_t[3]){32, 48, 64})));
        ab_plan_add(b, AS_SWELL, AB_PICK(&r, ((const uint8_t[3]){32, 48, 64})));
        ab_plan_add(b, AS_PEAK, AB_PICK(&r, ((const uint8_t[3]){8, 12, 16})));
        ab_plan_add(b, AS_VOID, ab_range(&r, 2, 4));
        ab_plan_add(b, AS_DECAY, AB_PICK(&r, ((const uint8_t[3]){32, 48, 64})));
        ab_plan_add(b, AS_TAIL, AB_PICK(&r, ((const uint8_t[2]){16, 24})));
    }
    b->plan_at = 0; b->sec_bar = 0; b->piece_start = b->bar; b->lift = 0; b->hold = 0;
    b->theme_ver = 0; ab_theme(b); ab_patterns(b);
    b->chord_i = -1; b->last_vn = 0;
    /* the sounds: each layer one of the world's (kept if locked) */
    for (i = 0; i < AB_NLAYERS; i++) {
        int n = b->pal ? b->pal->ncur[w][i] : 0;
        if (i == AL_DRUMS) n = b->pal ? b->pal->nkits[w] : 0;
        if (b->locks[i] && b->snd[i] >= 0 && n) continue;
        b->snd[i] = (int16_t)(n ? (int)ab_ri(&r, (uint32_t)n) : -1);
    }
    b->sounds_changed = 1;
}

void ab_init(ab_brain_t *b, const ab_palette_t *pal)
{
    int i;
    for (i = 0; i < (int)sizeof *b; i++) ((uint8_t *)b)[i] = 0;
    b->pal = pal; b->world_q = 0xFF;
    for (i = 0; i < AB_NKNOBS; i++) b->knobs[i] = 500;
    b->knobs[AK_TENSION] = 400;
    for (i = 0; i < AB_NLAYERS; i++) b->snd[i] = -1;
    ab_set_seed(b, 0x0AB1550u, AW_CINEMA);
}
void ab_set_seed(ab_brain_t *b, uint32_t seed, int world)
{
    int i;
    b->seed = seed;
    if (world >= 0 && world < AB_NWORLDS) b->world = (uint8_t)world;
    b->world_q = 0xFF; b->piece_no = 0; b->bar = 0; b->step = 0;
    b->rise_req = b->calm_req = b->breath_req = b->void_req = b->fade = b->faded = b->lift_req = 0;
    for (i = 0; i < AB_NLAYERS; i++) b->g[i] = 0;
    b->drift = 0; b->drift_v = 0;
    ab_new_piece(b, 1);
}
void ab_next_piece(ab_brain_t *b) { b->piece_no++; ab_new_piece(b, 0); }
void ab_knob(ab_brain_t *b, int k, int v) { if (k >= 0 && k < AB_NKNOBS) b->knobs[k] = (int16_t)ab_clamp(v, 0, 1000); }
void ab_reroll(ab_brain_t *b, int L)
{
    if (L < 0 || L >= AB_NLAYERS) return;
    b->rerolls[L]++;
    { ab_rng_t r = ab_rng(ab_hash(b->seed, b->piece_no * 31u + (uint32_t)L * 1009u + b->rerolls[L] * 7u));
      int n = b->pal ? (L == AL_DRUMS ? b->pal->nkits[b->world] : b->pal->ncur[b->world][L]) : 0;
      if (n > 1) { b->snd[L] = (int16_t)((b->snd[L] + 1 + (int)ab_ri(&r, (uint32_t)(n - 1))) % n); b->sounds_changed = 1; } }
    if (L == AL_MELO) { b->theme_ver++; ab_theme(b); }
    if (L == AL_LOW || L == AL_BELL) ab_patterns(b);
}
void ab_lock(ab_brain_t *b, int L, int on) { if (L >= 0 && L < AB_NLAYERS) b->locks[L] = (uint8_t)!!on; }

/* the plan moved to a section of this kind ahead (or one put in after this one) */
static void ab_jump(ab_brain_t *b, int sec, int insert_bars)
{
    int i;
    for (i = b->plan_at + 1; i < b->plan_n; i++)
        if (b->plan_sec[i] == sec) { b->plan_at = (uint8_t)(i - 1); b->sec_bar = b->plan_len[i - 1]; return; }
    if (insert_bars && b->plan_n < AB_PLAN_MAX) {            /* (none ahead: put one in, next) */
        for (i = b->plan_n; i > b->plan_at + 1; i--) { b->plan_sec[i] = b->plan_sec[i - 1]; b->plan_len[i] = b->plan_len[i - 1]; }
        b->plan_sec[b->plan_at + 1] = (uint8_t)sec; b->plan_len[b->plan_at + 1] = (uint16_t)insert_bars; b->plan_n++;
        b->sec_bar = b->plan_len[b->plan_at];
    }
}
static void ab_insert_next(ab_brain_t *b, int sec, int bars)
{
    int i;
    if (b->plan_n >= AB_PLAN_MAX) return;
    for (i = b->plan_n; i > b->plan_at + 1; i--) { b->plan_sec[i] = b->plan_sec[i - 1]; b->plan_len[i] = b->plan_len[i - 1]; }
    b->plan_sec[b->plan_at + 1] = (uint8_t)sec; b->plan_len[b->plan_at + 1] = (uint16_t)bars; b->plan_n++;
}
void ab_gesture(ab_brain_t *b, int g)
{
    switch (g) {
    case AG_RISE: b->rise_req = 1; break;
    case AG_PEAK: b->rise_req = 2; break;
    case AG_CALM: b->calm_req = 1; break;
    case AG_BREATH: b->breath_req = 1; break;
    case AG_LIFT: b->lift_req = 1; break;
    case AG_THEME: b->theme_ver++; ab_theme(b); break;
    case AG_HARMONY: { ab_rng_t r = ab_rng(ab_hash(b->seed, b->piece_no * 53u + b->bar)); ab_harmony(b, &r); b->chord_i = -1; break; }
    case AG_HOLD: b->hold = (uint8_t)!b->hold; break;
    case AG_NEXT: b->rise_req = 0; b->calm_req = 3; break;
    case AG_VOID: b->void_req = 1; break;
    case AG_FADE: b->fade = (uint8_t)!b->fade; if (!b->fade) b->faded = 0; break;
    default: break;
    }
}

/* ------------------------------------------------------------------------------------------------- the notes --- */
static void ab_add(ab_events_t *ev, int L, int n, int v, int len)
{
    ab_note_t *x;
    if (ev->nnotes >= AB_MAX_NOTES || len <= 0) return;
    { static const uint8_t TOP[AB_NLAYERS] = {127, 72, 84, 72, 86, 96, 88, 48};   /* (each layer's top: an octave down past it) */
      while (n > TOP[L]) n -= 12; }
    x = &ev->notes[ev->nnotes++];
    x->layer = (uint8_t)L; x->note = (uint8_t)ab_clamp(n, 0, 127); x->vel = (uint8_t)ab_clamp(v, 1, 127); x->len = (uint16_t)(len > 65535 ? 65535 : len);
}
static int ab_deg(const ab_brain_t *b, int d)       /* a scale degree (0 the tonic) in semitones */
{
    int o = d >= 0 ? d / 7 : -((6 - d) / 7);
    return 12 * o + AB_MODES[b->mode][d - 7 * o];
}
static int ab_is_tone(int pc, int root, int q)      /* a pitch class one of the chord's */
{
    int i;
    for (i = 0; i < ab_qn(q); i++) if (ab_mod(root + AB_Q[q][i], 12) == pc) return 1;
    return 0;
}
static int ab_near(int pc, int around)              /* the pitch class pc nearest a note */
{
    int n = around - ab_mod(around - pc, 12);
    if (around - n > 6) n += 12;
    return n;
}

void ab_step16(ab_brain_t *b, ab_events_t *ev)
{
    ab_rng_t r = ab_rng(ab_hash(b->seed, b->bar * 16u + b->step + 99991u));
    int w = b->world, step = b->step, sec, e, L, pb, i, root, q, inv, newchord, key, peak1, sec_len;
    uint32_t mask;
    ev->nnotes = 0; ev->frisson = 0;
    for (L = 0; L < AB_NLAYERS; L++) ev->cut[L] = -1;
    /* the bar's start: the hands' requests, then the section */
    if (step == 0) {
        if (b->calm_req == 3) { b->calm_req = 0; ab_next_piece(b); }
        if (b->rise_req == 1) { int s = b->plan_sec[b->plan_at]; b->rise_req = 0; if (s != AS_SWELL && s != AS_PEAK && s != AS_BREATH) ab_jump(b, AS_SWELL, 8); }
        if (b->rise_req == 2) { b->rise_req = 0; if (b->plan_sec[b->plan_at] != AS_PEAK) { ab_jump(b, AS_PEAK, 8); ab_insert_next(b, AS_BREATH, 1); } }
        if (b->calm_req == 1) { b->calm_req = 0; if (b->plan_sec[b->plan_at] != AS_FALL && b->plan_sec[b->plan_at] != AS_TAIL) ab_jump(b, AS_FALL, 8); }
        if (b->breath_req) { b->breath_req = 0; ab_insert_next(b, AS_BREATH, 1); b->sec_bar = b->plan_len[b->plan_at]; }
        if (b->void_req) { b->void_req = 0; ab_insert_next(b, AS_VOID, 2); b->sec_bar = b->plan_len[b->plan_at]; }
        if (b->fade && !b->faded && b->plan_sec[b->plan_at] != AS_TAIL) ab_jump(b, AS_TAIL, 8);
        /* a bar past the section's end: the next (HOLD: this one again); past the plan's end: the next piece */
        if (b->sec_bar >= b->plan_len[b->plan_at]) {
            if (b->hold && b->sec_bar < 0xFFFFu) b->sec_bar = 0;
            else {
                b->sec_bar = 0; b->plan_at++;
                if (b->plan_at >= b->plan_n) {
                    if (b->fade) { b->faded = 1; b->plan_at = (uint8_t)(b->plan_n - 1); b->sec_bar = b->plan_len[b->plan_at] - 1; }
                    else ab_next_piece(b);
                }
            }
        }
    }
    sec = b->plan_sec[b->plan_at]; sec_len = b->plan_len[b->plan_at];
    pb = (int)(b->bar - b->piece_start);
    /* the energy: the section's, across it; TENSION and DENSITY lean on it a little */
    e = AB_SEC_E[sec][0] + (AB_SEC_E[sec][1] - AB_SEC_E[sec][0]) * ((int)b->sec_bar * 16 + step) / (sec_len * 16);
    if (w == AW_DARK && sec == AS_THEME) e = 200 + 200 * ((int)b->sec_bar * 16 + step) / (sec_len * 16);
    if (w == AW_DARK && sec == AS_SWELL) e = 400 + 550 * ((int)b->sec_bar * 16 + step) / (sec_len * 16);
    if (b->kind == AP_DECAYLOOP && sec == AS_DECAY) e = 750 - 350 * ((int)b->sec_bar * 16 + step) / (sec_len * 16);
    if (b->kind == AP_DECAYLOOP && sec == AS_DAWN) e = 350;
    e = ab_clamp(e, 0, 1000);
    if (b->faded) e = 0;
    peak1 = sec == AS_PEAK && b->sec_bar == 0 && step == 0;
    if (peak1 || (sec == AS_ORBIT && b->sec_bar == 0 && step == 0) || (sec == AS_VOID && b->sec_bar == 0 && step == 0)) ev->frisson = 1;
    /* the lift (COSMIC's mediant, at its peak; or asked for) */
    if ((b->lift_req && step == 0 && pb % (b->chord_bars == 255 ? 1 : b->chord_bars) == 0) || (w == AW_COSMOS && peak1 && b->lift == 0 && ab_chance(&r, 600))) {
        static const int8_t LF[3] = {4, -4, 3};
        b->lift_req = 0; b->lift = (int8_t)(b->lift ? 0 : AB_PICK(&r, LF)); b->chord_i = -1;
        if (w == AW_COSMOS) ev->frisson = 1;
    }
    key = b->key + b->lift;
    /* the chord */
    newchord = 0;
    if (b->chord_bars == 255) { if (b->chord_i < 0) { b->chord_i = 0; newchord = 1; b->chord_at = b->bar; } }
    else if (step == 0 && (pb % b->chord_bars == 0 || b->chord_i < 0)) {
        b->chord_i = (int8_t)((pb / b->chord_bars) % b->nprog); newchord = 1; b->chord_at = b->bar;
    }
    if (b->chord_i < 0) b->chord_i = 0;
    root = b->prog[b->chord_i].root; q = b->prog[b->chord_i].q; inv = b->prog[b->chord_i].inv;
    if (sec == AS_PEAK && (int)b->sec_bar < (b->chord_bars == 255 ? 2 : b->chord_bars) && ab_chance(&r, 1000) && b->knobs[AK_TENSION] > 150) {
        root = b->surprise_root; q = b->surprise_q; inv = 0;   /* (the climax's unexpected chord: its first chord) */
    }
    if (sec == AS_PEAK && b->sec_bar == 0 && step == 0) newchord = 1;
    if (sec == AS_PEAK && (int)b->sec_bar == (b->chord_bars == 255 ? 2 : b->chord_bars) && step == 0) newchord = 1;
    /* the layers in the mix: toward the section's (BREATH and VOID at once; the peak back at once: subito) */
    mask = b->faded ? 0u : AB_MASK[w][sec];
    if (w == AW_CINEMA && b->kind == AP_TICK && sec >= AS_THEME && sec <= AS_FALL && sec != AS_BREATH) mask |= MDR;
    if (w == AW_COSMOS && b->kind != AP_BEAT) mask &= ~MDR;
    if (w == AW_COSMOS && b->kind == AP_PLAIN && sec != AS_SWELL && sec != AS_PEAK) mask &= ~MLO;
    if (w == AW_DARK && (sec == AS_PEAK || sec == AS_VOID || (sec == AS_SWELL && b->sec_bar * 3 >= sec_len * 2))) mask |= MDR;
    if (b->kind == AP_DECAYLOOP) mask = b->faded ? 0u : sec == AS_TAIL ? MPD : MPD | MDN | MSU;
    if (w == AW_CINEMA && sec == AS_THEME && (int)b->sec_bar * 2 < sec_len) mask &= ~MME;   /* (the theme waits) */
    if (w == AW_CINEMA && sec == AS_FALL && (int)b->sec_bar * 2 >= sec_len) mask &= ~(MME | MBE);
    {
        int fade = w == AW_CINEMA ? 4 : w == AW_COSMOS ? 8 : 12;
        for (L = 0; L < AB_NLAYERS; L++) {
            int tgt = (mask >> L) & 1u ? 1000 : 0, rate = 1000 / (fade * 16) + 1;
            if (L == AL_DRONE && sec == AS_BREATH) tgt = 250;
            if (sec == AS_BREATH || sec == AS_VOID) rate = 1000;
            if (peak1 && tgt) rate = 1000;
            if (L == AL_SWELL) rate = 1000 / (2 * 16) + 1;                 /* (the new voice: two bars) */
            b->g[L] = (int16_t)(b->g[L] < tgt ? ab_clamp(b->g[L] + rate, 0, tgt) : ab_clamp(b->g[L] - rate, tgt, 1000));
            ev->lvl[L] = (int16_t)(b->g[L] * (300 + 700 * e / 1000) / 1000);
            b->layer_g[L] = (uint8_t)(b->g[L] / 4);
        }
        ev->lvl[AL_DRUMS] = (int16_t)(b->g[AL_DRUMS] * (w == AW_CINEMA ? 450 : 800 + 200 * e / 1000) / 1000);
    }
    /* the filters: closed when quiet, open at the peak; LIGHT opens or closes them */
    {
        int light = (b->knobs[AK_LIGHT] - 500) / 2, base = w == AW_DARK ? 200 : w == AW_CINEMA ? 350 : 300;
        for (L = AL_DRONE; L <= AL_SWELL; L++) ev->cut[L] = (int16_t)ab_clamp(base + (1000 - base) * e / 1000 + light, 80, 1000);
        ev->lp = (int16_t)(w == AW_DARK ? ab_clamp(600 - e * 6 / 10 - light, 0, 900) : ab_clamp(300 - e * 3 / 10 - light / 2, 0, 800));
        ev->gain = (int16_t)(sec == AS_VOID ? 400 : 650 + 350 * e / 1000);
        ev->verb = (int16_t)(w == AW_CINEMA ? 550 + 200 * e / 1000 : w == AW_COSMOS ? 800 : 950);
    }
    /* the textures */
    {
        int tri = (int)((b->bar * 16u + (uint32_t)step) % 512u);          /* (the wind's colour: a slow wave, 2 minutes) */
        tri = tri < 256 ? tri : 511 - tri;
        ev->hiss = (int16_t)(w == AW_CINEMA ? 260 : w == AW_COSMOS ? 90 : b->kind == AP_DECAYLOOP && sec == AS_DECAY ? 150 + 500 * b->sec_bar / sec_len : 60);
        ev->wind = (int16_t)(w == AW_COSMOS ? 250 + 200 * e / 1000 : w == AW_DARK ? 160 : 0);
        ev->wind_f = (int16_t)(tri * 1000 / 256);
        ev->rumble = (int16_t)(w == AW_DARK ? 350 + 450 * e / 1000 : 0);
        ev->sub_am = (int16_t)(w == AW_DARK ? 450 : 0);
        if (sec == AS_VOID || b->faded) ev->hiss = ev->wind = ev->rumble = 0;
        if (sec == AS_BREATH) { ev->wind /= 3; ev->rumble /= 3; }
        {   /* (never alone: as loud as the loudest layer, full once one is at its dawn's level, 300 per mille) */
            int m = 0, x;
            for (L = AL_DRONE; L <= AL_SWELL; L++) if (ev->lvl[L] > m) m = ev->lvl[L];
            x = ab_clamp(m * 1000 / 300, 0, 1000);
            ev->tex = (int16_t)(x * x / 1000);
            ev->hiss = (int16_t)(ev->hiss * ev->tex / 1000); ev->wind = (int16_t)(ev->wind * ev->tex / 1000);
            ev->rumble = (int16_t)(ev->rumble * ev->tex / 1000);
        }
    }
    /* the pitch drift: a slow random walk (COSMIC a few cents, DARK more, CINEMATIC a tape's) */
    if (step == 0) {
        int lim = w == AW_COSMOS ? 205 : w == AW_DARK ? 330 : 80;
        b->drift_v = ab_clamp(b->drift_v + (int32_t)ab_range(&r, -6, 6), -12, 12);
        b->drift = ab_clamp(b->drift + b->drift_v, -lim, lim);
        if (b->drift == lim || b->drift == -lim) b->drift_v = -b->drift_v / 2;
    }
    ev->drift = (int16_t)b->drift;

    /* ---- the notes */
    {
        int win = b->chord_bars == 255 ? 16 : b->chord_bars * 16;   /* the chord's length, sixteenths */
        int vel = 60 + 50 * e / 1000, dens = b->knobs[AK_DENSITY], tension = b->knobs[AK_TENSION];
        int bass = key + root + (inv == 1 ? AB_Q[q][1] : inv == 2 ? AB_Q[q][2] : 0);
        int sec_start = b->sec_bar == 0 && step == 0;
        /* the drone: the pedal (or the chord's root), held; struck again at each section and each drone change */
        if (b->g[AL_DRONE] && b->kind != AP_DECAYLOOP) {
            int dr = b->pedal >= 0 && w == AW_CINEMA ? b->pedal : w == AW_DARK ? root : (b->pedal >= 0 ? b->pedal : root);
            int every = w == AW_CINEMA ? (b->nprog * b->chord_bars < 8 ? 8 : b->nprog * b->chord_bars) : w == AW_COSMOS ? (b->chord_bars < 16 ? 16 : b->chord_bars) : b->chord_bars;
            if (step == 0 && (sec_start || pb % every == 0 || (w == AW_DARK && newchord))) {
                int n = AB_BASE[AL_DRONE] + ab_mod(key + dr, 12), len = every * 16;
                if (sec == AS_BREATH) len = sec_len * 16;
                else if ((int)(sec_len - b->sec_bar) * 16 < len) len = (sec_len - b->sec_bar) * 16;
                ab_add(ev, AL_DRONE, n, vel, len);
                if (w == AW_COSMOS) ab_add(ev, AL_DRONE, n + 7, vel - 10, len);
                if (w == AW_DARK || (w == AW_CINEMA && e > 600)) ab_add(ev, AL_DRONE, n + 12, vel - 15, len);
            }
        }
        /* the pad: the chord, voiced near the last (a swell: struck again every chord in the swell and the peak) */
        if (b->g[AL_PAD] && newchord && b->kind != AP_DECAYLOOP) {
            int nn = ab_qn(q), want = 3 + (e > 350) + (dens > 650), vn = 0, len = win, lo = w == AW_DARK ? 41 : 48, j;
            int lowi = inv == 1 && nn > 1 ? 1 : inv == 2 && nn > 2 ? 2 : 0, first, prev;
            uint8_t vo[6];
            /* open voicing: the lowest (the root, or the inversion's) near the last one's, then each tone above with
             * room between (at least a minor third), the root again on top when there is room */
            first = ab_near(ab_mod(key + root + AB_Q[q][lowi], 12), b->last_vn ? b->last_vo[0] : lo + 5 + (b->knobs[AK_LIGHT] - 500) / 200);
            while (first < lo) first += 12;
            while (first > lo + 11) first -= 12;
            vo[vn++] = (uint8_t)first; prev = first;
            for (j = 1; j < nn + 1 && vn < want; j++) {
                int pc = j < nn ? ab_mod(key + root + AB_Q[q][(lowi + j) % nn], 12) : ab_mod(key + root, 12), n = prev + 3;
                while (ab_mod(n, 12) != pc) n++;
                if (n > 84) break;
                vo[vn++] = (uint8_t)n; prev = n;
            }
            if ((int)(sec_len - b->sec_bar) * 16 < len && sec != AS_PEAK) len = (sec_len - b->sec_bar) * 16 + 8;
            for (i = 0; i < vn; i++) { ab_add(ev, AL_PAD, vo[i], vel - 8, len); b->last_vo[i] = vo[i]; }
            b->last_vn = (uint8_t)vn;
        }
        /* (DARK) the disintegrating loop on the pad: its notes fall away, the ends first, faster and faster */
        if (b->kind == AP_DECAYLOOP && b->g[AL_PAD]) {
            int lb = b->loop_bars * 16, at = (int)((b->bar * 16u + (uint32_t)step) % (uint32_t)lb), slot = lb / b->loop_n;
            if (at % slot == 0) {
                int j = at / slot, d = b->loop[j % b->loop_n], p = 0, len = slot + slot / 2;
                if (sec == AS_DECAY) { int x = (int)b->sec_bar * 1000 / sec_len; p = x * x / 1000 * 6 / 10; }   /* (0 .. 600, quadratic) */
                if (sec == AS_TAIL) p = 700;
                len = len * (1000 - p * (500 + (int)ab_ri(&r, 500)) / 1000) / 1000;    /* (the ends go first) */
                if (!ab_chance(&r, p * 7 / 10) && len > 1) {
                    int n = AB_BASE[AL_PAD] + 12 + ab_mod(key, 12) + ab_deg(b, ab_clamp(d * 2, 0, 8) % 7 + (d > 3 ? 7 : 0)) - 12;
                    ab_add(ev, AL_PAD, n, vel - p / 20, len);
                }
            }
            ev->cut[AL_PAD] = (int16_t)ab_clamp(900 - (sec == AS_DECAY ? 600 * b->sec_bar / sec_len : sec == AS_TAIL ? 650 : 0), 150, 1000);
        }
        /* the low: CINEMATIC the ground bass (a passing step in a long chord's second half); COSMIC the sequencer in its
         * pulse and orbit, else long roots; DARK a second drone, rough (a minor second in the middle) at the peak */
        if (b->g[AL_LOW]) {
            if (w == AW_CINEMA) {
                if (newchord) {
                    int n = AB_BASE[AL_LOW] + ab_mod(bass, 12), len = win;
                    if (win >= 32 && ab_chance(&r, 300)) len = win / 2;
                    ab_add(ev, AL_LOW, n, vel + 10, len);
                } else if (b->chord_bars >= 2 && pb % b->chord_bars == b->chord_bars / 2 && step == 0 && ab_chance(&r, 300)) {
                    int nxt = b->prog[(b->chord_i + 1) % b->nprog].root, cur = AB_BASE[AL_LOW] + ab_mod(bass, 12);
                    int to = AB_BASE[AL_LOW] + ab_mod(key + nxt, 12), n = cur + (to > cur ? 2 : to < cur ? -2 : 0);
                    if (n != cur) ab_add(ev, AL_LOW, n, vel, win / 2);
                }
            } else if (w == AW_COSMOS) {
                if ((sec == AS_PULSE || sec == AS_ORBIT || sec == AS_PEAK) && b->kind != AP_PLAIN) {
                    int s = (int)((b->bar * 16u + (uint32_t)step) % b->seq_n), x = b->seq[s];
                    if (x != 0xFF) {
                        static const int8_t ST[4] = {0, 7, 12, 14};
                        int n = AB_BASE[AL_LOW] + ab_mod(key + root, 12) + ST[x & 3];
                        ab_add(ev, AL_LOW, n, s % 4 == 0 ? vel + 15 : vel - 5, s % 4 == 0 ? 2 : 1);
                    }
                } else if (newchord) ab_add(ev, AL_LOW, AB_BASE[AL_LOW] + ab_mod(key + root, 12), vel, win);
            } else {
                if (newchord || sec_start) {
                    int n = AB_BASE[AL_LOW] + 12 + ab_mod(key + root + (ab_qn(q) > 1 ? AB_Q[q][1] : 7), 12), len = win;
                    if ((int)(sec_len - b->sec_bar) * 16 < len) len = (sec_len - b->sec_bar) * 16;
                    ab_add(ev, AL_LOW, n, vel - 10, len);
                }
                if (step == 0 && (sec == AS_PEAK || (sec == AS_SWELL && b->sec_bar * 3 >= sec_len * 2)) && pb % 4 == 0 && ab_chance(&r, 300 + tension / 2)) {
                    int n = 69 + ab_mod(key + root - 9, 12);               /* (the rough cluster: A4 .. G#5 and a semitone over) */
                    ab_add(ev, AL_LOW, n, vel - 20, 64); ab_add(ev, AL_LOW, n + 1, vel - 25, 64);
                    ev->frisson = 1;
                }
            }
        }
        /* the melody */
        if (b->g[AL_MELO]) {
            if (w == AW_CINEMA) {
                int tb = b->theme_bars, aug = sec == AS_PEAK, span = tb * 16 * (aug ? 2 : 1), cyc = span * 2;
                int at = (int)(((b->bar - b->piece_start) * 16u + (uint32_t)step) % (uint32_t)cyc), phrase = (int)(((b->bar - b->piece_start) * 16u + (uint32_t)step) / (uint32_t)cyc);
                ab_rng_t pr = ab_rng(ab_hash(b->seed, b->piece_no * 4049u + (uint32_t)phrase * 17u + b->theme_ver));
                int play = sec == AS_PEAK || ab_chance(&pr, 450 + dens / 2), up = sec == AS_PEAK && ab_chance(&pr, 500) ? 7 : 0;
                int sequp = sec == AS_SWELL && (int)b->sec_bar * 16 + step >= (sec_len - tb * 2) * 16;   /* (the rising sequence into the peak) */
                if (play && at < span) {
                    int t = 0, j;
                    for (j = 0; j < b->theme_n; j++) {
                        int len = b->theme[j].len * (aug ? 2 : 1);
                        if (at >= t && at < t + len) {
                            int d = b->theme[j].d + up + (sequp ? (t * 3 / span) : 0), n = AB_BASE[AL_MELO] + ab_mod(key, 12) + ab_deg(b, d);
                            uint32_t abs0 = (b->bar - b->piece_start) * 16u + (uint32_t)step - (uint32_t)(at - t);   /* (the note's start) */
                            int onchange = b->chord_bars != 255 && abs0 % (uint32_t)win == 0;
                            int app = onchange && len >= 8 && ab_is_tone(ab_mod(n, 12), key + root, q) && (int)(ab_hash(b->seed, abs0 + b->piece_no * 7u) % 1000u) < 450;
                            if (!ab_is_tone(ab_mod(n, 12), key + root, q) && len >= 8 && !onchange) {   /* (a long note off the chord: its nearest tone) */
                                int k2, best = n, bd = 99;
                                for (k2 = -2; k2 <= 2; k2++) if (ab_is_tone(ab_mod(n + k2, 12), key + root, q) && ab_abs(k2) < bd) { bd = ab_abs(k2); best = n + k2; }
                                n = best;
                            }
                            while (n > 84) n -= 12;                      /* (the voice's register) */
                            if (app) {                                   /* the appoggiatura: the step above, leaning, then the chord tone */
                                int up1 = ab_mod(n + 1 - key, 12), st = 2, m;
                                for (m = 0; m < 7; m++) if (AB_MODES[b->mode][m] == up1) st = 1;
                                if (at == t) { ab_add(ev, AL_MELO, n + st, ab_clamp(vel + 20, 1, 127), len / 2); if (sec == AS_PEAK) ev->frisson = 1; }
                                else if (at == t + len / 2) ab_add(ev, AL_MELO, n, ab_clamp(vel - 5, 1, 127), len - len / 2);
                            } else if (at == t)
                                ab_add(ev, AL_MELO, n, ab_clamp(vel * b->theme[j].v / 100, 1, 127), len);
                            break;
                        }
                        t += len;
                    }
                }
            } else if (w == AW_COSMOS) {                       /* lead swells: a long chord tone or the ninth */
                if (step == 0 && pb % 2 == 0 && ab_chance(&r, 150 + dens / 3 + (sec == AS_PEAK ? 300 : 0))) {
                    static const int8_t T[4] = {0, 7, 14, 4};
                    int n = AB_BASE[AL_MELO] + ab_mod(key + root, 12) + AB_PICK(&r, T) + (b->knobs[AK_LIGHT] > 700 ? 12 : 0);
                    ab_add(ev, AL_MELO, n, vel, ab_range(&r, 16, 32));
                }
            } else {                                            /* DARK: a low chant now and then */
                if (step == 0 && pb % 8 == 0 && ab_chance(&r, 350 + dens / 4)) {
                    static const int8_t T[3] = {0, 7, 1};
                    int t = AB_PICK(&r, T), len = ab_range(&r, 32, 64);
                    ab_add(ev, AL_MELO, AB_BASE[AL_MELO] - 12 + ab_mod(key + root, 12) + t, vel - 15, len);
                }
            }
        }
        /* the bells: CINEMATIC a felt piano repeating a chord tone, jumping octaves (Stratus); COSMIC the arp in the pulse,
         * sparkles otherwise; DARK metal struck far away */
        if (b->g[AL_BELL]) {
            if (w == AW_CINEMA) {
                int s8 = step / 2, on = step % 2 == 0 && ((s8 + b->bell_rot) * b->bell_k) % b->bell_n < b->bell_k;
                if (on && ab_chance(&r, 500 + dens / 2)) {
                    static const int8_t T[4] = {7, 0, 3, 14};
                    int t = T[(b->bar + b->piece_no) % 4], n = AB_BASE[AL_BELL] + ab_mod(key + root + (t == 3 ? AB_Q[q][1] : t), 12);
                    if (ab_chance(&r, 350)) n += 12;
                    if (ab_chance(&r, 120)) n -= 12;
                    if (sec == AS_PEAK) n += 12;                     /* (the register opening at the top) */
                    ab_add(ev, AL_BELL, n, 30 + 30 * e / 1000 + (int)ab_ri(&r, 10), 6);
                }
            } else if (w == AW_COSMOS) {
                if ((sec == AS_PULSE || sec == AS_ORBIT || sec == AS_PEAK) && b->kind != AP_PLAIN) {
                    if (step % b->arp_rate == 0) {
                        int nn = ab_qn(q), j = (int)((b->bar * 16u + (uint32_t)step) / b->arp_rate % (uint32_t)(nn * 2));
                        int n = AB_BASE[AL_BELL] + ab_mod(key + root, 12) + AB_Q[q][j % nn] % 12 + (j / nn) * 12;
                        ab_add(ev, AL_BELL, n, 50 + 40 * e / 1000 - (j % nn) * 4, b->arp_rate);
                    }
                } else if (step % 4 == 0 && ab_chance(&r, 60 + dens / 8)) {
                    static const int8_t T[4] = {0, 7, 14, 19};
                    int t = AB_PICK(&r, T), v = 40 + (int)ab_ri(&r, 30);
                    ab_add(ev, AL_BELL, AB_BASE[AL_BELL] + 12 + ab_mod(key + root, 12) + t, v, 8);
                }
            } else if (step == 0 && pb % 2 == 0 && ab_chance(&r, 120 + dens / 6)) {
                static const int8_t T[4] = {0, 6, 1, 12};
                int t = AB_PICK(&r, T), v = 60 + (int)ab_ri(&r, 50);
                ab_add(ev, AL_BELL, AB_BASE[AL_BELL] - 12 + ab_mod(key + root, 12) + t, v, 32);
            }
        }
        /* the swell: the new voice of the climax, wide; DARK the roughest */
        if (b->g[AL_SWELL] && newchord) {
            int n0 = AB_BASE[AL_SWELL] + ab_mod(key + root, 12), len = win;
            if ((int)(sec_len - b->sec_bar) * 16 < len) len = (sec_len - b->sec_bar) * 16 + 16;
            if (w == AW_DARK) { ab_add(ev, AL_SWELL, n0 - 12, vel, len); ab_add(ev, AL_SWELL, n0 - 11, vel - 10, len); ab_add(ev, AL_SWELL, n0 - 5, vel - 10, len); }
            else {
                ab_add(ev, AL_SWELL, n0, vel, len); ab_add(ev, AL_SWELL, n0 + 7, vel - 5, len);
                ab_add(ev, AL_SWELL, n0 + 12 + AB_Q[q][1] % 12, vel - 5, len);
                if (w == AW_CINEMA) ab_add(ev, AL_SWELL, n0 + 24, vel - 15, len);   /* (the top of the register) */
            }
        }
        /* the sub: the bass's octave down (CINEMATIC), the root (COSMIC 40-65 Hz, DARK 27-45 Hz) */
        if (b->g[AL_SUB] && (newchord || sec_start) && step == 0) {
            int n = w == AW_CINEMA ? AB_BASE[AL_SUB] + ab_mod(bass, 12) : w == AW_COSMOS ? 28 + ab_mod(key + root - 4, 12) : 21 + ab_mod(key + root - 9, 12);
            int len = b->chord_bars == 255 ? 64 : win;
            if (n < 24 && w != AW_DARK) n += 12;
            ab_add(ev, AL_SUB, n, w == AW_CINEMA ? 70 : w == AW_COSMOS ? 85 : 110, len);
        }
        /* the drums: COSMIC's beat (half time: the kick on one and the third beat's and, the clap on three, shakers);
         * CINEMATIC's clock (a rim on the beat); DARK's distant booms */
        if (b->g[AL_DRUMS]) {
            if (w == AW_COSMOS) {
                static const uint8_t KK[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
                int bv = 80 + 30 * e / 1000;
                if (KK[step] || (step == 14 && pb % 4 == 3 && ab_chance(&r, 400))) ab_add(ev, AL_DRUMS, 36, bv + 10, 1);
                if (step == 8) ab_add(ev, AL_DRUMS, pb % 2 ? 39 : 38, bv, 1);
                if (step % 2 == 0 && step % 4 != 0) ab_add(ev, AL_DRUMS, 42, 40 + (int)ab_ri(&r, 25), 1);
                if (ab_chance(&r, 550)) ab_add(ev, AL_DRUMS, 70, 25 + (int)ab_ri(&r, 30), 1);
                if (step == 15 && pb % 8 == 7) ab_add(ev, AL_DRUMS, 37, 60, 1);
            } else if (w == AW_CINEMA) {
                if (step % 4 == 0) ab_add(ev, AL_DRUMS, step == 0 ? 75 : 37, 40 + (step == 0 ? 15 : 0), 1);
            } else if (step == 0 && (peak1 || (sec == AS_VOID && b->sec_bar == 0) || (pb % 8 == 0 && ab_chance(&r, 300)))) {
                ab_add(ev, AL_DRUMS, 36, 110, 1); ab_add(ev, AL_DRUMS, 43, 90, 1);
            }
        }
    }
    /* what the screen shows */
    ev->bar = b->bar; ev->step = (uint8_t)step; ev->sec = (uint8_t)sec; ev->key = (uint8_t)ab_mod(key, 12); ev->mode = b->mode;
    ev->chord = (uint8_t)ab_mod(root, 12); ev->chord_q = (uint8_t)q; ev->e = (int16_t)e; ev->bpm = b->bpm;
    ev->sec_bar = b->sec_bar; ev->sec_len = (uint16_t)sec_len; ev->plan_at = b->plan_at; ev->plan_n = b->plan_n;
    /* the clock */
    if (++b->step >= 16) { b->step = 0; b->bar++; if (b->sec_bar < 0xFFFFu) b->sec_bar++; }
}
#undef MDR
#undef MDN
#undef MPD
#undef MLO
#undef MME
#undef MBE
#undef MSW
#undef MSU
#undef AB_B

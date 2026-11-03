/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN, the brain: a rave that writes itself (siren_brain.c). The prototype's brain.js, line for line, in integers:
 * energies, knobs, chances and levels in per mille, a seeded xorshift32. tests/siren_brain_test.c checks that the two
 * give the same events, step for step. */
#pragma once
#include <stdint.h>

enum { SB_TECHNO, SB_TRANCE, SB_PSY, SB_ACID, SB_RAVE, SB_HARD, SB_DNB, SB_HOUSE, SB_NSTYLES };
enum { SEC_INTRO, SEC_GROOVE, SEC_BUILD, SEC_DROP, SEC_BREAK, SEC_PEAK, SEC_OUTRO };
/* the layers: the drums, the six melodic ones (their sounds picked from the style's selection), and the sub */
enum { L_DRUMS, L_BASS, L_ARP, L_HOOK, L_PAD, L_STAB, L_FX, L_SUB, SB_NLAYERS };
enum { K_ENERGY, K_DENSITY, K_TENSION, K_MUTATION, SB_NKNOBS };
#define SB_PLAN_MAX 40
#define SB_MAX_NOTES 40

typedef struct { uint8_t rate3, shape; uint16_t depth, growl; } sb_wob_t;   /* rate in thirds of a sixteenth; per mille */
typedef struct { int8_t d, o; uint8_t len, v, slide, on; sb_wob_t wob; uint8_t has_wob; } sb_bstep_t;
typedef struct { int8_t d, o; uint8_t len, v, on; } sb_hstep_t;
typedef struct { uint8_t layer, note, vel; uint16_t len; } sb_note_t;   /* len in sixteenths */

/* what the brain is told of the sounds it may pick: how many the style's selection holds for each layer, how many kits
 * the style has and each kit's kick and boom pitch (MIDI note x 16) */
typedef struct {
    uint8_t ncur[SB_NSTYLES][SB_NLAYERS];
    uint8_t nkits[SB_NSTYLES];
    const uint16_t (*kick_n16)[2];    /* [kit of the style in order: style's first kit index + k] */
    uint8_t kit_first[SB_NSTYLES];
} sb_palette_t;

typedef struct {
    sb_note_t notes[SB_MAX_NOTES]; uint8_t nnotes;
    int16_t cut[SB_NLAYERS];          /* per mille, -1: no move */
    uint8_t has_wob; sb_wob_t wob;    /* the bass's wobble for its note this step */
    int16_t hp, duck, gain, verb;     /* the master (per mille) */
    int16_t lp;                       /* (0.6) the master's low-pass closed (per mille): the filtered intro opening */
    /* what the screen shows */
    uint32_t bar; uint8_t step, sec, chord, key, scale; int16_t e; uint8_t bpm;
    uint16_t sec_bar, sec_len; uint8_t plan_at, plan_n;
} sb_events_t;

typedef struct {
    uint32_t seed; uint8_t style, style_lock;
    int16_t knobs[SB_NKNOBS];
    uint8_t locks[SB_NLAYERS]; uint16_t rerolls[SB_NLAYERS];
    uint32_t track_no, bar; uint8_t step, phrase_bar;
    const sb_palette_t *pal;
    uint8_t dive, break_req;
    /* (0.5) the hands: the drop at the next bar, the section held (looped on its last phrase), the key up a tone at
     * the next drop; a layer's motif version (a new melody, bassline, groove: the sound kept), and while they are
     * written again, only those layers (a mask; 0: all) */
    uint8_t drop_req, stay, key_req, motif_only; uint8_t motif_ver[SB_NLAYERS];
    /* the track */
    uint8_t scale, key, bpm, bpm_target; const uint8_t *prog; uint8_t nprog, chord_bars;
    uint32_t kit_pick; uint8_t swing, bass_kind;
    uint8_t plan_sec[SB_PLAN_MAX]; uint16_t plan_len[SB_PLAN_MAX]; uint8_t plan_n, plan_at; uint16_t sec_bar;
    uint8_t intro_kind;               /* (0.6) how the track begins (siren_brain.c IK_*) */
    uint8_t layer_on[6];              /* arp hook pad stab fx piano */
    uint32_t track_start_bar;
    /* the sounds: an index in the style's selection per layer (-1 none); the kit, its kick tuned */
    int16_t snd[SB_NLAYERS]; uint8_t sounds_changed;
    uint16_t kick_n16, kick2_n16;
    int16_t bass_alt[3]; uint8_t has_alt;
    /* the patterns */
    uint8_t pat_init;
    struct { uint8_t hat_k, hat_rot, perc_k, perc_rot, perc_lane, clap_groove, ride, hat_mode, gallop, four;
             const uint8_t *break_k; uint8_t nbreak_k; const uint8_t *ghost; uint8_t nghost; } dr;
    sb_bstep_t bass[32]; uint8_t bass_n;
    sb_hstep_t hook[32], theme[32]; uint8_t hook_n;
    struct { uint8_t order, rate, oct, gate; int8_t skip; uint32_t seed; } arp;
    uint8_t stab[16];
    struct { uint8_t inv, add9; } pad;
} sb_brain_t;

void sb_init(sb_brain_t *b, const sb_palette_t *pal);
void sb_set_seed(sb_brain_t *b, uint32_t seed, int style);   /* style -1: keep */
void sb_step16(sb_brain_t *b, sb_events_t *ev);
void sb_reroll(sb_brain_t *b, int layer);
void sb_lock(sb_brain_t *b, int layer, int on);
void sb_rewind(sb_brain_t *b);
void sb_next_track(sb_brain_t *b);                           /* (0.5) the track ends now: the next one */
void sb_remotif(sb_brain_t *b, int layer, int delta);        /* (0.5) another motif for that layer, its sound kept */                               /* (0.2) the section from its first beat */
void sb_knob(sb_brain_t *b, int k, int v);                   /* per mille */
void sb_code(uint32_t seed, char out[9]);                    /* "XXX-XXXX" */
uint32_t sb_uncode(const char *c);
extern const char *const SB_STYLE_NAMES[SB_NSTYLES];
extern const char *const SB_SEC_NAMES[7];

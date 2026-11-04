/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (ABYSS) */
/* ABYSS, the brain: ambient that writes itself (abyss_brain.c). Freestanding C (no library: the FM-1 and, compiled to
 * WebAssembly, the site play the same events), integers only: energies, knobs, chances and levels in per mille, a
 * seeded xorshift32. Three worlds: CINEMATIC (Richter, Zimmer's calm, Hammock), COSMIC (Roach, Carbon Based Lifeforms,
 * Stearns), DARK (Lustmord, Hecker, Basinski). */
#pragma once
#include <stdint.h>

enum { AW_CINEMA, AW_COSMOS, AW_DARK, AB_NWORLDS };
/* the sections a piece is made of (each world its grammar of them) */
enum { AS_DAWN, AS_THEME, AS_RISE, AS_SWELL, AS_BREATH, AS_PEAK, AS_FALL, AS_TAIL, AS_PULSE, AS_ORBIT, AS_VOID, AS_DECAY, AB_NSECS };
/* the layers: the drums (a beat, a clock's tick, distant booms), six synth parts (their sounds from the world's
 * selection), the sub (a sine) */
enum { AL_DRUMS, AL_DRONE, AL_PAD, AL_LOW, AL_MELO, AL_BELL, AL_SWELL, AL_SUB, AB_NLAYERS };
/* the hands: LIGHT (shadow .. light), DENSITY (how much is played), TIME (slower .. faster changes), TENSION (rough) */
enum { AK_LIGHT, AK_DENSITY, AK_TIME, AK_TENSION, AB_NKNOBS };
/* the gestures (the black keys) */
enum { AG_RISE, AG_PEAK, AG_CALM, AG_BREATH, AG_LIFT, AG_THEME, AG_HARMONY, AG_HOLD, AG_NEXT, AG_VOID, AG_FADE, AB_NGESTURES };
/* the piece kinds: as the world plays it, or (DARK) the disintegrating loop; (COSMIC) beatless, sequencer, beat */
enum { AP_PLAIN, AP_DECAYLOOP, AP_SEQ, AP_BEAT, AP_TICK, AP_ORGAN };
#define AB_PLAN_MAX 24
#define AB_MAX_NOTES 40

typedef struct { uint8_t layer, note, vel; uint16_t len; } ab_note_t;   /* len in sixteenths */
typedef struct { uint8_t ncur[AB_NWORLDS][AB_NLAYERS]; uint8_t nkits[AB_NWORLDS]; } ab_palette_t;

typedef struct {
    ab_note_t notes[AB_MAX_NOTES]; uint8_t nnotes;
    int16_t cut[AB_NLAYERS];          /* each layer's filter, per mille (1000 open), -1 no move */
    int16_t lvl[AB_NLAYERS];          /* each layer's level, per mille of its place (its fade in and out, the swell) */
    int16_t gain, verb, lp;           /* the master: gain, the reverb's share, its low-pass closed (per mille) */
    int16_t hiss, wind, wind_f, rumble, sub_am;   /* the textures (per mille): tape hiss, wind (and its colour), rumble;
                                                   * the sub's slow swell (0 none) */
    int16_t tex;                      /* how much the textures may sound (per mille): as loud as the music is, so they
                                       * come in with it at the start and go with it at the end (never alone) */
    int16_t drift;                    /* the whole piece's pitch drifting (1/4096 semitone) */
    /* what the screen shows */
    uint32_t bar; uint8_t step, sec, key, mode, chord, chord_q; int16_t e; uint8_t bpm, frisson;
    uint16_t sec_bar, sec_len; uint8_t plan_at, plan_n;
} ab_events_t;

typedef struct { int8_t root; uint8_t q, inv; } ab_chord_t;            /* root in semitones over the key, AQ_* */
typedef struct { int8_t d; uint8_t len, v; } ab_tnote_t;               /* a theme's note: scale degree, sixteenths */

typedef struct {
    uint32_t seed; uint8_t world, world_q, kind;
    int16_t knobs[AB_NKNOBS];
    uint8_t locks[AB_NLAYERS]; uint16_t rerolls[AB_NLAYERS];
    uint32_t piece_no, bar; uint8_t step;
    const ab_palette_t *pal;
    /* the piece */
    uint8_t key, mode, bpm; ab_chord_t prog[8]; uint8_t nprog, chord_bars; int8_t pedal;
    uint8_t plan_sec[AB_PLAN_MAX]; uint16_t plan_len[AB_PLAN_MAX]; uint8_t plan_n, plan_at; uint16_t sec_bar;
    uint32_t piece_start;             /* the bar it began at */
    int8_t lift;                      /* (COSMIC) the key moved up a mediant, the rest of the piece */
    uint8_t surprise_q; int8_t surprise_root;   /* the climax's unexpected chord */
    ab_tnote_t theme[16]; uint8_t theme_n, theme_bars, theme_ver;
    uint8_t seq[16], seq_n, arp_rate, bell_k, bell_n, bell_rot;          /* the sequencer, the arp, the bell's rhythm */
    uint8_t loop[8], loop_n, loop_bars;                                  /* (DARK, the decaying loop) its notes */
    uint8_t hold, rise_req, calm_req, breath_req, void_req, fade, faded, lift_req;
    uint8_t layer_g[AB_NLAYERS];      /* (screen) each layer in the mix now, per mille / 4 */
    /* the sounds: an index in the world's selection per layer; the kit */
    int16_t snd[AB_NLAYERS]; uint8_t sounds_changed;
    /* running */
    int16_t g[AB_NLAYERS];            /* each layer's fade (per mille), toward its section's target */
    uint8_t last_vo[6]; uint8_t last_vn;   /* the pad's last voicing (voice leading) */
    uint32_t chord_at;                /* the bar the chord changed */
    int8_t chord_i;                   /* the chord in the loop now */
    int32_t drift, drift_v;           /* the pitch drift's random walk */
} ab_brain_t;

void ab_init(ab_brain_t *b, const ab_palette_t *pal);
void ab_set_seed(ab_brain_t *b, uint32_t seed, int world);   /* world -1: keep */
void ab_step16(ab_brain_t *b, ab_events_t *ev);
void ab_knob(ab_brain_t *b, int k, int v);
void ab_gesture(ab_brain_t *b, int g);
void ab_reroll(ab_brain_t *b, int layer);
void ab_lock(ab_brain_t *b, int layer, int on);
void ab_next_piece(ab_brain_t *b);
void ab_code(uint32_t seed, char out[9]);                    /* "XXX-XXXX" */
uint32_t ab_uncode(const char *c);
extern const char *const AB_WORLD_NAMES[AB_NWORLDS];
extern const char *const AB_SEC_NAMES[AB_NSECS];
extern const char *const AB_MODE_NAMES[7];

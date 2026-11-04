/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (ABYSS) */
/* ABYSS: ambient that writes itself, on Clipper's engines (SIREN's sister: the same hooks, siren_*, in the firmware).
 * Six synth parts (the layers: drone, pad, low, melody, bells, swell) and the drum track, played by the brain
 * (abyss_brain.c) a sixteenth at a time in the audio interrupt; each part a sound from the world's selection
 * (abyss_data.h, tools/gen_abyss.py) with ABYSS's own envelopes (slow attacks, long releases); the sub, a sine under it
 * all (27-65 Hz, DARK's swelling slowly); the textures, made here: tape hiss, wind (band-passed, its colour moving),
 * rumble (brown noise, low-passed); the reverb the plate's HALL, long (DARK longer than the knob goes: ~25 s), the
 * echo per world; the whole set's pitch drifting a few cents. The main loop applies the sounds the brain asks for
 * (siren_service) and runs the screen and the controls (abyss_ui.c). Commands from the main loop to the brain go
 * through a ring (the brain runs in the interrupt only). */
#include "abyss_brain.c"
#include "abyss_data.h"

#define SIREN_PART(L) (&trk[(L) - 1])
/* each layer's place in the mix: level (P_LEVEL: 112 = 0 dB, 2 a dB), pan, delay, reverb */
static const int8_t ABYSS_MIX[6][4] = {
    {100, 0, 0, 62},     /* DRONE */
    {100, 0, 12, 80},    /* PAD */
    {104, 0, 0, 34},     /* LOW */
    {100, 10, 34, 64},   /* MELODY */
    {92, -16, 44, 74},   /* BELLS */
    {100, 0, 10, 92},    /* SWELL */
};
/* each world's envelopes for each layer: attack, release (P_ATK / P_REL: 1 ms .. 10 s, exponential; -1 the sound's):
 * CINEMATIC strings that swell in a second or two, COSMIC pads of seconds, DARK drones of many */
static const int8_t ABYSS_ENV[AB_NWORLDS][6][2] = {
    {{105, 119}, {98, 117}, {83, 110}, {69, 108}, {-1, 110}, {108, 119}},
    {{114, 123}, {110, 123}, {-1, -1}, {95, 114}, {-1, 114}, {114, 123}},
    {{119, 127}, {114, 127}, {117, 127}, {110, 123}, {-1, 123}, {110, 123}},
};
/* each world's space: the reverb (type, size, damping, pre-delay ms), the echo (time, feedback), the drums' sends */
static const int16_t ABYSS_FXW[AB_NWORLDS][8] = {
    {RT_HALL, 104, 48, 25, 6, 36, 30, 10},
    {RT_HALL, 127, 34, 40, 0, 52, 40, 20},
    {RT_HALL, 146, 78, 90, 0, 74, 60, 10},
};
/* the voices each layer may hold (the shared budget is NVOICE): fewer under load */
static const uint8_t ABYSS_POLY[3][6] = {{3, 5, 2, 2, 3, 4}, {2, 4, 2, 2, 2, 3}, {2, 3, 1, 1, 2, 2}};

static ab_brain_t ab;
static ab_palette_t ab_pal;
static struct {
    volatile uint8_t playing;
    int32_t until;
    struct { uint8_t trk, note; int32_t left; } off[64]; uint8_t noff;
    /* the sub: a sine; its slow swell */
    uint32_t sph, sinc, sam, rph; int32_t senv, sgate, svel; uint8_t snote;
    /* the textures: their levels now (Q15) and wanted, the wind's band-pass, the rumble's brown noise */
    int32_t nz, hiss, hiss_t, wind, wind_t, wf, wf_t, rum, rum_t, ram;
    int32_t w_lp, w_bp, b1, b2, h1;
    int32_t gain_q12;
    /* what the screen shows */
    volatile uint32_t seq; ab_events_t ev;
    volatile uint32_t kicks, notes_n; uint8_t notes_l[16], notes_k[16];
    int16_t scope[512]; uint32_t sw;
    volatile uint32_t cw, cr; struct { uint8_t op; int16_t a, b; uint32_t c; } cmd[32];
    int16_t applied[7], applied_world[7]; uint8_t ap_kind;
    int16_t ap_kit;
    uint8_t mute[8];
    int16_t sound_eng[AB_NLAYERS], sound_pre[AB_NLAYERS];
    uint8_t lean; uint16_t lean_hold; uint16_t cpu_test;
    volatile uint32_t ld_peak, ld_sum, ld_n;
    uint32_t n16;
    /* the mixer: the space (% of the sends), each layer's volume (P_LEVEL steps), filter (-63..63), space (%); each
     * sound's trim and tone (its calibration); the layer chosen (the keyboard's) */
    uint16_t space; int8_t lvol[8], lflt[8]; uint16_t lspace[8]; int8_t trim[8], tone[8]; uint8_t focus;
    uint8_t shimmer, tape;            /* (the textures page) SHIMMER: the reverb's octave; TAPE: more hiss and wow */
    /* the keyboard in the hands (as SIREN 0.8): the layers taken, the keys held, the arpeggiator */
    uint8_t live, larp, nlh, lplay; uint32_t larp_i; struct { uint8_t L, n; } lh[16];
    int32_t bend;
} sr;
enum { AC_PLAY, AC_STOP, AC_KNOB, AC_GESTURE, AC_WORLD, AC_SEED, AC_REROLL, AC_LOCK, AC_SOUND, AC_MIX, AC_LIVE, AC_LIVEOFF,
       AC_LARP, AC_TEXTURE };
enum { SM_SPACE, SM_FOCUS, SM_LVOL = 10, SM_LFLT = 20, SM_LSPACE = 30 };

static void siren_cmd(uint32_t op, int32_t a, int32_t b, uint32_t c)
{
    uint32_t w = sr.cw;
    if (w - sr.cr >= 32u)
        return;
    sr.cmd[w % 32u].op = (uint8_t)op; sr.cmd[w % 32u].a = (int16_t)a; sr.cmd[w % 32u].b = (int16_t)b; sr.cmd[w % 32u].c = c;
    RING_PUBLISH();
    sr.cw = w + 1u;
}
static void siren_all_off(void)
{
    uint32_t i;
    for (i = 0; i < sr.noff; i++)
        if (sr.off[i].trk < NPART)
            trk_note_off1(&trk[sr.off[i].trk], sr.off[i].note);
    sr.noff = 0;
    for (i = 0; i < NPART; i++)
        trk_all_off1(&trk[i]);
    sr.sgate = 0;
}
static int32_t siren_bend(void) { return sr.bend; }                 /* (voice.c: the drift, 1/4096 semitone) */
static int32_t siren_eq_db(uint32_t band) { (void)band; return 12; } /* (bus.c: flat) */
static uint32_t siren_poly(uint32_t part) { return ABYSS_POLY[sr.lean > 2u ? 2u : sr.lean][part % 6u]; }
static void siren_load_half(uint32_t pct) { if (pct > sr.ld_peak) sr.ld_peak = pct; sr.ld_sum += pct; sr.ld_n++; }
static void siren_trim_voices(void)
{
    uint32_t p, i;
    for (p = 0; p < 6u; p++) {
        uint32_t cap = siren_poly(p), n = 0;
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active && trk[p].v[i].stage != 4u && ++n > cap)
                voice_kill(&trk[p].v[i]);
    }
}

/* ---- the keyboard in the hands (SIREN 0.8's): a key's notes on a layer, the layer taken until given back */
static void siren_spark(uint32_t L, uint32_t n) { uint32_t q = sr.notes_n % 16u; sr.notes_l[q] = (uint8_t)L; sr.notes_k[q] = (uint8_t)n; sr.notes_n++; }
static void siren_take(uint32_t L)
{
    uint32_t i;
    sr.live |= (uint8_t)(1u << L);
    trk_all_off1(SIREN_PART(L));
    for (i = 0; i < sr.noff;)
        if (sr.off[i].trk == L - 1u) sr.off[i] = sr.off[--sr.noff]; else i++;
}
static void siren_live(uint32_t L, uint32_t vel, uint32_t c)
{
    uint32_t k, i;
    if (L > AL_SWELL)
        return;
    for (k = 0; k < 3u; k++, c >>= 8) {
        uint32_t n = (c & 0xFFu);
        if (!n--)
            continue;
        if (L == AL_DRUMS) { if (vel) { trk_note_on1(TDRUM, n, vel); siren_spark(L, n); } continue; }
        if (vel) {
            if (!((sr.live >> L) & 1u)) siren_take(L);
            for (i = 0; i < sr.nlh && !(sr.lh[i].L == L && sr.lh[i].n == n); i++) {}
            if (i == sr.nlh && sr.nlh < 16u) { sr.lh[i].L = (uint8_t)L; sr.lh[i].n = (uint8_t)n; sr.nlh++; }
            if (!(sr.larp && sr.playing)) { trk_note_on1(SIREN_PART(L), n, vel); siren_spark(L, n); }
        } else {
            for (i = 0; i < sr.nlh; i++)
                if (sr.lh[i].L == L && sr.lh[i].n == n) { sr.lh[i] = sr.lh[--sr.nlh]; break; }
            trk_note_off1(SIREN_PART(L), n);
        }
    }
}
static void siren_live_off(uint32_t mask)
{
    uint32_t i, L;
    for (i = 0; i < sr.nlh;)
        if ((mask >> sr.lh[i].L) & 1u) sr.lh[i] = sr.lh[--sr.nlh]; else i++;
    for (L = AL_DRONE; L <= AL_SWELL; L++)
        if ((mask >> L) & (sr.live >> L) & 1u) trk_all_off1(SIREN_PART(L));
    sr.live &= (uint8_t)~mask;
}
static void siren_larp(uint32_t on)
{
    uint32_t i;
    if (sr.larp == (uint8_t)!!on)
        return;
    sr.larp = (uint8_t)!!on; sr.larp_i = 0;
    for (i = 0; i < sr.nlh; i++) {
        track_t *t = SIREN_PART(sr.lh[i].L);
        if (on) trk_note_off1(t, sr.lh[i].n);
        else trk_note_on1(t, sr.lh[i].n, 100);
    }
}

/* PRESET turned on a layer: the next or previous sound of the world's selection for it, kept (locked) */
static void siren_sound_step(int32_t L, int32_t d)
{
    int32_t n;
    if (L < 0 || L > AL_SWELL)
        return;
    n = L == AL_DRUMS ? ab_pal.nkits[ab.world] : ab_pal.ncur[ab.world][L];
    if (n < 1)
        return;
    ab.snd[L] = (int16_t)((ab.snd[L] + d % n + n) % n);
    ab.locks[L] = 1; ab.sounds_changed = 1;
}
static void siren_mix_set(int32_t what, int32_t v)
{
    if (what == SM_SPACE) sr.space = (uint16_t)ab_clamp(v, 0, 200);
    else if (what == SM_FOCUS) sr.focus = (uint8_t)(v > 0 && v <= AL_SWELL ? v : 0);
    else if (what >= SM_LVOL && what < SM_LVOL + 8) sr.lvol[what - SM_LVOL] = (int8_t)ab_clamp(v, -30, 12);
    else if (what >= SM_LFLT && what < SM_LFLT + 8) sr.lflt[what - SM_LFLT] = (int8_t)ab_clamp(v, -63, 63);
    else if (what >= SM_LSPACE && what < SM_LSPACE + 8) sr.lspace[what - SM_LSPACE] = (uint16_t)ab_clamp(v, 0, 200);
}
static void siren_commands(void)                 /* (audio interrupt) */
{
    while (sr.cr != sr.cw) {
        uint32_t i = sr.cr % 32u;
        int32_t a = sr.cmd[i].a, b = sr.cmd[i].b;
        switch (sr.cmd[i].op) {
        case AC_PLAY: if (!sr.playing) { sr.playing = 1; sr.until = 0; } break;
        case AC_STOP: sr.playing = 0; siren_all_off(); break;
        case AC_KNOB: ab_knob(&ab, a, b); break;
        case AC_GESTURE: ab_gesture(&ab, a); break;
        case AC_WORLD: if (b) { siren_all_off(); ab_set_seed(&ab, ab.seed, a); } else ab.world_q = (uint8_t)a; break;   /* (now, or the next piece) */
        case AC_SEED: siren_all_off(); ab_set_seed(&ab, sr.cmd[i].c, a); break;
        case AC_REROLL: ab_reroll(&ab, a); break;
        case AC_LOCK: ab_lock(&ab, a, b); break;
        case AC_SOUND: siren_sound_step(a, b); break;
        case AC_MIX: siren_mix_set(a, b); break;
        case AC_LIVE: siren_live((uint32_t)a, (uint32_t)b, sr.cmd[i].c); break;
        case AC_LIVEOFF: siren_live_off((uint32_t)a); break;
        case AC_LARP: siren_larp((uint32_t)a); break;
        case AC_TEXTURE: if (a == 0) sr.shimmer = (uint8_t)b; else sr.tape = (uint8_t)b; break;
        }
        sr.cr++;
    }
}

/* one note into its part, its end len units on */
static void siren_play(const ab_note_t *x, uint32_t len)
{
    uint32_t n = x->note, tk, k;
    if (!sr.lplay && (sr.mute[x->layer] || ((sr.live >> x->layer) & 1u)))
        return;
    if (x->layer == AL_DRUMS) {
        trk_note_on1(TDRUM, x->note, x->vel);
        if (x->note == 36u) sr.kicks++;
        siren_spark(AL_DRUMS, n);
        return;
    }
    if (x->layer == AL_SUB) {
        sr.snote = (uint8_t)n; sr.sinc = SD_NOTE_INC[n]; sr.svel = x->vel * 258; sr.sgate = sr.svel * sr.ev.lvl[AL_SUB] / 1000; tk = 0xFFu;
    } else {
        tk = (uint32_t)x->layer - 1u;
        for (k = 0; k < sr.noff; k++)
            if (sr.off[k].trk == tk && sr.off[k].note == n) { trk_note_off1(&trk[tk], n); sr.off[k] = sr.off[--sr.noff]; break; }
        trk_note_on1(&trk[tk], n, x->vel);
    }
    if (sr.noff < 64u) {
        sr.off[sr.noff].trk = (uint8_t)tk; sr.off[sr.noff].note = (uint8_t)n; sr.off[sr.noff].left = (int32_t)len; sr.noff++;
    }
    siren_spark(x->layer, n);
}

/* a level (per mille of the layer's place) in P_LEVEL steps (2 a dB): 40 log10(v / 1000), from 0 down to -60 */
static int32_t abyss_steps(int32_t v)
{
    static const int8_t T[17] = {-120, -48, -36, -29, -24, -20, -17, -14, -12, -10, -8, -6, -5, -3, -2, -1, 0};   /* v = k/16 */
    int32_t k = ab_clamp(v, 0, 1000) * 16, i = k / 1000, f = k % 1000;
    if (i >= 16) return 0;
    return T[i] + (T[i + 1] - T[i]) * f / 1000;
}
static int16_t siren_level(uint32_t L)
{
    return (int16_t)ab_clamp(ABYSS_MIX[L - 1u][0] + sr.trim[L] + sr.lvol[L] + abyss_steps(sr.ev.lvl[L]), 1, 127);
}
/* the mixer, every sixteenth: each layer's level (its place, its sound's trim, the brain's fade and swell, the hand's),
 * its filter (the brain's, the sound's tone, the hand's), its sends (the world's space, the hand's); the drums; the
 * master's low-pass (the brain's: DARK closed but at its peak); the reverb and the echo, the world's */
static void siren_mixer(const ab_events_t *ev)
{
    uint32_t L, w = ab.world % AB_NWORLDS;
    int32_t ls;
    for (L = AL_DRONE; L <= AL_SWELL; L++) {
        track_t *t = SIREN_PART(L);
        int32_t c = ev->cut[L] >= 0 ? ev->cut[L] : 1000, lp = c >= 950 ? 0 : -(950 - c) * 52 / 950, f = sr.lflt[L];
        if (sr.tone[L] < lp) lp = sr.tone[L];
        ls = (int32_t)sr.lspace[L] * sr.space / 100;
        t->p[P_TFLT] = (int16_t)(f > 0 ? f : ab_clamp(lp + f, -60, 0));
        t->p[P_LEVEL] = ev->lvl[L] <= 0 ? 1 : siren_level(L);
        t->p[P_REV] = (int16_t)ab_clamp(ABYSS_MIX[L - 1u][3] * ev->verb / 1000 * ls / 100, 0, 127);
        t->p[P_DLY] = (int16_t)ab_clamp(ABYSS_MIX[L - 1u][2] * ls / 100, 0, 127);
    }
    ls = (int32_t)sr.lspace[AL_DRUMS] * sr.space / 100;
    song.g[G_DRLVL] = (int16_t)ab_clamp(ev->lvl[AL_DRUMS] * (100 + sr.lvol[AL_DRUMS] * 6) / 1000, 0, 127);
    song.g[G_DRREV] = (int16_t)ab_clamp(ABYSS_FXW[w][6] * ls / 100, 0, 127);
    song.g[G_DRDLY] = (int16_t)ab_clamp(ABYSS_FXW[w][7] * ls / 100, 0, 127);
    TDRUM->p[P_TFLT] = sr.lflt[AL_DRUMS];
    song.g[G_FILT] = (int16_t)-(ev->lp * 50 / 1000);
    song.g[G_RTYPE] = (int16_t)(sr.shimmer ? RT_SHIMMER : ABYSS_FXW[w][0]);
    song.g[G_RSIZE] = (int16_t)ab_clamp(ABYSS_FXW[w][1] + ((int32_t)sr.space - 100) / 5, 40, 150);
    song.g[G_RDAMP] = ABYSS_FXW[w][2]; song.g[G_RPRE] = ABYSS_FXW[w][3];
    song.g[G_DTIME] = ABYSS_FXW[w][4]; song.g[G_DFDBK] = ABYSS_FXW[w][5];
}

/* one sixteenth: the brain's events into the parts, the drums, the sub; the textures and the master */
static void siren_fire(void)
{
    ab_events_t *ev = &sr.ev;
    uint32_t i, u16 = BEAT_U / 4u;
    sr.n16++;
    ab_step16(&ab, ev);
    song.g[G_BPM] = ev->bpm;
    {   /* the CPU guard: the worst half since the last sixteenth; up a level over 88 %, down after two bars under 60 */
        uint32_t pk = sr.ld_peak;
        sr.ld_peak = 0; sr.ld_sum = 0; sr.ld_n = 0;
        if (sr.cpu_test) pk = sr.cpu_test;
        if (pk >= 88u) { if (sr.lean < 2u) { sr.lean++; siren_trim_voices(); } sr.lean_hold = 32; }
        else if (sr.lean_hold) sr.lean_hold--;
        else if (sr.lean && pk < 60u) { sr.lean--; sr.lean_hold = 32; }
    }
    for (i = 0; i < ev->nnotes; i++) {
        const ab_note_t *x = &ev->notes[i];
        uint32_t len = x->len * (u16 / 100u) * 98u;
        if (x->layer == AL_SUB || x->layer == AL_DRUMS) { siren_play(x, len); continue; }
        if (ev->lvl[x->layer] <= 0) continue;
        siren_play(x, len);
    }
    if (sr.larp && sr.nlh) {                         /* the arpeggiator on the keys held: upwards, a sixteenth each */
        uint32_t j, k = sr.larp_i++ % sr.nlh, best = 0;
        ab_note_t x;
        for (i = 0; i < sr.nlh; i++) {
            uint32_t below = 0, v = sr.lh[i].n * 16u + i;
            for (j = 0; j < sr.nlh; j++) below += sr.lh[j].n * 16u + j < v;
            if (below == k) best = i;
        }
        x.layer = sr.lh[best].L; x.note = sr.lh[best].n; x.vel = 90; x.len = 1;
        sr.lplay = 1; siren_play(&x, u16 / 100u * 80u); sr.lplay = 0;
    }
    /* the textures (Q15 levels: hiss ~-31 dBFS at its most, wind ~-21, rumble ~-15) and the sub's swell */
    sr.hiss_t = (ev->hiss + (sr.tape ? 250 : 0)) * 1100 / 1000;
    sr.wind_t = ev->wind * 3200 / 1000;
    sr.wf_t = ev->wind_f;
    sr.rum_t = ev->rumble * 5200 / 1000;
    sr.ram = ev->sub_am;
    sr.bend = ev->drift * (sr.tape ? 2 : 1);
    if (sr.sgate) sr.sgate = sr.mute[AL_SUB] ? 0 : sr.svel * ev->lvl[AL_SUB] / 1000 + 1;   /* (the sub follows its fade: a breath, a void, silent) */
    siren_mixer(ev);
    sr.gain_q12 = ev->gain * 4096 / 1000;
    sr.seq++;
}

/* (audio interrupt, events_block) the brain's clock */
static void siren_block(uint32_t n, uint32_t adv)
{
    uint32_t i;
    (void)n;
    siren_commands();
    for (i = 0; i < sr.noff;) {
        sr.off[i].left -= (int32_t)adv;
        if (sr.off[i].left <= 0) {
            if (sr.off[i].trk == 0xFFu) { if (sr.snote == sr.off[i].note) sr.sgate = 0; }
            else if (sr.off[i].trk != TRK_DRUM) trk_note_off1(&trk[sr.off[i].trk], sr.off[i].note);
            sr.off[i] = sr.off[--sr.noff];
        } else
            i++;
    }
    if (sr.playing) {
        sr.until -= (int32_t)adv;
        while (sr.until <= 0) {
            siren_fire();
            sr.until += (int32_t)(BEAT_U / 4u);
        }
    }
}

/* (audio interrupt, mix_block) the textures and the sub, into the dry mix */
static void siren_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t gh, gw, gr, kf;
    /* the levels glide to their targets over the block */
    sr.hiss += (sr.hiss_t - sr.hiss) >> 4; sr.wind += (sr.wind_t - sr.wind) >> 4; sr.rum += (sr.rum_t - sr.rum) >> 4; sr.wf += (sr.wf_t - sr.wf) >> 4;
    gh = sr.hiss; gw = sr.wind; gr = sr.rum;
    if (sr.ram) {                                    /* (DARK) the rumble breathes: a 20-second wave */
        sr.rph += 4870u * n;
        gr = gr * (24576 + (sine_i(sr.rph) >> 2)) >> 15;
    }
    kf = 2200 + sr.wf * 9;                           /* the wind's band: ~250 .. 2000 Hz (a state-variable filter's f) */
    if (gh | gw | gr) for (i = 0; i < n; i++) {
        int32_t wn = (int32_t)(noise32(&sr.nz) >> 17) - 16384, y, hp;
        sr.h1 += (wn - sr.h1) >> 2; hp = wn - sr.h1;  /* (the hiss: its top) */
        sr.w_lp += mulq15(sr.w_bp, kf); sr.w_bp += mulq15(wn - sr.w_lp - (sr.w_bp >> 1), kf);   /* (Q 2) */
        sr.b1 += (wn - sr.b1) >> 7; sr.b2 += (sr.b1 - sr.b2) >> 6;   /* (the rumble: brown, low-passed ~ 100 Hz) */
        y = mulq15(hp, gh) + mulq15(sr.w_bp, gw) + mulq15(sr.b2 << 3, gr);
        l[i] += y; r[i] += y;
    }
    if (!sr.sgate && sr.senv < 8)
        return;
    for (i = 0; i < n; i++) {
        int32_t y, am = 32767;
        sr.senv += ((sr.sgate) - sr.senv) >> (sr.sgate ? 9 : 11);     /* (slow: the sub swells in and out) */
        sr.sph += sr.sinc;
        if (sr.ram) { sr.sam += 19478u; am = 22000 + (sine_i(sr.sam) >> 2); }   /* (DARK: a 5-second swell) */
        y = mulq15(sine_i(sr.sph), 6000) * sr.senv >> 15;
        y = mulq15(y, am);
        l[i] += y; r[i] += y;
    }
}
static int32_t siren_duck_depth(void) { return 0; }
static int32_t siren_gain_q12(void) { return sr.gain_q12; }
static void siren_scope_tap(void)
{
    uint32_t i;
    for (i = 1; i < CTL; i += 2u) { sr.scope[sr.sw % 512u] = vis_tap[2u * i]; sr.sw++; }
}

/* ---- the main loop's side: the sounds the brain picked, applied with the world's envelopes; the kit */
static int16_t siren_find(const sd_snd_t *s, int16_t *pe)
{
    uint32_t e, p;
    for (e = 0; e < NENGINES; e++) {
        const engine_t *E = ENGINES[e];
        if (!str_eq(E->name, s->eng))
            continue;
        for (p = 0; p < E->npresets; p++)
            if (str_eq(E->presets[p].name, s->pre)) { *pe = (int16_t)p; return (int16_t)e; }
    }
    *pe = 0;
    return -1;
}
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p);
static void abyss_env(uint32_t L)                /* the world's envelope on a part (DARK's decaying loop: short) */
{
    track_t *t = SIREN_PART(L);
    const int8_t *v = ABYSS_ENV[ab.world % AB_NWORLDS][L - 1u];
    int8_t a = v[0], r = v[1];
    if (ab.kind == AP_DECAYLOOP && L == AL_PAD) a = 80, r = 112;
    if (a >= 0) t->p[P_ATK] = a;
    if (r >= 0) t->p[P_REL] = r;
}
static void siren_apply_kit(void)
{
    const sd_kit_t *K;
    dsu_kit_t *u;
    int ki = ab.snd[AL_DRUMS], w = ab.world % AB_NWORLDS;
    if (ki < 0 || !SD_KIT_N[w])
        return;
    K = &SD_KITS[SD_KIT_FIRST[w] + ki % SD_KIT_N[w]];
    (void)dsu_kit(0);
    u = &dsu.k[0];
    fm1_irq_off();
    memcpy(u->s, K->s, sizeof u->s);
    u->s[0].pitch = (uint8_t)(K->kick_n16[0] >> 4); u->s[0].fine = (uint8_t)(K->kick_n16[0] & 15u);
    u->s[14].pitch = (uint8_t)(K->kick_n16[1] >> 4); u->s[14].fine = (uint8_t)(K->kick_n16[1] & 15u);
    u->crush = K->crush;
    memcpy(u->name, K->name, 8);
    fm1_irq_on();
    TDRUM->p[P_E0] = (int16_t)DRUM_SYN;
}
static void siren_service(void)
{
    uint32_t L;
    int w = ab.world % AB_NWORLDS;
    for (L = AL_DRONE; L <= AL_SWELL; L++) {
        int16_t want = ab.snd[L];
        const sd_span_t *sp = &SD_CUR[w][L];
        if (want < 0 || !sp->n || (want == sr.applied[L] && w == sr.applied_world[L]))
            continue;
        {
            uint32_t at = sp->first + (uint32_t)want % sp->n;
            const sd_snd_t *s = &SD_SND[SD_LIST[at]];
            int16_t pe, e = siren_find(s, &pe);
            track_t *t = SIREN_PART(L);
            sr.applied[L] = want; sr.applied_world[L] = (int16_t)w;
            if (e < 0)
                continue;
            sr.trim[L] = SD_TRIM[at]; sr.tone[L] = SD_TONE[at];
            siren_load_sound(t, (uint32_t)e, (uint32_t)pe);
            t->p[P_LEVEL] = siren_level(L); t->p[P_PAN] = ABYSS_MIX[L - 1][1];
            t->p[P_DLY] = ABYSS_MIX[L - 1][2]; t->p[P_REV] = ABYSS_MIX[L - 1][3]; t->p[P_CHOR] = 0;
            t->p[P_AMODE] = 0;                   /* (the brain is the arpeggiator) */
            abyss_env(L);
            sr.sound_eng[L] = e; sr.sound_pre[L] = pe;
            return;                              /* (one sound a call) */
        }
    }
    if (ab.kind != sr.ap_kind) { sr.ap_kind = ab.kind; abyss_env(AL_PAD); }
    if (ab.snd[AL_DRUMS] != sr.ap_kit || w != sr.applied_world[0]) {
        sr.ap_kit = ab.snd[AL_DRUMS]; sr.applied_world[0] = (int16_t)w;
        siren_apply_kit();
    }
}

static void siren_init(void)
{
    uint32_t w, L, i;
    for (w = 0; w < AB_NWORLDS; w++) {
        for (L = 0; L < AB_NLAYERS; L++)
            ab_pal.ncur[w][L] = L >= AL_DRONE && L <= AL_SWELL ? SD_CUR[w][L].n : 0;
        ab_pal.nkits[w] = SD_KIT_N[w];
    }
    ab_init(&ab, &ab_pal);
    for (i = 0; i < 7u; i++) sr.applied[i] = -1, sr.applied_world[i] = -1;
    sr.ap_kit = -1; sr.ap_kind = 0xFF; sr.gain_q12 = 4096; sr.nz = 0xAB1;
    sr.space = 100;
    for (i = 0; i < 8u; i++) sr.lspace[i] = 100;
    for (i = 0; i < NTRK; i++) { trk[i].p[P_MUTE] = 0; trk[i].p[P_AMODE] = 0; }
    trk[1].p[P_LYMODE] = 0;
    song.g[G_DRDLY] = 0;
    song.g[G_FILT] = 0;
    siren_service();
}

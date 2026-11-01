/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN: a rave that writes itself, on Clipper's engines. Six synth parts (the layers: bass, arp, hook, pad, stabs,
 * fx) and the drum track, played by the brain (siren_brain.c) a sixteenth at a time, in the audio interrupt
 * (events_block calls siren_block instead of the sequencer). Each track a sound picked by the brain from the style's
 * selection (siren_data.h, tools/gen_siren.py); the drums SIREN's own kits in the drum synth's first user kit, the kick
 * tuned to the key; the mix as on the prototype: the kick's duck, the build's high-pass on the master, the sends, the
 * WOB's wobble, the sub under a dubstep or drum & bass bass. The main loop applies the sounds the brain asks for
 * (siren_service) and runs the screen and the controls (siren_ui.c). Commands from the main loop to the brain go
 * through a ring (the brain runs in the interrupt only). */
#include "siren_brain.c"
#include "siren_data.h"

enum { SL_BASS, SL_ARP, SL_HOOK, SL_PAD, SL_STAB, SL_FX };        /* the parts: track = layer - 1 */
#define SIREN_PART(L) (&trk[(L) - 1])
/* each layer's place in the mix (the prototype's): level (P_LEVEL: 112 = 0 dB, 2 a dB), pan, delay, reverb */
static const int8_t SIREN_MIX[6][4] = {
    {110, 0, 0, 0},      /* BASS */
    {97, -16, 40, 32},   /* ARP */
    {102, 10, 36, 38},   /* HOOK */
    {97, 0, 6, 70},      /* PAD */
    {100, 16, 25, 38},   /* STAB */
    {95, 0, 0, 57},      /* FX */
};
/* each style's drums: level (x/100), reverb send, delay send (the drum track's), the rumble */
static const uint8_t SIREN_DMIX[SB_NSTYLES][3] = {
    {100, 12, 6}, {100, 30, 8}, {95, 12, 12}, {100, 8, 4}, {100, 16, 6}, {100, 18, 6}, {100, 14, 6}, {104, 28, 4},
};

static sb_brain_t sb;
static sb_palette_t sb_pal;
static struct {
    volatile uint8_t playing;
    int32_t until;                               /* units to the next sixteenth */
    struct { uint8_t trk, note; int32_t left; } off[64]; uint8_t noff;
    /* the bass's wobble (WOB): its phase (Q32) and increment a unit, its shape and depth */
    uint32_t wph, winc; uint8_t wshape, won; uint16_t wdepth;
    /* the sub: a sine under the bass */
    uint32_t sph, sinc; int32_t senv, sgate; uint8_t snote;
    /* the master: gain (Q12), duck depth (Q15 per mille x 32.767) */
    int32_t gain_q12, duck;
    /* what the screen shows: the last step, the notes for the sparks, the scope */
    volatile uint32_t seq; sb_events_t ev; uint8_t sec_names[8];
    volatile uint32_t kicks, notes_n; uint8_t notes_l[16], notes_k[16];
    int16_t scope[512]; uint32_t sw;
    /* the commands from the main loop (siren_cmd) */
    volatile uint32_t cw, cr; struct { uint8_t op; int16_t a, b; uint32_t c; } cmd[16];
    /* the sounds as applied (main loop): the selection index per layer, the kit (-1: none yet), and the style
     * they were picked in; the brain's own, as it last asked */
    int16_t applied[7], applied_style[7];
    int16_t ap_kick; uint16_t ap_kn16;
    uint8_t mute[8];
    int16_t sound_eng[SB_NLAYERS], sound_pre[SB_NLAYERS];   /* (the screen: what plays) */
} sr;
enum { SC_PLAY, SC_STOP, SC_KNOB, SC_DIVE, SC_BREAK, SC_STYLE, SC_SEED, SC_REROLL, SC_LOCK, SC_STYLELOCK };

/* the main loop's way into the brain */
static void siren_cmd(uint32_t op, int32_t a, int32_t b, uint32_t c)
{
    uint32_t w = sr.cw;
    if (w - sr.cr >= 16u)
        return;                                  /* (full: the hand was too fast, drop it) */
    sr.cmd[w % 16u].op = (uint8_t)op; sr.cmd[w % 16u].a = (int16_t)a; sr.cmd[w % 16u].b = (int16_t)b; sr.cmd[w % 16u].c = c;
    RING_PUBLISH();
    sr.cw = w + 1u;
}
static void siren_all_off(void)
{
    uint32_t i;
    for (i = 0; i < sr.noff; i++)
        if (sr.off[i].trk != TRK_DRUM)
            trk_note_off1(&trk[sr.off[i].trk], sr.off[i].note);
    sr.noff = 0;
    for (i = 0; i < NPART; i++)
        trk_all_off1(&trk[i]);
    sr.sgate = 0; sr.won = 0;
}
static void siren_commands(void)                 /* (audio interrupt) */
{
    while (sr.cr != sr.cw) {
        uint32_t i = sr.cr % 16u;
        int32_t a = sr.cmd[i].a, b = sr.cmd[i].b;
        switch (sr.cmd[i].op) {
        case SC_PLAY: if (!sr.playing) { sr.playing = 1; sr.until = 0; } break;
        case SC_STOP: sr.playing = 0; siren_all_off(); break;
        case SC_KNOB: sb_knob(&sb, a, b); break;
        case SC_DIVE: sb.dive = (uint8_t)a; break;
        case SC_BREAK: sb.break_req = 1; break;
        case SC_STYLE: siren_all_off(); sb_set_seed(&sb, sb.seed, a); break;
        case SC_SEED: siren_all_off(); sb_set_seed(&sb, sr.cmd[i].c, a); break;
        case SC_REROLL: sb_reroll(&sb, a); break;
        case SC_LOCK: sb_lock(&sb, a, b); break;
        case SC_STYLELOCK: sb.style_lock = (uint8_t)a; break;
        }
        sr.cr++;
    }
}

/* the bass's wobble, this block: its LFO (the brain's rate, shape, depth for each hit) into WOB's wob_lfo */
static void siren_wobble(uint32_t adv)
{
    int32_t v;
    uint32_t ph = sr.wph;
    if (!sr.won) { wob_lfo[SL_BASS] = 19660; return; }
    switch (sr.wshape) {
    case 0: v = 16384 - (sine_i(ph + 0x40000000u) >> 1); break;   /* sine, from the bottom */
    case 1: v = 32767 - (int32_t)(ph >> 17); break;               /* saw down */
    case 2: v = ph < 0x80000000u ? 32767 : 2621; break;           /* square */
    default: v = (int32_t)((ph < 0x80000000u ? ph : ~ph) >> 16); break;   /* triangle */
    }
    v = 9830 + (v - 9830) * sr.wdepth / 1000;                     /* (depth around 0.3) */
    wob_lfo[SL_BASS] = clamp(v, 0, 32767);
    sr.wph = ph + adv * sr.winc;
}

/* one sixteenth: the brain's events into the parts and the drums */
static void siren_fire(void)
{
    sb_events_t *ev = &sr.ev;
    uint32_t i, u16 = BEAT_U / 4u;
    sb_step16(&sb, ev);
    song.g[G_BPM] = ev->bpm;
    for (i = 0; i < ev->nnotes; i++) {
        const sb_note_t *x = &ev->notes[i];
        if (sr.mute[x->layer])
            continue;
        if (x->layer == L_SUB) {
            sr.snote = x->note; sr.sinc = SD_NOTE_INC[x->note & 127u]; sr.sgate = x->vel * 258;
        } else {
            uint32_t tk = x->layer == L_DRUMS ? TRK_DRUM : (uint32_t)x->layer - 1u, k;
            if (x->layer == L_DRUMS) {
                trk_note_on1(TDRUM, x->note, x->vel);
                if (x->note == 36u) sr.kicks++;
                continue;
            }
            for (k = 0; k < sr.noff; k++)                       /* (the same note again: the last one ends first) */
                if (sr.off[k].trk == tk && sr.off[k].note == x->note) { trk_note_off1(&trk[tk], x->note); sr.off[k] = sr.off[--sr.noff]; break; }
            trk_note_on1(&trk[tk], x->note, x->vel);
        }
        if (sr.noff < 64u) {
            sr.off[sr.noff].trk = (uint8_t)(x->layer == L_SUB ? 0xFFu : (uint32_t)x->layer - 1u);
            sr.off[sr.noff].note = x->note;
            sr.off[sr.noff].left = (int32_t)(x->len * (u16 / 100u) * 92u);
            sr.noff++;
        }
        { uint32_t q = sr.notes_n % 16u; sr.notes_l[q] = x->layer; sr.notes_k[q] = x->note; sr.notes_n++; }   /* (the screen's sparks) */
    }
    if (ev->has_wob && !sr.mute[L_BASS]) {
        sr.won = 1; sr.wph = 0; sr.wshape = ev->wob.shape; sr.wdepth = ev->wob.depth;
        sr.winc = 19478u * 1000u / (uint32_t)(ev->wob.rate3 ? ev->wob.rate3 : 12) / 1000u;   /* 3 x 2^32 / (rate3 x a sixteenth's units) */
    }
    /* the filters: each part's one-knob filter, closed as the energy falls (the sound's own cutoff kept above 0.7) */
    for (i = L_BASS; i <= L_FX; i++) {
        track_t *t = SIREN_PART(i);
        int32_t c = ev->cut[i];
        if (c < 0)
            continue;
        t->p[P_TFLT] = (int16_t)(i == L_FX ? (c >= 950 ? 0 : -(950 - c) * 45 / 850) : (c >= 700 ? 0 : -(700 - c) * 40 / 700));
    }
    /* the master: the build's high-pass, the duck, the reverb (the parts' sends), the gain */
    song.g[G_FILT] = (int16_t)(ev->hp * 63 / 1000);
    sr.duck = ev->duck * 33;
    for (i = 0; i < 6u; i++)
        trk[i].p[P_REV] = (int16_t)(SIREN_MIX[i][3] * (450 + ev->verb / 2) / 1000);
    sr.gain_q12 = ev->gain * 4096 / 1000 * 11 / 10;
    sr.seq++;
}

/* (audio interrupt, events_block) the brain's clock: n samples, adv units */
static void siren_block(uint32_t n, uint32_t adv)
{
    uint32_t i;
    (void)n;
    siren_commands();
    for (i = 0; i < sr.noff;) {                                     /* the note-offs due */
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
            int32_t u16 = (int32_t)(BEAT_U / 4u), sw = sb.step % 2 ? sb.swing : -(int32_t)sb.swing;
            siren_fire();
            sr.until += u16 + u16 / 1000 * sw;
        }
    }
    siren_wobble(adv);
}

/* (audio interrupt, mix_block) the sub, into the dry mix, ducked with the parts */
static void siren_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    if (!sr.sgate && sr.senv < 8)
        return;
    for (i = 0; i < n; i++) {
        int32_t y;
        sr.senv += ((sr.sgate) - sr.senv) >> (sr.sgate ? 7 : 9);
        sr.sph += sr.sinc;
        y = softclip(mulq15(sine_i(sr.sph), 52428)) * sr.senv >> 15;   /* (a little warmth: tanh(1.6 sin)) */
        y = mulq15(y, duck.g1) * 3 >> 3;
        l[i] += y; r[i] += y;
    }
}
static int32_t siren_duck_depth(void) { return sr.duck; }
static int32_t siren_gain_q12(void) { return sr.gain_q12; }
static void siren_scope_tap(void)                /* (mix_block: the visualiser's taps, for the screen) */
{
    uint32_t i;
    for (i = 0; i < CTL; i += 2u) { sr.scope[sr.sw % 512u] = vis_tap[2u * i]; sr.sw++; }
}

/* ---- the main loop's side: the sounds the brain picked, applied (an engine and a preset per part), the kit */
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
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p);   /* (ui.c set_engine_of + apply_preset_to; host: tests) */
static void siren_apply_kit(void)
{
    const sd_kit_t *K;
    dsu_kit_t *u;
    int ki = sb.snd[L_DRUMS];
    if (ki < 0 || !SD_KIT_N[sb.style])
        return;
    K = &SD_KITS[SD_KIT_FIRST[sb.style] + ki % SD_KIT_N[sb.style]];
    (void)dsu_kit(0);                            /* (the bank set up) */
    u = &dsu.k[0];
    fm1_irq_off();
    memcpy(u->s, K->s, sizeof u->s);
    u->s[0].pitch = (uint8_t)(sb.kick_n16 >> 4); u->s[0].fine = (uint8_t)(sb.kick_n16 & 15u);
    u->s[14].pitch = (uint8_t)(sb.kick2_n16 >> 4); u->s[14].fine = (uint8_t)(sb.kick2_n16 & 15u);
    u->crush = K->crush;
    memcpy(u->name, K->name, 8);
    fm1_irq_on();
    TDRUM->p[P_E0] = (int16_t)DRUM_SYN;
    song.g[G_DRLVL] = (int16_t)SIREN_DMIX[sb.style][0];
    song.g[G_DRREV] = (int16_t)SIREN_DMIX[sb.style][1];
}
static void siren_service(void)
{
    uint32_t L;
    int style = sb.style;
    for (L = L_BASS; L <= L_FX; L++) {
        int16_t want = sb.snd[L];
        const sd_span_t *sp = &SD_CUR[style][L];
        if (want < 0 || !sp->n || (want == sr.applied[L] && style == sr.applied_style[L]))
            continue;
        {
            const sd_snd_t *s = &SD_SND[SD_LIST[sp->first + want % sp->n]];
            int16_t pe, e = siren_find(s, &pe);
            track_t *t = SIREN_PART(L);
            sr.applied[L] = want; sr.applied_style[L] = (int16_t)style;
            if (e < 0)
                continue;
            siren_load_sound(t, (uint32_t)e, (uint32_t)pe);
            t->p[P_LEVEL] = SIREN_MIX[L - 1][0]; t->p[P_PAN] = SIREN_MIX[L - 1][1];
            t->p[P_DLY] = SIREN_MIX[L - 1][2]; t->p[P_REV] = SIREN_MIX[L - 1][3]; t->p[P_CHOR] = 0;
            t->p[P_AMODE] = 0;                   /* (the brain is the arpeggiator: a preset's arp off) */
            sr.sound_eng[L] = e; sr.sound_pre[L] = pe;
        }
    }
    if (sb.snd[L_DRUMS] != sr.ap_kick || sb.kick_n16 != sr.ap_kn16 || style != sr.applied_style[0]) {
        sr.ap_kick = sb.snd[L_DRUMS]; sr.ap_kn16 = sb.kick_n16; sr.applied_style[0] = (int16_t)style;
        siren_apply_kit();
    }
}

static void siren_init(void)
{
    uint32_t s, L, i;
    for (s = 0; s < SB_NSTYLES; s++) {
        for (L = 0; L < SB_NLAYERS; L++)
            sb_pal.ncur[s][L] = L >= L_BASS && L <= L_FX ? SD_CUR[s][L].n : 0;
        sb_pal.nkits[s] = SD_KIT_N[s];
        sb_pal.kit_first[s] = SD_KIT_FIRST[s];
    }
    sb_pal.kick_n16 = SD_KICK_N16;
    sb_init(&sb, &sb_pal);
    for (i = 0; i < 7u; i++) sr.applied[i] = -1, sr.applied_style[i] = -1;
    sr.ap_kick = -1; sr.gain_q12 = 4096;
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_MUTE] = 0;
        trk[i].p[P_AMODE] = 0;
    }
    trk[1].p[P_LYMODE] = 0;                      /* (Clipper's layer: off, the parts are SIREN's) */
    song.g[G_DRDLY] = 0;                         /* (Clipper: the master compressor off) */
    song.g[G_FILT] = 0;
    siren_service();
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN: a rave that writes itself, on Clipper's engines. Six synth parts (the layers: bass, arp, hook, pad, stabs,
 * fx) and the drum track, played by the brain (siren_brain.c) a sixteenth at a time, in the audio interrupt
 * (events_block calls siren_block instead of the sequencer). Each track a sound picked by the brain from the style's
 * selection (siren_data.h, tools/gen_siren.py); the drums SIREN's own kits in the drum synth's first user kit, the kick
 * tuned to the key; the mix as on the prototype: the kick's duck, the build's high-pass on the master, the sends, the
 * WOB's wobble, the sub under a drum & bass bass. The main loop applies the sounds the brain asks for
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
    {100, 12, 6}, {100, 30, 8}, {95, 12, 12}, {100, 8, 4}, {100, 16, 6}, {100, 18, 6}, {100, 14, 6}, {100, 22, 8},   /* (.. DNB, HOUSE) */
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
    /* (0.2) the CPU guard: 0 the set as written; 1 pads and stabs two notes; 2 the FX layer out, the pad its root, the
     * arp in eighths. From the audio interrupt's own load (audio.c song.cpu_q8), with a hold (the hysteresis) */
    uint8_t lean; uint16_t lean_hold; uint16_t cpu_test;
    volatile uint32_t ld_peak, ld_sum, ld_n; uint32_t ld_track;   /* (0.4) the audio interrupt's load, each half buffer */
    /* (0.2) the DJ's hands (siren_px_*): the effect held, its argument, sixteenths and units since it began */
    volatile int8_t px; uint8_t px_arg, px_slot; uint32_t px_n16, px_u;
    uint32_t n16;                                /* sixteenths fired since power-on (the rolls' grid) */
    struct { uint8_t n; sb_note_t x[12]; } hist[8], loop[4];   /* the last eight sixteenths as written; a roll's loop */
    uint32_t r_d0; uint8_t r_len, r_32, r_on, r_mask; uint16_t r_rep; int32_t r_half;
    uint8_t m_layers, m_dlo, m_dhi, m_dall;       /* live notes held back: layers (bits), the kicks, the tops, drums */
    int16_t sv_dtime, sv_dfdbk, sv_dust, sv_rsize, sv_rdamp; uint8_t sv_on;
    int8_t eqk[3];                               /* the isolator: the master EQ's bands (bus.c, 12 flat .. 0 -12 dB) */
    int32_t spd_q12, bend, gate_q12;             /* the clock's speed (BRAKE, HALF), the parts' pitch, the gate */
    int32_t nz, nz1, nz2, nz_lvl;        /* the riser's noise */
    volatile int8_t q_style; uint8_t q_now;      /* (0.3) the style asked for, on the next phrase (-1 none); now */
    /* (0.5) the mixer: the master filter (K1, -63 low-pass .. 63 high-pass), the space (K3, % of the sends); each
     * layer's volume (P_LEVEL steps, 2 a dB), filter (-63..63), space (%); each sound's own trim and tone (its
     * calibration, siren_data.h SD_TRIM / SD_TONE: the same loudness in its layer, its hard highs softened) */
    int8_t ufilt; uint16_t space; int8_t lvol[7], lflt[7]; uint16_t lspace[7]; int8_t trim[7], tone[7];
    /* (0.5) the arrangement: when each melodic layer last had a note written (sixteenths), the ones allowed now (at
     * most three at once, by the section's priorities); a drum fill asked for, playing; the next track on the phrase */
    uint32_t last_note[7]; uint8_t allowed, focus, fill_req, fill_on, q_next;
    int16_t bcut[7];                             /* the brain's own filter for each layer, as it last wrote it */
} sr;
enum { SC_PLAY, SC_STOP, SC_KNOB, SC_DIVE, SC_BREAK, SC_STYLE, SC_SEED, SC_REROLL, SC_LOCK, SC_STYLELOCK, SC_PUNCH, SC_STYLEQ,
       SC_DROP, SC_FILL, SC_REMOTIF, SC_KEYUP, SC_NEXT, SC_STAY, SC_SOUND, SC_MIX };   /* (0.5) the black keys, the mixer */
enum { SM_FILT, SM_SPACE, SM_FOCUS, SM_LVOL = 10, SM_LFLT = 20, SM_LSPACE = 30 };  /* SC_MIX's targets (+ the layer) */

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
        if (sr.off[i].trk < NPART)               /* (not the sub's 0xFF, not the drums) */
            trk_note_off1(&trk[sr.off[i].trk], sr.off[i].note);
    sr.noff = 0;
    for (i = 0; i < NPART; i++)
        trk_all_off1(&trk[i]);
    sr.sgate = 0; sr.won = 0;
}
/* ---- (0.2) the DJ's hands: hold FX, press a white key, and the set goes through that effect while the key is held;
 * let it go and the set is back where it would have been (the brain never stops: a slip roll). SLOOP's punch-in
 * effects loop the mix's audio (64 KB of RAM SIREN's six parts have taken); SIREN writes the music, so it loops
 * and cuts the notes themselves (rolls, kills, cut, rewind) and moves the master (filter, echo, wash, gate, crush,
 * isolator, brake). Keys 1-12 the same in every style; 13-16 the style's own. */
enum { SPX_NONE, SPX_ROLL, SPX_PROLL, SPX_DROLL, SPX_LOW, SPX_HIGH, SPX_ECHO, SPX_DUB, SPX_WASH, SPX_KLOW, SPX_KMID, SPX_KHIGH,
       SPX_GATE, SPX_CRUSH, SPX_STRIP, SPX_CUT, SPX_BRAKE, SPX_HALF, SPX_REWIND, SPX_RISER, SPX_SQUELCH };
typedef struct { uint8_t fx, arg; const char *name; } siren_px_t;
#define SPX_COMMON {SPX_ROLL, 4, "ROLL 1/4"}, {SPX_ROLL, 2, "ROLL 1/8"}, {SPX_ROLL, 1, "ROLL 1/16"}, {SPX_ROLL, 0, "ROLL 1/32"}, \
    {SPX_LOW, 0, "LOW PASS"}, {SPX_HIGH, 0, "HIGH PASS"}, {SPX_ECHO, 0, "ECHO OUT"}, {SPX_WASH, 0, "WASH OUT"},          \
    {SPX_KLOW, 0, "KILL LOW"}, {SPX_KMID, 0, "KILL MID"}, {SPX_KHIGH, 0, "KILL HIGH"}, {SPX_GATE, 0, "GATE"}
static const siren_px_t SIREN_PX[SB_NSTYLES][16] = {
    {SPX_COMMON, {SPX_DUB, 6, "DUB ECHO"}, {SPX_CRUSH, 0, "CRUSH"}, {SPX_STRIP, 0, "KICK+BASS"}, {SPX_CUT, 0, "CUT"}},           /* TECHNO */
    {SPX_COMMON, {SPX_RISER, 0, "RISER"}, {SPX_PROLL, 1, "PITCH ROLL"}, {SPX_DUB, 6, "DUB ECHO"}, {SPX_CUT, 0, "CUT"}},          /* TRANCE */
    {SPX_COMMON, {SPX_DUB, 6, "DUB ECHO"}, {SPX_SQUELCH, 0, "SQUELCH"}, {SPX_PROLL, 1, "PITCH ROLL"}, {SPX_CRUSH, 0, "CRUSH"}}, /* PSY */
    {SPX_COMMON, {SPX_SQUELCH, 0, "SQUELCH"}, {SPX_DUB, 6, "DUB ECHO"}, {SPX_CRUSH, 0, "CRUSH"}, {SPX_CUT, 0, "CUT"}},          /* ACID */
    {SPX_COMMON, {SPX_PROLL, 1, "PITCH ROLL"}, {SPX_BRAKE, 0, "BRAKE"}, {SPX_CRUSH, 0, "CRUSH"}, {SPX_RISER, 0, "RISER"}},     /* RAVE */
    {SPX_COMMON, {SPX_CRUSH, 0, "CRUSH"}, {SPX_STRIP, 0, "KICK+BASS"}, {SPX_PROLL, 1, "PITCH ROLL"}, {SPX_CUT, 0, "CUT"}},      /* HARD */
    {SPX_COMMON, {SPX_REWIND, 0, "REWIND"}, {SPX_HALF, 0, "HALF TIME"}, {SPX_DROLL, 1, "DRUM ROLL"}, {SPX_CRUSH, 0, "CRUSH"}}, /* DNB */
    {SPX_COMMON, {SPX_STRIP, 0, "KICK+BASS"}, {SPX_DUB, 6, "DUB ECHO"}, {SPX_DROLL, 1, "DRUM ROLL"}, {SPX_CUT, 0, "CUT"}},       /* HOUSE */
};
#define SPX_ALL 0xFFu
#define SPX_MELO ((1u << L_ARP) | (1u << L_HOOK) | (1u << L_PAD) | (1u << L_STAB) | (1u << L_FX))
static int siren_drum_lo(uint32_t n) { return n == 35u || n == 36u; }
static int siren_drum_hi(uint32_t n) { return n == 42u || n == 46u || n == 49u || n == 51u || n == 70u || n == 56u || n == 75u; }
static void siren_parts_off(uint32_t layers)    /* the parts of these layers silent now (and the sub) */
{
    uint32_t L;
    for (L = L_BASS; L <= L_FX; L++)
        if ((layers >> L) & 1u)
            trk_all_off1(SIREN_PART(L));
    if ((layers >> L_SUB) & 1u)
        sr.sgate = 0;
}
static void siren_px_end(void)
{
    uint32_t i;
    int fx = sr.px;
    if (fx == SPX_NONE)
        return;
    sr.px = SPX_NONE;
    if (sr.sv_on) {
        song.g[G_DTIME] = sr.sv_dtime; song.g[G_DFDBK] = sr.sv_dfdbk; song.g[G_DUST] = sr.sv_dust;
        song.g[G_RSIZE] = sr.sv_rsize; song.g[G_RDAMP] = sr.sv_rdamp; sr.sv_on = 0;
    }
    for (i = 0; i < 6u; i++)
        trk[i].p[P_DLY] = SIREN_MIX[i][2];
    song.g[G_DRDLY] = (int16_t)SIREN_DMIX[sb.style][2];
    song.g[G_DRREV] = (int16_t)SIREN_DMIX[sb.style][1];
    sr.eqk[0] = sr.eqk[1] = sr.eqk[2] = 12;
    sr.m_layers = sr.m_dlo = sr.m_dhi = sr.m_dall = 0;
    sr.r_on = 0; sr.r_half = 0;
    sr.spd_q12 = 4096; sr.bend = 0; sr.gate_q12 = 4096; sr.nz_lvl = 0;
    song.g[G_FILT] = (int16_t)(sr.ev.hp * 63 / 1000);
    song.g[G_BPM] = sb.bpm;
    if (fx == SPX_REWIND) {                       /* the reload: the section again from its first beat, now */
        siren_all_off();
        sb_rewind(&sb);
        sr.until = 0;
    }
}
static void siren_px_start(uint32_t slot)
{
    const siren_px_t *k = &SIREN_PX[sb.style % SB_NSTYLES][slot % 16u];
    uint32_t i;
    siren_px_end();
    sr.px_slot = (uint8_t)(slot % 16u); sr.px_arg = k->arg; sr.px_n16 = 0; sr.px_u = 0;
    sr.sv_dtime = song.g[G_DTIME]; sr.sv_dfdbk = song.g[G_DFDBK]; sr.sv_dust = song.g[G_DUST];
    sr.sv_rsize = song.g[G_RSIZE]; sr.sv_rdamp = song.g[G_RDAMP]; sr.sv_on = 1;
    switch (k->fx) {
    case SPX_ROLL: case SPX_PROLL: case SPX_DROLL: {
        uint32_t L = k->arg ? k->arg : 1u, cur = sr.n16 ? sr.n16 - 1u : 0u;   /* (from the beat playing: its first */
        sr.r_len = (uint8_t)L; sr.r_32 = (uint8_t)(k->arg == 0); sr.r_d0 = cur - cur % 4u; sr.r_on = 0; sr.r_rep = 0;   /* sixteenth holds the kick) */
        sr.r_mask = (uint8_t)(k->fx == SPX_DROLL ? 1u << L_DRUMS : SPX_ALL);
        if (sr.r_d0 >= 4u && sr.r_d0 + L <= sr.n16) {   /* (nothing in it, as written: the beat before, if it had) */
            uint32_t j, here = 0, before = 0;
            for (j = 0; j < L; j++) { here += sr.hist[(sr.r_d0 + j) % 8u].n; before += sr.hist[(sr.r_d0 - 4u + j) % 8u].n; }
            if (!here && before) sr.r_d0 -= 4u;
        }
        break;
    }
    case SPX_ECHO:                                /* everything into a long echo, then the live notes stop: the echo out */
        song.g[G_DTIME] = 0; song.g[G_DFDBK] = 100;
        for (i = 0; i < 6u; i++) trk[i].p[P_DLY] = 120;
        song.g[G_DRDLY] = 110;
        break;
    case SPX_DUB:                                 /* a dub throw: the leads and the snare into a dotted echo, the set goes on */
        song.g[G_DTIME] = k->arg; song.g[G_DFDBK] = 88;
        trk[L_ARP - 1].p[P_DLY] = 110; trk[L_HOOK - 1].p[P_DLY] = 120; trk[L_STAB - 1].p[P_DLY] = 120;
        song.g[G_DRDLY] = 70;
        break;
    case SPX_WASH:                                /* into a long reverb, then the live notes stop: the wash out */
        song.g[G_DRREV] = 110; song.g[G_RSIZE] = 124; song.g[G_RDAMP] = 30;
        break;
    case SPX_KLOW: sr.eqk[0] = 0; sr.m_layers = (1u << L_BASS) | (1u << L_SUB); sr.m_dlo = 1; siren_parts_off(sr.m_layers); break;
    case SPX_KMID: sr.eqk[1] = 2; sr.m_layers = SPX_MELO; siren_parts_off(SPX_MELO); break;
    case SPX_KHIGH: sr.eqk[2] = 0; sr.m_dhi = 1; break;
    case SPX_CRUSH: song.g[G_DUST] = 112; break;
    case SPX_STRIP: sr.m_layers = SPX_MELO; sr.m_dhi = 1; siren_parts_off(SPX_MELO); break;
    case SPX_CUT: sr.m_layers = SPX_ALL & ~(1u << L_DRUMS); sr.m_dall = 1; siren_all_off(); break;
    case SPX_HALF: sr.spd_q12 = 2048; break;
    case SPX_RISER: case SPX_REWIND: sr.nz_lvl = 0; break;
    default: break;
    }
    sr.px = (int8_t)k->fx;
}
/* (audio interrupt, every block) the effects that move: filters, gate, brake, rewind, squelch, riser */
static void siren_px_block(uint32_t adv)
{
    int32_t u16 = (int32_t)(BEAT_U / 4u), p;
    uint32_t i;
    if (sr.px == SPX_NONE)
        return;
    sr.px_u += adv;
    p = (int32_t)(sr.px_u / (uint32_t)(u16 / 8));                  /* eighths of a sixteenth since it began */
    switch (sr.px) {
    case SPX_LOW: song.g[G_FILT] = (int16_t)-(10 + sb_clamp(p * 50 / 256, 0, 50)); break;    /* over two bars */
    case SPX_HIGH: song.g[G_FILT] = (int16_t)(10 + sb_clamp(p * 46 / 256, 0, 46)); break;
    case SPX_PROLL: song.g[G_FILT] = (int16_t)sb_clamp(sr.r_rep * 3, 0, 42); break;
    case SPX_RISER:                               /* four bars of noise, rising */
        sr.nz_lvl = sb_clamp(p * 32767 / 512, 0, 32767);
        song.g[G_FILT] = (int16_t)sb_clamp(p * 30 / 512, 0, 30);
        break;
    case SPX_ECHO: case SPX_WASH:
        if (sr.px_n16 >= 4u) { sr.m_layers = SPX_ALL & ~(1u << L_DRUMS); sr.m_dall = 1; }
        if (sr.px == SPX_WASH) { for (i = 0; i < 6u; i++) trk[i].p[P_REV] = 127; song.g[G_FILT] = 16; }
        break;
    case SPX_GATE: {                              /* sixteenths: open for the first half of each */
        int32_t into = u16 - (sr.until > 0 ? sr.until : 0);
        sr.gate_q12 = into < u16 / 2 ? 4096 : 330;
        break;
    }
    case SPX_BRAKE: case SPX_REWIND: {             /* the clock to a stop over a beat (REWIND: half), the pitch down with it */
        int32_t len = sr.px == SPX_BRAKE ? 32 * 8 / 2 : 32 * 8 / 4;        /* in eighths of a sixteenth: a beat, half */
        int32_t left = sb_clamp(len - p, 0, len);
        sr.spd_q12 = left * 4096 / len;
        sr.bend = sr.px == SPX_BRAKE ? -(4096 - sr.spd_q12) * 24 : (p < len / 3 ? p * 4096 * 12 / (len / 3) : -(4096 - sr.spd_q12) * 30);
        if (sr.px == SPX_REWIND) sr.nz_lvl = left ? 26000 : 0;
        if (!left && !sr.m_dall) { sr.m_layers = SPX_ALL & ~(1u << L_DRUMS); sr.m_dall = 1; siren_all_off(); }
        break;
    }
    case SPX_SQUELCH: {                           /* the bass, the arp, the hook's filters swept by a half-bar wave */
        int32_t w = (int32_t)((sr.px_u % (uint32_t)(u16 * 8)) * 64 / (uint32_t)(u16 * 8)), tri = w < 32 ? w : 63 - w;
        trk[L_BASS - 1].p[P_TFLT] = trk[L_ARP - 1].p[P_TFLT] = trk[L_HOOK - 1].p[P_TFLT] = (int16_t)-(6 + (31 - tri));
        break;
    }
    case SPX_HALF: song.g[G_BPM] = (int16_t)(sb.bpm / 2u); break;
    default: break;
    }
}
static int32_t siren_bend(void) { return sr.bend; }                /* (voice.c: 1/4096 semitone on every part) */
static int32_t siren_eq_db(uint32_t band) { return sr.eqk[band % 3u]; }   /* (bus.c) */

/* (0.5) PRESETS turned on a layer: the next or previous sound of the style's selection for it (a kit for the drums),
 * kept from then on (the layer locked: the brain's next tracks leave it) */
static void siren_sound_step(int32_t L, int32_t d)
{
    const sb_palette_t *p = sb.pal;
    int32_t n;
    if (L < 0 || L > L_FX || !p)
        return;
    if (L == L_DRUMS) {
        n = p->nkits[sb.style];
        if (n) { sb.kit_pick = (uint32_t)((sb.snd[L_DRUMS] + d % n + n) % n); sb_pick_sounds(&sb); }
        return;
    }
    n = p->ncur[sb.style][L];
    if (n < 1)
        return;
    sb.snd[L] = (int16_t)((sb.snd[L] + d % n + n) % n);
    sb.locks[L] = 1; sb.sounds_changed = 1;
}
static void siren_mix_set(int32_t what, int32_t v)
{
    if (what == SM_FILT) sr.ufilt = (int8_t)sb_clamp(v, -63, 63);
    else if (what == SM_SPACE) sr.space = (uint16_t)sb_clamp(v, 0, 200);
    else if (what == SM_FOCUS) sr.focus = (uint8_t)(v > 0 && v <= L_FX ? v : 0);   /* (the layer in the hands: it plays) */
    else if (what >= SM_LVOL && what < SM_LVOL + 7) sr.lvol[what - SM_LVOL] = (int8_t)sb_clamp(v, -30, 12);
    else if (what >= SM_LFLT && what < SM_LFLT + 7) sr.lflt[what - SM_LFLT] = (int8_t)sb_clamp(v, -63, 63);
    else if (what >= SM_LSPACE && what < SM_LSPACE + 7) sr.lspace[what - SM_LSPACE] = (uint16_t)sb_clamp(v, 0, 200);
}

static void siren_commands(void)                 /* (audio interrupt) */
{
    while (sr.cr != sr.cw) {
        uint32_t i = sr.cr % 16u;
        int32_t a = sr.cmd[i].a, b = sr.cmd[i].b;
        switch (sr.cmd[i].op) {
        case SC_PLAY: if (!sr.playing) { sr.playing = 1; sr.until = 0; } break;
        case SC_STOP: siren_px_end(); sr.playing = 0; siren_all_off(); break;
        case SC_KNOB: sb_knob(&sb, a, b); break;
        case SC_DIVE: sb.dive = (uint8_t)a; break;
        case SC_BREAK: sb.break_req = 1; break;
        case SC_STYLE: siren_px_end(); siren_all_off(); sb_set_seed(&sb, sb.seed, a); break;
        case SC_SEED: siren_px_end(); siren_all_off(); sb_set_seed(&sb, sr.cmd[i].c, a); break;
        case SC_REROLL: sb_reroll(&sb, a); break;
        case SC_LOCK: sb_lock(&sb, a, b); break;
        case SC_STYLELOCK: sb.style_lock = (uint8_t)a; break;
        case SC_PUNCH: if (a < 0) siren_px_end(); else siren_px_start((uint32_t)a); break;
        case SC_STYLEQ: if (a == sr.q_style) sr.q_now = 1; else { sr.q_style = (int8_t)a; sr.q_now = 0; } break;   /* (twice: now) */
        case SC_DROP: sb.drop_req = 1; break;
        case SC_FILL: sr.fill_req = 1; break;
        case SC_REMOTIF: if (a >= 0 && a < 7) sb_remotif(&sb, a, b); break;
        case SC_KEYUP: sb.key_req = 1; break;
        case SC_NEXT: sr.q_next = (uint8_t)(sr.q_next ? 2 : 1); break;   /* (twice: now) */
        case SC_STAY: sb.stay = (uint8_t)!sb.stay; break;
        case SC_SOUND: siren_sound_step(a, b); break;
        case SC_MIX: siren_mix_set(a, b); break;
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

/* one note into its part (tr: semitones up, the pitch roll), its end len units on */
static void siren_play(const sb_note_t *x, int tr, uint32_t len)
{
    uint32_t n = (uint32_t)sb_clamp((int)x->note + (x->layer == L_DRUMS ? 0 : tr), 0, 127), tk = 0xFFu, k;
    if (sr.mute[x->layer] || (x->layer >= L_ARP && x->layer <= L_FX && !((sr.allowed >> x->layer) & 1u)))
        return;                                  /* (0.5: a melodic layer over the three the section affords) */
    if (x->layer == L_DRUMS) {
        trk_note_on1(TDRUM, x->note, x->vel);
        if (x->note == 36u) sr.kicks++;
        return;
    }
    if (x->layer == L_SUB) {
        sr.snote = (uint8_t)n; sr.sinc = SD_NOTE_INC[n]; sr.sgate = x->vel * 258;
    } else {
        tk = (uint32_t)x->layer - 1u;
        for (k = 0; k < sr.noff; k++)                       /* (the same note again: the last one ends first) */
            if (sr.off[k].trk == tk && sr.off[k].note == n) { trk_note_off1(&trk[tk], n); sr.off[k] = sr.off[--sr.noff]; break; }
        trk_note_on1(&trk[tk], n, x->vel);
    }
    if (sr.noff < 64u) {
        sr.off[sr.noff].trk = (uint8_t)tk; sr.off[sr.noff].note = (uint8_t)n; sr.off[sr.noff].left = (int32_t)len; sr.noff++;
    }
    { uint32_t q = sr.notes_n % 16u; sr.notes_l[q] = x->layer; sr.notes_k[q] = (uint8_t)n; sr.notes_n++; }   /* (the screen's sparks) */
}
static int siren_held_back(const sb_note_t *x)   /* (0.2) a live note an effect keeps out (a kill, the cut, the outs) */
{
    if ((sr.m_layers >> x->layer) & 1u)
        return 1;
    return x->layer == L_DRUMS && (sr.m_dall || (sr.m_dlo && siren_drum_lo(x->note)) || (sr.m_dhi && siren_drum_hi(x->note)));
}
/* (0.4) what the guard leaves out, level by level: the notes that cost the most and say the least first (a pad's
 * upper notes, ghost drums, the FX layer outside a build), then the busy ones (the arp and the hats in eighths), then
 * the pad and the arp (the bare groove: drums, bass, hook, stabs) */
static int siren_lean_skip(const sb_note_t *x, const sb_events_t *ev, uint32_t *npad, uint32_t *nstab)
{
    uint32_t L = x->layer, lv = sr.lean, odd = ev->step & 1u;
    if (L == L_PAD) return (lv >= 3u && ev->sec != SEC_BREAK && ev->sec != SEC_INTRO && ev->sec != SEC_OUTRO) || ++*npad > (lv >= 2u ? 2u : 3u);
    if (L == L_STAB) return ++*nstab > 2u;
    if (L == L_FX) return ev->sec != SEC_BUILD;
    if (L == L_ARP) return lv >= 3u || (lv >= 2u && odd);
    if (L == L_DRUMS) {
        uint32_t n = x->note, core = n == 35u || n == 36u || n == 38u || n == 39u || n == 40u || n == 49u;
        if (!core && x->vel < 70u) return 1;                                  /* the ghosts */
        if (lv >= 2u && odd && (n == 42u || n == 44u || n == 70u)) return 1;  /* the hats and shakers in eighths */
        if (lv >= 3u && !core && n != 42u && n != 46u) return 1;              /* the percussion */
    }
    return 0;
}
/* (0.4) each layer's voices at most (voice.c trk_nvoice): what it needs (a bass one and its release, a pad its chord),
 * fewer as the guard rises; the stolen voices fade out (voice_kill) */
static uint32_t siren_poly(uint32_t part)
{
    static const uint8_t CAP[4][6] = {{2, 3, 3, 5, 4, 2}, {2, 2, 3, 4, 3, 1}, {2, 2, 2, 3, 3, 1}, {2, 1, 2, 2, 2, 1}};
    return CAP[sr.lean & 3u][part % 6u];
}
/* (0.4) the guard went up: the voices over a layer's new cap go now (faded out, voice.c voice_kill), the oldest last */
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
static void siren_load_half(uint32_t pct) { if (pct > sr.ld_peak) sr.ld_peak = pct; sr.ld_sum += pct; sr.ld_n++; }   /* (audio.c) */

/* (0.2) a roll's sixteenth: the loop's notes again (the layers it holds), as long as the loop at most */
static void siren_replay(uint32_t k, uint32_t len16, uint32_t u16)
{
    uint32_t i;
    int tr = sr.px == SPX_PROLL ? sb_clamp((int)sr.r_rep, 0, 12) : 0;
    for (i = 0; i < sr.loop[k].n; i++) {
        const sb_note_t *x = &sr.loop[k].x[i];
        uint32_t l = x->len < len16 ? x->len : len16;
        if ((sr.r_mask >> x->layer) & 1u)
            siren_play(x, tr, sr.r_32 ? u16 / 200u * 92u : l * (u16 / 100u) * 92u);
    }
    sr.r_rep++;
}

/* (0.5) the arrangement: at most three melodic layers at once over the bass and the drums (arp, hook, pad, stabs, fx),
 * the ones the section needs most among those the brain wrote for in the last eight bars (and the one in the hands
 * first). The others wait, their notes still written (a slot freed, a mute, the next section: they are back) */
static const uint8_t SIREN_PRI[7][5] = {
    {L_PAD, L_ARP, L_HOOK, L_FX, L_STAB},       /* INTRO */
    {L_ARP, L_HOOK, L_STAB, L_PAD, L_FX},       /* GROOVE */
    {L_FX, L_ARP, L_PAD, L_HOOK, L_STAB},       /* BUILD */
    {L_HOOK, L_STAB, L_ARP, L_PAD, L_FX},       /* DROP */
    {L_PAD, L_HOOK, L_ARP, L_FX, L_STAB},       /* BREAK */
    {L_HOOK, L_ARP, L_STAB, L_PAD, L_FX},       /* PEAK */
    {L_PAD, L_ARP, L_HOOK, L_STAB, L_FX},       /* OUTRO */
};
#define SIREN_MELO3 ((1u << L_ARP) | (1u << L_HOOK) | (1u << L_PAD) | (1u << L_STAB) | (1u << L_FX))
static int siren_wrote(uint32_t L, uint32_t now) { return sr.last_note[L] && now + 1u - sr.last_note[L] <= 128u; }
static void siren_arrange(uint32_t sec, uint32_t now)
{
    uint32_t k, n = 0, a = 0, gone, L;
    if (sr.focus >= L_ARP && sr.focus <= L_FX && !sr.mute[sr.focus]) { a = 1u << sr.focus; n = 1; }
    for (k = 0; k < 5u && n < 3u; k++) {
        L = SIREN_PRI[sec % 7u][k];
        if (((a >> L) & 1u) || sr.mute[L] || !siren_wrote(L, now))
            continue;
        a |= 1u << L; n++;
    }
    gone = sr.allowed & SIREN_MELO3 & ~a;
    for (L = L_ARP; L <= L_FX; L++)
        if ((gone >> L) & 1u)
            trk_all_off1(SIREN_PART(L));         /* (released: its tail fades) */
    sr.allowed = (uint8_t)(a | (1u << L_DRUMS) | (1u << L_BASS) | (1u << L_SUB));
}
/* (0.5) the black key FILL: the second half of the next bar a fill on the snare and the toms, rising, then a crash */
static void siren_fill(uint32_t step)
{
    static const uint8_t FN[8] = {38, 50, 38, 47, 38, 45, 38, 41};
    sb_note_t x;
    uint32_t k = step - 8u;
    x.layer = L_DRUMS; x.len = 1;
    x.note = FN[k]; x.vel = (uint8_t)(62 + k * 8u);
    if (!siren_held_back(&x)) siren_play(&x, 0, 0);
    if (k >= 4u && (k & 1u)) { x.note = 38; if (!siren_held_back(&x)) siren_play(&x, 0, 0); }
}
static int16_t siren_level(uint32_t L)           /* a part's level: its layer's place, its sound's trim, the hand's */
{
    return (int16_t)sb_clamp(SIREN_MIX[L - 1u][0] + sr.trim[L] + sr.lvol[L], 1, 127);
}
/* (0.5) the mixer, every sixteenth: each layer's level, filter (the brain's, the sound's tone ceiling, the hand's:
 * down a low-pass deeper, up a high-pass), sends (the space, the master's and the layer's); the drums' the same; the
 * master's filter (K1: the hand's low-pass, or its high-pass over the build's) */
static void siren_mixer(const sb_events_t *ev)
{
    uint32_t L, st = sb.style % SB_NSTYLES;
    int32_t ls, hp = ev->hp * 63 / 1000, keep = sr.px == SPX_ECHO || sr.px == SPX_DUB || sr.px == SPX_WASH;
    for (L = L_BASS; L <= L_FX; L++) {
        track_t *t = SIREN_PART(L);
        int32_t lp = sr.bcut[L] < sr.tone[L] ? sr.bcut[L] : sr.tone[L], f = sr.lflt[L];
        ls = (int32_t)sr.lspace[L] * sr.space / 100;
        t->p[P_TFLT] = (int16_t)(f > 0 ? f : sb_clamp(lp + f, -60, 0));
        t->p[P_LEVEL] = siren_level(L);
        t->p[P_REV] = (int16_t)sb_clamp(SIREN_MIX[L - 1u][3] * (450 + ev->verb / 2) / 1000 * ls / 100, 0, 127);
        if (!keep) t->p[P_DLY] = (int16_t)sb_clamp(SIREN_MIX[L - 1u][2] * ls / 100, 0, 127);
    }
    ls = (int32_t)sr.lspace[L_DRUMS] * sr.space / 100;
    song.g[G_DRLVL] = (int16_t)sb_clamp(SIREN_DMIX[st][0] * (100 + sr.lvol[L_DRUMS] * 6) / 100, 0, 127);
    TDRUM->p[P_TFLT] = sr.lflt[L_DRUMS];
    if (!keep) {
        song.g[G_DRREV] = (int16_t)sb_clamp(SIREN_DMIX[st][1] * ls / 100, 0, 127);
        song.g[G_DRDLY] = (int16_t)sb_clamp(SIREN_DMIX[st][2] * ls / 100, 0, 127);
    }
    song.g[G_FILT] = (int16_t)(sr.ufilt < 0 ? sr.ufilt : sr.ufilt > hp ? sr.ufilt : hp);
}

/* one sixteenth: the brain's events into the parts and the drums */
static void siren_fire(void)
{
    sb_events_t *ev = &sr.ev;
    uint32_t i, u16 = BEAT_U / 4u, s, npad = 0, nstab = 0, roll;
    if (sr.q_style >= 0 && (sr.q_now || (sb.step == 0 && sb.phrase_bar == 0))) {   /* (0.3) the style asked for: on the */
        int st = sr.q_style;                                                     /* phrase, as a DJ mixes in (or now) */
        sr.q_style = -1; sr.q_now = 0;
        siren_px_end(); siren_all_off(); sb_set_seed(&sb, sb.seed, st);
    }
    if (sr.q_next && (sr.q_next == 2u || (sb.step == 0 && sb.phrase_bar == 0))) {   /* (0.5) NEXT: on the phrase (twice: now) */
        sr.q_next = 0; sr.fill_on = 0;
        siren_px_end(); siren_all_off(); sb_next_track(&sb);
    }
    s = sr.n16++;
    sb_step16(&sb, ev);
    for (i = 0; i < ev->nnotes; i++)                 /* (0.5) the layers the brain writes for, for the arrangement */
        if (ev->notes[i].layer < 7u) sr.last_note[ev->notes[i].layer] = s + 1u;
    if (ev->step == 0 || !sr.allowed)
        siren_arrange(ev->sec, s);
    if (sr.fill_req && ev->step == 8) { sr.fill_req = 0; sr.fill_on = 1; }
    song.g[G_BPM] = ev->bpm;
    {   /* (0.4) the CPU guard: the real load of every half buffer since the last sixteenth (audio.c, %), the worst and
         * the mean. Up a level at once when a half went over 90 % (or the mean over 80 %), at least one at 78 % and
         * for the first bars of a new track (six sounds changing, the brain's new track); down one when it has been
         * under 62 % for two bars. 0 the set as written .. 3 the bare groove (siren_fire, siren_poly) */
        uint32_t pk = sr.ld_peak, avg = sr.ld_n ? sr.ld_sum / sr.ld_n : 0u;
        sr.ld_peak = 0; sr.ld_sum = 0; sr.ld_n = 0;
        if (sr.cpu_test) pk = avg = sr.cpu_test;                                 /* (the host tests: a load told) */
        if (!sr.cpu_test && sb.track_no != sr.ld_track) { sr.ld_track = sb.track_no; if (sr.lean < 1u) sr.lean = 1; if (sr.lean_hold < 32u) sr.lean_hold = 32; }
        if (pk >= 90u || avg >= 80u) { if (sr.lean < 3u) sr.lean++, siren_trim_voices(); sr.lean_hold = 32; }
        else if (pk >= 78u && sr.lean < 1u) { sr.lean = 1; sr.lean_hold = 32; siren_trim_voices(); }
        else if (sr.lean_hold) sr.lean_hold--;
        else if (sr.lean && pk < 62u) { sr.lean--; sr.lean_hold = 32; }
    }
    roll = sr.px == SPX_ROLL || sr.px == SPX_PROLL || sr.px == SPX_DROLL;
    if (!sr.r_on) {                                  /* (0.2) the notes as written, for a roll */
        uint32_t h = s % 8u;
        sr.hist[h].n = 0;
        for (i = 0; i < ev->nnotes && sr.hist[h].n < 12u; i++)
            sr.hist[h].x[sr.hist[h].n++] = ev->notes[i];
    }
    if (roll && !sr.r_on && s >= sr.r_d0 + sr.r_len) {   /* the loop closes: from here, its notes again */
        uint32_t k;
        uint32_t any = 0;
        for (k = 0; k < sr.r_len; k++) { sr.loop[k] = sr.hist[(sr.r_d0 + k) % 8u]; any += sr.loop[k].n; }
        if (!any) sr.r_mask = 0;                     /* (an empty loop, a break's: the set plays through it) */
        sr.r_on = 1;
    }
    for (i = 0; i < ev->nnotes; i++) {
        const sb_note_t *x = &ev->notes[i];
        if ((sr.r_on && ((sr.r_mask >> x->layer) & 1u)) || siren_held_back(x))
            continue;
        if (sr.lean && siren_lean_skip(x, ev, &npad, &nstab))
            continue;
        if (sr.fill_on && x->layer == L_DRUMS && (ev->step >= 8 ? x->note >= 38u && x->note <= 40u : x->note == 49u))
            continue;                            /* (the fill's snare is the snare; its crash the crash) */
        siren_play(x, 0, (uint32_t)(x->len * (u16 / 100u) * 92u));
    }
    if (sr.fill_on) {
        if (ev->step >= 8) siren_fill(ev->step);
        else {
            sb_note_t x; x.layer = L_DRUMS; x.note = 49; x.vel = 112; x.len = 1;
            sr.fill_on = 0;
            if (!siren_held_back(&x)) siren_play(&x, 0, 0);
        }
    }
    if (sr.r_on) {
        siren_replay((s - sr.r_d0) % sr.r_len, sr.r_len, u16);
        if (sr.r_32) sr.r_half = (int32_t)(u16 / 2u);
    }
    if (sr.px != SPX_NONE) sr.px_n16++;
    if (ev->has_wob && !sr.mute[L_BASS]) {
        sr.won = 1; sr.wph = 0; sr.wshape = ev->wob.shape; sr.wdepth = ev->wob.depth;
        sr.winc = 19478u * 1000u / (uint32_t)(ev->wob.rate3 ? ev->wob.rate3 : 12) / 1000u;   /* 3 x 2^32 / (rate3 x a sixteenth's units) */
    }
    /* the filters: each part's one-knob filter, closed as the energy falls (the sound's own cutoff kept above 0.7) */
    for (i = L_BASS; i <= L_FX; i++) {
        int32_t c = ev->cut[i];
        if (c < 0)
            continue;
        sr.bcut[i] = (int16_t)(i == L_FX ? (c >= 950 ? 0 : -(950 - c) * 45 / 850) : (c >= 700 ? 0 : -(700 - c) * 40 / 700));
    }
    /* the master: the build's high-pass, the duck, the reverb (the parts' sends), the gain; the hands' mix (0.5) */
    sr.duck = ev->duck * 33;
    siren_mixer(ev);
    sr.gain_q12 = ev->gain * 4096 / 1000 * 11 / 10;
    sr.seq++;
}

/* (audio interrupt, events_block) the brain's clock: n samples, adv units */
static void siren_block(uint32_t n, uint32_t adv)
{
    uint32_t i, real = adv;
    (void)n;
    siren_commands();
    adv = (uint32_t)(((uint64_t)adv * (uint32_t)sr.spd_q12) >> 12);   /* (0.2) BRAKE, HALF: the clock slower */
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
        if (sr.r_half > 0 && (sr.r_half -= (int32_t)adv) <= 0) {   /* (0.2) ROLL 1/32: the loop again, half a sixteenth on */
            sr.r_half = 0;
            if (sr.r_on) siren_replay(0, 1, BEAT_U / 4u);
        }
    }
    siren_px_block(real);                                          /* (its own time: the brake runs out) */
    siren_wobble(adv);
}

/* (audio interrupt, mix_block) the sub, into the dry mix, ducked with the parts */
static void siren_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    if (sr.nz_lvl) {                                 /* (0.2) RISER, REWIND: noise, its colour rising with its level */
        int32_t k = 2000 + (sr.nz_lvl >> 2);
        for (i = 0; i < n; i++) {
            int32_t w = (int32_t)(noise32(&sr.nz) >> 17) - 16384, y;
            sr.nz1 += mulq15(w - sr.nz1, k);         /* low-pass, opening */
            sr.nz2 += mulq15(sr.nz1 - sr.nz2, 1200); /* its own low end, taken off */
            y = mulq15(sr.nz1 - sr.nz2, sr.nz_lvl) >> 1;
            l[i] += y; r[i] += y;
        }
    }
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
static int32_t siren_gain_q12(void) { return (sr.gain_q12 * sr.gate_q12) >> 12; }   /* (0.2: the GATE) */
static void siren_scope_tap(void)                /* (mix_block: the visualiser's taps, for the screen) */
{
    uint32_t i;
    for (i = 1; i < CTL; i += 2u) { sr.scope[sr.sw % 512u] = vis_tap[2u * i]; sr.sw++; }   /* (mix_block writes the odd samples) */
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
    song.g[G_DRDLY] = (int16_t)SIREN_DMIX[sb.style][2];   /* (0.2: the drums' echo send; SIREN's master has no COMP) */
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
            uint32_t at = sp->first + (uint32_t)want % sp->n;
            const sd_snd_t *s = &SD_SND[SD_LIST[at]];
            int16_t pe, e = siren_find(s, &pe);
            track_t *t = SIREN_PART(L);
            sr.applied[L] = want; sr.applied_style[L] = (int16_t)style;
            if (e < 0)
                continue;
#ifdef SD_HAS_TRIM
            sr.trim[L] = SD_TRIM[at]; sr.tone[L] = SD_TONE[at];   /* (0.5) its calibration */
#endif
            siren_load_sound(t, (uint32_t)e, (uint32_t)pe);
            t->p[P_LEVEL] = siren_level(L); t->p[P_PAN] = SIREN_MIX[L - 1][1];
            t->p[P_DLY] = SIREN_MIX[L - 1][2]; t->p[P_REV] = SIREN_MIX[L - 1][3]; t->p[P_CHOR] = 0;
            t->p[P_AMODE] = 0;                   /* (the brain is the arpeggiator: a preset's arp off) */
            sr.sound_eng[L] = e; sr.sound_pre[L] = pe;
            break;                               /* (0.2) one sound a call: a new track's six fades not all at once */
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
    sr.spd_q12 = 4096; sr.gate_q12 = 4096; sr.eqk[0] = sr.eqk[1] = sr.eqk[2] = 12; sr.nz = 0x5EED; sr.px = SPX_NONE; sr.q_style = -1;
    sr.space = 100;
    for (i = 0; i < 7u; i++) sr.lspace[i] = 100;
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_MUTE] = 0;
        trk[i].p[P_AMODE] = 0;
    }
    trk[1].p[P_LYMODE] = 0;                      /* (Clipper's layer: off, the parts are SIREN's) */
    song.g[G_DRDLY] = 0;                         /* (Clipper: the master compressor off) */
    song.g[G_FILT] = 0;
    siren_service();
}

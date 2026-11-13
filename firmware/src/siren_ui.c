/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN's screen and controls (the main loop; the sound is siren.c's, in the audio interrupt).
 *
 * (0.8) Pages, no button to hold: a page button opens its page and it stays open; the same button again, or HOME,
 * back to the deck; another page button, that page. The page's name on top of it, its button lit.
 *
 *   HOME (the deck)  the seven layers; the keyboard plays the layer chosen (SELECT): the white keys its scale (the
 *                    track's key and scale, from the layer's octave; OCT- OCT+ move it), the black keys the track's
 *                    chords in order (the bass: their roots). The first note takes the layer: SIREN stops writing for
 *                    it (LIVE) until HOME, pressed on the deck, gives it back. The drums: each white key a sound of
 *                    the kit, on top of the groove (not taken); their black keys the composition's.
 *   FX               the DJ's effects, while a white key is held: 1-4 ROLL 1/4 1/8 1/16 1/32, 5 LOW PASS, 6 HIGH PASS,
 *                    7 ECHO OUT, 8 WASH OUT, 9-11 KILL LOW MID HIGH, 12 GATE, 13-16 the style's own (siren.c SIREN_PX)
 *   SEL              the style: white keys 1-8 TECHNO TRANCE PSY ACID RAVE OUTRUN DRUM&BASS HOUSE (playing: on the next
 *                    8-bar phrase; the same key again: at once); 9 ALL SOUNDS (0.9: the next tracks' sounds from every
 *                    style's bank, not the style's own; PRESET always goes through all of them; 0.10: the drums too,
 *                    every style's kits); 16 keep this style
 *   ENV              the track's form: white keys 1-11 BREAK, BUILD 4, BUILD 16, DROP, FILL, NEW MELODY, NEW BASS,
 *                    NEW GROOVE, KEY UP, NEXT TRACK, STAY (as the black keys, outside the deck)
 *   LFO              the layers' mix: white keys 1-7 the layer, 9-15 its on / off, 16 the strips back to flat; the
 *                    knobs its VOLUME, FILTER, SPACE, VARIATION
 *   SAVE             16 seeds: an empty key keeps the one playing; a kept one, tapped, plays it again; held, the one
 *                    playing takes its place (0.9.2: kept over a restart, written once the music is stopped; 0.10: with
 *                    its style and ALL, played in them)
 *   GLO              the settings: K1 TENSION, K2 MUTATION, K3 INTENSITY, K4 MELODY; keys 1 keep the style, 2 the
 *                    strips back to flat, 3 every layer given back to SIREN; the buttons, what they do
 *   everywhere       PLAY start / stop; REC a new rave (a new seed); the knobs a DJ's (K1 FILTER, K2 INTENSITY,
 *                    K3 SPACE, K4 MELODY; LFO and GLO their own); SELECT the layer, PRESET its sound, ALGO its motif;
 *                    EDIT keep the layer (lock); SEQ reroll it (another sound, another pattern); ARP the keys held
 *                    arpeggiated; OCT- OCT+ the keyboard's octave; outside the deck the black keys are the form's
 *                    (BREAK .. STAY, left to right)
 *
 * The screen (0.3): a DJ's deck (siren_draw.c): style, key, tempo, the beat; the track's plan and what comes next, when;
 * the page; the knobs as they act now; what was just done, the load. PLAY blinks on the beat. */
#include "siren_draw.c"

/* SIREN on the FM-1: a part's sound from the brain's pick, as the preset browser loads one (ui.c) */
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p)
{
    set_engine_of(t, e);
    fm1_irq_off();
    apply_preset_to(t, p);
    fm1_irq_on();
}
static void su_message(const char *m) { uint32_t i; for (i = 0; i < 19u && m[i]; i++) su.msg[i] = m[i]; su.msg[i] = 0; su.msg_t = 80; }
/* (0.9.2) the kept seeds survive a restart: in the settings' side record (project.c siren_side_t), written when the
 * music is stopped and quiet (a flash erase stops the sound for a moment) */
static int siren_side_unsaved(void);                /* project.c */
static int audio_quiet(void);                       /* project.c */
static void settings_save(void);                    /* project.c */
static void su_fav_take(uint32_t k)                 /* (0.10) the track playing into slot k: its seed, its style, ALL */
{
    su.fav[k] = sb.seed; su.fav_set[k] = 1; su.fav_st[k] = (uint8_t)((sb.style & 7u) | (sr.allbank ? 8u : 0u));
}
static void su_fav_play(uint32_t k)                 /* (0.10) slot k's track: in its style, with its ALL */
{
    uint32_t st = su.fav_st[k] & 7u;
    if (((su.fav_st[k] >> 3) & 1u) != sr.allbank) siren_cmd(SC_ALLBANK, (su.fav_st[k] >> 3) & 1u, 0, 0);
    su.style = (uint8_t)st;
    siren_cmd(SC_SEED, (int32_t)st, 0, su.fav[k]);
    su_message("SEED PLAYS");
}
static void su_fav_kept(void)
{
    su.fav_dirty = 1;
    su_message(sr.playing ? "KEPT: SAVED AT STOP" : "SEED KEPT");
}
static int srb_write(void);                         /* project.c (0.10): the user's banks of sounds */
static void su_fav_flush(void)                      /* (0.10: the banks too) */
{
    if ((!su.fav_dirty && !srb_dirty) || sr.playing || transport_req || !audio_quiet() || fm1_ms - su.fav_t < 1500u)
        return;
    su.fav_t = fm1_ms;                                  /* (a failed write: again in 1.5 s) */
    if (srb_dirty) {
        int r = srb_write();
        if (r >= 0) srb_dirty = 0;
        if (r == 0 && !su.fav_dirty) su_message("SOUNDS SAVED");
    }
    if (su.fav_dirty) {
        int u;
        settings_save();
        u = siren_side_unsaved();                       /* (-1: no flash on this FM-1: RAM only, as before) */
        su.fav_dirty = (uint8_t)(u == 1);
        if (u == 0)
            su_message("SEEDS SAVED");
        else if (u < 0)
            su.fav_dirty = 0;
    }
}
static uint32_t su_seed_new(void)
{
    static uint32_t n;
    uint32_t s = fm1_ms * 2654435761u ^ ++n * 40503u ^ su.frame * 977u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s ? s : 1u;
}
static void siren_ui_init(void)
{
    uint32_t L;
    su.knob[0] = 500; su.knob[1] = 500; su.knob[2] = 500; su.knob[3] = 350;
    su.style = SB_OUTRUN;
    su.batt = -1;                                       /* (0.9.2) not read yet */
    su.px_key = -1; su.focus = L_HOOK; su.slot_k = -1;
    su.space = 100;
    for (L = 0; L < 7u; L++) su.lspace[L] = 100;
    siren_cmd(SC_SEED, su.style, 0, su_seed_new());
    siren_cmd(SC_MIX, SM_FOCUS, su.focus, 0);
}

/* ---- the controls */
/* (0.5) the black keys: their key bits on the keyboard (F# G# A# C# D# F# G# A# C# D# F#); the form's actions, the
 * same on the ENV page's white keys */
static const uint8_t SU_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static void su_black(uint32_t k)
{
    switch (k) {
    case 0: siren_cmd(SC_BREAK, 0, 0, 0); break;
    case 1: siren_cmd(SC_DIVE, 4, 0, 0); break;
    case 2: siren_cmd(SC_DIVE, 16, 0, 0); break;
    case 3: siren_cmd(SC_DROP, 0, 0, 0); break;
    case 4: siren_cmd(SC_FILL, 0, 0, 0); break;
    case 5: siren_cmd(SC_REMOTIF, L_HOOK, 1, 0); siren_cmd(SC_REMOTIF, L_ARP, 1, 0); break;
    case 6: siren_cmd(SC_REMOTIF, L_BASS, 1, 0); break;
    case 7: siren_cmd(SC_REMOTIF, L_DRUMS, 1, 0); break;
    case 8: siren_cmd(SC_KEYUP, 0, 0, 0); break;
    case 9: siren_cmd(SC_NEXT, 0, 0, 0); su_message(sr.q_next ? "NEXT TRACK NOW" : "NEXT TRACK"); return;
    default: siren_cmd(SC_STAY, 0, 0, 0); su_message(sb.stay ? "STAY OFF" : "STAY: THIS SECTION LOOPS"); return;
    }
    su_message(SU_BNAME[k]);
}
/* (0.5) a layer's sound one step on (PRESET): the brain keeps it (locked) */
static void su_sound_step(uint32_t L, int32_t d)
{
    siren_cmd(SC_SOUND, (int32_t)L, d, 0);
    if (L != L_DRUMS) su.locks[L] = 1;
    su.msg_t = 0; su.snd_t = 60;                        /* (the footer names the new sound when it is in) */
}
static void su_strip(uint32_t L, uint32_t k, int32_t d)    /* (LFO) a knob on the layer chosen */
{
    switch (k) {
    case 0: su.lvol[L] = (int8_t)clamp(su.lvol[L] + d, -30, 12); siren_cmd(SC_MIX, SM_LVOL + (int32_t)L, su.lvol[L], 0); break;
    case 1: su.lflt[L] = (int8_t)clamp(su.lflt[L] + d * 2, -60, 60); siren_cmd(SC_MIX, SM_LFLT + (int32_t)L, su.lflt[L], 0); break;
    case 2: su.lspace[L] = (int16_t)clamp(su.lspace[L] + d * 5, 0, 200); siren_cmd(SC_MIX, SM_LSPACE + (int32_t)L, su.lspace[L], 0); break;
    default:
        su.vacc = (int8_t)(su.vacc + d);
        if (su.vacc >= 3 || su.vacc <= -3) { siren_cmd(SC_REMOTIF, (int32_t)L, su.vacc > 0 ? 1 : -1, 0); su.vacc = 0; su_message("VARIATION"); }
        break;
    }
}
static void su_flat(void)                                   /* the strips back to flat */
{
    uint32_t L;
    for (L = 0; L < 7u; L++) {
        su.lvol[L] = 0; su.lflt[L] = 0; su.lspace[L] = 100;
        siren_cmd(SC_MIX, SM_LVOL + (int32_t)L, 0, 0); siren_cmd(SC_MIX, SM_LFLT + (int32_t)L, 0, 0); siren_cmd(SC_MIX, SM_LSPACE + (int32_t)L, 100, 0);
    }
    su_message("MIX FLAT");
}
static void su_keep_style(void)
{
    su.lock_style ^= 1u; siren_cmd(SC_STYLELOCK, su.lock_style, 0, 0); su_message(su.lock_style ? "STYLE KEPT" : "STYLE FREE");
}
static void su_give_back(void)
{
    if (!sr.live) { su_message("SIREN PLAYS IT ALL"); return; }
    siren_cmd(SC_LIVEOFF, 0xFF, 0, 0); su_message("GIVEN BACK TO SIREN");
}
static void su_dj_knob(uint32_t k, int32_t d)                /* the DJ's four */
{
    switch (k) {
    case 0: {                                               /* FILTER: the middle holds (open) a moment */
        int32_t v = su.mfilt + d * 2;
        if ((su.mfilt > 0 && v < 0) || (su.mfilt < 0 && v > 0)) v = 0;
        su.mfilt = (int8_t)clamp(v, -60, 60);
        siren_cmd(SC_MIX, SM_FILT, su.mfilt, 0);
        break;
    }
    case 1: su.knob[0] = (int16_t)clamp(su.knob[0] + d * 20, 0, 1000); siren_cmd(SC_KNOB, K_ENERGY, su.knob[0], 0); break;
    case 2: su.space = (int16_t)clamp(su.space + d * 5, 0, 200); siren_cmd(SC_MIX, SM_SPACE, su.space, 0); break;
    default: su.knob[1] = (int16_t)clamp(su.knob[1] + d * 20, 0, 1000); siren_cmd(SC_KNOB, K_DENSITY, su.knob[1], 0); break;
    }
}
static void su_glo_knob(uint32_t k, int32_t d)               /* (GLO) TENSION MUTATION, then INTENSITY MELODY */
{
    static const uint8_t KI[4] = {2, 3, 0, 1}, KB[4] = {K_TENSION, K_MUTATION, K_ENERGY, K_DENSITY};
    uint32_t i = KI[k & 3u];
    su.knob[i] = (int16_t)clamp(su.knob[i] + d * 20, 0, 1000);
    siren_cmd(SC_KNOB, KB[k & 3u], su.knob[i], 0);
}

/* (0.8) the keyboard on the deck: a key's notes for the layer chosen, packed for SC_LIVE (each + 1 in a byte) */
static const uint8_t SU_KIT[16] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};   /* the drum lanes */
static const uint8_t SU_BASE[7] = {0, 36, 60, 60, 48, 60, 60};                                         /* each layer's C */
static uint32_t su_n(uint32_t L, int32_t deg)              /* a degree of the track's scale, from the layer's octave */
{
    int32_t o = deg >= 0 ? deg / 7 : -((6 - deg) / 7), d = deg - o * 7;
    int32_t n = SU_BASE[L % 7u] + (int32_t)sb.key % 12 + 12 * (o + su.oct) + SB_SCALES[sb.scale % 5u][d];
    return (uint32_t)clamp(n, 0, 127);
}
static uint32_t su_key_notes(uint32_t L, uint32_t bit, int black, uint32_t k)
{
    uint32_t c;
    (void)bit;
    if (L == L_DRUMS) return SU_KIT[k % 16u] + 1u;
    if (!black) return su_n(L, (int32_t)k) + 1u;
    {   /* the track's chords in order, then again an octave up (the bass: the root) */
        uint32_t np = sb.nprog ? sb.nprog : 1u, deg = sb.prog ? sb.prog[k % np] : 0u;
        int32_t up = (int32_t)(k / np) * 7;
        c = su_n(L, (int32_t)deg + up) + 1u;
        if (L != L_BASS) c |= (su_n(L, (int32_t)deg + up + 2) + 1u) << 8 | (su_n(L, (int32_t)deg + up + 4) + 1u) << 16;
    }
    return c;
}
static void su_key_down(uint32_t L, uint32_t bit, int black, uint32_t k)
{
    uint32_t c = su_key_notes(L, bit, black, k);
    su.kn[bit] = c; su.kl[bit] = (uint8_t)L; su.down |= 1u << bit;
    siren_cmd(SC_LIVE, (int32_t)L, L == L_PAD ? 90 : L == L_DRUMS ? 110 : 100, c);
}
static void su_key_up(uint32_t bit)
{
    su.down &= ~(1u << bit);
    if (su.kl[bit] != L_DRUMS) siren_cmd(SC_LIVE, su.kl[bit], 0, su.kn[bit]);
}

/* the page buttons: theirs, opened; again, the deck */
static const uint8_t SU_PAGE_BTN[SP_N] = {B_HOME, B_FX, B_SCL, B_ENV, B_LFO, B_SAVE, B_GLO};
static void su_page(uint32_t p)
{
    uint32_t b;
    if (su.px_key >= 0) { su.px_key = -1; siren_cmd(SC_PUNCH, -1, 0, 0); }   /* (an effect held: let go with its page) */
    for (b = 0; b < 27u; b++)                                                /* (notes held: their ends) */
        if ((su.down >> b) & 1u) su_key_up(b);
    su.slot_k = -1;
    su.info = (uint8_t)(p < SP_N ? p : SP_HOME);
}

static void siren_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), held = fm1_in.notes | su.vheld, k, b;   /* (0.10: and the web's) */
    int32_t d;
    uint32_t page = su.info;
    /* the keys let go: the deck's notes end; the effect held ends; a seed key let go (SAVE) */
    for (b = 0; b < 27u; b++)
        if (((su.down >> b) & 1u) && !((held >> b) & 1u) && !((notes >> b) & 1u)) su_key_up(b);
    if (su.px_key >= 0 && !((held >> SU_WHITE[su.px_key]) & 1u) && !((notes >> SU_WHITE[su.px_key]) & 1u)) {
        su.px_key = -1;
        siren_cmd(SC_PUNCH, -1, 0, 0);
    }
    if (su.slot_k >= 0) {
        uint32_t s = (uint32_t)su.slot_k, down = (held >> SU_WHITE[s]) & 1u;
        if (down && !su.slot_done && fm1_ms - su.slot_t0 >= 700u) {          /* held: the seed playing takes its place */
            su_fav_take(s); su.slot_done = 1; su_fav_kept();
        } else if (!down) {                                                 /* tapped: it plays */
            if (!su.slot_done) su_fav_play(s);
            su.slot_k = -1;
        }
    }
    for (k = 0; k < 4u; k++)                                               /* the four knobs */
        if ((d = panel_enc(EN_K1 + k)) != 0) {
            if (page == SP_LFO) su_strip(su.focus, k, d);
            else if (page == SP_GLO) su_glo_knob(k, d);
            else su_dj_knob(k, d);
        }
    if ((d = panel_enc(EN_SELECT)) != 0) {                    /* (0.5) the encoders: the layer, its sound, its motif */
        su.focus = (uint8_t)(((int32_t)su.focus + d % 7 + 7) % 7);
        siren_cmd(SC_MIX, SM_FOCUS, su.focus, 0);
    }
    if ((d = panel_enc(EN_PRESET)) != 0) su_sound_step(su.focus, d > 0 ? 1 : -1);
    if ((d = panel_enc(EN_ALGO)) != 0) { siren_cmd(SC_REMOTIF, su.focus, d > 0 ? 1 : -1, 0); su_message("NEW MOTIF"); }
    for (b = 0; b < NB; b++) {
        if (!((pressed >> panel.btn[b]) & 1u))
            continue;
        switch (b) {
        case B_PLAY: if (sr.playing) { siren_cmd(SC_STOP, 0, 0, 0); transport_req = 2; } else { siren_cmd(SC_PLAY, 0, 0, 0); transport_req = 1; } break;
        case B_REC: siren_cmd(SC_SEED, sb.style, 0, su_seed_new()); su_message("NEW RAVE"); break;
        case B_HOME: if (page != SP_HOME) su_page(SP_HOME); else su_give_back(); break;
        case B_FX: case B_SCL: case B_ENV: case B_LFO: case B_SAVE: case B_GLO: {
            uint32_t p;
            for (p = 1; p < SP_N && SU_PAGE_BTN[p] != b; p++) {}
            su_page(page == p ? SP_HOME : p);
            break;
        }
        case B_ARP: su.larp ^= 1u; siren_cmd(SC_LARP, su.larp, 0, 0); su_message(su.larp ? "ARP ON THE KEYS" : "ARP OFF"); break;
        case B_EDIT:
            su.locks[su.focus] ^= 1u; siren_cmd(SC_LOCK, su.focus, su.locks[su.focus], 0);
            su_message(su.locks[su.focus] ? "LAYER KEPT" : "LAYER FREE");
            break;
        case B_SEQ: siren_cmd(SC_REROLL, su.focus, 0, 0); su_message("REROLL"); break;
        case B_OCTDN: case B_OCTUP: {
            su_s_t m = {{0}, 0};
            su.oct = (int8_t)clamp(su.oct + (b == B_OCTUP ? 1 : -1), -2, 2);
            su_c(&m, "OCTAVE "); su_c(&m, su.oct < 0 ? "-" : "+"); su_u(&m, (uint32_t)(su.oct < 0 ? -su.oct : su.oct), 1);
            su_message(m.b);
            break;
        }
        default: break;
        }
    }
    page = su.info;
    for (k = 0; k < 11u; k++) {                               /* the black keys: the chords on the deck, else the form */
        if (!((notes >> SU_BLACK[k]) & 1u))
            continue;
        if (page == SP_HOME && su.focus != L_DRUMS) su_key_down(su.focus, SU_BLACK[k], 1, k);
        else su_black(k);
    }
    for (k = 0; k < 16u; k++) {                               /* the white keys: the page's */
        if (!((notes >> SU_WHITE[k]) & 1u))
            continue;
        switch (page) {
        case SP_HOME: su_key_down(su.focus, SU_WHITE[k], 0, k); break;
        case SP_FX:
            su.px_key = (int8_t)k;
            siren_cmd(SC_PUNCH, (int32_t)k, 0, 0);
            su_message(SIREN_PX[sb.style % SB_NSTYLES][k].name);
            break;
        case SP_SEL:
            if (k < 8u) {
                su.style = (uint8_t)k;                       /* (0.3) on the next phrase; the same key again: now */
                if (!sr.playing) siren_cmd(SC_STYLE, su.style, 0, 0);
                else siren_cmd(SC_STYLEQ, su.style, 0, 0);
                su_message(SU_STYLE[su.style]);
            } else if (k == 8u) {                            /* (0.9) ALL: every style's sounds for the next tracks */
                siren_cmd(SC_ALLBANK, !sr.allbank, 0, 0);
                su_message(!sr.allbank ? "ALL SOUNDS: NEXT TRACK" : "STYLE SOUNDS: NEXT TRACK");
            } else if (k == 15u) su_keep_style();
            break;
        case SP_ENV: if (k < 11u) su_black(k); break;
        case SP_LFO:
            if (k < 7u) { su.focus = (uint8_t)k; su.vacc = 0; siren_cmd(SC_MIX, SM_FOCUS, (int32_t)k, 0); }
            else if (k >= 8u && k < 15u) {
                uint32_t L = k - 8u;
                sr.mute[L] ^= 1u; if (L == L_BASS) sr.mute[L_SUB] = sr.mute[L];
                su_message(sr.mute[L] ? "LAYER OFF" : "LAYER ON");
            } else if (k == 15u) su_flat();
            break;
        case SP_SAVE:
            if (!su.fav_set[k]) { su_fav_take(k); su_fav_kept(); }
            else { su.slot_k = (int8_t)k; su.slot_t0 = fm1_ms; su.slot_done = 0; }
            break;
        default:   /* SP_GLO */
            if (k == 0u) su_keep_style();
            else if (k == 1u) su_flat();
            else if (k == 2u) su_give_back();
            break;
        }
    }
}

/* ---- the lights: PLAY on the beat; the page's button; the keys as the page uses them */
static void siren_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t k, c, page = su.info, blink = (fm1_ms / 250u) & 1u;
    static uint8_t ready;
    if (!ready) { led_pos_init(); ready = 1; }
    led_put(nl, panel.btn[B_PLAY], sr.playing && sr.ev.step % 4u < 2u);   /* (0.3) on the beat, as a CDJ's */
    for (k = 1; k < SP_N; k++) led_put(page == k ? nl : dl, panel.btn[SU_PAGE_BTN[k]], 1);
    led_put(page == SP_HOME && !(sr.live && blink) ? nl : dl, panel.btn[B_HOME], 1);   /* (blinking: layers in the hands) */
    led_put(nl, panel.btn[B_ARP], su.larp);
    led_put(nl, panel.btn[B_EDIT], su.locks[su.focus % 7u]);
    led_put(nl, panel.btn[B_OCTDN], su.oct < 0);
    led_put(nl, panel.btn[B_OCTUP], su.oct > 0);
    for (k = 0; k < 16u; k++) {
        uint32_t id = 14u + SU_WHITE[k], on = 0, dim = 0;
        switch (page) {
        case SP_HOME:
            dim = 1;
            on = (su.down >> SU_WHITE[k]) & 1u || (su.focus != L_DRUMS && k % 7u == 0u);   /* (the tonic: a landmark) */
            break;
        case SP_FX: dim = 1; on = su.px_key == (int8_t)k; break;
        case SP_SEL:
            dim = k < 9u || k == 15u;
            on = (k < 8u && k == sb.style % 8u) || (k == 8u && sr.allbank) || (k == 15u && su.lock_style) || (sr.q_style == (int8_t)k && blink);
            break;
        case SP_ENV: {
            uint8_t req[11] = {sb.break_req, sb.dive == 4u, sb.dive == 16u, sb.drop_req, (uint8_t)(sr.fill_req || sr.fill_on), 0, 0, 0,
                               sb.key_req, (uint8_t)(sr.q_next != 0), sb.stay};
            dim = k < 11u; on = k < 11u && req[k];
            break;
        }
        case SP_LFO:
            dim = k != 7u;
            on = (k < 7u && k == su.focus) || (k >= 8u && k < 15u && !sr.mute[k - 8u]);
            break;
        case SP_SAVE: dim = 1; on = su.fav_set[k] && !(su.fav[k] == sb.seed && blink); break;
        default: dim = k < 3u; on = k == 0u && su.lock_style; break;
        }
        if (on) led_put(nl, id, 1); else if (dim) led_put(dl, id, 1);
    }
    for (k = 0; k < 11u; k++) {                                       /* the black keys */
        uint32_t id = 14u + SU_BLACK[k], on;
        if (page == SP_HOME && su.focus != L_DRUMS) {                /* the chords: the one playing lit */
            uint32_t np = sb.nprog ? sb.nprog : 1u;
            on = (su.down >> SU_BLACK[k]) & 1u || (sr.playing && sb.prog && sb.prog[k % np] == sr.ev.chord && k < np);
        } else {                                                     /* (0.5) the form: asked for and still to come */
            uint8_t req[11] = {sb.break_req, sb.dive == 4u, sb.dive == 16u, sb.drop_req, (uint8_t)(sr.fill_req || sr.fill_on), 0, 0, 0,
                               sb.key_req, (uint8_t)(sr.q_next != 0), sb.stay};
            on = req[k];
        }
        led_put(on ? nl : dl, id, 1);
    }
    for (c = 0; c < FM1_NCOL; c++) { fm1_led[c] = nl[c]; fm1_led_dim[c] = dl[c]; fm1_led_bg[c] = 0; }
}

static void siren_frame(void)
{
    siren_input();
    if (su.px_key >= 0 && su.msg_t < 2u) su.msg_t = 2;             /* (0.2) the effect's name while it is held */
    siren_service();
    su_fav_flush();
    siren_leds();
    siren_draw();
}

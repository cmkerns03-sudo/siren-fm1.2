/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN's screen and controls (the main loop; the sound is siren.c's, in the audio interrupt).
 *
 *   PLAY            start / stop
 *   (0.5) the knobs, a DJ's: K1 FILTER (the master's: left a low-pass, right a high-pass, the middle open), K2 INTENSITY
 *                   (the energy), K3 SPACE (the reverb and the echo), K4 MELODY (how much the leads play)
 *   (0.5) a white key 1-7 held (a layer: DRUMS BASS ARP HOOK PAD STABS FX), the knobs are its channel strip: K1 VOLUME,
 *                   K2 FILTER, K3 SPACE, K4 VARIATION (another motif); tapped alone: the layer on / off
 *   (0.5) the encoders on the layer chosen: SELECT the layer, PRESET its sound (the style's selection; kept from then on),
 *                   ALGO its motif
 *   (0.5) the black keys, left to right: BREAK, BUILD 4, BUILD 16, DROP (at the next bar), FILL, NEW MELODY, NEW BASS,
 *                   NEW GROOVE, KEY UP (at the next drop), NEXT TRACK (on the phrase; twice: now), STAY (the section loops)
 *   FX + white key  (0.2) the DJ's effects, while the key is held: 1-4 ROLL 1/4 1/8 1/16 1/32, 5 LOW PASS, 6 HIGH PASS,
 *                   7 ECHO OUT, 8 WASH OUT, 9-11 KILL LOW MID HIGH, 12 GATE, 13-16 the style's own (siren.c SIREN_PX)
 *   ENV BREAK  ARP BUILD 16 (as on the black keys)   LFO keep this style   REC a new seed   HOME deck / effects / keys
 *   white keys 9-16 the style: TECHNO TRANCE PSY ACID RAVE HARD DRUM&BASS HOUSE (0.3: playing, it comes in on the
 *                   next 8-bar phrase, as a DJ mixes; the same key again: at once)
 *   SEQ + key 1-7   reroll that layer (another sound, another pattern)       EDIT + key 1-7  keep that layer (lock)
 *   SAVE + key      keep the seed playing in that slot                       SEL + key       play the seed kept there
 *
 * The screen (0.3): a DJ's deck (siren_draw.c): style, key, tempo, the beat; the track's plan and what comes next, when;
 * the seven layers, their sounds and meters (the one in the hands lit, the ones waiting for room marked); the knobs as
 * they act now; the effect held, what was just done, the load. HOME: the 16 effects of the style, then the keys.
 * PLAY blinks on the beat; FX held lights the effect keys; a black key asked for is lit until it happens. */
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
static uint32_t su_seed_new(void)
{
    uint32_t s = fm1_ms * 2654435761u ^ (uint32_t)fm1_in.frames * 40503u ^ su.frame * 977u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s ? s : 1u;
}
static void siren_ui_init(void)
{
    uint32_t L;
    su.knob[0] = 500; su.knob[1] = 500; su.knob[2] = 500; su.knob[3] = 350;
    su.style = SB_TRANCE;
    su.px_key = -1; su.held = -1; su.focus = L_HOOK;
    su.space = 100;
    for (L = 0; L < 7u; L++) su.lspace[L] = 100;
    siren_cmd(SC_SEED, su.style, 0, su_seed_new());
    siren_cmd(SC_MIX, SM_FOCUS, su.focus, 0);
}

/* ---- the controls */
static int su_held(uint32_t label) { return ((fm1_in.buttons >> panel.btn[label]) & 1u) != 0u; }
/* (0.5) the black keys: their key bits on the keyboard (F# G# A# C# D# F# G# A# C# D# F#), what each does */
static const uint8_t SU_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const char *const SU_BNAME[11] = {"BREAK", "BUILD 4", "BUILD 16", "DROP", "FILL", "NEW MELODY", "NEW BASS",
                                         "NEW GROOVE", "KEY UP", "NEXT TRACK", "STAY"};
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
    su.msg_t = 0; su.snd_t = 60;                       /* (the footer names the new sound when it is in) */
}
static void su_strip(uint32_t L, uint32_t k, int32_t d)    /* a knob turned while a layer's key is held */
{
    su.held_used = 1;
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
static void siren_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), k, b;
    int32_t d;
    if (su.held >= 0 && !((fm1_in.notes >> SU_WHITE[su.held]) & 1u)) {   /* (0.5) a layer's key let go: tapped, its mute */
        uint32_t L = (uint32_t)su.held;
        if (!su.held_used) { sr.mute[L] ^= 1u; if (L == L_BASS) sr.mute[L_SUB] = sr.mute[L]; }
        su.held = -1;
    }
    for (k = 0; k < 4u; k++)                                 /* the four knobs */
        if ((d = panel_enc(EN_K1 + k)) != 0) {
            if (su.held >= 0) { su_strip((uint32_t)su.held, k, d); continue; }
            switch (k) {
            case 0: {                                        /* FILTER: the middle holds (open) a moment */
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
    if ((d = panel_enc(EN_SELECT)) != 0) {                   /* (0.5) the encoders: the layer, its sound, its motif */
        uint32_t L = su.held >= 0 ? (uint32_t)su.held : su.focus;
        su.focus = (uint8_t)((L + 7u + (uint32_t)(d > 0 ? 1 : 6)) % 7u);
        siren_cmd(SC_MIX, SM_FOCUS, su.focus, 0);
    }
    if ((d = panel_enc(EN_PRESET)) != 0) { su_sound_step(su.held >= 0 ? (uint32_t)su.held : su.focus, d > 0 ? 1 : -1); su.held_used = 1; }
    if ((d = panel_enc(EN_ALGO)) != 0) {
        uint32_t L = su.held >= 0 ? (uint32_t)su.held : su.focus;
        siren_cmd(SC_REMOTIF, (int32_t)L, d > 0 ? 1 : -1, 0); su.held_used = 1; su_message("NEW MOTIF");
    }
    for (b = 0; b < NB; b++) {
        if (!((pressed >> panel.btn[b]) & 1u))
            continue;
        switch (b) {
        case B_PLAY: if (sr.playing) { siren_cmd(SC_STOP, 0, 0, 0); transport_req = 2; } else { siren_cmd(SC_PLAY, 0, 0, 0); transport_req = 1; } break;
        case B_ARP: su_black(2); break;
        case B_ENV: su_black(0); break;
        case B_LFO: su.lock_style ^= 1u; siren_cmd(SC_STYLELOCK, su.lock_style, 0, 0); su_message(su.lock_style ? "STYLE KEPT" : "STYLE FREE"); break;
        case B_REC: siren_cmd(SC_SEED, sb.style, 0, su_seed_new()); su_message("NEW SEED"); break;
        case B_HOME: su.info = (uint8_t)((su.info + 1u) % 3u); break;
        default: break;
        }
    }
    for (k = 0; k < 11u; k++)                                /* (0.5) the black keys */
        if ((notes >> SU_BLACK[k]) & 1u)
            su_black(k);
    {   /* (0.2) FX held with a white key: the DJ's effects */
        uint32_t fx = su_held(B_FX);
        su.fx_was = (uint8_t)fx;
        if (su.px_key >= 0 && !((fm1_in.notes >> SU_WHITE[su.px_key]) & 1u)) {   /* its key let go: the set back */
            su.px_key = -1;
            siren_cmd(SC_PUNCH, -1, 0, 0);
        }
        for (k = 0; k < 16u && fx; k++)
            if ((notes >> SU_WHITE[k]) & 1u) {
                su.px_key = (int8_t)k;
                siren_cmd(SC_PUNCH, (int32_t)k, 0, 0);
                su_message(SIREN_PX[sb.style % SB_NSTYLES][k].name);
            }
        if (fx)
            notes = 0;                                       /* (the keys are the effects while FX is held) */
    }
    for (k = 0; k < 16u; k++) {                              /* the white keys */
        if (!((notes >> SU_WHITE[k]) & 1u))
            continue;
        if (su_held(B_SAVE)) { su.fav[k] = sb.seed; su.fav_set[k] = 1; su_message("SEED KEPT"); continue; }
        if (su_held(B_SCL)) { if (su.fav_set[k]) { siren_cmd(SC_SEED, -1, 0, su.fav[k]); su_message("SEED"); } continue; }
        if (k < 7u) {
            if (su_held(B_SEQ)) { siren_cmd(SC_REROLL, (int32_t)k, 0, 0); su_message("REROLL"); }
            else if (su_held(B_EDIT)) { su.locks[k] ^= 1u; siren_cmd(SC_LOCK, (int32_t)k, su.locks[k], 0); su_message(su.locks[k] ? "LOCKED" : "FREE"); }
            else {                                           /* (0.5) held: its strip; let go untouched: its mute */
                su.held = (int8_t)k; su.held_used = 0; su.vacc = 0;
                if (k != su.focus) { su.focus = (uint8_t)k; siren_cmd(SC_MIX, SM_FOCUS, (int32_t)k, 0); }
            }
        } else if (k >= 8u) {
            su.style = (uint8_t)(k - 8u);                    /* (0.3) on the next phrase; the same key again: now */
            if (!sr.playing) siren_cmd(SC_STYLE, su.style, 0, 0);
            else siren_cmd(SC_STYLEQ, su.style, 0, 0);
            su_message(SU_STYLE[su.style]);
        }
    }
}

/* ---- the lights: PLAY; the layers' keys lit while on; the style's key */
static void siren_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t k, c;
    static uint8_t ready;
    if (!ready) { led_pos_init(); ready = 1; }
    led_put(nl, panel.btn[B_PLAY], sr.playing && sr.ev.step % 4u < 2u);   /* (0.3) on the beat, as a CDJ's */
    led_put(nl, panel.btn[B_LFO], su.lock_style);
    led_put(nl, panel.btn[B_HOME], su.info != 0u);
    if (su.fx_was || su.px_key >= 0) {                              /* (0.3) FX held: the 16 effect keys, dim; the one held */
        for (k = 0; k < 16u; k++) led_put(dl, 14u + SU_WHITE[k], 1);
        if (su.px_key >= 0) led_put(nl, 14u + SU_WHITE[su.px_key], 1);
    } else {
        for (k = 0; k < 7u; k++)
            led_put(sr.mute[k] ? dl : nl, 14u + SU_WHITE[k], 1);
        led_put(nl, 14u + SU_WHITE[8u + sb.style % 8u], 1);
        if (sr.q_style >= 0) led_put(dl, 14u + SU_WHITE[8u + (uint32_t)sr.q_style % 8u], 1);   /* (0.3) the style to come */
    }
    {   /* (0.5) the black keys: dim, ready; lit, asked for and still to come (STAY: on) */
        uint8_t on[11] = {sb.break_req, sb.dive == 4u, sb.dive == 16u, sb.drop_req, (uint8_t)(sr.fill_req || sr.fill_on), 0, 0, 0,
                          sb.key_req, (uint8_t)(sr.q_next != 0), sb.stay};
        for (k = 0; k < 11u; k++) led_put(on[k] ? nl : dl, 14u + SU_BLACK[k], 1);
    }
    led_put(nl, panel.btn[B_FX], su.fx_was);
    for (c = 0; c < FM1_NCOL; c++) { fm1_led[c] = nl[c]; fm1_led_dim[c] = dl[c]; fm1_led_bg[c] = 0; }
}

static void siren_frame(void)
{
    siren_input();
    if (su.px_key >= 0 && su.msg_t < 2u) su.msg_t = 2;            /* (0.2) the effect's name while it is held */
    siren_service();
    siren_leds();
    siren_draw();
}

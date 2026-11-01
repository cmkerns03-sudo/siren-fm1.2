/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN's screen and controls (the main loop; the sound is siren.c's, in the audio interrupt).
 *
 *   PLAY            start / stop
 *   KNOB 1 .. 4     ENERGY, DENSITY, TENSION, MUTATION
 *   FX              DIVE: a 4-bar build from the next bar, then the drop     ARP   DIVE 16: a 16-bar build
 *   FX + white key  (0.2) the DJ's effects, while the key is held: 1-4 ROLL 1/4 1/8 1/16 1/32, 5 LOW PASS, 6 HIGH PASS,
 *                   7 ECHO OUT, 8 WASH OUT, 9-11 KILL LOW MID HIGH, 12 GATE, 13-16 the style's own (siren.c SIREN_PX)
 *                   (FX alone, let go without a key: DIVE)
 *   ENV             BREAK                                                    LFO   keep this style (the set stays in it)
 *   REC             a new seed (a new rave)                                  HOME  the sea / the info screen
 *   white keys 1-7  the layers on / off: DRUMS BASS ARP HOOK PAD STABS FX
 *   white keys 9-16 the style: TECHNO TRANCE PSY ACID RAVE HARD DRUM&BASS DUBSTEP
 *   SEQ + key 1-7   reroll that layer (another sound, another pattern)       EDIT + key 1-7  keep that layer (lock)
 *   SAVE + key      keep the seed playing in that slot                       SEL + key       play the seed kept there
 *
 * The screen: the sea (the kick's rings, the mix in four circles in the family's colours, each note a spark on a
 * spiral, the eye; a build pulls it in and spins it, a drop flashes it white), the style, seed, section and tempo; or
 * the info screen (the layers and their sounds, the knobs, the CPU). */
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
    su.knob[0] = 500; su.knob[1] = 500; su.knob[2] = 500; su.knob[3] = 350;
    su.style = SB_TRANCE;
    su.px_key = -1;
    siren_cmd(SC_SEED, su.style, 0, su_seed_new());
}

/* ---- the controls */
static int su_held(uint32_t label) { return ((fm1_in.buttons >> panel.btn[label]) & 1u) != 0u; }
static void siren_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), k, b;
    int32_t d;
    for (k = 0; k < 4u; k++)                                 /* the four knobs */
        if ((d = panel_enc(EN_K1 + k)) != 0) {
            su.knob[k] = (int16_t)clamp(su.knob[k] + d * 20, 0, 1000);
            siren_cmd(SC_KNOB, (int32_t)k, su.knob[k], 0);
        }
    for (b = 0; b < NB; b++) {
        if (!((pressed >> panel.btn[b]) & 1u))
            continue;
        switch (b) {
        case B_PLAY: if (sr.playing) { siren_cmd(SC_STOP, 0, 0, 0); transport_req = 2; } else { siren_cmd(SC_PLAY, 0, 0, 0); transport_req = 1; } break;
        case B_FX: su.fx_used = 0; break;                       /* (0.2) DIVE on its release, if no key was played */
        case B_ARP: siren_cmd(SC_DIVE, 16, 0, 0); su_message("DIVE 16"); break;
        case B_ENV: siren_cmd(SC_BREAK, 0, 0, 0); su_message("BREAK"); break;
        case B_LFO: su.lock_style ^= 1u; siren_cmd(SC_STYLELOCK, su.lock_style, 0, 0); su_message(su.lock_style ? "STYLE KEPT" : "STYLE FREE"); break;
        case B_REC: siren_cmd(SC_SEED, sb.style, 0, su_seed_new()); su_message("NEW SEED"); break;
        case B_HOME: su.info ^= 1u; break;
        default: break;
        }
    }
    {   /* (0.2) FX: held with a white key, the DJ's effects; tapped alone, DIVE */
        uint32_t fx = su_held(B_FX);
        if (su.fx_was && !fx && !su.fx_used) { siren_cmd(SC_DIVE, 4, 0, 0); su_message("DIVE"); }
        su.fx_was = (uint8_t)fx;
        if (su.px_key >= 0 && !((fm1_in.notes >> SU_WHITE[su.px_key]) & 1u)) {   /* its key let go: the set back */
            su.px_key = -1;
            siren_cmd(SC_PUNCH, -1, 0, 0);
        }
        for (k = 0; k < 16u && fx; k++)
            if ((notes >> SU_WHITE[k]) & 1u) {
                su.fx_used = 1; su.px_key = (int8_t)k;
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
            else { sr.mute[k] ^= 1u; if (k == 1u) sr.mute[L_SUB] = sr.mute[k]; }
        } else if (k >= 8u) {
            su.style = (uint8_t)(k - 8u);
            siren_cmd(SC_STYLE, su.style, 0, 0);
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
    led_put(nl, panel.btn[B_PLAY], sr.playing);
    led_put(nl, panel.btn[B_LFO], su.lock_style);
    led_put(nl, panel.btn[B_HOME], su.info);
    for (k = 0; k < 7u; k++)
        led_put(sr.mute[k] ? dl : nl, 14u + SU_WHITE[k], 1);
    led_put(nl, 14u + SU_WHITE[8u + sb.style % 8u], 1);
    if (su.px_key >= 0) led_put(nl, 14u + SU_WHITE[su.px_key], 1);   /* (0.2) the effect held */
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

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (ABYSS) */
/* ABYSS's screen and controls (the main loop; the sound is abyss.c's, in the audio interrupt). It plays by itself; the
 * hands sculpt it. Pages as SIREN 0.8: a page button opens its page and it stays; the same button or HOME, the deck.
 *
 *   HOME (the deck)  the seven layers; the knobs K1 LIGHT (shadow .. light: the filters, the register, the harmonies
 *                    the next pieces lean to), K2 SPACE (the reverb and the echo), K3 DENSITY (how much is played),
 *                    K4 TIME (slower .. faster sections and chords, the next pieces'); the keyboard plays the layer
 *                    chosen (SELECT) in the piece's key and mode, the black keys its chords (SIREN 0.8's: the layer
 *                    LIVE until HOME, on the deck, gives it back; the drums: the kit, on top)
 *   black keys       (outside the deck, and on the deck with the drums chosen) the gestures, left to right: RISE (to
 *                    the swell), PEAK NOW (a breath, then the peak), CALM (to the fall), BREATH (a bar of silence),
 *                    LIFT (the key up a mediant), NEW THEME, NEW HARMONY, HOLD (this section again and again), NEXT
 *                    PIECE, VOID (two bars of nothing), FADE OUT (the tail, then silence; again: on)
 *   FX               the textures: 1 SHIMMER (the reverb's tail an octave up), 2 TAPE (hiss and wow)
 *   SEL              the world: 1 CINEMATIC 2 COSMIC 3 DARK (playing: the next piece; the same key again: now)
 *   ENV              the gestures on the white keys 1-11
 *   LFO              the layers: 1-7 the layer, 9-15 its on / off, 16 the strips flat; the knobs VOLUME FILTER SPACE
 *                    VARIATION
 *   SAVE             16 seeds: an empty key keeps the one playing, a kept one plays it, held it takes its place
 *   GLO              K1 TENSION (the roughness of the clusters, the surprise), K2-K4 LIGHT DENSITY TIME; 1 every layer
 *                    given back, 2 the mix flat; the buttons
 *   everywhere       PLAY, REC a new set, SELECT the layer, PRESET its sound, ALGO a variation, EDIT keep the layer,
 *                    SEQ reroll it, ARP the keys held arpeggiated, OCT- OCT+ the keyboard's octave */
#include "abyss_draw.c"

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
    static uint32_t n;
    uint32_t s = fm1_ms * 2654435761u ^ ++n * 40503u ^ su.frame * 977u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s ? s : 1u;
}
static void siren_ui_init(void)
{
    uint32_t L;
    su.knob[0] = 500; su.knob[1] = 500; su.knob[2] = 500; su.knob[3] = 400;
    su.world = AW_CINEMA; su.focus = AL_MELO; su.slot_k = -1;
    su.space = 100;
    for (L = 0; L < 7u; L++) su.lspace[L] = 100;
    siren_cmd(AC_SEED, su.world, 0, su_seed_new());
    siren_cmd(AC_MIX, SM_FOCUS, su.focus, 0);
}

/* ---- the controls */
static const uint8_t SU_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static void su_gesture(uint32_t k)
{
    siren_cmd(AC_GESTURE, (int32_t)k, 0, 0);
    if (k == AG_HOLD) su_message(ab.hold ? "HOLD OFF" : "HOLD: THIS SECTION");
    else if (k == AG_FADE) su_message(ab.fade ? "FADE OFF" : "FADING OUT");
    else su_message(SU_GNAME[k % AB_NGESTURES]);
}
static void su_sound_step(uint32_t L, int32_t d)
{
    siren_cmd(AC_SOUND, (int32_t)L, d, 0);
    su.locks[L] = 1;
    su.msg_t = 0; su.snd_t = 60;
}
static void su_strip(uint32_t L, uint32_t k, int32_t d)
{
    switch (k) {
    case 0: su.lvol[L] = (int8_t)clamp(su.lvol[L] + d, -30, 12); siren_cmd(AC_MIX, SM_LVOL + (int32_t)L, su.lvol[L], 0); break;
    case 1: su.lflt[L] = (int8_t)clamp(su.lflt[L] + d * 2, -60, 60); siren_cmd(AC_MIX, SM_LFLT + (int32_t)L, su.lflt[L], 0); break;
    case 2: su.lspace[L] = (int16_t)clamp(su.lspace[L] + d * 5, 0, 200); siren_cmd(AC_MIX, SM_LSPACE + (int32_t)L, su.lspace[L], 0); break;
    default:
        su.vacc = (int8_t)(su.vacc + d);
        if (su.vacc >= 3 || su.vacc <= -3) { siren_cmd(AC_REROLL, (int32_t)L, 0, 0); su.vacc = 0; su_message("VARIATION"); }
        break;
    }
}
static void su_flat(void)
{
    uint32_t L;
    for (L = 0; L < 7u; L++) {
        su.lvol[L] = 0; su.lflt[L] = 0; su.lspace[L] = 100;
        siren_cmd(AC_MIX, SM_LVOL + (int32_t)L, 0, 0); siren_cmd(AC_MIX, SM_LFLT + (int32_t)L, 0, 0); siren_cmd(AC_MIX, SM_LSPACE + (int32_t)L, 100, 0);
    }
    su_message("MIX FLAT");
}
static void su_give_back(void)
{
    if (!sr.live) { su_message("ABYSS PLAYS IT ALL"); return; }
    siren_cmd(AC_LIVEOFF, 0xFF, 0, 0); su_message("GIVEN BACK TO ABYSS");
}
static void su_knob_brain(uint32_t i, int32_t d)            /* su.knob: LIGHT DENSITY TIME TENSION, the brain's AK_* */
{
    static const uint8_t KB[4] = {AK_LIGHT, AK_DENSITY, AK_TIME, AK_TENSION};
    su.knob[i & 3u] = (int16_t)clamp(su.knob[i & 3u] + d * 20, 0, 1000);
    siren_cmd(AC_KNOB, KB[i & 3u], su.knob[i & 3u], 0);
}
static void su_deck_knob(uint32_t k, int32_t d)             /* LIGHT SPACE DENSITY TIME */
{
    if (k == 1u) { su.space = (int16_t)clamp(su.space + d * 5, 0, 200); siren_cmd(AC_MIX, SM_SPACE, su.space, 0); }
    else su_knob_brain(k == 0u ? 0u : k == 2u ? 1u : 2u, d);
}
static void su_glo_knob(uint32_t k, int32_t d) { static const uint8_t I[4] = {3, 0, 1, 2}; su_knob_brain(I[k & 3u], d); }

/* the keyboard on the deck: the piece's scale on the white keys, its chords on the black (SIREN 0.8's) */
static const uint8_t SU_KIT[16] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};
static const uint8_t SU_BASE[7] = {0, 36, 48, 36, 60, 72, 48};
static uint32_t su_n(uint32_t L, int32_t deg)
{
    int32_t o = deg >= 0 ? deg / 7 : -((6 - deg) / 7), d = deg - o * 7;
    int32_t n = SU_BASE[L % 7u] + (int32_t)(ab.key + ab.lift + 24) % 12 + 12 * (o + su.oct) + AB_MODES[ab.mode % 7u][d];
    return (uint32_t)clamp(n, 0, 127);
}
static uint32_t su_key_notes(uint32_t L, int black, uint32_t k)
{
    if (L == AL_DRUMS) return SU_KIT[k % 16u] + 1u;
    if (!black) return su_n(L, (int32_t)k) + 1u;
    {   /* the piece's chords in order, then an octave up (the low: the root) */
        uint32_t np = ab.nprog ? ab.nprog : 1u, i, c = 0;
        const ab_chord_t *ch = &ab.prog[k % np];
        int32_t base = SU_BASE[L % 7u] + (int32_t)(ab.key + ab.lift + 24) % 12 + 12 * ((int32_t)(k / np) + su.oct) + ch->root;
        for (i = 0; i < 3u && i < (uint32_t)ab_qn(ch->q); i++) {
            if (L == AL_LOW && i) break;
            c |= (uint32_t)(clamp(base + AB_Q[ch->q][i], 0, 127) + 1) << (8u * i);
        }
        return c;
    }
}
static void su_key_down(uint32_t L, uint32_t bit, int black, uint32_t k)
{
    uint32_t c = su_key_notes(L, black, k);
    su.kn[bit] = c; su.kl[bit] = (uint8_t)L; su.down |= 1u << bit;
    siren_cmd(AC_LIVE, (int32_t)L, L == AL_DRUMS ? 100 : 90, c);
}
static void su_key_up(uint32_t bit)
{
    su.down &= ~(1u << bit);
    if (su.kl[bit] != AL_DRUMS) siren_cmd(AC_LIVE, su.kl[bit], 0, su.kn[bit]);
}

static const uint8_t SU_PAGE_BTN[SP_N] = {B_HOME, B_FX, B_SCL, B_ENV, B_LFO, B_SAVE, B_GLO};
static void su_page(uint32_t p)
{
    uint32_t b;
    for (b = 0; b < 27u; b++)
        if ((su.down >> b) & 1u) su_key_up(b);
    su.slot_k = -1;
    su.info = (uint8_t)(p < SP_N ? p : SP_HOME);
}

static void siren_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), held = fm1_in.notes, k, b;
    int32_t d;
    uint32_t page = su.info;
    for (b = 0; b < 27u; b++)
        if (((su.down >> b) & 1u) && !((held >> b) & 1u) && !((notes >> b) & 1u)) su_key_up(b);
    if (su.slot_k >= 0) {
        uint32_t s = (uint32_t)su.slot_k, down = (held >> SU_WHITE[s]) & 1u;
        if (down && !su.slot_done && fm1_ms - su.slot_t0 >= 700u) { su.fav[s] = ab.seed; su.fav_set[s] = 1; su.slot_done = 1; su_message("SEED KEPT"); }
        else if (!down) { if (!su.slot_done) { siren_cmd(AC_SEED, -1, 0, su.fav[s]); su_message("SEED PLAYS"); } su.slot_k = -1; }
    }
    for (k = 0; k < 4u; k++)
        if ((d = panel_enc(EN_K1 + k)) != 0) {
            if (page == SP_LFO) su_strip(su.focus, k, d);
            else if (page == SP_GLO) su_glo_knob(k, d);
            else su_deck_knob(k, d);
        }
    if ((d = panel_enc(EN_SELECT)) != 0) {
        su.focus = (uint8_t)(((int32_t)su.focus + d % 7 + 7) % 7);
        siren_cmd(AC_MIX, SM_FOCUS, su.focus, 0);
    }
    if ((d = panel_enc(EN_PRESET)) != 0) su_sound_step(su.focus, d > 0 ? 1 : -1);
    if ((d = panel_enc(EN_ALGO)) != 0) { siren_cmd(AC_REROLL, su.focus, 0, 0); su_message("VARIATION"); }
    for (b = 0; b < NB; b++) {
        if (!((pressed >> panel.btn[b]) & 1u))
            continue;
        switch (b) {
        case B_PLAY: if (sr.playing) { siren_cmd(AC_STOP, 0, 0, 0); transport_req = 2; } else { siren_cmd(AC_PLAY, 0, 0, 0); transport_req = 1; } break;
        case B_REC: siren_cmd(AC_SEED, ab.world, 0, su_seed_new()); su_message("A NEW SET"); break;
        case B_HOME: if (page != SP_HOME) su_page(SP_HOME); else su_give_back(); break;
        case B_FX: case B_SCL: case B_ENV: case B_LFO: case B_SAVE: case B_GLO: {
            uint32_t p;
            for (p = 1; p < SP_N && SU_PAGE_BTN[p] != b; p++) {}
            su_page(page == p ? SP_HOME : p);
            break;
        }
        case B_ARP: su.larp ^= 1u; siren_cmd(AC_LARP, su.larp, 0, 0); su_message(su.larp ? "ARP ON THE KEYS" : "ARP OFF"); break;
        case B_EDIT:
            su.locks[su.focus] ^= 1u; siren_cmd(AC_LOCK, su.focus, su.locks[su.focus], 0);
            su_message(su.locks[su.focus] ? "LAYER KEPT" : "LAYER FREE");
            break;
        case B_SEQ: siren_cmd(AC_REROLL, su.focus, 0, 0); su_message("REROLL"); break;
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
    for (k = 0; k < 11u; k++) {
        if (!((notes >> SU_BLACK[k]) & 1u))
            continue;
        if (page == SP_HOME && su.focus != AL_DRUMS) su_key_down(su.focus, SU_BLACK[k], 1, k);
        else su_gesture(k);
    }
    for (k = 0; k < 16u; k++) {
        if (!((notes >> SU_WHITE[k]) & 1u))
            continue;
        switch (page) {
        case SP_HOME: su_key_down(su.focus, SU_WHITE[k], 0, k); break;
        case SP_FX:
            if (k == 0u) { su.shimmer ^= 1u; siren_cmd(AC_TEXTURE, 0, su.shimmer, 0); su_message(su.shimmer ? "SHIMMER" : "SHIMMER OFF"); }
            else if (k == 1u) { su.tape ^= 1u; siren_cmd(AC_TEXTURE, 1, su.tape, 0); su_message(su.tape ? "TAPE" : "TAPE OFF"); }
            break;
        case SP_SEL:
            if (k < AB_NWORLDS) {
                int now = !sr.playing || ab.world_q == k;
                su.world = (uint8_t)k;
                siren_cmd(AC_WORLD, (int32_t)k, now, 0);
                su_message(AB_WORLD_NAMES[k]);
            }
            break;
        case SP_ENV: if (k < 11u) su_gesture(k); break;
        case SP_LFO:
            if (k < 7u) { su.focus = (uint8_t)k; su.vacc = 0; siren_cmd(AC_MIX, SM_FOCUS, (int32_t)k, 0); }
            else if (k >= 8u && k < 15u) {
                uint32_t L = k - 8u;
                sr.mute[L] ^= 1u; if (L == AL_LOW) sr.mute[AL_SUB] = sr.mute[L];
                su_message(sr.mute[L] ? "LAYER OFF" : "LAYER ON");
            } else if (k == 15u) su_flat();
            break;
        case SP_SAVE:
            if (!su.fav_set[k]) { su.fav[k] = ab.seed; su.fav_set[k] = 1; su_message("SEED KEPT"); }
            else { su.slot_k = (int8_t)k; su.slot_t0 = fm1_ms; su.slot_done = 0; }
            break;
        default:   /* SP_GLO */
            if (k == 0u) su_give_back();
            else if (k == 1u) su_flat();
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
    led_put(nl, panel.btn[B_PLAY], sr.playing && sr.ev.step % 4u < 2u);
    for (k = 1; k < SP_N; k++) led_put(page == k ? nl : dl, panel.btn[SU_PAGE_BTN[k]], 1);
    led_put(page == SP_HOME && !(sr.live && blink) ? nl : dl, panel.btn[B_HOME], 1);
    led_put(nl, panel.btn[B_ARP], su.larp);
    led_put(nl, panel.btn[B_EDIT], su.locks[su.focus % 7u]);
    led_put(nl, panel.btn[B_OCTDN], su.oct < 0);
    led_put(nl, panel.btn[B_OCTUP], su.oct > 0);
    for (k = 0; k < 16u; k++) {
        uint32_t id = 14u + SU_WHITE[k], on = 0, dim = 0;
        switch (page) {
        case SP_HOME: dim = 1; on = (su.down >> SU_WHITE[k]) & 1u || (su.focus != AL_DRUMS && k % 7u == 0u); break;
        case SP_FX: dim = k < 2u; on = (k == 0u && su.shimmer) || (k == 1u && su.tape); break;
        case SP_SEL: dim = k < AB_NWORLDS; on = k < AB_NWORLDS && (k == ab.world || (ab.world_q == k && blink)); break;
        case SP_ENV: dim = k < 11u; on = (k == AG_HOLD && ab.hold) || (k == AG_FADE && ab.fade); break;
        case SP_LFO: dim = k != 7u; on = (k < 7u && k == su.focus) || (k >= 8u && k < 15u && !sr.mute[k - 8u]); break;
        case SP_SAVE: dim = 1; on = su.fav_set[k] && !(su.fav[k] == ab.seed && blink); break;
        default: dim = k < 2u; break;
        }
        if (on) led_put(nl, id, 1); else if (dim) led_put(dl, id, 1);
    }
    for (k = 0; k < 11u; k++) {
        uint32_t id = 14u + SU_BLACK[k], on;
        if (page == SP_HOME && su.focus != AL_DRUMS) {
            uint32_t np = ab.nprog ? ab.nprog : 1u;
            on = (su.down >> SU_BLACK[k]) & 1u || (sr.playing && k < np && (int)k == ab.chord_i);
        } else on = (k == AG_HOLD && ab.hold) || (k == AG_FADE && ab.fade);
        led_put(on ? nl : dl, id, 1);
    }
    for (c = 0; c < FM1_NCOL; c++) { fm1_led[c] = nl[c]; fm1_led_dim[c] = dl[c]; fm1_led_bg[c] = 0; }
}

static void siren_frame(void)
{
    siren_input();
    siren_service();
    siren_leds();
    siren_draw();
}

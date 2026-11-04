/* SPDX-License-Identifier: GPL-3.0-only */
/* ABYSS 0.1: the pages and the hands, through abyss_ui.c as the FM-1 runs it (buttons, keys and knobs told here, the
 * firmware's audio path playing): each page opens with its button and stays, the same button or HOME closes it; the
 * textures, the world (on the next piece; again: now), the gestures (RISE to the swell, PEAK NOW through a breath, CALM,
 * HOLD, NEXT PIECE, FADE OUT), the layers (a strip, on / off), the seeds, the settings; the deck's keyboard (the
 * layer taken, its own notes out, given back by HOME); the knobs. Screens of every page as PPM in OUT_DIR.
 *   build/host/abyss_pages OUT_DIR */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/abyss.c"
/* ---- what the FM-1's panel gives siren_ui.c (firmware/src/panel.c, hal/fm1_input.h), told by the test */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static struct { uint8_t btn[NB]; } panel = {{2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1}};
static uint32_t t_pressed, t_notes; static int32_t t_enc[NE];
static uint32_t fm1_input_edges(uint32_t *rel) { uint32_t p = t_pressed; (void)rel; t_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t p = t_notes; t_notes = 0; return p; }
static int32_t panel_enc(uint32_t r) { int32_t s = t_enc[r]; t_enc[r] = 0; return s; }
#define FM1_NCOL 8
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static void led_pos_init(void) {}
static void led_put(uint8_t *nl, uint32_t id, int on) { (void)nl; (void)id; (void)on; }
static uint32_t eng_pend;
static void set_engine_of(track_t *t, uint32_t e) { (void)t; eng_pend = e; }
static void apply_preset_to(track_t *t, uint32_t p) { sound_fade_begin(t); host_preset_req(t, eng_pend, p); }
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) if (x + i < 240 && y + j < 240) { uint16_t c = p[j * w + i]; fb[(y + j) * 240 + x + i] = (uint16_t)((c >> 8) | (c << 8)); }
}
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/abyss_ui.c"

static int fails;
static const char *const B_NAME_T[6] = {"FX", "SEL", "ENV", "LFO", "SAVE", "GLO"};
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static const char *dir;
static void save(const char *name)
{
    char p[300]; FILE *f; uint32_t i;
    snprintf(p, sizeof p, "%s/page_%s.ppm", dir, name);
    f = fopen(p, "wb"); if (!f) return;
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240 * 240; i++) { uint16_t c = fb[i]; uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
/* time: blocks of audio, a frame of the main loop every ~16 ms */
static uint32_t blk;
static void frames(uint32_t n)
{
    static int32_t out[2 * CTL];
    uint32_t per = FS / 60u / CTL + 1u, i;
    while (n--) {
        for (i = 0; i < per; i++) { mix_block_svc(out, CTL); blk++; fm1_ms = blk * CTL * 1000u / FS; }
        song.cpu_q8 = 70;
        siren_frame();
    }
}
static void bars(uint32_t n) { uint32_t s0 = sr.seq; while (sr.seq - s0 < 16u * n) frames(1); }
static void button(uint32_t b) { t_pressed |= 1u << panel.btn[b]; frames(2); }
static void key_on(uint32_t bit) { t_notes |= 1u << bit; fm1_in.notes |= 1u << bit; frames(2); }
static void key_off(uint32_t bit) { fm1_in.notes &= ~(1u << bit); frames(2); }
static void tap(uint32_t bit) { key_on(bit); key_off(bit); }
#define W(k) SU_WHITE[(k) - 1]
#define BK(k) SU_BLACK[(k) - 1]
static uint32_t gated_notes(uint32_t L, uint8_t *list)   /* the notes held on a layer's part now */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        if (trk[L - 1].v[i].active && trk[L - 1].v[i].gate) list[n++] = trk[L - 1].v[i].note;
    return n;
}

int main(int argc, char **argv)
{
    uint32_t i, k;
    uint8_t nl[NVOICE];
    dir = argc > 1 ? argv[1] : ".";
    memset(trk, 0, sizeof trk); host_tracks_init(); memset(&sr, 0, sizeof sr); siren_init();
    siren_ui_init();
    siren_cmd(AC_SEED, AW_CINEMA, 0, 0xAB55u);
    song.playing = 1;
    frames(2);
    button(B_PLAY);
    CHECK(sr.playing, "PLAY: not playing");
    bars(12);
    CHECK(su.info == SP_HOME, "the deck first");
    save("deck");
    { static const uint8_t PB[6] = {B_FX, B_SCL, B_ENV, B_LFO, B_SAVE, B_GLO};
      for (k = 0; k < 6u; k++) {
          button(PB[k]); frames(30);
          CHECK(su.info == k + 1u, "%s: page %u, not %u", B_NAME_T[k], su.info, k + 1u);
          button(PB[k]);
          CHECK(su.info == SP_HOME, "%s again: not the deck", B_NAME_T[k]);
      }
      button(B_FX); button(B_LFO); CHECK(su.info == SP_LFO, "FX then LFO: the layers page");
      button(B_HOME); CHECK(su.info == SP_HOME, "HOME: not the deck"); }
    /* the deck's knobs: LIGHT, SPACE, DENSITY, TIME */
    t_enc[EN_K1] = 10; t_enc[EN_K2] = -6; t_enc[EN_K3] = 5; t_enc[EN_K4] = -5; frames(3);
    CHECK(ab.knobs[AK_LIGHT] == 700 && sr.space == 70 && ab.knobs[AK_DENSITY] == 600 && ab.knobs[AK_TIME] == 400,
          "the deck's knobs: light %d space %u density %d time %d", ab.knobs[AK_LIGHT], sr.space, ab.knobs[AK_DENSITY], ab.knobs[AK_TIME]);
    t_enc[EN_K1] = -10; t_enc[EN_K2] = 6; t_enc[EN_K3] = -5; t_enc[EN_K4] = 5; frames(3);
    /* FX: the textures */
    button(B_FX); tap(W(1)); frames(4);
    CHECK(sr.shimmer && song.g[G_RTYPE] == RT_SHIMMER, "FX 1: no shimmer");
    save("fx");
    tap(W(1)); tap(W(2)); frames(4); CHECK(!sr.shimmer && sr.tape, "FX: shimmer off, tape on");
    tap(W(2)); button(B_FX);
    /* ENV: RISE goes to the swell at the next bar; HOLD; PEAK NOW through a breath */
    button(B_ENV);
    tap(W(1)); bars(1);
    CHECK(ab.plan_sec[ab.plan_at] == AS_SWELL, "RISE: in %s, not the swell", AB_SEC_NAMES[ab.plan_sec[ab.plan_at]]);
    frames(4); save("env");
    tap(W(8)); { uint32_t at = ab.plan_at; bars(20); CHECK(ab.plan_at == at, "HOLD: the plan moved"); } tap(W(8));
    tap(W(2)); bars(1);
    CHECK(ab.plan_sec[ab.plan_at] == AS_BREATH, "PEAK NOW: no breath first (%s)", AB_SEC_NAMES[ab.plan_sec[ab.plan_at]]);
    bars(ab.plan_len[ab.plan_at]);
    CHECK(ab.plan_sec[ab.plan_at] == AS_PEAK, "PEAK NOW: not the peak (%s)", AB_SEC_NAMES[ab.plan_sec[ab.plan_at]]);
    bars(2); save("deck_peak");
    tap(W(3)); bars(1);
    CHECK(ab.plan_sec[ab.plan_at] == AS_FALL, "CALM: not the fall (%s)", AB_SEC_NAMES[ab.plan_sec[ab.plan_at]]);
    { uint32_t p0 = ab.piece_no; tap(W(9)); bars(1); CHECK(ab.piece_no == p0 + 1u, "NEXT PIECE: still piece %u", ab.piece_no); }
    tap(BK(4));                                              /* (outside the deck the black keys are the gestures) */
    bars(1); CHECK(ab.plan_sec[ab.plan_at] == AS_BREATH || ab.plan_sec[ab.plan_at - 1] == AS_BREATH, "black key 4: no breath");
    button(B_ENV);
    /* SEL: the world on the next piece; again: now */
    button(B_SCL);
    tap(W(2)); frames(2);
    CHECK(ab.world_q == AW_COSMOS && ab.world == AW_CINEMA, "SEL 2: COSMIC not asked for the next piece");
    frames(4); save("sel");
    tap(W(2)); frames(2);
    CHECK(ab.world == AW_COSMOS, "SEL 2 again: not COSMIC now (%u)", ab.world);
    button(B_SCL);
    bars(4);
    /* LFO: the pad's strip; the bells off and on; flat */
    button(B_LFO);
    tap(W(3)); CHECK(su.focus == AL_PAD, "LFO 3: the pad not chosen");
    t_enc[EN_K1] = -8; t_enc[EN_K2] = 10; frames(3);
    CHECK(sr.lvol[AL_PAD] == -8 && sr.lflt[AL_PAD] == 20, "LFO: the pad's strip (%d %d)", sr.lvol[AL_PAD], sr.lflt[AL_PAD]);
    tap(W(14)); CHECK(sr.mute[AL_BELL], "LFO 14: the bells on");
    frames(4); save("lfo");
    tap(W(14)); CHECK(!sr.mute[AL_BELL], "LFO 14 again: the bells off");
    tap(W(16)); frames(2); CHECK(sr.lvol[AL_PAD] == 0 && sr.lflt[AL_PAD] == 0, "LFO 16: not flat");
    button(B_HOME);
    /* SAVE: keep, a new set, play it again, replace */
    { uint32_t s0 = ab.seed, s1;
      button(B_SAVE); tap(W(1)); CHECK(su.fav_set[0] && su.fav[0] == s0, "SAVE 1: not kept");
      button(B_REC); frames(4); s1 = ab.seed; CHECK(s1 != s0, "REC: the same seed");
      tap(W(2)); frames(4); save("save");
      tap(W(1)); frames(4); CHECK(ab.seed == s0, "SAVE 1 tapped: not played");
      key_on(W(2)); frames(60); key_off(W(2)); frames(2);
      CHECK(su.fav[1] == s0 && ab.seed == s0, "SAVE 2 held: not replaced, or played");
      button(B_SAVE); }
    /* GLO: TENSION */
    button(B_GLO); t_enc[EN_K1] = 10; frames(3);
    CHECK(ab.knobs[AK_TENSION] == 600, "GLO K1: tension %d", ab.knobs[AK_TENSION]);
    frames(4); save("glo"); button(B_GLO);
    /* the deck's keyboard: the pad taken (its own chords out), a chord on a black key, given back */
    bars(8);
    frames(2);                                               /* (the pad, chosen on the LFO page) */
    CHECK(su.focus == AL_PAD, "SELECT: not the pad (%u)", su.focus);
    key_on(BK(1)); frames(3);
    CHECK((sr.live >> AL_PAD) & 1u, "the pad not taken");
    { uint32_t n = gated_notes(AL_PAD, nl); CHECK(n == 3u, "black key 1 on the pad: %u notes", n); }
    for (i = 0; i < 64u; i++) { uint32_t n = gated_notes(AL_PAD, nl); CHECK(n <= 3u, "the pad taken: ABYSS's chords sound (%u)", n); if (fails > 6) break; frames(1); }
    frames(4); save("deck_live");
    key_off(BK(1)); frames(2);
    CHECK(gated_notes(AL_PAD, nl) == 0u, "the chord let go: the pad holds");
    key_on(W(1)); frames(2);
    { uint32_t n = gated_notes(AL_PAD, nl), want = 48u + (ab.key + ab.lift + 24u) % 12u; CHECK(n == 1u && nl[0] == want, "key 1 on the pad: %u (want %u)", n ? nl[0] : 0u, want); }
    key_off(W(1));
    button(B_HOME);
    CHECK(!sr.live, "HOME on the deck: the pad not given back");
    bars(4);
    CHECK(sr.playing, "still playing");
    button(B_PLAY); CHECK(!sr.playing, "PLAY: not stopped");
    printf(fails ? "pages: %d FAILED\n" : "pages: ok\n", fails);
    return fails != 0;
}

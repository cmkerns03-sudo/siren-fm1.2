/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.8: the pages and the keyboard, through siren_ui.c as the FM-1 runs it (the buttons, keys and knobs told
 * here, the firmware's audio path playing): each page opens with its button and stays open, the same button or HOME
 * closes it; FX (an effect while its key is held, gone with it), SEL (the style on the phrase), ENV (the form), LFO
 * (a layer's strip, its on / off), SAVE (keep, play, replace a seed), GLO (the settings); the deck's keyboard (the
 * scale's notes on the layer chosen, SIREN's own notes for it out while it is in the hands and back when HOME gives
 * it back; the chords on the black keys; OCT; the arpeggiator; the drums on top of the groove); EDIT, SEQ, REC.
 * Screens of every page as PPM in OUT_DIR.
 *   build/host/siren_pages OUT_DIR */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
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
#include "../../firmware/src/siren_ui.c"

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
    siren_cmd(SC_STYLELOCK, 1, 0, 0); siren_cmd(SC_SEED, SB_TECHNO, 0, 0x51DE5u);
    song.playing = 1;
    frames(2);
    button(B_PLAY);
    CHECK(sr.playing, "PLAY: not playing");
    bars(8);
    CHECK(su.info == SP_HOME, "the deck first");
    save("deck");

    /* the pages: open with their button, stay open, the same button closes, another switches, HOME closes */
    { static const uint8_t PB[6] = {B_FX, B_SCL, B_ENV, B_LFO, B_SAVE, B_GLO};
      for (k = 0; k < 6u; k++) {
          button(PB[k]); frames(30);
          CHECK(su.info == k + 1u, "%s: page %u, not %u", B_NAME_T[k], su.info, k + 1u);
          button(PB[k]);
          CHECK(su.info == SP_HOME, "%s again: not the deck", B_NAME_T[k]);
      }
      button(B_FX); button(B_LFO);
      CHECK(su.info == SP_LFO, "FX then LFO: the layers page");
      button(B_HOME);
      CHECK(su.info == SP_HOME, "HOME: not the deck"); }

    /* FX: an effect while its key is held, the set back when it is let go */
    button(B_FX);
    key_on(W(5)); frames(10);
    CHECK(sr.px == SPX_LOW, "FX 5: LOW PASS not on (%d)", sr.px);
    save("fx");
    key_off(W(5)); frames(2);
    CHECK(sr.px == SPX_NONE, "FX 5 let go: still on");
    key_on(W(1)); button(B_FX);
    CHECK(sr.px == SPX_NONE && su.info == SP_HOME, "FX closed with a key held: the effect stays");
    key_off(W(1));

    /* SEL: a style on the phrase; 16 keeps it */
    button(B_SCL);
    tap(W(2));
    CHECK(sr.q_style == SB_TRANCE, "SEL 2: TRANCE not asked for (%d)", sr.q_style);
    frames(4); save("sel");
    bars(9);
    CHECK(sb.style == SB_TRANCE, "SEL 2: still %u after 9 bars", sb.style);
    tap(W(16));
    CHECK(su.lock_style == 1u, "SEL 16: style not kept");
    tap(W(16));
    button(B_SCL);

    /* ENV: the form: DROP at the next bar, STAY */
    button(B_ENV);
    tap(W(4));
    CHECK(sb.drop_req || sr.ev.sec == SEC_DROP, "ENV 4: DROP not asked for");
    save("env");
    bars(2);
    CHECK(sr.ev.sec == SEC_DROP, "ENV 4: not in the drop");
    tap(W(11)); CHECK(sb.stay, "ENV 11: STAY off");
    tap(W(11)); CHECK(!sb.stay, "ENV 11 again: STAY on");
    tap(BK(4));                                              /* (outside the deck the black keys are the form's) */
    button(B_ENV);

    /* LFO: the hook's strip on the knobs; the pad off and on; flat */
    button(B_LFO);
    tap(W(4));
    CHECK(su.focus == L_HOOK, "LFO 4: the hook not chosen");
    t_enc[EN_K1] = -8; t_enc[EN_K2] = 10; frames(3);
    CHECK(sr.lvol[L_HOOK] == -8 && sr.lflt[L_HOOK] == 20, "LFO: the hook's strip (%d %d)", sr.lvol[L_HOOK], sr.lflt[L_HOOK]);
    CHECK(su.mfilt == 0, "LFO: K2 moved the master's filter");
    tap(W(13));
    CHECK(sr.mute[L_PAD], "LFO 13: the pad on");
    frames(4); save("lfo");
    tap(W(13));
    CHECK(!sr.mute[L_PAD], "LFO 13 again: the pad off");
    tap(W(16)); frames(2);
    CHECK(sr.lvol[L_HOOK] == 0 && sr.lflt[L_HOOK] == 0, "LFO 16: not flat");
    button(B_HOME);
    t_enc[EN_K1] = -5; frames(2);
    CHECK(su.mfilt == -10 && sr.ufilt == -10, "the deck: K1 not the master's filter (%d)", su.mfilt);
    t_enc[EN_K1] = 5; frames(2);

    /* SAVE: keep, a new rave, play it again, replace */
    { uint32_t s0 = sb.seed, s1;
      button(B_SAVE);
      tap(W(1));
      CHECK(su.fav_set[0] && su.fav[0] == s0, "SAVE 1: not kept");
      button(B_REC); frames(4);
      s1 = sb.seed;
      CHECK(s1 != s0, "REC: the same seed");
      tap(W(2)); frames(4);
      save("save");
      tap(W(1)); frames(4);
      CHECK(sb.seed == s0, "SAVE 1 tapped: not played (%08x, kept %08x)", sb.seed, s0);
      key_on(W(2)); frames(60); key_off(W(2)); frames(2);
      CHECK(su.fav[1] == s0 && sb.seed == s0, "SAVE 2 held: not replaced, or played");
      button(B_SAVE); }

    /* GLO: the settings' knobs */
    button(B_GLO);
    t_enc[EN_K1] = 10; frames(3);
    CHECK(sb.knobs[K_TENSION] == 700, "GLO K1: tension %d", sb.knobs[K_TENSION]);
    frames(4); save("glo");
    t_enc[EN_K1] = -10; frames(2);
    button(B_GLO);

    /* the deck's keyboard: the bass taken, SIREN's bass out, the notes of the scale; given back */
    bars(1);
    t_enc[EN_SELECT] = -2; frames(2);                    /* (HOOK -> BASS) */
    CHECK(su.focus == L_BASS, "SELECT: not the bass (%u)", su.focus);
    key_on(W(1)); frames(3);
    CHECK((sr.live >> L_BASS) & 1u, "the bass not taken");
    { uint32_t want = 36u + sb.key, n = gated_notes(L_BASS, nl), ok = n == 1u && nl[0] == want;
      CHECK(ok, "key 1 on the bass: %u notes, %u (want %u)", n, n ? nl[0] : 0u, want); }
    for (i = 0; i < 64u; i++) {                              /* four bars: only the hand's note on the bass */
        uint32_t n = gated_notes(L_BASS, nl), j;
        for (j = 0; j < n; j++) CHECK(nl[j] == 36u + sb.key, "the bass taken: SIREN's %u sounds", nl[j]);
        if (fails > 4) break;
        frames(1);
    }
    CHECK(!sr.sgate, "the bass taken: its sub sounds");
    frames(4); save("deck_live");
    key_off(W(1)); frames(2);
    CHECK(gated_notes(L_BASS, nl) == 0u, "key 1 let go: the bass still holds");
    tap(W(3));                                               /* (the third degree: the scale's) */
    key_on(W(8)); frames(2);
    CHECK(gated_notes(L_BASS, nl) == 1u && nl[0] == 48u + sb.key, "key 8: an octave up (%u)", nl[0]);
    key_off(W(8));
    button(B_OCTUP); key_on(W(1)); frames(2);
    CHECK(gated_notes(L_BASS, nl) == 1u && nl[0] == 48u + sb.key, "OCT+: key 1 an octave up (%u)", nl[0]);
    key_off(W(1)); button(B_OCTDN);
    bars(2);
    CHECK(gated_notes(L_BASS, nl) == 0u, "the bass taken, no key: it is silent");
    button(B_HOME);
    CHECK(!sr.live, "HOME on the deck: the bass not given back");
    { uint32_t heard = 0, wrote = 0, n0 = sr.last_note[L_BASS];   /* (SIREN's bass back as soon as it writes one) */
      for (i = 0; i < 32u * 16u * 4u && !heard; i++) { frames(1); wrote |= sr.last_note[L_BASS] != n0; heard = gated_notes(L_BASS, nl) > 0u; }
      CHECK(heard && wrote, "given back: SIREN's bass not back (written %u)", wrote); }

    /* the pad: a chord on a black key (the track's first), its notes the scale's */
    t_enc[EN_SELECT] = 3; frames(2);                         /* (BASS -> PAD) */
    CHECK(su.focus == L_PAD, "SELECT: not the pad (%u)", su.focus);
    key_on(BK(1)); frames(3);
    { uint32_t n = gated_notes(L_PAD, nl), d0 = sb.prog[0], want[3], j, hit = 0;
      for (j = 0; j < 3u; j++) want[j] = su_n(L_PAD, (int32_t)(d0 + 2u * j));
      for (j = 0; j < n; j++) hit += nl[j] == want[0] || nl[j] == want[1] || nl[j] == want[2];
      CHECK(n == 3u && hit == 3u, "black key 1 on the pad: %u notes, %u of the chord", n, hit); }
    frames(4); save("deck_chord");
    key_off(BK(1)); frames(2);
    CHECK(gated_notes(L_PAD, nl) == 0u, "the chord let go: the pad holds");

    /* the arpeggiator: three keys held on the hook, one note a sixteenth, upwards */
    t_enc[EN_SELECT] = -1; frames(2);                        /* (PAD -> HOOK) */
    button(B_ARP);
    CHECK(sr.larp, "ARP: not on");
    t_notes |= (1u << W(1)) | (1u << W(3)) | (1u << W(5)); fm1_in.notes |= (1u << W(1)) | (1u << W(3)) | (1u << W(5)); frames(2);
    { uint32_t seen[3] = {0, 0, 0}, last = 0, ups = 0, most = 0, s0 = sr.seq;
      while (sr.seq - s0 < 32u) {
          uint32_t n = gated_notes(L_HOOK, nl), j;
          if (n > most) most = n;
          for (j = 0; j < n; j++) {
              uint32_t q;
              for (q = 0; q < 3u; q++) if (nl[j] == su_n(L_HOOK, (int32_t)(2u * q))) { if (nl[j] > last) ups++; if (nl[j] != last) seen[q]++; last = nl[j]; }
          }
          frames(1);
      }
      CHECK(seen[0] && seen[1] && seen[2] && most <= 2u, "ARP: notes %u %u %u, at most %u at once", seen[0], seen[1], seen[2], most);
      CHECK(ups >= 12u, "ARP: not upwards (%u)", ups); }
    fm1_in.notes = 0; frames(2);
    button(B_ARP);
    CHECK(!sr.larp, "ARP: not off");
    button(B_HOME);

    /* the drums: the kit on the white keys, on top of the groove (not taken) */
    t_enc[EN_SELECT] = -3; frames(2);                        /* (HOOK -> DRUMS) */
    { uint32_t k0 = sr.kicks;
      tap(W(1));
      CHECK(sr.kicks > k0, "drums key 1: no kick");
      CHECK(!sr.live, "drums: taken"); }
    tap(BK(5));                                              /* (the drums' black keys: the form's) */
    CHECK(sr.fill_req || sr.fill_on, "drums, black key 5: no FILL");

    /* EDIT keeps the layer chosen, SEQ rerolls it */
    t_enc[EN_SELECT] = 3; frames(2);                         /* (DRUMS -> HOOK) */
    button(B_EDIT);
    CHECK(sb.locks[L_HOOK], "EDIT: the hook not kept");
    button(B_EDIT);
    CHECK(!sb.locks[L_HOOK], "EDIT again: the hook still kept");
    { uint16_t r0 = sb.rerolls[L_HOOK]; button(B_SEQ); CHECK(sb.rerolls[L_HOOK] != r0, "SEQ: the hook not rerolled"); }
    bars(4);
    CHECK(sr.playing, "still playing");
    button(B_PLAY);
    CHECK(!sr.playing, "PLAY: not stopped");
    printf(fails ? "pages: %d FAILED\n" : "pages: ok\n", fails);
    return fails != 0;
}

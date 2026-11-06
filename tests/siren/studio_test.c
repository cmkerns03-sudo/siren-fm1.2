/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.10: SIREN STUDIO's SysEx (firmware/src/editor_siren.c) through siren_ui.c and the firmware's audio path:
 * INFO and NAMES (every layer's universe), the live gestures (PLAY, the form, an effect held from the web and let go,
 * the style, ALL with every kit, the mixer, the knobs, PRESET through every kit, a note held), the seeds (put, read,
 * played in their style), the banks (a style's bass narrowed to one sound, its next track picks it; back to the
 * factory's), an audition, the state.  build/host/siren_studio        (bridge: frames as hex lines, for the web test)
 */
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
/* (0.9.2) the flash: settings_save counted, the voices as project.c's audio_quiet sees them */
static uint32_t t_saves; static int t_unsaved;
static void settings_save(void) { t_saves++; t_unsaved = 0; }
static int siren_side_unsaved(void) { return t_unsaved; }
static int srb_write(void) { return 1; }
static int audio_quiet(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++) for (i = 0; i < NVOICE; i++) if (trk[p].v[i].active) return 0;
    for (i = 0; i < NDRUM; i++) if (drums.v[i].active) return 0;
    return 1;
}
#include "../../firmware/src/siren_ui.c"

/* ---- the editor's framing (editor.c), the replies kept */
#define FELUCCA_VERSION_T "SIREN TEST"
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION FELUCCA_VERSION_T
#endif
static uint8_t ed_out[600]; static uint32_t ed_n;
static void ed_begin(uint32_t cmd) { ed_out[0] = 0xF0; ed_out[1] = 0x7D; ed_out[2] = 0x46; ed_out[3] = 0x4C; ed_out[4] = (uint8_t)cmd; ed_n = 5; }
static void ed_b(uint32_t v) { if (ed_n < sizeof ed_out - 1u) ed_out[ed_n++] = (uint8_t)(v & 0x7Fu); }
static void ed_v(int32_t v) { uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192); ed_b(u); ed_b(u >> 7); }
static void ed_str(const char *s, uint32_t max) { uint32_t i; for (i = 0; s && s[i] && i < max; i++) ed_b((uint8_t)s[i] & 0x7Fu); ed_b(0); }
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
#include "../../firmware/src/editor_siren.c"
/* a request (cmd, args) -> the reply's bytes after the cmd (r), their count */
static const uint8_t *rq(uint32_t cmd, const uint8_t *a, uint32_t na, uint32_t *n)
{
    ed_begin(cmd);
    if (!ed_siren_handle(cmd, a, na)) { *n = 0; return 0; }
    ed_out[ed_n++] = 0xF7;
    *n = ed_n - 6u;
    return ed_out + 5;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
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
static int act(uint32_t kind, uint32_t a, int32_t v)
{
    uint8_t q[4]; uint32_t n; const uint8_t *r;
    q[0] = (uint8_t)kind; q[1] = (uint8_t)a; q[2] = (uint8_t)((v + 8192) & 127); q[3] = (uint8_t)(((v + 8192) >> 7) & 127);
    r = rq(SR_ACT, q, 4, &n);
    frames(2);
    return r && n == 1 && r[0] == 1;
}
/* bridge: SIREN STUDIO's frames as hex lines on stdin ("F0 7D .. F7"), the replies as hex lines on stdout, the
 * firmware playing between them (tests/siren/studio_e2e.py, the web page in a browser) */
static int bridge(void)
{
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        uint8_t f[1400]; uint32_t n = 0, i, rn; char *p = line; const uint8_t *r;
        if (line[0] == 't') { frames((uint32_t)atoi(line + 1)); printf("ok\n"); fflush(stdout); continue; }   /* (time) */
        while (*p && n < sizeof f) { unsigned v; int c; if (sscanf(p, "%2x%n", &v, &c) != 1) break; f[n++] = (uint8_t)v; p += c; while (*p == ' ') p++; }
        if (n < 6 || f[0] != 0xF0 || f[n - 1] != 0xF7 || f[1] != 0x7D) { printf("\n"); fflush(stdout); continue; }
        r = rq(f[4], f + 5, n - 6, &rn);
        if (!r) { printf("\n"); fflush(stdout); continue; }
        for (i = 0; i < ed_n; i++) printf("%02X%s", ed_out[i], i + 1 < ed_n ? " " : "\n");
        fflush(stdout);
        frames(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t n, i, L;
    const uint8_t *r;
    memset(trk, 0, sizeof trk); host_tracks_init(); memset(&sr, 0, sizeof sr); siren_init();
    siren_ui_init();
    siren_cmd(SC_STYLELOCK, 1, 0, 0); siren_cmd(SC_SEED, SB_TECHNO, 0, 0x51DE5u);
    song.playing = 1;
    frames(2);
    if (argc > 1 && !strcmp(argv[1], "bridge")) return bridge();

    /* INFO: the universe of each layer */
    r = rq(SR_INFO, 0, 0, &n);
    { uint32_t o = 0; while (r[o]) o++; o++;
      CHECK(r[o] == SR_PROTO && r[o + 1] == 8 && r[o + 2] == 7, "INFO: proto / styles / layers");
      CHECK(r[o + 3 + L_DRUMS] == SD_NKITS && r[o + 3 + L_FX] == RS_N && r[o + 3 + L_BASS] == srb_nuni[L_BASS] && srb_nuni[L_BASS] >= 50,
            "INFO: the universes (drums %u, bass %u)", r[o + 3 + L_DRUMS], r[o + 3 + L_BASS]); }
    /* NAMES: every sound of every layer, once */
    for (L = 0; L < 7u; L++) {
        uint32_t got = 0, j0 = 0;
        while (j0 < srb_nuni[L]) {
            uint8_t q[2] = {(uint8_t)L, (uint8_t)j0}; uint32_t k, o = 3;
            r = rq(SR_NAMES, q, 2, &n);
            CHECK(r && r[0] == L && r[1] == j0 && r[2] > 0, "NAMES %u %u", L, j0);
            if (!r || !r[2]) break;
            for (k = 0; k < r[2]; k++) { while (r[o]) o++; o++; while (r[o]) o++; o++; o += 2; }
            CHECK(o == n, "NAMES %u %u: %u bytes, %u read", L, j0, n, o);
            got += r[2]; j0 += r[2];
        }
        CHECK(got == srb_nuni[L], "NAMES %u: %u of %u", L, got, srb_nuni[L]);
    }
    { uint8_t q[2] = {L_BASS, 0}; r = rq(SR_NAMES, q, 2, &n); CHECK(!strcmp((const char *)r + 3, "ANALOG") && !strcmp((const char *)r + 10, "SAW BASS"), "NAMES: the first bass (%s %s)", r + 3, r + 10); }

    /* ACT: PLAY, the form, an effect held and let go, the style, ALL (every kit then), the mixer, the knobs */
    CHECK(act(SA_PLAY, 0, 0) && (frames(4), sr.playing), "ACT PLAY");
    for (i = 0; i < 200 && sb.plan_at == 0; i++) frames(4);
    CHECK(act(SA_FORM, 3, 0) && sb.drop_req, "ACT FORM 4: no DROP asked");
    CHECK(act(SA_FX_DOWN, 4, 0) && su.px_key == 4 && sr.px != SPX_NONE, "ACT FX 5 held");
    frames(10);
    CHECK(su.px_key == 4, "ACT FX 5: let go before FX_UP");
    CHECK(act(SA_FX_UP, 0, 0) && su.px_key == -1, "ACT FX_UP: still held");
    /* the drums through every kit (PRESET), any style's */
    { uint32_t seen = 0; for (i = 0; i < SD_NKITS; i++) { act(SA_PRESET, L_DRUMS, 1); frames(2); seen |= 1u << srb_list[sb.style % 8][L_DRUMS][sb.snd[L_DRUMS] % srb_all[L_DRUMS]]; }
      CHECK(seen == (1u << SD_NKITS) - 1u, "PRESET on the drums: kits %04x", seen); }
    act(SA_LOCK, L_DRUMS, 0); act(SA_LOCK, L_DRUMS, 0);
    CHECK(act(SA_STYLE, SB_HOUSE, 0) && sr.q_style == SB_HOUSE, "ACT STYLE: not queued (%d)", sr.q_style);
    CHECK(act(SA_ALL, 0, 1) && sr.allbank && sb_pal.nkits[SB_TECHNO] == SD_NKITS, "ACT ALL: every kit (%u)", sb_pal.nkits[SB_TECHNO]);
    CHECK(act(SA_ALL, 0, 0) && !sr.allbank && sb_pal.nkits[SB_TECHNO] == SD_KIT_N[SB_TECHNO], "ACT ALL off");
    { int8_t v0 = su.lvol[L_PAD]; CHECK(act(SA_STRIP, L_PAD * 4 + 0, -3) && su.lvol[L_PAD] == v0 - 3 && sr.lvol[L_PAD] == su.lvol[L_PAD], "ACT STRIP: the pad's volume"); }
    { int16_t k0 = su.knob[0]; CHECK(act(SA_DJKNOB, 1, 5) && su.knob[0] == k0 + 100, "ACT DJ knob INTENSITY"); }
    CHECK(act(SA_MUTE, L_ARP, 0) && sr.mute[L_ARP], "ACT MUTE"); act(SA_MUTE, L_ARP, 0);
    CHECK(su.info == SP_HOME, "ACT: a page opened (%u)", su.info);
    /* the keyboard: a note held from the web, let go */
    act(SA_FOCUS, L_HOOK, 0);
    CHECK(act(SA_NOTE_ON, SU_WHITE[3], 100) && (su.down >> SU_WHITE[3] & 1u), "ACT NOTE_ON");
    frames(5);
    CHECK(su.down >> SU_WHITE[3] & 1u, "ACT NOTE_ON: let go before NOTE_OFF");
    act(SA_NOTE_OFF, SU_WHITE[3], 0); frames(2);
    CHECK(!(su.down >> SU_WHITE[3] & 1u), "ACT NOTE_OFF: still down");
    act(SA_GIVEBACK, 0, 0);

    /* SEEDS: put, read back, play one (its style, its ALL) */
    { uint8_t q[16 * 7]; memset(q, 0, sizeof q);
      q[0] = 1; q[1] = SB_PSY | 8; { uint32_t v = 0x0BADCAFEu & 0x7FFFFFFF, k; for (k = 0; k < 5; k++) { q[2 + k] = v & 127; v >>= 7; } }
      q[3 * 7] = 1; q[3 * 7 + 1] = SB_DNB; q[3 * 7 + 2] = 5;
      r = rq(SR_SEEDS_PUT, q, sizeof q, &n); CHECK(r && r[0] == 1 && su.fav_dirty, "SEEDS_PUT");
      r = rq(SR_SEEDS, 0, 0, &n); CHECK(n == 16 * 7 && !memcmp(r, q, sizeof q), "SEEDS: not what was put");
      CHECK(act(SA_SEED_PLAY, 0, 0), "ACT SEED_PLAY"); frames(4);
      CHECK(sb.seed == (0x0BADCAFEu & 0x7FFFFFFF) && sb.style == SB_PSY && sr.allbank, "SEED_PLAY: seed %08x style %u all %u", sb.seed, sb.style, sr.allbank);
      CHECK(!act(SA_SEED_PLAY, 1, 0), "SEED_PLAY of an empty slot"); }

    /* BANK: TECHNO's bass only one sound; the next tracks pick it; back to the factory's */
    { uint8_t q[12] = {SB_TECHNO, L_BASS}; uint64_t m = (uint64_t)1 << 20; uint32_t k, j;
      act(SA_ALL, 0, 0);
      for (k = 0; k < 10; k++) q[2 + k] = (uint8_t)((m >> (7 * k)) & 127);
      r = rq(SR_BANK_PUT, q, 12, &n); CHECK(r && r[0] == 1 && srb_dirty, "BANK_PUT");
      CHECK(srb_own[SB_TECHNO][L_BASS] == 1 && sb_pal.ncur[SB_TECHNO][L_BASS] == 1 && srb_list[SB_TECHNO][L_BASS][0] == 20, "BANK_PUT: the list");
      siren_cmd(SC_SEED, SB_TECHNO, 0, 777u); frames(6);
      j = (uint32_t)sr_now(L_BASS);
      CHECK(j == 20, "BANK_PUT: TECHNO's new track's bass is universe %u, not 20", j);
      r = rq(SR_BANK, q, 2, &n); CHECK(r && r[3] == 1 && sr_ru64(r + 4) == m, "BANK: the user's");
      memset(q + 2, 0, 10); r = rq(SR_BANK_PUT, q, 12, &n); CHECK(r && r[0] == 0, "BANK_PUT: an empty bank taken");
      { uint8_t z[2] = {127, 127}; r = rq(SR_BANK_RESET, z, 2, &n); }
      CHECK(srb_mask[SB_TECHNO][L_BASS] == srb_fact[SB_TECHNO][L_BASS] && srb_own[SB_TECHNO][L_BASS] == SD_CUR[SB_TECHNO][L_BASS].n, "BANK_RESET"); }

    /* AUDITION: a hook sound in no bank of the style, now */
    { uint8_t q[2] = {L_HOOK, 3}; int16_t e0 = sr.sound_eng[L_HOOK], p0 = sr.sound_pre[L_HOOK];
      r = rq(SR_AUDITION, q, 2, &n); frames(2);
      CHECK(r && r[0] == 1, "AUDITION"); (void)e0; (void)p0;
      { int16_t pe, e = siren_find(&SD_SND[SD_LIST[srb_uni[L_HOOK][3]]], &pe); CHECK(sr.sound_eng[L_HOOK] == e && sr.sound_pre[L_HOOK] == pe, "AUDITION: not loaded"); } }

    /* STATE: what plays */
    r = rq(SR_STATE, 0, 0, &n);
    CHECK(r && (r[0] & 1u) && r[2] == sb.style && sr_ru32(r + 6) == sb.seed, "STATE: playing, style, seed");
    CHECK(n > 7 * 9 + 30 && n < 200, "STATE: %u bytes", n);
    printf(fails ? "studio: %d FAILED\n" : "studio: ok\n", fails);
    return fails != 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.5: the hands, through the firmware's audio path, every style: the black keys (DROP at the next bar, FILL in
 * the next bar's second half then a crash, KEY UP at the drop, NEXT TRACK on the phrase and twice at once, STAY loops
 * the section, NEW MELODY / BASS / GROOVE another motif), the mixer (the master's filter and space, each layer's
 * volume, filter, space), PRESET on a layer (another sound of the style's, its calibration, kept), and the arrangement:
 * never more than three melodic layers sounding at once (arp, hook, pad, stabs, fx), the layer in the hands among them.
 * (With "wav": OUT_DIR/hands-STYLE.wav, about 9 minutes each.)
 *   build/host/siren_hands OUT_DIR [STYLE [wav]] */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { sound_fade_begin(t); host_preset_req(t, e, p); }

static FILE *wf; static uint32_t wn;
static int fails; static uint32_t worst, steps_seen, peak_n;
static double peak;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static uint32_t gated(uint32_t L)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += trk[L - 1].v[i].active && trk[L - 1].v[i].gate;
    return n;
}
/* one block; at each new sixteenth, the arrangement's rule checked: the melodic layers sounding (a voice held) */
static void block1(void)
{
    static int32_t out[2 * CTL]; static int16_t o[2 * CTL]; static uint32_t seq, b;
    uint32_t i;
    if (b++ % 40 == 0) siren_service();
    mix_block_svc(out, CTL);
    for (i = 0; i < 2 * CTL; i++) {
        double x = fabs(out[i] / 32768.0);
        if (x != x) { printf("NaN\n"); exit(1); }
        if (x > peak) peak = x;
        if (x > 0.999) peak_n++;
        o[i] = (int16_t)out[i];
    }
    if (wf) { fwrite(o, 2, 2 * CTL, wf); wn += CTL; }
    if (sr.seq != seq) {
        uint32_t L, n = 0;
        seq = sr.seq; steps_seen++;
        for (L = L_ARP; L <= L_FX; L++) n += gated(L) > 0;
        if (n > worst) worst = n;
        if (sr.focus && !sr.mute[sr.focus] && siren_wrote(sr.focus, sr.n16 - 1u))
            CHECK((sr.allowed >> sr.focus) & 1u, "the layer in the hands (%u) left out", sr.focus);
    }
}
static void steps(uint32_t n) { uint32_t s0 = sr.seq; while (sr.seq - s0 < n) block1(); }
static void bars(uint32_t n) { steps(16 * n); }
static void next_bar(void) { uint32_t i; steps(1); for (i = 0; i < 16 && sr.ev.step != 0; i++) steps(1); }   /* (to the next bar's first step) */

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "."; int s0 = argc > 2 ? atoi(argv[2]) : 0, s1 = argc > 2 ? s0 + 1 : 8, st, wav = argc > 3;
    for (st = s0; st < s1; st++) {
        char path[512]; uint32_t k, t0, key0, v0, i;
        memset(trk, 0, sizeof trk); host_tracks_init(); memset(&sr, 0, sizeof sr); siren_init();
        siren_cmd(SC_STYLELOCK, 1, 0, 0); siren_cmd(SC_SEED, st, 0, 0x5A5Au + (uint32_t)st); siren_cmd(SC_KNOB, K_ENERGY, 700, 0);
        siren_cmd(SC_MIX, SM_FOCUS, L_HOOK, 0);
        siren_cmd(SC_PLAY, 0, 0, 0); song.playing = 1;
        if (wav) { snprintf(path, sizeof path, "%s/hands-%s.wav", dir, SB_STYLE_NAMES[st]); wf = fopen(path, "wb"); wn = 0; wav_hdr(wf, 0); }
        worst = 0; steps_seen = 0; peak = 0; peak_n = 0;
        bars(24);
        printf("hands: %s (%u BPM)\n", SB_STYLE_NAMES[st], sb.bpm);

        /* DROP: from the next bar */
        steps(5); siren_cmd(SC_DROP, 0, 0, 0); next_bar();
        CHECK(sr.ev.sec == SEC_DROP, "DROP: not in the drop at the next bar (%s)", SB_SEC_NAMES[sr.ev.sec % 7]);
        bars(4);
        /* FILL: the next bar's second half, the snare rising; a crash on the bar after */
        { uint32_t on = 0, crash0;
          siren_cmd(SC_FILL, 0, 0, 0);
          for (i = 0; i < 40 && !(sr.fill_on && sr.ev.step >= 8); i++) steps(1);
          CHECK(sr.fill_on && sr.ev.step >= 8, "FILL: it did not start");
          while (sr.fill_on && on < 20) { steps(1); on++; }
          crash0 = sr.ev.step;
          CHECK(!sr.fill_on && crash0 == 0 && on <= 9, "FILL: %u steps, ends at step %u", on, crash0); }
        bars(2);
        /* KEY UP: a tone up at the next drop */
        siren_cmd(SC_BREAK, 0, 0, 0); bars(3);
        key0 = sb.key; siren_cmd(SC_KEYUP, 0, 0, 0); steps(3);
        CHECK(sb.key == key0, "KEY UP: before the drop");
        siren_cmd(SC_DROP, 0, 0, 0); next_bar();
        CHECK(sb.key == (key0 + 2) % 12, "KEY UP: key %u, was %u", sb.key, key0);
        /* NEW MELODY: another motif for the hook and the arp, the sound kept */
        v0 = sb.motif_ver[L_HOOK]; k = (uint32_t)sb.snd[L_HOOK];
        siren_cmd(SC_REMOTIF, L_HOOK, 1, 0); siren_cmd(SC_REMOTIF, L_ARP, 1, 0); steps(1);
        CHECK(sb.motif_ver[L_HOOK] == (uint8_t)(v0 + 1) && (uint32_t)sb.snd[L_HOOK] == k, "NEW MELODY: motif %u sound %d", sb.motif_ver[L_HOOK], sb.snd[L_HOOK]);
        bars(4);
        /* STAY: the section goes round */
        { uint32_t at = sb.plan_at, sec = sb.plan_sec[sb.plan_at];
          siren_cmd(SC_STAY, 0, 0, 0); bars(72);
          CHECK(sb.plan_at == at || sec == SEC_BUILD, "STAY: the plan moved on (%u -> %u)", at, sb.plan_at);
          siren_cmd(SC_STAY, 0, 0, 0); bars(1); }
        /* PRESET on the hook: the next sound of the style's, calibrated, kept */
        { int16_t was = sb.snd[L_HOOK]; uint32_t n = SD_CUR[sb.style][L_HOOK].n, at;
          siren_cmd(SC_SOUND, L_HOOK, 1, 0); steps(1);
          for (i = 0; i < 400; i++) block1();
          at = SD_CUR[sb.style][L_HOOK].first + (uint32_t)sb.snd[L_HOOK] % n;
          CHECK(n < 2 || (sb.snd[L_HOOK] != was && sb.locks[L_HOOK]), "PRESET: the hook's sound did not change");
          CHECK(sr.applied[L_HOOK] == sb.snd[L_HOOK] && sr.trim[L_HOOK] == SD_TRIM[at] && sr.tone[L_HOOK] == SD_TONE[at], "PRESET: not applied / calibrated");
          CHECK(trk[L_HOOK - 1].p[P_LEVEL] == siren_level(L_HOOK), "PRESET: the hook's level"); }
        /* the mixer */
        siren_cmd(SC_MIX, SM_LVOL + L_HOOK, -20, 0); siren_cmd(SC_MIX, SM_LFLT + L_ARP, 40, 0); siren_cmd(SC_MIX, SM_FILT, -30, 0);
        siren_cmd(SC_MIX, SM_SPACE, 0, 0); steps(2);
        CHECK(trk[L_HOOK - 1].p[P_LEVEL] == sb_clamp(SIREN_MIX[L_HOOK - 1][0] + sr.trim[L_HOOK] - 20, 1, 127), "MIX: the hook's volume");
        CHECK(trk[L_ARP - 1].p[P_TFLT] == 40, "MIX: the arp's high-pass (%d)", trk[L_ARP - 1].p[P_TFLT]);
        CHECK(song.g[G_FILT] == -30, "MIX: the master's low-pass (%d)", song.g[G_FILT]);
        for (i = 0; i < 6u; i++) CHECK(trk[i].p[P_REV] == 0 && trk[i].p[P_DLY] == 0, "MIX: SPACE 0, a send left on part %u", i);
        bars(2);
        siren_cmd(SC_MIX, SM_LVOL + L_HOOK, 0, 0); siren_cmd(SC_MIX, SM_LFLT + L_ARP, 0, 0); siren_cmd(SC_MIX, SM_FILT, 0, 0);
        siren_cmd(SC_MIX, SM_SPACE, 100, 0); steps(2);
        CHECK(trk[L_ARP - 1].p[P_TFLT] <= 0 && song.g[G_FILT] >= 0, "MIX: not back");
        /* NEXT TRACK: on the phrase (at most 8 bars); twice: at once */
        t0 = sb.track_no; siren_cmd(SC_NEXT, 0, 0, 0);
        for (i = 0; i < 9 * 16 && sb.track_no == t0; i++) steps(1);
        CHECK(sb.track_no == t0 + 1 && sb.phrase_bar == 0, "NEXT: track %u (was %u), phrase bar %u", sb.track_no, t0, sb.phrase_bar);
        bars(3);
        t0 = sb.track_no; siren_cmd(SC_NEXT, 0, 0, 0); siren_cmd(SC_NEXT, 0, 0, 0); steps(2);
        CHECK(sb.track_no == t0 + 1, "NEXT twice: not at once");
        /* NEW BASS, NEW GROOVE; then a long run: the arrangement's rule throughout */
        siren_cmd(SC_REMOTIF, L_BASS, 1, 0); siren_cmd(SC_REMOTIF, L_DRUMS, 1, 0);
        siren_cmd(SC_MIX, SM_FOCUS, L_PAD, 0);
        bars(160);
        CHECK(worst <= 3, "the arrangement: %u melodic layers at once", worst);
        CHECK(peak_n < 64, "the master clips (%u samples at full scale)", peak_n);
        printf("  %u sixteenths, at most %u melodic layers at once, peak %.2f\n", steps_seen, worst, peak);
        if (wf) { wav_hdr(wf, wn); fclose(wf); wf = 0; }
    }
    printf(fails ? "hands: %d FAILED\n" : "hands: ok\n", fails);
    return fails != 0;
}

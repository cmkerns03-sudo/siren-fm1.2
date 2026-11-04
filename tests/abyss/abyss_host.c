/* SPDX-License-Identifier: GPL-3.0-only */
/* ABYSS on the host: the firmware's own audio path (Clipper's engines, voices, drums, the reverb) played by ABYSS's
 * brain (abyss.c), rendered to a WAV, with what a set needs checked: every world plays, nothing clips into the limiter
 * for long, the voices hold, no NaN; the cost of a block. One line a section.
 *   build/host/abyss_host WORLD SEED SECONDS OUT.wav [GESTURE_AT_S GESTURE]   (WORLD 0..2) */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/abyss.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { sound_fade_begin(t); host_preset_req(t, e, p); }
static const char *LN[AB_NLAYERS] = {"DRUMS", "DRONE", "PAD", "LOW", "MELO", "BELL", "SWELL", "SUB"};
int main(int argc, char **argv)
{
    int world = argc > 1 ? atoi(argv[1]) : 0, secs = argc > 3 ? atoi(argv[3]) : 60, gat = argc > 6 ? atoi(argv[5]) : -1, gg = argc > 6 ? atoi(argv[6]) : 0;
    uint32_t seed = argc > 2 ? ab_uncode(argv[2]) : 12345u, blocks = secs ? (uint32_t)secs * FS / CTL : 3600u * FS / CTL, written = 0, end_at = 0, b, i, maxv = 0, sec_last = 99, piece_last = 99;
    FILE *f = fopen(argc > 4 ? argv[4] : "abyss.wav", "wb");
    static int16_t o[2 * CTL]; int32_t out[2 * CTL];
    double e = 0, pk = 0, sns = 0, worst_sec = 0; uint32_t sb_n = 0, cnt = 0, hot = 0, nan = 0; uint64_t t0, ns = 0, worst = 0;
    char code[9];
    double idle = 1e9, hy = 1e9;
    {   /* (the FM-1's load, estimated: one HYBRID voice is ~2.5 % of it; the host's time for one, and for none) */
        uint32_t r, k;
        for (r = 0; r < 2; r++) for (k = 0; k < 2; k++) {
            uint32_t bb; uint64_t s0, tot = 0;
            memset(trk, 0, sizeof trk); host_tracks_init(); sr.gain_q12 = 4096;
            if (k) { host_preset(&trk[1], ENGI_HYBRID, 0); trk[1].p[P_AMODE] = 0; trk_note_on1(&trk[1], 57, 90); }
            for (bb = 0; bb < 3000; bb++) { s0 = now_ns(); mix_block_svc(out, CTL); if (bb > 200) tot += now_ns() - s0; }
            if (k) { if (tot / 2800.0 < hy) hy = tot / 2800.0; } else if (tot / 2800.0 < idle) idle = tot / 2800.0;
        }
        hy -= idle;
    }
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    siren_init();
    siren_cmd(AC_SEED, world, 0, seed);
    siren_cmd(AC_PLAY, 0, 0, 0);
    song.playing = 1;
    wav_hdr(f, blocks * CTL);
    ab_code(seed, code);
    printf("abyss: %s %s\n", AB_WORLD_NAMES[world], code);
    for (b = 0; b < blocks; b++) {
        if (gat >= 0 && b == (uint32_t)gat * FS / CTL) siren_cmd(AC_GESTURE, gg, 0, 0);
        if (b % 20 == 0) siren_service();
        t0 = now_ns();
        mix_block_svc(out, CTL);
        t0 = now_ns() - t0; ns += t0; if (t0 > worst) worst = t0; sns += t0; sb_n++;
        for (i = 0; i < 2 * CTL; i++) {
            double x = out[i] / 32768.0;
            if (x != x) nan++;
            o[i] = (int16_t)out[i]; e += x * x; cnt++; if (fabs(x) > pk) pk = fabs(x); if (fabs(x) > 0.97) hot++;
        }
        fwrite(o, 2, 2 * CTL, f); written++;
        if (!secs && !end_at && ab.piece_no > 0) end_at = b + 6u * FS / CTL;   /* (0 seconds: the first piece, and 6 s of the next) */
        if (end_at && b >= end_at) break;
        { uint32_t v = busy_now(); if (v > maxv) maxv = v; }
        if (sr.ev.sec != sec_last || (b + 1) == blocks) {
            if (cnt && sec_last < AB_NSECS) {
                double est = (sns / 1000.0 / sb_n - idle / 1000.0) / (hy / 1000.0) * 2.5;
                if (est > worst_sec) worst_sec = est;
                printf("  %5.0fs  %-6s rms %.3f (%5.1f dB) peak %.2f  voices<=%u  ~%.0f %%\n", (double)b * CTL / FS, AB_SEC_NAMES[sec_last], sqrt(e / cnt), 20 * log10(sqrt(e / cnt) + 1e-9), pk, maxv, est);
            }
            e = 0; pk = 0; cnt = 0; maxv = 0; sec_last = sr.ev.sec; sns = 0; sb_n = 0;
            if (ab.piece_no != piece_last) {
                piece_last = ab.piece_no;
                printf(" piece %u: %s, key %d %s, %d BPM, kind %d\n  sounds:", ab.piece_no, AB_WORLD_NAMES[ab.world], ab.key, AB_MODE_NAMES[ab.mode], ab.bpm, ab.kind);
                for (i = AL_DRONE; i <= AL_SWELL; i++) printf(" %s=%s/%s", LN[i], sr.sound_eng[i] >= 0 ? ENGINES[sr.sound_eng[i]]->name : "-", sr.sound_eng[i] >= 0 ? ENGINES[sr.sound_eng[i]]->presets[sr.sound_pre[i]].name : "-");
                printf("\n");
            }
        }
    }
    fseek(f, 0, SEEK_SET); wav_hdr(f, written * CTL);
    fclose(f);
    blocks = written;
    printf("abyss: %.1f us a block (mean), worst %.1f us; the voices ~%.0f %% of the FM-1 (a HYBRID voice 2.5 %%); hot samples %u, NaN %u\n",
           ns / 1000.0 / blocks, worst / 1000.0, (ns / 1000.0 / blocks - idle / 1000.0) / (hy / 1000.0) * 2.5, hot, nan);
    printf("abyss: the busiest section ~%.0f %% of the FM-1\n", worst_sec);
    return nan != 0;
}

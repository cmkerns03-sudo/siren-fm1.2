/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN on the host: the firmware's own audio path (Clipper's engines, voices, drums, mix) played by SIREN's brain
 * (siren.c), rendered to a WAV, with what a set needs checked: every style plays (each layer heard where the brain
 * asks for it), nothing clips into the limiter for long, the voice budget holds, no NaN, and the cost of a block.
 *   build/host/siren_host STYLE SEED SECONDS OUT.wav [DIVE_AT_S]   (STYLE 0..7) */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }

int main(int argc, char **argv)
{
    int style = argc > 1 ? atoi(argv[1]) : 1, secs = argc > 3 ? atoi(argv[3]) : 60, dive = argc > 5 ? atoi(argv[5]) : -1;
    uint32_t seed = argc > 2 ? sb_uncode(argv[2]) : 12345u, blocks = (uint32_t)secs * FS / CTL, b, i, maxv = 0, sec_last = 99;
    FILE *f = fopen(argc > 4 ? argv[4] : "siren.wav", "wb");
    static int16_t o[2 * CTL]; int32_t out[2 * CTL];
    double e = 0, pk = 0; uint32_t cnt = 0, hot = 0; uint64_t t0, ns = 0, worst = 0;
    char code[9];
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    siren_init();
    siren_cmd(SC_STYLELOCK, 1, 0, 0);
    siren_cmd(SC_SEED, style, 0, seed);
    siren_cmd(SC_PLAY, 0, 0, 0);
    song.playing = 1;
    wav_hdr(f, blocks * CTL);
    sb_code(seed, code);
    printf("siren: %s %s\n", SB_STYLE_NAMES[style], code);
    for (b = 0; b < blocks; b++) {
        if (dive >= 0 && b == (uint32_t)dive * FS / CTL) siren_cmd(SC_DIVE, 4, 0, 0);
        if (b % 300 == 0) siren_service();                       /* (the main loop, every ~200 ms here) */
        t0 = now_ns();
        mix_block_svc(out, CTL);
        t0 = now_ns() - t0; ns += t0; if (t0 > worst) worst = t0;
        for (i = 0; i < 2 * CTL; i++) {
            double x = out[i] / 32768.0;
            if (x != x) { printf("siren: NaN\n"); return 1; }
            o[i] = (int16_t)out[i]; e += x * x; cnt++; if (fabs(x) > pk) pk = fabs(x); if (fabs(x) > 0.97) hot++;
        }
        fwrite(o, 2, 2 * CTL, f);
        { uint32_t v = busy_now(); if (v > maxv) maxv = v; }
        if (sr.ev.sec != sec_last || (b + 1) == blocks) {
            if (cnt) printf("  %4.0fs %-6s rms %.3f peak %.2f  voices<=%u\n", (double)b * CTL / FS, SB_SEC_NAMES[sec_last < 7 ? sec_last : 0], sqrt(e / cnt), pk, maxv);
            e = 0; pk = 0; cnt = 0; maxv = 0; sec_last = sr.ev.sec;
            printf("  sounds:"); for (i = L_BASS; i <= L_FX; i++) printf(" %s/%s", sr.sound_eng[i] >= 0 ? ENGINES[sr.sound_eng[i]]->name : "-", sr.sound_eng[i] >= 0 ? ENGINES[sr.sound_eng[i]]->presets[sr.sound_pre[i]].name : "-"); printf(" kit %s\n", dsu.k[0].name);
        }
    }
    fclose(f);
    printf("siren: %.1f us a block (mean), worst %.1f us; hot samples %u\n", ns / 1000.0 / blocks, worst / 1000.0, hot);
    return 0;
}

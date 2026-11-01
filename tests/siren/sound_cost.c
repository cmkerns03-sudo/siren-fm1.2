/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN: what each sound of the selection costs on the host, played as its layer plays it (bass 8ths, arp 16ths, a
 * pad's chords held, stabs, a long FX note), against one HYBRID voice (~2.5 % of the FM-1). One line a sound:
 *   ENGINE:PRESET  LAYER  mean_us  worst_us  x_hybrid  est_device_%      (build/host/siren_cost [filter]) */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }

static double run(int e, int pre, int layer, double *worst, uint32_t *vmax)
{
    track_t *t = &trk[1];
    int32_t out[2 * CTL]; uint32_t b, blocks = 6u * FS / CTL, s16 = FS * 60 / 140 / 4 / CTL;   /* blocks a 16th */
    uint64_t t0, ns = 0, w = 0; uint32_t k;
    static const uint8_t CH[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {55, 59, 62, 65}, {52, 55, 59, 62}};
    memset(trk, 0, sizeof trk); host_tracks_init(); song.g[G_BPM] = 140;
    if (e >= 0) { host_preset(t, (uint32_t)e, (uint32_t)pre); t->p[P_AMODE] = 0; }
    *vmax = 0;
    for (b = 0; b < blocks; b++) {
        uint32_t st = b / s16, at = b % s16 == 0;
        if (e >= 0 && at) {
            switch (layer) {
            case L_BASS: if (st % 2 == 0) { trk_note_off1(t, 33 + (st / 2 % 4 == 3 ? 12 : 0)); trk_note_on1(t, 33 + (st / 2 % 4 == 2 ? 12 : 0), 100); } else trk_note_off1(t, 33), trk_note_off1(t, 45); break;
            case L_ARP: trk_note_off1(t, 60 + (st + 3) % 4 * 3); trk_note_on1(t, 60 + st % 4 * 3, 100); break;
            case L_HOOK: if (st % 2 == 0) { trk_note_off1(t, 72 + (st / 2 + 3) % 4 * 2); trk_note_on1(t, 72 + st / 2 % 4 * 2, 100); } break;
            case L_PAD: if (st % 32 == 0) { for (k = 0; k < 4; k++) trk_note_off1(t, CH[(st / 32 + 3) % 4][k]); for (k = 0; k < 4; k++) trk_note_on1(t, CH[st / 32 % 4][k], 90); } break;
            case L_STAB: if (st % 4 == 2) for (k = 0; k < 4; k++) trk_note_on1(t, CH[st / 16 % 4][k], 100); if (st % 4 == 3) for (k = 0; k < 4; k++) trk_note_off1(t, CH[st / 16 % 4][k]); break;
            default: if (st % 64 == 0) { trk_note_off1(t, 60); trk_note_on1(t, 60, 100); } if (st % 64 == 48) trk_note_off1(t, 60); break;
            }
        }
        t0 = now_ns(); mix_block_svc(out, CTL); t0 = now_ns() - t0;
        if (b > 100) { ns += t0; if (t0 > w) w = t0; }
        { uint32_t v = busy_now(); if (v > *vmax) *vmax = v; }
    }
    *worst = w / 1000.0;
    return ns / 1000.0 / (blocks - 101);
}
static const char *LN[] = {"DRUMS", "BASS", "ARP", "HOOK", "PAD", "STAB", "FX"};
int main(int argc, char **argv)
{
    double w, idle = 1e9, hy = 1e9, m; uint32_t vm, s, L, r, st;
    const char *filt = argc > 1 ? argv[1] : 0;
    for (r = 0; r < 3; r++) { m = run(-1, 0, 0, &w, &vm); if (m < idle) idle = m; }
    for (r = 0; r < 3; r++) { m = run(ENGI_HYBRID, 0, L_FX, &w, &vm); if (m < hy) hy = m; }
    hy -= idle;
    printf("# idle %.2f us/block, one HYBRID voice %.2f us/block (= 2.5 %% of the FM-1)\n", idle, hy);
    for (s = 0; s < SD_NSND; s++) {
        const sd_snd_t *S = &SD_SND[s]; int16_t pe, e = siren_find(S, &pe); int lay = -1;
        for (st = 0; st < SB_NSTYLES && lay < 0; st++) for (L = L_BASS; L <= L_FX && lay < 0; L++) {
            const sd_span_t *sp = &SD_CUR[st][L]; uint32_t i;
            for (i = 0; i < sp->n; i++) if (SD_LIST[sp->first + i] == s) { lay = (int)L; break; }
        }
        if (e < 0 || lay < 0 || (filt && !strstr(S->eng, filt))) continue;
        { double best = 1e9, bw = 0; for (r = 0; r < 3; r++) { m = run(e, pe, lay, &w, &vm); if (m < best) best = m, bw = w; }
          best -= idle;
          printf("%-8s %-14s %-5s %7.2f %8.1f  x%5.2f  %5.1f%%  v%u\n", S->eng, S->pre, LN[lay], best, bw, best / hy, best / hy * 2.5, vm); }
    }
    return 0;
}

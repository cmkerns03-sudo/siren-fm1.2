/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.2: the DJ's effects (FX + a white key, siren.c SIREN_PX) through the firmware's audio path, every style,
 * every key: the set goes through it while held and comes back after (nothing stuck: the clock, the pitch, the EQ,
 * the sends, the mutes); the kills take their band out; CUT and the outs go quiet; the rolls keep playing; the CPU
 * guard thins the set. Writes OUT_DIR/dj-STYLE.wav (each key in turn, two bars each, a bar of the set between).
 *   build/host/siren_dj OUT_DIR [STYLE] */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { sound_fade_begin(t); host_preset_req(t, e, p); }

static FILE *wf; static uint32_t wn;
static double acc_e, acc_lo; static uint32_t acc_n; static int32_t lp;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static void blocks(uint32_t nb)
{
    static int32_t out[2 * CTL]; static int16_t o[2 * CTL]; uint32_t b, i;
    for (b = 0; b < nb; b++) {
        if (b % 40 == 0) siren_service();
        mix_block_svc(out, CTL);
        for (i = 0; i < 2 * CTL; i++) {
            double x = out[i] / 32768.0;
            if (x != x) { printf("NaN\n"); exit(1); }
            o[i] = (int16_t)out[i];
            if (!(i & 1)) { lp += (out[i] - lp) >> 6; acc_lo += (lp / 32768.0) * (lp / 32768.0); acc_e += x * x; acc_n++; }
        }
        if (wf) { fwrite(o, 2, 2 * CTL, wf); wn += CTL; }
    }
}
static void meas_reset(void) { acc_e = acc_lo = 0; acc_n = 0; }
static double rms(void) { return acc_n ? sqrt(acc_e / acc_n) : 0; }
static double rms_lo(void) { return acc_n ? sqrt(acc_lo / acc_n) : 0; }
static uint32_t bar_blocks(void) { return (uint32_t)((uint64_t)FS * 60u * 4u / sb.bpm / CTL); }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "."; int s0 = argc > 2 ? atoi(argv[2]) : 0, s1 = argc > 2 ? s0 + 1 : 8, st;
    for (st = s0; st < s1; st++) {
        char path[512]; uint32_t k, bb;
        memset(trk, 0, sizeof trk); host_tracks_init(); memset(&sr, 0, sizeof sr); siren_init();
        siren_cmd(SC_STYLELOCK, 1, 0, 0); siren_cmd(SC_SEED, st, 0, 0xD1D1u + (uint32_t)st); siren_cmd(SC_KNOB, K_ENERGY, 800, 0);
        siren_cmd(SC_PLAY, 0, 0, 0); song.playing = 1;
        snprintf(path, sizeof path, "%s/dj-%s.wav", dir, SB_STYLE_NAMES[st]);
        wf = fopen(path, "wb"); wn = 0; wav_hdr(wf, 0);
        bb = bar_blocks();
        blocks(bb * 20);                                     /* into the set */
        printf("dj: %s (%u BPM)\n", SB_STYLE_NAMES[st], sb.bpm);
        for (k = 0; k < 16; k++) {
            const siren_px_t *P = &SIREN_PX[st][k]; double before, before_lo, during, during_lo, after; uint32_t i;
            double at;
            meas_reset(); blocks(bb); before = rms(); before_lo = rms_lo();
            at = (double)wn / FS;
            siren_cmd(SC_PUNCH, (int32_t)k, 0, 0);
            blocks(bb); meas_reset(); blocks(bb); during = rms(); during_lo = rms_lo();
            siren_cmd(SC_PUNCH, -1, 0, 0);
            blocks(bb / 2); meas_reset(); blocks(bb); after = rms();
            printf("  %2u %-10s %3u:%02u  before %.3f (low %.3f)  held %.3f (low %.3f)  after %.3f\n", k + 1, P->name, (unsigned)at / 60u, (unsigned)at % 60u, before, before_lo, during, during_lo, after);
            /* nothing left behind */
            CHECK(sr.px == SPX_NONE && sr.spd_q12 == 4096 && sr.bend == 0 && sr.gate_q12 == 4096 && !sr.m_layers && !sr.m_dall, "%s: state left", P->name);
            CHECK(sr.eqk[0] == 12 && sr.eqk[1] == 12 && sr.eqk[2] == 12 && song.g[G_DUST] == 0 && song.g[G_BPM] == sb.bpm, "%s: master left", P->name);
            for (i = 0; i < 6; i++) CHECK(trk[i].p[P_DLY] == SIREN_MIX[i][2], "%s: send left", P->name);
            CHECK(after > 0.02, "%s: the set did not come back (%.3f)", P->name, after);
            if (P->fx == SPX_CUT || P->fx == SPX_BRAKE || P->fx == SPX_REWIND)
                CHECK(during < 0.02, "%s: not silent in its second bar (%.3f)", P->name, during);
            if (P->fx == SPX_ECHO || P->fx == SPX_WASH)       /* (the tail goes on: quieter, or quiet) */
                CHECK(during < before * 0.8 || during < 0.08, "%s: not quieter (%.3f vs %.3f)", P->name, during, before);
            if (P->fx == SPX_KLOW) CHECK(during_lo < before_lo * 0.5, "KILL LOW: the low end stayed (%.3f vs %.3f)", during_lo, before_lo);
            if (P->fx == SPX_ROLL || P->fx == SPX_PROLL || P->fx == SPX_DROLL || P->fx == SPX_GATE || P->fx == SPX_HALF)
                CHECK(during > 0.02 || before < 0.1, "%s: silent while held", P->name);
            blocks(bb);
        }
        {   /* the CPU guard: told the FM-1 is full, the set thins (the FX layer out), and comes back */
            uint32_t i, fxv = 0;
            sr.cpu_test = 230; blocks(bb * 4);
            CHECK(sr.lean == 2, "guard: not lean (%u)", sr.lean);
            for (i = 0; i < NVOICE; i++) fxv += trk[L_FX - 1].v[i].active && trk[L_FX - 1].v[i].gate;
            CHECK(fxv == 0, "guard: the FX layer still plays");
            sr.cpu_test = 0; blocks(bb * 20);
            CHECK(sr.lean == 0, "guard: still lean after the load went (%u)", sr.lean);
        }
        fseek(wf, 0, SEEK_SET); wav_hdr(wf, wn); fclose(wf); wf = 0;
    }
    printf(fails ? "dj: %d FAILED\n" : "dj: all good\n", fails);
    return fails != 0;
}

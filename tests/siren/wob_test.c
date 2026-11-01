/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN's WOB (firmware/src/eng_wob.c), CLIPPER_SYNTH 1: each type, a held note under a 2 Hz wobble written into
 * wob_lfo[] as siren.c does, then: bounded, finite, the pitch right, the wobble heard (the energy above 600 Hz swings
 * with the wobble, and more than with the wobble still); every preset under random EDIT values bounded.
 *   build/host/wob_test [DEMODIR]   (DEMODIR: each type playing a riff, WAV) */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"         /* (a SIREN build: its hooks in the mix) */
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
#include <math.h>
static int bad;
static void check(const char *what, int ok) { printf("wob: %-90s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }
static void setup(uint32_t pi)
{
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], ENGI_WOB, pi);
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
    trk[0].p[P_LD_PIT] = trk[0].p[P_LD_FLT] = trk[0].p[P_LD_SHP] = trk[0].p[P_LD_AMP] = 0;
}
#define SECS 2
static float y[FS * SECS];
/* render a held note, the wobble at hz (0: still), into y[] */
static int32_t run(uint32_t note, double hz, int32_t *peak)
{
    static int32_t b[CTL];
    uint32_t i, k, n = FS * SECS / CTL; int32_t pk = 0;
    trk_note_on(&trk[0], note, 110);
    for (i = 0; i < n; i++) {
        double ph = hz * i * CTL / FS;
        wob_lfo[0] = hz > 0 ? (int32_t)(16383.5 - 16383.5 * cos(2 * M_PI * ph)) : 19660;
        memset(b, 0, sizeof b);
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL; k++) { y[i * CTL + k] = (float)b[k]; if (abs(b[k]) > pk) pk = abs(b[k]); }
    }
    trk_note_off(&trk[0], note);
    *peak = pk; return 0;
}
static double swing_db(void)   /* the energy above ~600 Hz, in 25 ms windows: max / min */
{
    double lp = 0, mx = 0, mn = 1e30; uint32_t i, w = FS / 40, a = FS / 4;
    for (i = a; i + w <= FS * SECS; i += w) {
        double e = 0; uint32_t k;
        for (k = i; k < i + w; k++) { lp += 0.08 * (y[k] - lp); e += (y[k] - lp) * (y[k] - lp); }
        e = sqrt(e / w); if (e > mx) mx = e; if (e < mn) mn = e;
    }
    return 20 * log10((mx + 1) / (mn + 1));
}
static double pitch(void)      /* the autocorrelation's strongest peak between 30 and 120 Hz, over the second half */
{
    uint32_t a = FS * SECS / 2, n = FS / 2, lag, best = 0; double mx = -1e30;
    for (lag = FS / 120; lag <= FS / 30; lag++) {
        double e = 0; uint32_t i;
        for (i = a; i < a + n; i++) e += (double)y[i] * y[i + lag - (i + lag >= FS * SECS ? lag : 0)];
        if (e > mx) { mx = e; best = lag; }
    }
    return (double)FS / best;
}
int main(int argc, char **argv)
{
    uint32_t pi, np = ENGINES[ENGI_WOB]->npresets;
    char w[200]; int32_t pk;
    for (pi = 0; pi < np; pi++) {
        double sw, still, f;
        setup(pi); run(33, 0, &pk); still = swing_db();
        setup(pi); run(33, 2.0, &pk); sw = swing_db(); f = pitch();
        snprintf(w, sizeof w, "%-8s bounded (peak %d), a 2 Hz wobble swings %.1f dB (still: %.1f), pitch %.1f Hz (55)", ENGINES[ENGI_WOB]->presets[pi].name, pk, sw, still, f);
        check(w, pk > 2000 && pk < 1 << 20 && sw > still + 3 && ((f > 50 && f < 60) || pi == 4));   /* (TEAR: FM 3:2, its fundamental an octave down) */
    }
    /* random EDIT values: bounded, finite */
    {
        uint32_t s = 1, k, it; int ok = 1;
        for (it = 0; it < 200 && ok; it++) {
            setup(it % np);
            for (k = 0; k < 8; k++) { s = s * 1103515245u + 12345u; trk[0].p[P_E0 + k] = (int16_t)(k == 0 ? (s >> 16) % 6 : (s >> 16) % 128); }
            run(24 + it % 48, 0.5 + (it % 13), &pk);
            ok = pk < 1 << 20;
        }
        check("200 random patches, notes and wobble rates: bounded", ok);
    }
    if (argc > 1) {                                   /* the demos: each type, a held note, the wobble at 1/8 then 1/16 triplets (140 BPM) */
        for (pi = 0; pi < np; pi++) {
            FILE *f; char path[300]; static int16_t o[FS * 8]; static int32_t b[CTL]; uint32_t i, k, n = FS * 4 / CTL;
            setup(pi); trk_note_on(&trk[0], 33, 115);
            for (i = 0; i < n; i++) {
                double t = (double)i * CTL / FS, hz = t < 2 ? 140.0 / 60 * 2 : 140.0 / 60 * 6;
                wob_lfo[0] = (int32_t)(16383.5 - 16383.5 * cos(2 * M_PI * hz * t));
                memset(b, 0, sizeof b); track_render(&trk[0], b, CTL);
                for (k = 0; k < CTL; k++) o[2 * (i * CTL + k)] = o[2 * (i * CTL + k) + 1] = (int16_t)clamp(b[k] / 2, -32767, 32767);
            }
            snprintf(path, sizeof path, "%s/wob_%s.wav", argv[1], ENGINES[ENGI_WOB]->presets[pi].name);
            f = fopen(path, "wb"); if (f) { wav_hdr(f, FS * 4); fwrite(o, 4, FS * 4, f); fclose(f); }
        }
    }
    printf("wob: %d failed\n", bad);
    return bad;
}

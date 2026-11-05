/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.9: the pad at half the rate (voice.c abyss_half_part, fx.c abyss_up), as ABYSS's slow layers (tests/abyss/
 * half_test.c). Each pad sound of every style, played as level_cal.c plays it (chords, 8 s), three times: at the full rate
 * twice (what its own randomness moves: free phases, drift, noise) and at half the rate. Its loudness and its third-octave
 * bands from 63 Hz to 6.4 kHz (those over 1 % of it) must stay within that randomness plus 1.5 dB (the loudness) and 4 dB
 * (a band); what
 * the engines left at the full rate (voice.c: GRAIN, SAMPLE, VOICE; HYBRID GLASS PAD) shown as such. One line a sound. */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
#define NS (7u * FS)
#define NF 4096u
#define NB 21
static float buf[3][NS];
static void play(int e, int pre, float *o)
{
    track_t *t = &trk[L_PAD - 1]; int32_t out[2 * CTL]; uint32_t b, blocks = 8u * FS / CTL, s16 = FS * 60 / 128 / 4 / CTL, k, w = 0;
    static const uint8_t CH[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {55, 59, 62, 65}, {52, 55, 59, 62}};
    memset(trk, 0, sizeof trk); host_tracks_init(); song.g[G_BPM] = 128; memset(hr_hist, 0, sizeof hr_hist);
    sr.gain_q12 = 1024; sr.gate_q12 = 4096; sr.spd_q12 = 4096; sr.eqk[0] = sr.eqk[1] = sr.eqk[2] = 12; sr.duck = 0;
    host_preset(t, (uint32_t)e, (uint32_t)pre); t->p[P_AMODE] = 0;
    t->p[P_LEVEL] = 100; t->p[P_PAN] = 0; t->p[P_DLY] = 0; t->p[P_REV] = 0; t->p[P_CHOR] = 0; t->p[P_TFLT] = 0;
    for (b = 0; b < blocks; b++) {
        uint32_t st = b / s16, i;
        if (b % s16 == 0 && st % 32 == 0) {
            for (k = 0; k < 4; k++) trk_note_off1(t, CH[(st / 32 + 3) % 4][k] - 12);
            for (k = 0; k < 3; k++) trk_note_on1(t, CH[st / 32 % 4][k] - 12, 85);
        }
        mix_block_svc(out, CTL);
        if (b >= FS / CTL) for (i = 0; i < CTL && w < NS; i++) o[w++] = (float)((out[2 * i] + out[2 * i + 1]) / 65536.0);
    }
}
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j, k, m;
    for (i = 1, j = 0; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2 * M_PI / m;
        for (k = 0; k < n; k += m) for (j = 0; j < m / 2; j++) {
            double wr = cos(a * j), wi = sin(a * j), xr = re[k + j + m / 2] * wr - im[k + j + m / 2] * wi, xi = re[k + j + m / 2] * wi + im[k + j + m / 2] * wr;
            re[k + j + m / 2] = re[k + j] - xr; im[k + j + m / 2] = im[k + j] - xi; re[k + j] += xr; im[k + j] += xi;
        }
    }
}
static double hi8[3];
static int hix;
static double bands(const float *x, double *B)     /* the mean power; B: the third-octave bands */
{
    static double re[NF], im[NF];
    uint32_t s, i, k; double e = 0;
    double all = 1e-20, hi = 0;
    for (k = 0; k < NB; k++) B[k] = 1e-20;
    for (i = 0; i < NS; i++) e += (double)x[i] * x[i];
    for (s = 0; s + NF <= NS; s += NF) {
        for (i = 0; i < NF; i++) { re[i] = x[s + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NF)); im[i] = 0; }
        fft(re, im, NF);
        for (i = 1; i < NF / 2; i++) {
            double f = (double)i * FS / NF, p = re[i] * re[i] + im[i] * im[i];
            int b = (int)floor(3 * log2(f / 63.0) + 1e-9);
            if (b >= 0 && b < NB) B[b] += p;
            all += p; if (f > 8000) hi += p;
        }
    }
    hi8[hix % 3] = hi / all * 100;
    return e / NS + 1e-20;
}
int main(void)
{
    uint32_t s, st; int bad = 0, n = 0, h;
    for (s = 0; s < SD_NSND; s++) {
        const sd_snd_t *S = &SD_SND[s]; int16_t pe, e = siren_find(S, &pe); int in = 0;
        double E[3], B[3][NB], tot = 0, nat, natb = 0, dl, db = 0; int ran, ok; uint32_t k;
        for (st = 0; st < SB_NSTYLES; st++) { const sd_span_t *sp = &SD_CUR[st][L_PAD]; for (k = 0; k < sp->n; k++) in |= SD_LIST[sp->first + k] == s; }
        if (!in || e < 0) continue;
        for (h = 0; h < 3; h++) { abyss_half_off = (uint8_t)(h < 2); play(e, pe, buf[h]); hix = h; E[h] = bands(buf[h], B[h]); }
        ran = hr_on[L_PAD - 1];
        for (k = 0; k < NB; k++) tot += B[0][k];
        nat = fabs(10 * log10(E[1] / E[0]));
        dl = fabs(10 * log10(E[2] / E[0]));
        for (k = 0; k < NB; k++) if (B[0][k] > tot * 0.01) {
            double a = fabs(10 * log10(B[1][k] / B[0][k])), c = fabs(10 * log10(B[2][k] / B[0][k]));
            natb = a > natb ? a : natb; db = c > db ? c : db;
        }
        ok = !ran || (dl <= 1.5 + nat && db <= 4 + natb);
        printf("pad half: %-8s %-13s %s  loudness %4.1f dB (itself %3.1f)  bands %4.1f dB (itself %3.1f)  over 8 kHz %4.1f -> %4.1f %%%s\n",
               S->eng, S->pre, ran ? "half" : "full", dl, nat, db, natb, hi8[0], hi8[2], ok ? "" : "  FAIL");
        bad += !ok; n++;
    }
    abyss_half_off = 0;
    printf("pad half: %d sounds, %d failed\n", n, bad);
    return bad != 0;
}

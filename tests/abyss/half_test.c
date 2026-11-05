/* SPDX-License-Identifier: GPL-3.0-only */
/* ABYSS 0.1.5: the slow layers at half the rate (voice.c abyss_half_part, fx.c abyss_up). Each sound of the drone, the pad,
 * the low and the swell, played as level_cal.c plays it (8 s), three times: at the full rate twice (what its own
 * randomness moves: free phases, drift, noise) and at half the rate. Its loudness and its third-octave bands from 63 Hz to
 * 6.4 kHz (those over 1 % of it) must stay within that randomness plus 1.5 dB (the loudness) and 4 dB (a band); what
 * the engines left at the full rate (voice.c: GRAIN, SAMPLE, VOICE; HYBRID GLASS PAD) shown as such. One line a sound. */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/abyss.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
static const char *LN[] = {"DRUMS", "DRONE", "PAD", "LOW", "MELO", "BELL", "SWELL"};
#define NS (7u * FS)
#define NF 4096u
#define NB 21
static float buf[3][NS];
static void play(int e, int pre, int layer, int world, float *o)
{
    track_t *t = &trk[layer - 1]; int32_t out[2 * CTL]; uint32_t b, blocks = 8u * FS / CTL, s16 = FS * 60 / 70 / 4 / CTL, k, w = 0;
    static const uint8_t CH[2][4] = {{57, 60, 64, 69}, {53, 57, 60, 65}};
    memset(trk, 0, sizeof trk); host_tracks_init(); song.g[G_BPM] = 70;
    memset(ens, 0, sizeof ens); memset(hr_hist, 0, sizeof hr_hist);
    sr.gain_q12 = 1024; ab.world = (uint8_t)world; ab.kind = 0;
    host_preset(t, (uint32_t)e, (uint32_t)pre); t->p[P_AMODE] = 0;
    abyss_env((uint32_t)layer);
    t->p[P_LEVEL] = 100; t->p[P_PAN] = 0; t->p[P_DLY] = 0; t->p[P_REV] = 0; t->p[P_CHOR] = 0; t->p[P_TFLT] = 0;
    for (b = 0; b < blocks; b++) {
        uint32_t st = b / s16, at = b % s16 == 0, i;
        if (at) switch (layer) {
            case AL_DRONE: if (st == 0) { trk_note_on1(t, 45, 90); trk_note_on1(t, 52, 80); } break;
            case AL_PAD: case AL_SWELL: if (st % 32 == 0) { for (k = 0; k < 4; k++) trk_note_off1(t, CH[(st / 32 + 1) % 2][k]); for (k = 0; k < 4; k++) trk_note_on1(t, CH[st / 32 % 2][k], 85); } break;
            default: if (st % 16 == 0) { trk_note_off1(t, 45 - (st / 16 + 1) % 2 * 5); trk_note_on1(t, 45 - st / 16 % 2 * 5, 95); } break;
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
static double bands(const float *x, double *B)     /* the mean power; B: the third-octave bands */
{
    static double re[NF], im[NF];
    uint32_t s, i, k; double e = 0;
    for (k = 0; k < NB; k++) B[k] = 1e-20;
    for (i = 0; i < NS; i++) e += (double)x[i] * x[i];
    for (s = 0; s + NF <= NS; s += NF) {
        for (i = 0; i < NF; i++) { re[i] = x[s + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NF)); im[i] = 0; }
        fft(re, im, NF);
        for (i = 1; i < NF / 2; i++) {
            double f = (double)i * FS / NF, p = re[i] * re[i] + im[i] * im[i];
            int b = (int)floor(3 * log2(f / 63.0) + 1e-9);
            if (b >= 0 && b < NB) B[b] += p;
        }
    }
    return e / NS + 1e-20;
}
int main(void)
{
    uint32_t w, L, i; int bad = 0, n = 0, h;
    siren_init();
    for (w = 0; w < AB_NWORLDS; w++) for (L = AL_DRONE; L <= AL_SWELL; L++) {
        const sd_span_t *sp = &SD_CUR[w][L];
        if (L == AL_MELO || L == AL_BELL) continue;
        for (i = 0; i < sp->n; i++) {
            uint32_t at = sp->first + i, k; const sd_snd_t *S = &SD_SND[SD_LIST[at]]; int16_t pe, e = siren_find(S, &pe);
            double E[3], B[3][NB], tot = 0, nat = 0, natb = 0, dl, db = 0; int ran, ok;
            if (e < 0) continue;
            for (h = 0; h < 3; h++) { abyss_half_off = (uint8_t)(h < 2); play(e, pe, (int)L, (int)w, buf[h]); E[h] = bands(buf[h], B[h]); }
            ran = hr_on[L - 1];                       /* (the last block: at half the rate or not) */
            for (k = 0; k < NB; k++) tot += B[0][k];
            nat = fabs(10 * log10(E[1] / E[0]));
            dl = fabs(10 * log10(E[2] / E[0]));
            for (k = 0; k < NB; k++) if (B[0][k] > tot * 0.01) {
                double a = fabs(10 * log10(B[1][k] / B[0][k])), c = fabs(10 * log10(B[2][k] / B[0][k]));
                natb = a > natb ? a : natb; db = c > db ? c : db;
            }
            ok = !ran || (dl <= 1.5 + nat && db <= 4 + natb);
            printf("half: %-9s %-5s %-8s %-13s %s  loudness %4.1f dB (itself %3.1f)  bands %4.1f dB (itself %3.1f)%s\n",
                   AB_WORLD_NAMES[w], LN[L], S->eng, S->pre, ran ? "half" : "full", dl, nat, db, natb, ok ? "" : "  FAIL");
            bad += !ok; n++;
        }
    }
    abyss_half_off = 0;
    printf("half: %d sounds, %d failed\n", n, bad);
    return bad != 0;
}

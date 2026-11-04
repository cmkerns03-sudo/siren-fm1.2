/* SPDX-License-Identifier: GPL-3.0-only */
/* ABYSS: every sound of the FM-1 measured for ambient, through the firmware's audio path (no sends): a minor chord held
 * (A2 E3 C4) eight seconds then let go, four seconds of tail. Per sound: its cost on the host (the voices' render, x one
 * HYBRID voice ~2.5 % of the FM-1), the attack (s to half its peak), the sustain (the last two held seconds against the
 * loudest), the tail (after the release, -20 dB at), the brightness (spectral centroid, Hz), the noise (flatness 0..1),
 * the loudness (dB). One line a sound, tab separated:
 *   build/host/abyss_scan > tools/abyss/scan.tsv */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
#define NF 2048
static double re_[NF], im_[NF];
static void fft(double *re, double *im, int n)
{
    int i, j, k, m;
    for (i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (m = 2; m <= n; m <<= 1) { double a = -2 * M_PI / m; for (k = 0; k < n; k += m) for (j = 0; j < m / 2; j++) {
        double wr = cos(a * j), wi = sin(a * j), xr = re[k + j + m / 2] * wr - im[k + j + m / 2] * wi, xi = re[k + j + m / 2] * wi + im[k + j + m / 2] * wr;
        re[k + j + m / 2] = re[k + j] - xr; im[k + j + m / 2] = im[k + j] - xi; re[k + j] += xr; im[k + j] += xi; } }
}
static float buf[12 * FS];
static double cost(int e, int pre, int chord)
{
    track_t *t = &trk[1]; int32_t out[2 * CTL]; uint32_t b, blocks = 12u * FS / CTL, n = 0; uint64_t ns = 0, t0;
    static const uint8_t CH[3] = {45, 52, 60};
    memset(trk, 0, sizeof trk); host_tracks_init(); song.g[G_BPM] = 70;
    sr.gain_q12 = 1024; sr.gate_q12 = 4096; sr.spd_q12 = 4096; sr.eqk[0] = sr.eqk[1] = sr.eqk[2] = 12; sr.duck = 0;
    if (e >= 0) { host_preset(t, (uint32_t)e, (uint32_t)pre); t->p[P_AMODE] = 0; t->p[P_DLY] = 0; t->p[P_REV] = 0; t->p[P_CHOR] = 0; t->p[P_PAN] = 0; }
    for (b = 0; b < blocks; b++) {
        uint32_t k, i;
        if (e >= 0 && b == 2) for (k = 0; k < (uint32_t)chord; k++) trk_note_on1(t, CH[k], 90);
        if (e >= 0 && b == 8u * FS / CTL) for (k = 0; k < 3u; k++) trk_note_off1(t, CH[k]);
        t0 = now_ns(); mix_block_svc(out, CTL); t0 = now_ns() - t0;
        if (b > 100 && b < 8u * FS / CTL) { ns += t0; n++; }
        for (i = 0; i < CTL; i++) buf[b * CTL + i] = (float)((out[2 * i] + out[2 * i + 1]) / 65536.0);
    }
    return ns / 1000.0 / n;
}
static double rms(uint32_t a, uint32_t z) { double s = 0; uint32_t i; for (i = a; i < z; i++) s += buf[i] * buf[i]; return sqrt(s / (z - a) + 1e-20); }
int main(int argc, char **argv)
{
    double idle = 1e9, hy = 1e9, m; uint32_t e, p, r;
    const char *only = argc > 1 ? argv[1] : 0;
    for (r = 0; r < 3; r++) { m = cost(-1, 0, 0); if (m < idle) idle = m; }
    for (r = 0; r < 3; r++) { m = cost(ENGI_HYBRID, 0, 1); if (m < hy) hy = m; }
    hy -= idle;
    printf("engine\tpreset\tx_hybrid3\tattack_s\tsustain\ttail_s\tcentroid\tflat\tdb\n");
    for (e = 0; e < NENGINES; e++) for (p = 0; p < ENGINES[e]->npresets; p++) {
        double c = 1e9, pk = 0, att = -1, sus, tail = -1, cen = 0, fl = 0, held; uint32_t w, win = FS / 20, i, nw = 0;
        if (only && !strstr(ENGINES[e]->name, only)) continue;
        for (r = 0; r < 2; r++) { m = cost((int)e, (int)p, 3); if (m < c) c = m; }
        c -= idle;
        for (w = 0; w + win < 8u * FS; w += win) { double v = rms(w, w + win); if (v > pk) pk = v; }
        for (w = 0; w + win < 8u * FS; w += win) if (rms(w, w + win) >= pk / 2) { att = (double)w / FS; break; }
        held = rms(6u * FS, 8u * FS); sus = held / (pk + 1e-12);
        for (w = 8u * FS; w + win < 12u * FS; w += win) if (rms(w, w + win) < held / 10) { tail = (double)(w - 8u * FS) / FS; break; }
        if (tail < 0) tail = 4.0;
        for (w = FS * 2; w + NF <= 8u * FS; w += NF * 4) {   /* centroid and flatness over the held chord */
            double num = 0, den = 0, lg = 0, ar = 0; uint32_t k;
            for (i = 0; i < NF; i++) { re_[i] = buf[w + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NF)); im_[i] = 0; }
            fft(re_, im_, NF);
            for (k = 2; k < NF / 2; k++) { double a = re_[k] * re_[k] + im_[k] * im_[k] + 1e-18, f = (double)k * FS / NF; num += f * a; den += a; lg += log(a); ar += a; }
            cen += num / den; fl += exp(lg / (NF / 2 - 2)) / (ar / (NF / 2 - 2)); nw++;
        }
        printf("%s\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%.0f\t%.3f\t%.1f\n", ENGINES[e]->name, ENGINES[e]->presets[p].name, c / hy, att, sus, tail,
               cen / nw, fl / nw, 20 * log10(rms(2u * FS, 8u * FS) + 1e-9));
        fflush(stdout);
    }
    return 0;
}

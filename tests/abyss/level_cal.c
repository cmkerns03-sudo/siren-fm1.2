/* SPDX-License-Identifier: GPL-3.0-only */
/* ABYSS: each sound's calibration in its layer (tools/abyss/levels.json -> tools/gen_abyss.py SD_TRIM / SD_TONE). Each
 * sound played as its layer plays it, with its world's envelopes (a drone held on root and fifth, a pad's chords
 * changing every two bars, the low's bass notes, a slow melody, the bells' sparse notes, the swell's wide chord), through
 * ABYSS's mix level for its layer and the firmware's audio path (no sends). Its loudness as the ear weighs it (the highs
 * over ~2.8 kHz counted four times); too bright for its layer: the track's low-pass closed step by step; then the trim
 * (P_LEVEL steps, 2 a dB) to the layer's loudness. Output JSON {"LAYER|ENGINE:PRESET": {"trim", "tone", "db", "hf"}}.
 *   abyss_levels > tools/abyss/levels.json        (abyss_levels check: each sound's loudness after) */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/abyss.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
static const char *LN[] = {"DRUMS", "DRONE", "PAD", "LOW", "MELO", "BELL", "SWELL"};
static const double TARGET[7] = {0, -24.0, -21.0, -21.0, -19.0, -24.0, -20.0};
static const double HFMAX[7] = {0, 1.5, 2.5, 3.0, 3.0, 15.0, 8.0};
static void meas(int e, int pre, int layer, int world, int tone, int level, double *db, double *hf)
{
    track_t *t = &trk[layer - 1]; int32_t out[2 * CTL]; uint32_t b, blocks = 10u * FS / CTL, s16 = FS * 60 / 70 / 4 / CTL, k;
    static const uint8_t CH[2][4] = {{57, 60, 64, 69}, {53, 57, 60, 65}};
    double e2 = 0, h2 = 0; int32_t lp = 0; uint32_t n = 0;
    memset(trk, 0, sizeof trk); host_tracks_init(); song.g[G_BPM] = 70;
    sr.gain_q12 = 1024; ab.world = (uint8_t)world; ab.kind = 0;
    host_preset(t, (uint32_t)e, (uint32_t)pre); t->p[P_AMODE] = 0;
    abyss_env((uint32_t)layer);
    t->p[P_LEVEL] = (int16_t)level; t->p[P_PAN] = 0; t->p[P_DLY] = 0; t->p[P_REV] = 0; t->p[P_CHOR] = 0; t->p[P_TFLT] = (int16_t)tone;
    for (b = 0; b < blocks; b++) {
        uint32_t st = b / s16, at = b % s16 == 0, i;
        if (at) switch (layer) {
            case AL_DRONE: if (st == 0) { trk_note_on1(t, 45, 90); trk_note_on1(t, 52, 80); } break;
            case AL_PAD: case AL_SWELL: if (st % 32 == 0) { for (k = 0; k < 4; k++) trk_note_off1(t, CH[(st / 32 + 1) % 2][k]); for (k = 0; k < 4; k++) trk_note_on1(t, CH[st / 32 % 2][k], 85); } break;
            case AL_LOW: if (st % 16 == 0) { trk_note_off1(t, 45 - (st / 16 + 1) % 2 * 5); trk_note_on1(t, 45 - st / 16 % 2 * 5, 95); } break;
            case AL_MELO: if (st % 8 == 0) { trk_note_off1(t, 69 + (st / 8 + 3) % 4 * 2); trk_note_on1(t, 69 + st / 8 % 4 * 2, 95); } break;
            default: if (st % 4 == 0) { trk_note_off1(t, 72 + (st / 4 + 2) % 3 * 3); trk_note_on1(t, 72 + st / 4 % 3 * 3, 70); } break;
        }
        mix_block_svc(out, CTL);
        if (b > 3u * FS / CTL) for (i = 0; i < CTL; i++) {
            int32_t x = (out[2 * i] + out[2 * i + 1]) / 2; double v = x / 32768.0, h;
            lp += (x - lp) * 26 / 64; h = (x - lp) / 32768.0;
            e2 += v * v; h2 += h * h; n++;
        }
    }
    *db = 10 * log10((e2 + 3 * h2) / n + 1e-12) + 12;
    *hf = h2 / (e2 + 1e-12) * 100;
}
int main(int argc, char **argv)
{
    static const int TONES[] = {0, -6, -10, -14, -18, -22, -26, -30};
    int check = argc > 1 && !strcmp(argv[1], "check"), first = 1, bad = 0;
    uint32_t w, L, i;
    setvbuf(stdout, 0, _IONBF, 0);
    siren_init();
    if (!check) printf("{\n");
    for (w = 0; w < AB_NWORLDS; w++) for (L = AL_DRONE; L <= AL_SWELL; L++) {
        const sd_span_t *sp = &SD_CUR[w][L];
        for (i = 0; i < sp->n; i++) {
            uint32_t at = sp->first + i; const sd_snd_t *S = &SD_SND[SD_LIST[at]]; int16_t pe, e = siren_find(S, &pe);
            int tone = 0, trim, k; double db, hf, d0, h0;
            if (e < 0) { printf("  MISSING %s:%s\n", S->eng, S->pre); bad++; continue; }
            if (check) {
                meas(e, pe, (int)L, (int)w, SD_TONE[at], ABYSS_MIX[L - 1][0] + SD_TRIM[at], &db, &hf);
                printf("%-9s %-5s %-8s %-12s trim %4d tone %4d  %6.1f dB  hf %4.1f %%\n", AB_WORLD_NAMES[w], LN[L], S->eng, S->pre, SD_TRIM[at], SD_TONE[at], db, hf);
                if (fabs(db - TARGET[L]) > (L == AL_BELL || L == AL_LOW || L == AL_MELO ? 3.6 : 2.5)) { printf("  FAIL: %.1f dB, its layer's %.1f\n", db, TARGET[L]); bad++; }
                continue;
            }
            meas(e, pe, (int)L, (int)w, 0, ABYSS_MIX[L - 1][0], &d0, &h0);
            db = d0; hf = h0;
            for (k = 1; k < (int)(sizeof TONES / sizeof *TONES) && hf > HFMAX[L]; k++) { tone = TONES[k]; meas(e, pe, (int)L, (int)w, tone, ABYSS_MIX[L - 1][0], &db, &hf); }
            trim = (int)lround((TARGET[L] - db) * 2);
            for (k = 0; k < 3; k++) {
                double d;
                trim = trim < -30 ? -30 : trim > 20 ? 20 : trim;
                meas(e, pe, (int)L, (int)w, tone, ABYSS_MIX[L - 1][0] + trim, &d, &hf);
                if (fabs(TARGET[L] - d) < 0.4) break;
                trim += (int)lround((TARGET[L] - d) * 2);
            }
            trim = trim < -30 ? -30 : trim > 20 ? 20 : trim;
            printf("%s  \"%s|%s|%s:%s\": {\"trim\": %d, \"tone\": %d, \"db\": %.1f, \"hf\": %.1f}", first ? "" : ",\n", AB_WORLD_NAMES[w], LN[L], S->eng, S->pre, trim, tone, d0, h0);
            first = 0;
        }
    }
    if (check) printf(bad ? "levels: %d FAILED\n" : "levels: ok\n", bad); else printf("\n}\n");
    return bad != 0;
}

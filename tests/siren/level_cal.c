/* SIREN 0.5: each curated sound's calibration in its layer (tools/siren/levels.json, gen_siren.py SD_TRIM / SD_TONE).
 * Each sound played as its layer plays it (the bass in eighths, the arp in sixteenths, the hook a melody, the pad a
 * chord, the stabs off the beat, an FX hit), through SIREN's mix level for that layer and the firmware's audio path,
 * the master 12 dB down (under the limiter). Its loudness as the ear weighs it: the energy, its highs (over ~2.8 kHz)
 * counted four times (the ear's 2-5 kHz). Too bright for its layer: the track's low-pass (P_TFLT) closed step by step
 * until its highs are back in line (the "tone"); then the trim (P_LEVEL steps, 2 a dB) that brings it to the layer's
 * loudness. Output: JSON {"LAYER|ENGINE:PRESET": {"trim": t, "tone": f, "db": before, "hf": share}, ...}
 *   level_cal > tools/siren/levels.json      (level_cal check: each sound's loudness after, its spread per layer) */
#define main hostsim_main
#include "tests/hostsim.c"
#undef main
#include "firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
static const char *LN[] = {"DRUMS", "BASS", "ARP", "HOOK", "PAD", "STAB", "FX"};
/* each layer's loudness (dB, weighted) and the most of its energy in the highs (%) */
static const double TARGET[7] = {0, -11.0, -15.5, -14.0, -20.0, -15.5, -20.0};   /* (the layers' medians as they were, the arp and the stabs 3-4 dB under: the hook on top) */
static const double HFMAX[7] = {0, 1.5, 3.0, 3.0, 2.0, 3.0, 6.0};
static void meas(int e, int pre, int layer, int tone, int level, double *db, double *hf, double *pk)
{
    track_t *t = &trk[layer - 1]; int32_t out[2 * CTL]; uint32_t b, blocks = 6u * FS / CTL, s16 = FS * 60 / 128 / 4 / CTL, k;
    static const uint8_t CH[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {55, 59, 62, 65}, {52, 55, 59, 62}};
    double e2 = 0, h2 = 0; int32_t lp = 0; uint32_t n = 0; *pk = 0;
    memset(trk, 0, sizeof trk); host_tracks_init(); song.g[G_BPM] = 128;
    sr.gain_q12 = 1024; sr.gate_q12 = 4096; sr.spd_q12 = 4096; sr.eqk[0] = sr.eqk[1] = sr.eqk[2] = 12; sr.duck = 0;
    host_preset(t, (uint32_t)e, (uint32_t)pre); t->p[P_AMODE] = 0;
    t->p[P_LEVEL] = (int16_t)level; t->p[P_PAN] = 0; t->p[P_DLY] = 0; t->p[P_REV] = 0; t->p[P_CHOR] = 0; t->p[P_TFLT] = (int16_t)tone;
    for (b = 0; b < blocks; b++) {
        uint32_t st = b / s16, at = b % s16 == 0, i;
        if (at) switch (layer) {
            case L_BASS: if (st % 2 == 0) { trk_note_off1(t, 33); trk_note_on1(t, 33, 100); } else trk_note_off1(t, 33); break;
            case L_ARP: trk_note_off1(t, 60 + (st + 3) % 4 * 3); trk_note_on1(t, 60 + st % 4 * 3, 100); break;
            case L_HOOK: if (st % 2 == 0) { trk_note_off1(t, 60 + (st / 2 + 3) % 4 * 2); trk_note_on1(t, 60 + st / 2 % 4 * 2, 100); } break;
            case L_PAD: if (st % 32 == 0) { for (k = 0; k < 4; k++) trk_note_off1(t, CH[(st / 32 + 3) % 4][k] - 12); for (k = 0; k < 3; k++) trk_note_on1(t, CH[st / 32 % 4][k] - 12, 85); } break;
            case L_STAB: if (st % 4 == 2) for (k = 0; k < 3; k++) trk_note_on1(t, CH[st / 16 % 4][k], 100); if (st % 4 == 3) for (k = 0; k < 3; k++) trk_note_off1(t, CH[st / 16 % 4][k]); break;
            default: if (st % 16 == 0) { trk_note_off1(t, 60); trk_note_on1(t, 60, 110); } if (st % 16 == 12) trk_note_off1(t, 60); break;
        }
        mix_block_svc(out, CTL);
        if (b > 200) for (i = 0; i < CTL; i++) {
            int32_t x = (out[2 * i] + out[2 * i + 1]) / 2; double v = x / 32768.0, h;
            lp += (x - lp) * 26 / 64; h = (x - lp) / 32768.0;
            e2 += v * v; h2 += h * h; n++; if (fabs(v) > *pk) *pk = fabs(v);
        }
    }
    *db = 10 * log10((e2 + 3 * h2) / n + 1e-12) + 12;     /* (the master's 12 dB back) */
    *hf = h2 / (e2 + 1e-12) * 100;
    *pk *= 4;
}
int main(int argc, char **argv)
{
    static const int TONES[] = {0, -6, -10, -14, -18, -22, -26, -30, -34};
    int check = argc > 1 && !strcmp(argv[1], "check"), first = 1, bad = 0;
    uint32_t s, L, st;
    double sum[7] = {0}, sq[7] = {0}, lo[7], hi[7]; int cnt[7] = {0};
    for (L = 0; L < 7; L++) lo[L] = 1e9, hi[L] = -1e9;
    setvbuf(stdout, 0, _IONBF, 0);
    if (!check) printf("{\n");
    for (s = 0; s < SD_NSND; s++) {
        const sd_snd_t *S = &SD_SND[s]; int16_t pe, e = siren_find(S, &pe);
        for (L = L_BASS; L <= L_FX; L++) {
            int at = -1, tone = 0, trim = 0, k; double db, hf, pk, d0, h0;
            for (st = 0; st < SB_NSTYLES && at < 0; st++) { const sd_span_t *sp = &SD_CUR[st][L]; uint32_t i; for (i = 0; i < sp->n; i++) if (SD_LIST[sp->first + i] == s) at = (int)(sp->first + i); }
            if (at < 0 || e < 0) continue;
            if (check) {                                          /* the sound as SIREN now plays it */
                meas(e, pe, (int)L, SD_TONE[at], SIREN_MIX[L - 1][0] + SD_TRIM[at], &db, &hf, &pk);
                printf("%-5s %-8s %-12s trim %4d tone %4d  %6.1f dB  hf %4.1f %%  peak %.2f\n", LN[L], S->eng, S->pre, SD_TRIM[at], SD_TONE[at], db, hf, pk);
                if (fabs(db - TARGET[L]) > 2.0) { printf("  FAIL: %s %s:%s %.1f dB, its layer's %.1f\n", LN[L], S->eng, S->pre, db, TARGET[L]); bad++; }
                sum[L] += db; sq[L] += db * db; cnt[L]++; if (db < lo[L]) lo[L] = db; if (db > hi[L]) hi[L] = db;
                continue;
            }
            meas(e, pe, (int)L, 0, SIREN_MIX[L - 1][0], &d0, &h0, &pk);
            db = d0; hf = h0;
            for (k = 1; k < (int)(sizeof TONES / sizeof *TONES) && hf > HFMAX[L]; k++) {   /* too bright: the low-pass, closing */
                tone = TONES[k];
                meas(e, pe, (int)L, tone, SIREN_MIX[L - 1][0], &db, &hf, &pk);
            }
            trim = (int)lround((TARGET[L] - db) * 2);
            for (k = 0; k < 3; k++) {                             /* (the level is not always a dB a step: a drive in the */
                double d;                                         /* sound, its own compressor; measured again) */
                trim = trim < -24 ? -24 : trim > 14 ? 14 : trim;
                meas(e, pe, (int)L, tone, SIREN_MIX[L - 1][0] + trim, &d, &hf, &pk);
                if (fabs(TARGET[L] - d) < 0.4) break;
                trim += (int)lround((TARGET[L] - d) * 2);
            }
            trim = trim < -24 ? -24 : trim > 14 ? 14 : trim;
            printf("%s  \"%s|%s:%s\": {\"trim\": %d, \"tone\": %d, \"db\": %.1f, \"hf\": %.1f}", first ? "" : ",\n", LN[L], S->eng, S->pre, trim, tone, d0, h0);
            first = 0;
        }
    }
    if (check) {
        for (L = L_BASS; L <= L_FX; L++)
            if (cnt[L]) printf("%-5s %2d sounds  mean %6.1f dB  spread %4.1f dB  (%.1f .. %.1f)  target %.1f\n", LN[L], cnt[L], sum[L] / cnt[L],
                               sqrt(sq[L] / cnt[L] - (sum[L] / cnt[L]) * (sum[L] / cnt[L])), lo[L], hi[L], TARGET[L]);
        printf(bad ? "levels: %d FAILED\n" : "levels: ok\n", bad);
    } else
        printf("\n}\n");
    return bad != 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* ABYSS's brain alone: N pieces of each world, their plans, their notes per layer (counts, ranges, overlaps), the
 * events in order with "dump" (the site's brain, compiled to WebAssembly from the same file, gives the same).
 *   build/host/abyss_brain [WORLD SEED PIECES [dump]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/src/abyss_brain.c"
static const char *LN[AB_NLAYERS] = {"DRUMS", "DRONE", "PAD", "LOW", "MELO", "BELL", "SWELL", "SUB"};
int main(int argc, char **argv)
{
    static ab_brain_t b; static ab_events_t ev; static ab_palette_t pal;
    int w0 = argc > 1 ? atoi(argv[1]) : 0, w1 = argc > 1 ? w0 + 1 : AB_NWORLDS, pieces = argc > 3 ? atoi(argv[3]) : 3, dump = argc > 4, w, L, fails = 0;
    uint32_t seed = argc > 2 ? ab_uncode(argv[2]) : 0x1234567u;
    for (w = 0; w < AB_NWORLDS; w++) for (L = 0; L < AB_NLAYERS; L++) pal.ncur[w][L] = 4, pal.nkits[w] = 2;
    for (w = w0; w < w1; w++) {
        long cnt[AB_NLAYERS] = {0}; int lo[AB_NLAYERS], hi[AB_NLAYERS], maxn = 0, frissons = 0; uint32_t steps = 0, p0;
        for (L = 0; L < AB_NLAYERS; L++) lo[L] = 127, hi[L] = 0;
        ab_init(&b, &pal); ab_set_seed(&b, seed, w);
        printf("== %s\n", AB_WORLD_NAMES[w]);
        p0 = b.piece_no;
        { uint32_t shown = 0xFFFFFFFFu;
        while (b.piece_no < p0 + (uint32_t)pieces) {
            int i;
            if (b.piece_no != shown) { shown = b.piece_no;
                printf("piece %u: key %d %s %d BPM, chords every %d bars (%d), kind %d, %.1f min:", b.piece_no, b.key, AB_MODE_NAMES[b.mode], b.bpm, b.chord_bars, b.nprog, b.kind, 0.0);
                { int tot = 0; for (i = 0; i < b.plan_n; i++) { printf(" %s %d", AB_SEC_NAMES[b.plan_sec[i]], b.plan_len[i]); tot += b.plan_len[i]; }
                  printf("  = %d bars, %.1f min\n", tot, tot * 4 * 60.0 / b.bpm / 60.0); }
            }
            ab_step16(&b, &ev); steps++;
            if (ev.nnotes > maxn) maxn = ev.nnotes;
            frissons += ev.frisson;
            for (i = 0; i < ev.nnotes; i++) {
                ab_note_t *x = &ev.notes[i];
                cnt[x->layer]++; if (x->note < lo[x->layer]) lo[x->layer] = x->note; if (x->note > hi[x->layer]) hi[x->layer] = x->note;
                if (dump) printf("%u.%u %s %u %u %u\n", ev.bar, ev.step, LN[x->layer], x->note, x->vel, x->len);
            }
            if (steps > 16u * 4000u) { printf("FAIL: no end\n"); fails++; break; }
        } }
        for (L = 0; L < AB_NLAYERS; L++) printf("  %-6s %6ld notes  %3d..%3d\n", LN[L], cnt[L], cnt[L] ? lo[L] : 0, hi[L]);
        printf("  at most %d notes a step, %d frisson moments, %u bars\n", maxn, frissons, steps / 16);
        if (maxn >= AB_MAX_NOTES) { printf("FAIL: the notes overflow\n"); fails++; }
    }
    printf(fails ? "brain: %d FAILED\n" : "brain: ok\n", fails);
    return fails != 0;
}

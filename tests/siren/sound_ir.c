/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.9.1: one sound played once as its layer plays it (sound_cost.c's run), for an exact count of the
 * instructions under valgrind --tool=callgrind (tools/siren/cost_ir.py): the host's clock is too noisy to compare
 * sounds within a few percent.   sound_ir SOUND LAYER   (SOUND -1: nothing, -2: one HYBRID voice) */
#define SOUND_COST_NO_MAIN 1
#include "sound_cost.c"
int main(int argc, char **argv)
{
    int s = argc > 2 ? atoi(argv[1]) : -1, lay = argc > 2 ? atoi(argv[2]) : 0; double w; uint32_t vm;
    if (s == -1) run(-1, 0, 0, &w, &vm);
    else if (s == -2) run(ENGI_HYBRID, 0, L_FX, &w, &vm);
    else { int16_t pe, e = siren_find(&SD_SND[s], &pe); if (e < 0) return 1; run(e, pe, lay, &w, &vm); }
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/src/siren_brain.c"
#include "pal.h"
static const char *LN[] = {"DRUMS", "BASS", "ARP", "HOOK", "PAD", "STAB", "FX", "SUB"};
int main(int argc, char **argv)
{
    static sb_brain_t b; sb_events_t ev; int style = atoi(argv[1]); uint32_t seed = (uint32_t)strtoul(argv[2], 0, 10); int steps = atoi(argv[3]), i, L, k;
    sb_init(&b, &PAL); b.style_lock = (uint8_t)atoi(argv[4]); sb_set_seed(&b, seed, style);
    for (i = 0; i < steps; i++) {
        /* the hands: the same as the JS side (brain_js.js) */
        if (i % 5003 == 777) b.dive = (uint8_t)(i % 2 ? 4 : 16);
        if (i % 7919 == 1234) b.break_req = 1;
        if (i % 3001 == 2000) sb_knob(&b, (i / 3001) % 4, (int)((i * 37u) % 1001));
        if (i % 9001 == 4500) sb_reroll(&b, 1 + (i / 9001) % 6);
        if (i % 6007 == 3000) sb_lock(&b, (i / 6007) % 7, (i / 6007) % 2);
        sb_step16(&b, &ev);
        printf("%d %s %u e%d b%d k%d c%d", i, SB_SEC_NAMES[ev.sec], b.style, ev.e, ev.bpm, ev.key, ev.chord);
        for (k = 0; k < ev.nnotes; k++) printf(" %s:%d:%d:%d", LN[ev.notes[k].layer], ev.notes[k].note, ev.notes[k].vel, ev.notes[k].len);
        for (L = 0; L < SB_NLAYERS; L++) if (ev.cut[L] >= 0) printf(" c%s=%d", LN[L], ev.cut[L]);
        if (ev.has_wob) printf(" w%d/%d/%d/%d", ev.wob.rate3, ev.wob.shape, ev.wob.depth, ev.wob.growl);
        printf(" m%d/%d/%d/%d", ev.hp, ev.duck, ev.gain, ev.verb);
        if (b.sounds_changed) { printf(" S"); for (L = 0; L < 7; L++) printf("%d,", b.snd[L]); printf("k%d,%d", b.kick_n16, b.kick2_n16); b.sounds_changed = 0; }
        printf("\n");
    }
    return 0;
}

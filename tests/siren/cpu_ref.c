/* the host's time for one HYBRID voice (README: ~2.5 % of the FM-1's CPU), to read SIREN's host cost against */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
int main(void)
{
    int32_t out[2 * CTL]; uint32_t b; uint64_t t0, ns = 0, idle = 0;
    memset(trk, 0, sizeof trk); host_tracks_init();
    for (b = 0; b < 20000; b++) { t0 = now_ns(); mix_block_svc(out, CTL); idle += now_ns() - t0; }
    host_preset(&trk[0], ENGI_HYBRID, 0); trk_note_on1(&trk[0], 60, 100);
    for (b = 0; b < 20000; b++) { t0 = now_ns(); mix_block_svc(out, CTL); ns += now_ns() - t0; }
    printf("idle %.2f us/block, one HYBRID voice %.2f us/block (voice alone %.2f)\n", idle / 1000.0 / 20000, ns / 1000.0 / 20000, (ns - idle) / 1000.0 / 20000);
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN's screen on the host: the firmware's audio path playing (siren_host's), the screen drawn as the FM-1 would
 * (siren_draw.c on gfx.c's canvas, the LCD a 240 x 240 framebuffer here), saved as PPM at moments of the set:
 *   build/host/screen_test STYLE SEED OUTDIR   -> OUTDIR/screen_<n>.ppm (sea at intro, build, drop; the info screen) */
#define main hostsim_main
#include "../hostsim.c"
#undef main
#include "../../firmware/src/siren.c"
static void siren_load_sound(track_t *t, uint32_t e, uint32_t p) { host_preset_req(t, e, p); }
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h; j++) for (i = 0; i < w; i++) if (x + i < 240 && y + j < 240) { uint16_t c = p[j * w + i]; fb[(y + j) * 240 + x + i] = (uint16_t)((c >> 8) | (c << 8)); }
}
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/siren_draw.c"
static void save(const char *dir, int n)
{
    char p[300]; FILE *f; uint32_t i;
    snprintf(p, sizeof p, "%s/screen_%d.ppm", dir, n);
    f = fopen(p, "wb"); fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240 * 240; i++) { uint16_t c = fb[i]; uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
int main(int argc, char **argv)
{
    int style = atoi(argv[1]), shot = 0; uint32_t seed = sb_uncode(argv[2]), b, blocks = 40u * FS / CTL;
    int32_t out[2 * CTL]; uint32_t frame_every = FS / 60 / CTL;   /* (~60 frames a second) */
    memset(trk, 0, sizeof trk); host_tracks_init(); siren_init();
    siren_cmd(SC_STYLELOCK, 1, 0, 0); siren_cmd(SC_SEED, style, 0, seed); siren_cmd(SC_PLAY, 0, 0, 0); song.playing = 1;
    su.knob[0] = 500; su.knob[1] = 500; su.knob[2] = 500; su.knob[3] = 350;
    for (b = 0; b < blocks; b++) {
        if (b == 4u * FS / CTL) siren_cmd(SC_DIVE, 4, 0, 0);
        if (b % 300 == 0) siren_service();
        mix_block_svc(out, CTL);
        if (b % frame_every == 0) {
            siren_draw();
            if (b == 2u * FS / CTL / frame_every * frame_every || b == 10u * FS / CTL / frame_every * frame_every ||
                (sr.ev.sec == 3 && sr.ev.sec_bar == 0 && sr.ev.step == 1 && shot < 3) || b == 30u * FS / CTL / frame_every * frame_every) save(argv[3], shot++);
        }
    }
    su.info = 1; siren_draw(); save(argv[3], shot++);
    printf("screen: %d shots\n", shot);
    return 0;
}

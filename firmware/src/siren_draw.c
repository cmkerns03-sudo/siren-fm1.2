/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN's screen (siren_ui.c runs it): the sea (the kick's rings, the mix in four circles in the family's colours,
 * each note a spark on a spiral, the eye; a build pulls it in and spins it, a drop flashes it white), the style,
 * seed, section and tempo; or the info screen (the layers and their sounds, the knobs, the CPU). Drawn on gfx.c's
 * canvas in two bands of 120 rows. Its own file: the host tests draw it (tests/siren/screen_test.c). */
#define SU_BG 0x0021u
static const uint16_t SU_COL[8] = {0xF79Eu, 0x2BFFu, 0x1F1Au, 0xFB03u, 0x8AFFu, 0xFE23u, 0x1E6Eu, 0x2BFFu};   /* per layer */
static const uint16_t SU_RING[4] = {0x2BFFu, 0x1E6Eu, 0xFE23u, 0xFB03u};
static const uint8_t SU_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> note key bit */
static const char *const SU_LAYER[7] = {"DRUMS", "BASS", "ARP", "HOOK", "PAD", "STABS", "FX"};
static const char *const SU_STYLE[SB_NSTYLES] = {"TECHNO", "TRANCE", "PSY", "ACID", "RAVE", "HARD", "DRUM&BASS", "DUBSTEP"};
static struct {
    uint8_t info, style, lock_style, msg_t; char msg[20];
    int16_t knob[4];
    uint32_t fav[16]; uint8_t fav_set[16];
    uint8_t locks[7];
    uint32_t frame, kicks_seen, notes_seen, seq_seen, flash;
    struct { uint8_t age, e; } ring[6];
    struct { uint8_t age, layer, note; uint16_t a; } spark[24];
    uint8_t nspark;
} su;


/* ---- the screen, drawn in two bands of 120 rows (the canvas holds 124) */
static int32_t su_sin(uint32_t a) { return SINE[a & 1023u]; }   /* a: 1024 a turn, Q15 */
static void su_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t segs)
{
    uint32_t i;
    int32_t x0 = cx + r, y0 = cy;
    for (i = 1; i <= segs; i++) {
        uint32_t a = i * 1024u / segs;
        int32_t x = cx + ((r * su_sin(a + 256u)) >> 15), y = cy + ((r * su_sin(a)) >> 15);
        cv_line(x0, y0, x, y, c);
        x0 = x; y0 = y;
    }
}
static void su_text_c(int32_t y, int32_t band, const felucca_font_t *f, const char *s, uint16_t c)
{
    (void)band;                                  /* (cv_oy places it: every band draws all, the canvas keeps its rows) */
    cv_text((240 - text_w(f, s)) / 2, y, f, s, c);
}
static void su_sea(int32_t band)
{
    const sb_events_t *ev = &sr.ev;
    int32_t cx = 120, cy = 128, R = 78, e = ev->e, k, i;
    uint32_t spin = su.frame * (uint32_t)(4 + e / 90);
    int32_t build = ev->sec == 2 ? (int32_t)(ev->sec_bar * 1000 / (ev->sec_len ? ev->sec_len : 1)) : 0;
    R = R * (1000 - build * 35 / 100) / 1000;                 /* (a build pulls the sea in) */
    for (k = 0; k < 6; k++)                                   /* the kick's rings */
        if (su.ring[k].age) {
            int32_t a = su.ring[k].age, r = R * 4 / 10 + a * 3;
            uint16_t c = a < 12 ? 0xF79Eu : a < 24 ? 0x8C71u : 0x4208u;
            su_circle(cx, cy, r, c, 48);
        }
    for (k = 0; k < 4; k++) {                                 /* the mix, around four circles */
        uint32_t ph = k % 2 ? 1024u - (spin * (3 + k) / 4) % 1024u : (spin * (3 + k) / 4) % 1024u;
        int32_t base = R * (55 + 12 * k) / 100, amp = R * (35 + 25 * e / 1000) / 100;
        int32_t x0 = 0, y0 = 0;
        for (i = 0; i <= 64; i++) {
            uint32_t a = (uint32_t)i * 16u + ph;
            int32_t v = sr.scope[((uint32_t)i * 8u + sr.sw) % 512u], r = base + ((v * amp) >> 15);
            int32_t x = cx + ((r * su_sin(a + 256u)) >> 15), y = cy + ((r * su_sin(a)) >> 15);
            if (i) cv_line(x0, y0, x, y, SU_RING[k]);
            x0 = x; y0 = y;
        }
    }
    for (k = 0; k < su.nspark; k++) {                         /* the notes, sparks on a spiral */
        int32_t a = su.spark[k].age, r = R * 4 / 10 + a * 2;
        uint32_t ang = su.spark[k].a + (uint32_t)a * 12u + spin / 2u;
        int32_t x = cx + ((r * su_sin(ang + 256u)) >> 15), y = cy + ((r * su_sin(ang)) >> 15);
        uint16_t c = SU_COL[su.spark[k].layer % 8u];
        cv_rect(x - 1, y - 1, a < 20 ? 3 : 2, a < 20 ? 3 : 2, c);
    }
    {                                                         /* the eye */
        int32_t r = R * (12 + (int32_t)(su_sin(su.frame * 8u) * 4 >> 15)) / 100 + (su.ring[0].age && su.ring[0].age < 6 ? 3 : 0);
        for (i = -r; i <= r; i++) {
            int32_t h = r * r - i * i, w = 0;
            while ((w + 1) * (w + 1) <= h) w++;
            cv_rect(cx - w, cy + i, 2 * w + 1, 1, su.flash ? 0xFFFFu : 0xE77Fu);
        }
    }
    (void)band;
}
static void su_overlay(int32_t band)
{
    const sb_events_t *ev = &sr.ev;
    char l[40], code[9]; uint32_t n = 0, i;
    const char *st = SU_STYLE[sb.style % 8u];
    sb_code(sb.seed, code);
    for (i = 0; st[i]; i++) l[n++] = st[i];
    l[n++] = ' '; for (i = 0; code[i]; i++) l[n++] = code[i]; l[n] = 0;
    su_text_c(6, band, &FONT_S, l, 0xF79Eu);
    {
        char b[32]; uint32_t m = 0; const char *s = sr.playing ? SB_SEC_NAMES[ev->sec % 7u] : "PRESS PLAY";
        for (i = 0; s[i]; i++) b[m++] = s[i];
        if (sr.playing) { b[m++] = ' '; b[m++] = ' '; b[m++] = (char)('0' + ev->bpm / 100 % 10); b[m++] = (char)('0' + ev->bpm / 10 % 10); b[m++] = (char)('0' + ev->bpm % 10); b[m++] = ' '; b[m++] = 'B'; b[m++] = 'P'; b[m++] = 'M'; }
        b[m] = 0;
        su_text_c(218, band, &FONT_S, b, 0x1F1Au);
    }
    cv_rect(70, 236, (int32_t)(100 * ev->e / 1000), 2, 0xFB03u);   /* the energy */
    if (su.msg_t) su_text_c(44, band, &FONT_L, su.msg, 0xFE23u);
}
static void su_info(int32_t band)
{
    (void)band;
    uint32_t L;
    char l[48];
    for (L = 0; L < 7u; L++) {
        int32_t y = 30 + (int32_t)L * 18;
        uint32_t n = 0, i; const char *a = SU_LAYER[L], *s = "-";
        if (L >= L_BASS && sr.sound_eng[L] >= 0 && sr.applied[L] >= 0) s = ENGINES[sr.sound_eng[L]]->presets[sr.sound_pre[L]].name;
        else if (L == L_DRUMS) s = dsu.k[0].name;
        for (i = 0; a[i]; i++) l[n++] = a[i];
        while (n < 7u) l[n++] = ' ';
        for (i = 0; s[i] && n < 22u; i++) l[n++] = s[i];
        if (su.locks[L]) { l[n++] = ' '; l[n++] = '*'; }
        l[n] = 0;
        cv_text(12, y, &FONT_S, l, sr.mute[L] ? 0x4208u : SU_COL[L]);
    }
    {
        static const char *const KN[4] = {"ENERGY", "DENSITY", "TENSION", "MUTATION"};
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            int32_t y = 166 + (int32_t)k * 14;
            cv_text(12, y, &FONT_S, KN[k], 0x8C71u);
            cv_rect(110, y + 6, 110, 3, 0x2104u);
            cv_rect(110, y + 6, 110 * su.knob[k] / 1000, 3, 0x1F1Au);
        }
    }
    {
        char c[24]; uint32_t q = (uint32_t)song.cpu_q8 * 100u >> 8, m = 0;
        const char *p = "CPU ";
        for (; *p; p++) c[m++] = *p;
        c[m++] = (char)('0' + q / 100 % 10); c[m++] = (char)('0' + q / 10 % 10); c[m++] = (char)('0' + q % 10); c[m++] = '%'; c[m] = 0;
        cv_text(12, 224, &FONT_S, c, 0x8C71u);
    }
}
static void siren_draw(void)
{
    int32_t band;
    /* what happened since the last frame: kicks, notes, a drop */
    while (su.kicks_seen != sr.kicks) {
        uint32_t k;
        su.kicks_seen++;
        for (k = 5; k > 0; k--) su.ring[k] = su.ring[k - 1];
        su.ring[0].age = 1; su.ring[0].e = (uint8_t)(sr.ev.e / 4);
    }
    if (sr.notes_n - su.notes_seen > 16u) su.notes_seen = sr.notes_n - 16u;
    while (su.notes_seen != sr.notes_n) {
        uint32_t q = su.notes_seen % 16u, k;
        su.notes_seen++;
        if (sr.notes_l[q] == L_DRUMS) continue;
        if (su.nspark < 24u) su.nspark++;
        for (k = su.nspark - 1u; k > 0; k--) su.spark[k] = su.spark[k - 1];
        su.spark[0].age = 1; su.spark[0].layer = sr.notes_l[q]; su.spark[0].note = sr.notes_k[q]; su.spark[0].a = (uint16_t)(sr.notes_k[q] * 75u);
    }
    if (su.seq_seen != sr.seq) { su.seq_seen = sr.seq; if (sr.ev.sec == 3 && sr.ev.sec_bar == 0 && sr.ev.step == 0) su.flash = 6; }
    for (band = 0; band < 240; band += 120) {
        cv_begin(240, 120, su.flash ? 0x4A69u : SU_BG);
        cv_oy = -band;
        if (su.info) su_info(band); else { su_sea(band); su_overlay(band); }
        cv_oy = 0;
        cv_blit(0, (uint32_t)band);
    }
    /* time passes */
    {
        uint32_t k;
        for (k = 0; k < 6u; k++) if (su.ring[k].age && ++su.ring[k].age > 40u) su.ring[k].age = 0;
        for (k = 0; k < su.nspark; k++) if (++su.spark[k].age > 60u) { su.nspark = (uint8_t)k; break; }
        if (su.flash) su.flash--;
        if (su.msg_t) su.msg_t--;
        su.frame++;
    }
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (ABYSS) */
/* ABYSS's screen (abyss_ui.c runs it): SIREN's deck, for a slow music read from across a room:
 *
 *   CINEMATIC  [>        D DORIAN   64       the world, playing, the seed; the key and its mode; the tempo; the beat
 *   [dawn|theme|rise|swell|.|PEAK|fall|tail]  the piece's plan, coloured by section, the playhead; the section, the
 *   THEME          PEAK IN 42                 peak and when; the bar, the piece, the time
 *   | DRONE  DARK ENS          ######..       the seven layers: their sound, lock (@), off, LIVE, what they play
 *   ...
 *   LIGHT  SPACE  DENSITY  TIME               the four knobs as they act now (LFO: the layer's strip; GLO: TENSION..)
 *   footer                          CPU 31%   what was just done, what comes; the load
 *
 * Pages (0.1, as SIREN 0.8: a button opens one, it stays): FX the textures, SEL the world, ENV the gestures, LFO the
 * layers, SAVE the seeds, GLO the settings. Each part of the screen drawn again only when what it shows changes
 * (a signature), into gfx.c's canvas, blitted where it sits. The host draws it too (tests/abyss/pages_test.c). */
static const uint8_t SU_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> note key bit */
static const char *const SU_LAYER[7] = {"DRUMS", "DRONE", "PAD", "LOW", "MELODY", "BELLS", "SWELL"};
static const char *const SU_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const SU_GNAME[AB_NGESTURES] = {"RISE", "PEAK NOW", "CALM", "BREATH", "LIFT", "NEW THEME", "NEW HARMONY",
                                                   "HOLD", "NEXT PIECE", "VOID", "FADE OUT"};
enum { SP_HOME, SP_FX, SP_SEL, SP_ENV, SP_LFO, SP_SAVE, SP_GLO, SP_N };
#define SU_BG RGB(4, 6, 10)
#define SU_PANEL RGB(14, 18, 28)
#define SU_RULE RGB(32, 40, 56)
#define SU_TXT RGB(226, 232, 240)
#define SU_DIM RGB(110, 122, 140)
#define SU_FAINT RGB(52, 60, 76)
#define SU_ACC RGB(120, 200, 255)                /* ABYSS's: a cold light under water */
/* each section's colour: DAWN THEME RISE SWELL BREATH PEAK FALL TAIL PULSE ORBIT VOID DECAY */
static const uint16_t SU_SEC[AB_NSECS] = {RGB(60, 80, 130), RGB(0, 150, 160), RGB(70, 170, 220), RGB(140, 200, 255), RGB(40, 44, 56),
                                          RGB(255, 255, 255), RGB(110, 90, 200), RGB(60, 64, 90), RGB(0, 190, 140), RGB(255, 186, 0),
                                          RGB(20, 22, 30), RGB(150, 100, 60)};
static const uint16_t SU_LCOL[7] = {RGB(200, 200, 210), RGB(70, 110, 200), RGB(120, 200, 255), RGB(40, 150, 140), RGB(255, 210, 120),
                                    RGB(200, 160, 255), RGB(255, 255, 255)};
static struct {
    uint8_t info, world, msg_t; char msg[20];          /* (info: the page open, SP_*) */
    int16_t knob[4];                                   /* LIGHT DENSITY TIME TENSION, as the brain has them */
    uint32_t fav[16]; uint8_t fav_set[16];
    uint8_t locks[7];
    uint8_t focus, snd_t; int8_t vacc; int16_t space;
    int8_t lvol[7], lflt[7]; int16_t lspace[7];
    uint8_t shimmer, tape;
    int8_t oct, slot_k; uint8_t larp, slot_done, kl[27]; uint32_t down, kn[27], slot_t0;
    uint32_t frame, kicks_seen, notes_seen;
    uint8_t meter[7];
    uint32_t sig[24];
    uint32_t play_ms, page_was;
} su;

static const uint8_t SU_F5[64][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}   /*   */,
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}   /* ! */,
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}   /* " */,
    {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}   /* # */,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}   /* $ */,
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}   /* % */,
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}   /* & */,
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}   /* ' */,
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}   /* ( */,
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}   /* ) */,
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}   /* 42 */,
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}   /* + */,
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}   /* , */,
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}   /* - */,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}   /* . */,
    {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}   /* 47 */,
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}   /* 0 */,
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}   /* 1 */,
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}   /* 2 */,
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}   /* 3 */,
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}   /* 4 */,
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}   /* 5 */,
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}   /* 6 */,
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}   /* 7 */,
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}   /* 8 */,
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}   /* 9 */,
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}   /* : */,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}   /* ; */,
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}   /* < */,
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}   /* = */,
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}   /* > */,
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}   /* ? */,
    {0x0E, 0x11, 0x11, 0x1F, 0x1B, 0x1B, 0x1F}   /* @ */,
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}   /* A */,
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}   /* B */,
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}   /* C */,
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}   /* D */,
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}   /* E */,
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}   /* F */,
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}   /* G */,
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}   /* H */,
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}   /* I */,
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}   /* J */,
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}   /* K */,
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}   /* L */,
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}   /* M */,
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}   /* N */,
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}   /* O */,
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}   /* P */,
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}   /* Q */,
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}   /* R */,
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}   /* S */,
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}   /* T */,
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}   /* U */,
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}   /* V */,
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}   /* W */,
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}   /* X */,
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}   /* Y */,
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}   /* Z */,
    {0x10, 0x18, 0x1C, 0x1E, 0x1C, 0x18, 0x10}   /* [ */,
    {0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00}   /* 92 */,
    {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00}   /* ] */,
    {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x00}   /* ^ */,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}   /* _ */};

/* text in the 5 x 7 font at scale s (a cell 6 s wide), returns the end x */
static int32_t su_t(int32_t x, int32_t y, const char *s, uint16_t c, int32_t sc)
{
    for (; *s; s++) {
        uint32_t ch = (uint8_t)*s, r, k;
        if (ch >= 'a' && ch <= 'z') ch -= 32u;
        if (ch < 32u || ch > 95u) ch = '?';
        for (r = 0; r < 7u; r++) {
            uint32_t bits = SU_F5[ch - 32u][r];
            for (k = 0; k < 5u; k++)
                if ((bits >> (4u - k)) & 1u) {
                    if (sc == 1) cv_pset(x + (int32_t)k, y + (int32_t)r, c);
                    else cv_rect(x + (int32_t)k * sc, y + (int32_t)r * sc, sc, sc, c);
                }
        }
        x += 6 * sc;
    }
    return x;
}
static int32_t su_tw(const char *s, int32_t sc) { int32_t n = 0; while (s[n]) n++; return n ? n * 6 * sc - sc : 0; }
static void su_tr(int32_t xr, int32_t y, const char *s, uint16_t c, int32_t sc) { su_t(xr - su_tw(s, sc), y, s, c, sc); }
/* a small string builder (no printf in the firmware) */
typedef struct { char b[40]; uint32_t n; } su_s_t;
static void su_c(su_s_t *s, const char *a) { while (*a && s->n < 39u) s->b[s->n++] = *a++; s->b[s->n] = 0; }
static void su_u(su_s_t *s, uint32_t v, uint32_t digits)   /* digits: at least that many (0-padded) */
{
    char t[10]; uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10u);
    while (n < digits && n < 10u) t[n++] = '0';
    while (n && s->n < 39u) s->b[s->n++] = t[--n];
    s->b[s->n] = 0;
}
static uint32_t su_mix(uint32_t h, uint32_t v) { h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2); return h ? h : 1u; }
/* a part of the screen: drawn alone when its signature changes */
static int su_need(uint32_t k, uint32_t sig) { if (su.sig[k] == sig) return 0; su.sig[k] = sig; return 1; }

/* ---- the header: the world, playing, the seed; the key, its mode, the beat; the tempo */
static void su_head(void)
{
    char code[9]; su_s_t s = {{0}, 0};
    uint32_t sig = su_mix(su_mix(su_mix(ab.world, sr.playing), ab.seed), su_mix(sr.ev.bpm, sr.ev.key * 8u + sr.ev.mode));
    if (!su_need(0, sig)) return;
    su.sig[1] = 0;
    cv_begin(240, 34, SU_BG);
    su_t(6, 4, AB_WORLD_NAMES[ab.world % AB_NWORLDS], SU_TXT, 2);
    su_t(6, 23, sr.playing ? "[" : "\\", sr.playing ? SU_ACC : SU_DIM, 1);
    ab_code(ab.seed, code);
    su_c(&s, "SEED "); su_c(&s, code);
    su_t(16, 23, s.b, SU_DIM, 1);
    su_t(124, 4, SU_NOTE[sr.ev.key % 12u], SU_ACC, 2);
    su_t(124, 23, AB_MODE_NAMES[sr.ev.mode % 7u], SU_DIM, 1);
    s.n = 0; su_u(&s, sr.ev.bpm ? sr.ev.bpm : ab.bpm, 1);
    su_tr(234, 2, s.b, SU_TXT, 3);
    cv_rect(0, 33, 240, 1, SU_RULE);
    cv_blit(0, 0);
}
static void su_beat(void)
{
    uint32_t beat = sr.playing ? sr.ev.step / 4u : 9u, i;
    if (!su_need(1, beat + 1u)) return;
    cv_begin(54, 6, SU_BG);
    for (i = 0; i < 4u; i++)
        cv_rect((int32_t)i * 14, 1, 12, 4, i == beat ? (i == 0 ? SU_ACC : SU_TXT) : SU_FAINT);
    cv_blit(180, 26);
}
/* ---- the plan: the piece's sections, the playhead; the section, the peak to come and when */
static void su_plan(void)
{
    uint32_t i, tot = 0, at = ab.plan_at, n = ab.plan_n, cum = 0, pos, sec, j, bars;
    int32_t x;
    su_s_t s = {{0}, 0}, r = {{0}, 0};
    if (at >= n) at = n ? n - 1u : 0u;
    for (i = 0; i < n; i++) tot += ab.plan_len[i];
    for (i = 0; i < at; i++) cum += ab.plan_len[i];
    if (!tot) tot = 1;
    pos = (cum + ab.sec_bar) * 16u + sr.ev.step;
    x = 4 + (int32_t)(pos * 232u / (tot * 16u));
    sec = ab.plan_sec[at] % AB_NSECS;
    j = at + 1u;                                   /* what comes: the peak if it is ahead, else the next section */
    if (sec != AS_PEAK) for (i = at + 1u; i < n; i++) if (ab.plan_sec[i] == AS_PEAK) { j = i; break; }
    bars = ab.plan_len[at] > ab.sec_bar ? ab.plan_len[at] - ab.sec_bar : 1u;
    for (i = at + 1u; i < j && i < n; i++) bars += ab.plan_len[i];
    {
        uint32_t sig = su_mix(su_mix(su_mix(n, at), (uint32_t)x), su_mix(ab.sec_bar, bars * 4u + ab.hold * 2u + ab.fade));
        for (i = 0; i < n; i++) sig = su_mix(sig, ab.plan_sec[i] * 1024u + ab.plan_len[i]);
        if (!su_need(2, sig)) return;
        su.sig[3] = 0;
    }
    cv_begin(240, 46, SU_BG);
    for (i = 0, cum = 0; i < n; i++) {
        int32_t a = 4 + (int32_t)(cum * 232u / tot), b;
        cum += ab.plan_len[i];
        b = 4 + (int32_t)(cum * 232u / tot);
        cv_rect(a, i == at ? 2 : 4, b - a - 1 > 0 ? b - a - 1 : 1, i == at ? 14 : 10, i < at ? SU_FAINT : SU_SEC[ab.plan_sec[i] % AB_NSECS]);
    }
    cv_rect(x - 1, 0, 2, 18, SU_ACC);
    su_t(4, 22, AB_SEC_NAMES[sec], sec == AS_BREATH || sec == AS_VOID ? SU_DIM : SU_SEC[sec], 2);
    if (j < n) su_c(&r, AB_SEC_NAMES[ab.plan_sec[j] % AB_NSECS]); else su_c(&r, "NEXT PIECE");
    su_c(&r, " IN "); su_u(&r, bars, 1);
    if (ab.hold) { r.n = 0; su_c(&r, "HOLD"); }
    if (ab.fade) { r.n = 0; su_c(&r, ab.faded ? "FADED" : "FADING OUT"); }
    su_tr(236, 22, r.b, j < n && ab.plan_sec[j] == AS_PEAK ? SU_ACC : SU_TXT, 2);
    su_c(&s, "BAR "); su_u(&s, ab.sec_bar + 1u, 1); su_c(&s, "/"); su_u(&s, ab.plan_len[at], 1);
    su_t(4, 38, s.b, SU_DIM, 1);
    cv_rect(0, 45, 240, 1, SU_RULE);
    cv_blit(0, 36);
}
static void su_clock(void)
{
    uint32_t sec = sr.playing ? (fm1_ms - su.play_ms) / 1000u : 0u;
    su_s_t s = {{0}, 0};
    if (!su_need(3, su_mix(sec + 1u, ab.piece_no))) return;
    cv_begin(124, 7, SU_BG);
    su_c(&s, "PIECE "); su_u(&s, ab.piece_no + 1u, 1);
    su_c(&s, "  TIME "); su_u(&s, sec / 60u, 1); su_c(&s, ":"); su_u(&s, sec % 60u, 2);
    su_tr(124, 0, s.b, SU_DIM, 1);
    cv_blit(112, 74);
}

/* ---- the layers: sound, lock, off, LIVE; and what each plays */
static const char *su_sound(uint32_t L, char *buf)
{
    uint32_t i;
    if (L == AL_DRUMS) { for (i = 0; i < 8u && dsu.k[0].name[i]; i++) buf[i] = dsu.k[0].name[i]; buf[i] = 0; return buf; }
    if (sr.sound_eng[L] >= 0 && sr.applied[L] >= 0) return ENGINES[sr.sound_eng[L] % NENGINES]->presets[sr.sound_pre[L]].name;
    return "-";
}
static void su_layer(uint32_t L)
{
    char kb[9]; const char *snd = su_sound(L, kb);
    uint32_t hot = su.focus == L, live = (sr.live >> L) & 1u;
    uint32_t sig = su_mix(su_mix(sr.mute[L], su.locks[L]), su_mix(L * 4u + hot, su.info * 2u + live)), i;
    int32_t y = 82 + (int32_t)L * 16, m = sr.mute[L];
    for (i = 0; snd[i]; i++) sig = su_mix(sig, (uint8_t)snd[i]);
    if (!su_need(4 + L, sig)) return;
    su.sig[11 + L] = 0;
    cv_begin(188, 16, hot ? SU_PANEL : SU_BG);
    if (hot) cv_rect(0, 0, 186, 1, live ? SU_ACC : SU_RULE);
    cv_rect(2, 2, 3, 12, m ? SU_FAINT : SU_LCOL[L]);
    su_t(10, 5, SU_LAYER[L], m ? SU_FAINT : SU_DIM, 1);
    cv_text(52, 0, &FONT_S, snd, m ? SU_FAINT : SU_TXT);
    if (su.locks[L]) su_t(176, 5, "@", SU_ACC, 1);
    if (m) su_t(164, 5, "M", SU_ACC, 1);
    if (live) { cv_rect(132, 3, 27, 11, SU_ACC); su_t(134, 5, "LIVE", SU_BG, 1); }
    cv_blit(0, (uint32_t)y);
}
static void su_meter(uint32_t L)    /* what it plays (lit by its notes) and how far it is in the mix (its fade) */
{
    uint32_t lv = sr.mute[L] ? 99u : su.meter[L] * 9u / 256u, g = ab.layer_g[L] * 48u / 250u, k;
    int32_t y = 82 + (int32_t)L * 16, hot = su.focus == L;
    if (!su_need(11 + L, su_mix((lv + 1u) * 2u + (uint32_t)hot, g))) return;
    cv_begin(50, 16, hot ? SU_PANEL : SU_BG);
    if (lv == 99u) su_t(14, 5, "MUTE", SU_FAINT, 1);
    else {
        for (k = 0; k < 8u; k++) cv_rect((int32_t)k * 6, 4, 5, 6, k < lv ? SU_LCOL[L] : SU_PANEL);
        cv_rect(0, 12, (int32_t)g, 2, SU_DIM);
    }
    cv_blit(188, (uint32_t)y);
}

/* ---- the knobs as they act now: the deck's (LIGHT SPACE DENSITY TIME), the layer's strip (LFO), the settings (GLO) */
static void su_knobs(void)
{
    int32_t L = su.info == SP_LFO ? (int32_t)su.focus : -1, glo = su.info == SP_GLO, v[4], k;
    uint32_t sig = su_mix(7u + (uint32_t)glo, (uint32_t)(L + 1));
    const char *kn[4];
    su_s_t t[4] = {{{0}, 0}, {{0}, 0}, {{0}, 0}, {{0}, 0}};
    if (L >= 0) {
        kn[0] = "VOLUME"; kn[1] = "FILTER"; kn[2] = "SPACE"; kn[3] = "VARIATION";
        v[0] = (su.lvol[L] + 30) * 1000 / 42; v[1] = 500 + su.lflt[L] * 500 / 60; v[2] = su.lspace[L] * 5; v[3] = ab.rerolls[L] % 8u * 1000 / 7;
        su_c(&t[0], su.lvol[L] >= 0 ? "+" : "-"); su_u(&t[0], (uint32_t)(su.lvol[L] < 0 ? -su.lvol[L] : su.lvol[L]) / 2u, 1); su_c(&t[0], "DB");
        if (!su.lflt[L]) su_c(&t[1], "OPEN"); else { su_c(&t[1], su.lflt[L] < 0 ? "LP " : "HP "); su_u(&t[1], (uint32_t)(su.lflt[L] < 0 ? -su.lflt[L] : su.lflt[L]) * 100u / 60u, 1); }
        su_u(&t[2], (uint32_t)su.lspace[L], 1); su_c(&t[2], "%");
        su_c(&t[3], "V"); su_u(&t[3], ab.rerolls[L] + 1u, 1);
    } else if (glo) {
        kn[0] = "TENSION"; kn[1] = "LIGHT"; kn[2] = "DENSITY"; kn[3] = "TIME";
        v[0] = su.knob[3]; v[1] = su.knob[0]; v[2] = su.knob[1]; v[3] = su.knob[2];
        for (k = 0; k < 4; k++) su_u(&t[k], (uint32_t)v[k] / 10u, 1);
    } else {
        kn[0] = "LIGHT"; kn[1] = "SPACE"; kn[2] = "DENSITY"; kn[3] = "TIME";
        v[0] = su.knob[0]; v[1] = su.space * 5; v[2] = su.knob[1]; v[3] = su.knob[2];
        su_c(&t[0], su.knob[0] < 400 ? "SHADOW" : su.knob[0] > 600 ? "LIGHT" : "DUSK");
        su_u(&t[1], (uint32_t)su.space, 1); su_c(&t[1], "%");
        su_u(&t[2], (uint32_t)su.knob[1] / 10u, 1);
        su_c(&t[3], su.knob[2] < 400 ? "SLOW" : su.knob[2] > 600 ? "FAST" : "x1");
    }
    for (k = 0; k < 4; k++) { uint32_t i; sig = su_mix(sig, (uint32_t)v[k]); for (i = 0; i < t[k].n; i++) sig = su_mix(sig, (uint8_t)t[k].b[i]); }
    if (!su_need(18, sig)) return;
    cv_begin(240, 30, SU_BG);
    cv_rect(0, 0, 240, 1, L >= 0 ? SU_LCOL[L] : SU_RULE);
    for (k = 0; k < 4; k++) {
        int32_t x = 4 + k * 59, w = 54 * clamp(v[k], 0, 1000) / 1000, centred = (L >= 0 && k == 1) || (L < 0 && !glo && k == 0);
        su_t(x, 4, kn[k], L >= 0 ? SU_TXT : SU_DIM, 1);
        cv_rect(x, 14, 54, 4, SU_PANEL);
        if (centred) {
            int32_t m = x + 27, e = x + w;
            cv_rect(e < m ? e : m, 14, e < m ? m - e : e - m, 4, SU_ACC);
            cv_rect(m, 12, 1, 8, SU_DIM);
        } else
            cv_rect(x, 14, w, 4, SU_TXT);
        su_t(x, 21, t[k].b, SU_TXT, 1);
    }
    cv_blit(0, 196);
}

/* ---- a page: its name, a line of help, the white keys as a grid (st: 0 plain, 1 on, 2 empty, 3 marked, 4 to come) */
static void su_grid(const char *title, const char *hint, const char *const *top, const char *const *name, const uint8_t *st, uint32_t rows)
{
    uint32_t k, i, sig = su_mix(0x6A1Du, rows);
    for (i = 0; title[i]; i++) sig = su_mix(sig, (uint8_t)title[i]);
    for (i = 0; hint[i]; i++) sig = su_mix(sig, (uint8_t)hint[i]);
    for (k = 0; k < rows * 4u; k++) {
        sig = su_mix(sig, st[k] + 7u * k);
        for (i = 0; top[k][i]; i++) sig = su_mix(sig, (uint8_t)top[k][i]);
        for (i = 0; name[k][i]; i++) sig = su_mix(sig, (uint8_t)name[k][i]);
    }
    if (!su_need(4, sig)) return;
    cv_begin(240, rows * 25u + 12u, SU_BG);
    su_t(4, 1, title, SU_ACC, 1);
    su_t(10 + su_tw(title, 1), 1, hint, SU_DIM, 1);
    for (k = 0; k < rows * 4u; k++) {
        int32_t x = (int32_t)(k % 4u) * 60, y = 12 + (int32_t)(k / 4u) * 25;
        uint32_t t = st[k];
        uint16_t bg = t == 1u ? SU_ACC : t == 2u ? SU_BG : t == 3u || t == 4u ? SU_PANEL : RGB(10, 13, 20);
        cv_rect(x + 1, y, 58, 23, bg);
        if (t == 2u) { cv_rect(x + 1, y, 58, 1, SU_PANEL); cv_rect(x + 1, y + 22, 58, 1, SU_PANEL); }
        su_t(x + 3, y + 3, top[k], t == 1u ? SU_BG : t == 3u || t == 4u ? SU_ACC : t == 2u ? SU_FAINT : SU_DIM, 1);
        su_t(x + 3, y + 13, name[k], t == 1u ? SU_BG : t == 2u ? SU_FAINT : t == 4u ? SU_ACC : SU_TXT, 1);
    }
    cv_blit(0, 82);
}
static const char *const SU_NUM[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static void su_page_fx(void)                     /* FX: the textures, on and off */
{
    static const char *const TP[16] = {"1", "2", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};
    const char *nm[16]; uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) { nm[k] = ""; st[k] = 2; }
    nm[0] = "SHIMMER"; st[0] = su.shimmer ? 1u : 0u;
    nm[1] = "TAPE"; st[1] = su.tape ? 1u : 0u;
    su_grid("TEXTURES", "A KEY: ON / OFF", TP, nm, st, 4);
}
static void su_page_sel(void)                    /* SEL: the world */
{
    const char *nm[16], *tp[16]; uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) {
        tp[k] = k < AB_NWORLDS ? SU_NUM[k] : ""; nm[k] = k < AB_NWORLDS ? AB_WORLD_NAMES[k] : ""; st[k] = 2;
        if (k < AB_NWORLDS) st[k] = k == ab.world ? 1u : ab.world_q == k ? 4u : 0u;
    }
    su_grid("WORLD", sr.playing ? "THE NEXT PIECE; AGAIN: NOW" : "THE WORLD", tp, nm, st, 4);
}
static void su_page_env(void)                    /* ENV: the gestures */
{
    static const char *const TP[16] = {"1", "2", "3", "4", "5", "6 NEW", "7 NEW", "8", "9 NEXT", "10", "11", "", "", "", "", ""};
    static const char *const NM[16] = {"RISE", "PEAK NOW", "CALM", "BREATH", "LIFT", "THEME", "HARMONY", "HOLD", "PIECE", "VOID",
                                       "FADE OUT", "", "", "", "", ""};
    uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) st[k] = k < 11u ? 0u : 2u;
    st[0] = ab.rise_req == 1u; st[1] = ab.rise_req == 2u; st[2] = ab.calm_req == 1u; st[3] = ab.breath_req; st[4] = ab.lift_req || ab.lift ? 3u : 0u;
    st[7] = ab.hold; st[8] = ab.calm_req == 3u; st[9] = ab.void_req; st[10] = ab.fade;
    su_grid("GESTURES", "AT THE NEXT BAR; LIT: TO COME", TP, NM, st, 4);
}
static void su_page_lfo(void)                    /* LFO: the layers, their strips, on / off */
{
    static const char *const TP[16] = {"1", "2", "3", "4", "5", "6", "7", "", "9 DRUMS", "10 DRONE", "11 PAD", "12 LOW", "13 MELO",
                                       "14 BELLS", "15 SWELL", "16"};
    const char *nm[16]; uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) {
        if (k < 7u) { nm[k] = SU_LAYER[k]; st[k] = k == su.focus ? 1u : 0u; }
        else if (k == 7u) { nm[k] = ""; st[k] = 2; }
        else if (k < 15u) { nm[k] = sr.mute[k - 8u] ? "OFF" : "ON"; st[k] = sr.mute[k - 8u] ? 3u : 0u; }
        else { nm[k] = "FLAT"; st[k] = 0; }
    }
    su_grid("LAYERS", "1-7 THE LAYER  9-15 ON / OFF", TP, nm, st, 4);
}
static void su_page_save(void)                   /* SAVE: sixteen seeds */
{
    static char code[16][9];
    const char *nm[16]; uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) {
        if (su.fav_set[k]) { ab_code(su.fav[k], code[k]); nm[k] = code[k]; st[k] = su.fav[k] == ab.seed ? 1u : 0u; }
        else { nm[k] = "EMPTY"; st[k] = 2; }
    }
    su_grid("SEEDS", "TAP: KEEP / PLAY  HOLD: REPLACE", SU_NUM, nm, st, 4);
}
static void su_page_glo(void)                    /* GLO: the settings, and what the buttons do */
{
    static const char *const R[] = {"PAGES  FX TEXTURES  SEL WORLD", "ENV GESTURES  LFO LAYERS  SAVE SEEDS",
                                    "THE SAME BUTTON OR HOME: THE DECK", "BLACK KEYS: RISE PEAK CALM BREATH",
                                    "LIFT THEME HARMONY HOLD NEXT VOID FADE", "DECK: THE KEYS PLAY THE LAYER",
                                    "HOME ON THE DECK: ABYSS TAKES IT BACK", "REC A NEW SET         ABYSS 0.1.3"};
    static const char *const TP[4] = {"1 GIVE", "2 MIX", "", ""};
    const char *nm[4]; uint8_t st[4]; uint32_t k;
    nm[0] = "ALL BACK"; nm[1] = "FLAT"; nm[2] = ""; nm[3] = "";
    st[0] = sr.live ? 3u : 0u; st[1] = 0; st[2] = 2; st[3] = 2;
    su_grid("SETTINGS", "AND THE BUTTONS", TP, nm, st, 1);
    if (!su_need(5, 0x6105u)) return;
    cv_begin(240, 75, SU_BG);
    for (k = 0; k < sizeof R / sizeof *R; k++)
        su_t(4, 2 + (int32_t)k * 9, R[k], k == 7u ? SU_ACC : SU_TXT, 1);
    cv_blit(0, 119);
}

/* ---- the footer: what was just done, what comes; the load */
static void su_foot(void)
{
    uint32_t q = (uint32_t)song.cpu_q8 * 100u >> 8;
    su_s_t nx = {{0}, 0}, hm = {{0}, 0};
    static const char *const HM[SP_N] = {"", "SHIMMER  TAPE", "1-3 THE WORLD", "THE PIECE IN YOUR HANDS", "KNOBS: THE LAYER'S STRIP",
                                         "16 SEEDS", "ABYSS 0.1.3"};
    const char *m;
    char kb[9];
    if (su.info == SP_HOME) {
        uint32_t L = su.focus % 7u;
        if (sr.live) su_c(&hm, "LIVE  HOME: GIVE IT BACK");
        else if (L == AL_DRUMS) su_c(&hm, "THE KEYS PLAY THE KIT");
        else {
            su_c(&hm, "KEYS PLAY "); su_c(&hm, SU_LAYER[L]);
            if (su.oct) { su_c(&hm, su.oct < 0 ? " OCT-" : " OCT+"); su_u(&hm, (uint32_t)(su.oct < 0 ? -su.oct : su.oct), 1); }
            if (su.larp) su_c(&hm, " ARP");
        }
    } else su_c(&hm, HM[su.info % SP_N]);
    m = su.msg_t ? su.msg : hm.b;
    if (!su.msg_t && su.snd_t) { su_c(&nx, SU_LAYER[su.focus % 7u]); su_c(&nx, " > "); su_c(&nx, su_sound(su.focus % 7u, kb)); m = nx.b; }
    else if (!su.msg_t && ab.world_q != 0xFFu) { su_c(&nx, "NEXT PIECE: "); su_c(&nx, AB_WORLD_NAMES[ab.world_q % AB_NWORLDS]); m = nx.b; }
    {
        uint32_t sig = su_mix(su_mix(q / 2u, sr.lean), (su.msg_t ? 1u : 0u) + (nx.n ? 4u : 0u)), i;
        su_s_t s = {{0}, 0};
        for (i = 0; m[i]; i++) sig = su_mix(sig, (uint8_t)m[i]);
        if (!su_need(19, su_mix(sig, su.info))) return;
        cv_begin(240, 14, SU_PANEL);
        su_t(4, 4, m, su.msg_t || nx.n ? SU_ACC : SU_DIM, 1);
        if (sr.lean) { char e[6] = "ECO 0"; e[4] = (char)('0' + sr.lean); su_t(164, 4, e, SU_ACC, 1); }
        su_c(&s, "CPU "); su_u(&s, q, 1); su_c(&s, "%");
        su_tr(236, 4, s.b, q > 80u ? SU_ACC : SU_DIM, 1);
        cv_blit(0, 226);
    }
}

static void siren_draw(void)
{
    uint32_t L;
    while (su.kicks_seen != sr.kicks) { su.kicks_seen++; su.meter[AL_DRUMS] = 255; }
    if (sr.notes_n - su.notes_seen > 16u) su.notes_seen = sr.notes_n - 16u;
    while (su.notes_seen != sr.notes_n) {
        uint32_t q = su.notes_seen % 16u, l = sr.notes_l[q];
        su.notes_seen++;
        if (l == AL_SUB) l = AL_LOW;
        if (l < 7u) su.meter[l] = 255;
    }
    if (!sr.playing) su.play_ms = fm1_ms;
    if (su.page_was != su.info) {
        for (L = 0; L < 24u; L++) su.sig[L] = 0;
        su.page_was = su.info;
    }
    su_head(); su_beat(); su_plan(); su_clock();
    switch (su.info) {
    case SP_FX: su_page_fx(); break;
    case SP_SEL: su_page_sel(); break;
    case SP_ENV: su_page_env(); break;
    case SP_LFO: su_page_lfo(); break;
    case SP_SAVE: su_page_save(); break;
    case SP_GLO: su_page_glo(); break;
    default: for (L = 0; L < 7u; L++) { su_layer(L); su_meter(L); } break;
    }
    su_knobs();
    su_foot();
    for (L = 0; L < 7u; L++) su.meter[L] = (uint8_t)(su.meter[L] * 15u / 16u);   /* (they fall slowly: ambient) */
    if (su.msg_t) su.msg_t--;
    if (su.snd_t) su.snd_t--;
    su.frame++;
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* SIREN's screen (siren_ui.c runs it), 0.3: a DJ's deck, not a visualiser. Read at a glance in the dark:
 *
 *   TECHNO  [>               A MIN   128      the style, playing, the key, the tempo; the beat (four dots)
 *   SEED 4K2-9XQ  TRACK 3    . . . .  BPM     the seed (it plays the same rave again), the track of the set
 *   [intro|groove|BUILD|##DROP##|break|..]    the track's plan as the brain wrote it, coloured by section (the CDJs'
 *   GROOVE          DROP IN 8                 phrases), the playhead; the section, and what comes next and when
 *   BAR 5/16  PHRASE 3/8         SET 12:34
 *   | DRUMS  T-WAREHS          ####....       the seven layers: their sound, lock (@), mute, what they play (meters)
 *   | BASS   DYN BASS        @ ######..
 *   ...
 *   FILTER  INTENSITY  SPACE  MELODY          the four knobs as they act now (a layer's key held: its strip,
 *                                             VOLUME FILTER SPACE VARIATION)
 *   FX ROLL 1/8 / REROLL / ...      CPU 31%   the effect held, what was just done, what is to come; the load
 *
 * (0.5) The layer in the hands (SELECT, or its key held) lit; a layer the arrangement keeps waiting for room: WAIT.
 * HOME: the effects page, the 16 keys of FX + key for this style (the one held lit); HOME again: the keys.
 * Fast: each part of the screen keeps what it last drew (a signature) and is drawn again, alone, only when that
 * changes (a meter, a knob, the playhead), into gfx.c's canvas and blitted where it sits: a frame is a few small
 * transfers, most frames none, so the keys and knobs answer at once. Text in SIREN's own 5 x 7 pixel font (x1, x2,
 * x3: labels, values, the tempo) and the firmware's 16-pixel font for the sounds' names. Its own file: the host
 * draws it (tests/siren/screen_test.c). */
static const uint8_t SU_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> note key bit */
static const char *const SU_LAYER[7] = {"DRUMS", "BASS", "ARP", "HOOK", "PAD", "STABS", "FX"};
static const char *const SU_STYLE[SB_NSTYLES] = {"TECHNO", "TRANCE", "PSY", "ACID", "RAVE", "HARD", "DRUM&BASS", "HOUSE"};
static const char *const SU_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const SU_SCALE[5] = {"MINOR", "DORIAN", "PHRYGIAN", "HARMONIC", "MAJOR"};
static const char *const SU_BNAME[11] = {"BREAK", "BUILD 4", "BUILD 16", "DROP", "FILL", "NEW MELODY", "NEW BASS",
                                         "NEW GROOVE", "KEY UP", "NEXT TRACK", "STAY"};
/* (0.8) the pages: the deck, then one a button (siren_ui.c SU_PAGE_BTN) */
enum { SP_HOME, SP_FX, SP_SEL, SP_ENV, SP_LFO, SP_SAVE, SP_GLO, SP_N };
static const char *const SU_PAGE[SP_N] = {"DECK", "FX", "STYLE", "FORM", "LAYERS", "SEEDS", "SETTINGS"};
/* the colours: near black, panels, rules, text, labels, SIREN's orange; each section's (rekordbox's phrases' spirit);
 * each layer's */
#define SU_BG RGB(6, 7, 10)
#define SU_PANEL RGB(18, 20, 26)
#define SU_RULE RGB(38, 42, 52)
#define SU_TXT RGB(236, 236, 240)
#define SU_DIM RGB(118, 124, 138)
#define SU_FAINT RGB(60, 64, 76)
#define SU_ACC RGB(255, 98, 26)
static const uint16_t SU_SEC[7] = {RGB(72, 96, 150), RGB(0, 168, 156), RGB(255, 186, 0), RGB(255, 58, 36), RGB(140, 92, 224),
                                   RGB(255, 40, 140), RGB(90, 100, 112)};   /* INTRO GROOVE BUILD DROP BREAK PEAK OUTRO */
static const uint16_t SU_LCOL[7] = {RGB(236, 236, 240), RGB(255, 98, 26), RGB(0, 200, 230), RGB(255, 186, 0), RGB(140, 92, 224),
                                    RGB(255, 40, 140), RGB(0, 190, 120)};
static struct {
    uint8_t info, style, lock_style, msg_t; char msg[20];   /* (0.8: info, the page open, SP_*) */
    int16_t knob[4];
    uint32_t fav[16]; uint8_t fav_set[16];
    uint8_t locks[7];
    uint8_t fx_was; int8_t px_key;               /* (0.2) FX held, the effect's key (-1) */
    /* (0.5) the hands on the mix: the layer chosen (SELECT), the variation's clicks; the master's filter and space; each
     * layer's strip; the footer naming a sound just changed */
    uint8_t focus, snd_t; int8_t vacc, mfilt; int16_t space;
    int8_t lvol[7], lflt[7]; int16_t lspace[7];
    /* (0.8) the keyboard: its octave, the arpeggiator; the keys down (bits) and what each sent (its layer, its notes);
     * the seed key held (SAVE, -1), when, whether it acted already */
    int8_t oct, slot_k; uint8_t larp, slot_done, kl[27]; uint32_t down, kn[27], slot_t0;
    uint32_t frame, kicks_seen, notes_seen;
    uint8_t meter[7];                            /* each layer's activity, 0..255, falling */
    uint32_t sig[24];                            /* what each part of the screen last drew (0: draw it) */
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

/* ---- the header: style, playing, seed, track; key, beat; tempo */
static void su_head(void)
{
    char code[9]; su_s_t s = {{0}, 0};
    uint32_t sig = su_mix(su_mix(su_mix(sb.style, sr.playing), sb.seed), su_mix(sr.ev.bpm, sr.ev.key * 8u + sr.ev.scale));
    if (!su_need(0, sig)) return;
    su.sig[1] = 0;                                                      /* (the beat sits on it: drawn again after) */
    cv_begin(240, 34, SU_BG);
    su_t(6, 4, SU_STYLE[sb.style % SB_NSTYLES], SU_TXT, 2);
    su_t(6, 23, sr.playing ? "[" : "\\", sr.playing ? SU_ACC : SU_DIM, 1);
    sb_code(sb.seed, code);
    su_c(&s, "SEED "); su_c(&s, code);
    su_t(16, 23, s.b, SU_DIM, 1);
    su_t(124, 4, SU_NOTE[sr.ev.key % 12u], SU_ACC, 2);
    su_t(124, 23, SU_SCALE[sr.ev.scale % 5u], SU_DIM, 1);
    s.n = 0; su_u(&s, sr.ev.bpm ? sr.ev.bpm : sb.bpm, 3);
    su_tr(234, 2, s.b, SU_TXT, 3);
    cv_rect(0, 33, 240, 1, SU_RULE);
    cv_blit(0, 0);
}
static void su_beat(void)   /* the beat in the bar: four dots under the key */
{
    uint32_t beat = sr.playing ? sr.ev.step / 4u : 9u, i;
    if (!su_need(1, beat + 1u)) return;
    cv_begin(54, 6, SU_BG);
    for (i = 0; i < 4u; i++)
        cv_rect((int32_t)i * 14, 1, 12, 4, i == beat ? (i == 0 ? SU_ACC : SU_TXT) : SU_FAINT);
    cv_blit(180, 26);
}

/* ---- the plan: the track's sections, the playhead; the section, the next, when */
static void su_plan(void)
{
    uint32_t i, tot = 0, at = sb.plan_at, n = sb.plan_n, cum = 0, pos, sec, j, bars, beats = 0;
    int32_t x0, x;
    su_s_t s = {{0}, 0}, r = {{0}, 0};
    if (at >= n) at = n ? n - 1u : 0u;
    for (i = 0; i < n; i++) tot += sb.plan_len[i];
    for (i = 0; i < at; i++) cum += sb.plan_len[i];
    if (!tot) tot = 1;
    pos = (cum + sb.sec_bar) * 16u + sr.ev.step;                       /* sixteenths into the track */
    x = 4 + (int32_t)(pos * 232u / (tot * 16u));
    sec = sb.plan_sec[at] % 7u;
    /* what comes: a drop (or the peak) if one is ahead, else the next section; when, in bars (the last: beats) */
    j = at + 1u;
    if (sec != SEC_DROP && sec != SEC_PEAK)
        for (i = at + 1u; i < n; i++) if (sb.plan_sec[i] == SEC_DROP || sb.plan_sec[i] == SEC_PEAK) { j = i; break; }
    bars = sb.plan_len[at] > sb.sec_bar ? sb.plan_len[at] - sb.sec_bar : 1u;
    for (i = at + 1u; i < j && i < n; i++) bars += sb.plan_len[i];
    if (bars <= 1u) beats = 4u - sr.ev.step / 4u;
    {
        uint32_t sig = su_mix(su_mix(su_mix(su_mix(n, at), (uint32_t)x), su_mix(sb.sec_bar, bars * 8u + beats)), su_mix(tot, sb.phrase_bar * 2u + sb.stay));
        for (i = 0; i < n; i++) sig = su_mix(sig, sb.plan_sec[i] * 1024u + sb.plan_len[i]);
        if (!su_need(2, sig)) return;
        su.sig[3] = 0;                                                  /* (the clock sits on it: drawn again after) */
    }
    cv_begin(240, 46, SU_BG);
    x0 = 4;
    for (i = 0, cum = 0; i < n; i++) {                                 /* the sections, past ones dimmed */
        int32_t a = 4 + (int32_t)(cum * 232u / tot), b;
        cum += sb.plan_len[i];
        b = 4 + (int32_t)(cum * 232u / tot);
        cv_rect(a, i == at ? 2 : 4, b - a - 1, i == at ? 14 : 10, i < at ? SU_FAINT : SU_SEC[sb.plan_sec[i] % 7u]);
        (void)x0;
    }
    cv_rect(x - 1, 0, 2, 18, SU_TXT);                                  /* the playhead */
    su_t(4, 22, SB_SEC_NAMES[sec], SU_SEC[sec], 2);
    if (j < n) su_c(&r, SB_SEC_NAMES[sb.plan_sec[j] % 7u]); else su_c(&r, "NEXT TRACK");
    su_c(&r, " IN ");
    if (beats) { su_u(&r, beats, 1); su_c(&r, beats > 1u ? " BEATS" : " BEAT"); } else su_u(&r, bars, 1);
    if (sb.stay) { r.n = 0; su_c(&r, "STAY"); }                       /* (0.5) the section loops */
    su_tr(236, 22, r.b, (j < n && (sb.plan_sec[j] == SEC_DROP || sb.plan_sec[j] == SEC_PEAK)) ? SU_ACC : SU_TXT, beats ? 1 : 2);
    su_c(&s, "BAR "); su_u(&s, sb.sec_bar + 1u, 1); su_c(&s, "/"); su_u(&s, sb.plan_len[at], 1);
    su_c(&s, "  PHRASE "); su_u(&s, sb.phrase_bar + 1u, 1);
    su_t(4, 38, s.b, SU_DIM, 1);
    cv_rect(0, 45, 240, 1, SU_RULE);
    cv_blit(0, 36);
}
static void su_clock(void)   /* the set's time */
{
    uint32_t sec = sr.playing ? (fm1_ms - su.play_ms) / 1000u : 0u;
    su_s_t s = {{0}, 0};
    if (!su_need(3, su_mix(sec + 1u, sb.track_no))) return;
    cv_begin(104, 7, SU_BG);
    su_c(&s, "TRACK "); su_u(&s, sb.track_no + 1u, 1);
    su_c(&s, "  SET "); su_u(&s, sec / 60u, 1); su_c(&s, ":"); su_u(&s, sec % 60u, 2);
    su_tr(104, 0, s.b, SU_DIM, 1);
    cv_blit(132, 74);
}

/* ---- the layers: sound, lock, mute; and what each plays (its meter) */
static const char *su_sound(uint32_t L, char *buf)
{
    uint32_t i;
    if (L == L_DRUMS) { for (i = 0; i < 8u && dsu.k[0].name[i]; i++) buf[i] = dsu.k[0].name[i]; buf[i] = 0; return buf; }
    if (sr.sound_eng[L] >= 0 && sr.applied[L] >= 0) return ENGINES[sr.sound_eng[L] % NENGINES]->presets[sr.sound_pre[L]].name;
    return "-";
}
static int su_waits(uint32_t L)   /* (0.5) written for, but the arrangement has no room for it now */
{
    return L >= L_ARP && L <= L_FX && !sr.mute[L] && sr.allowed && !((sr.allowed >> L) & 1u) && siren_wrote(L, sr.n16);
}
static void su_layer(uint32_t L)
{
    char kb[9]; const char *snd = su_sound(L, kb);
    uint32_t hot = su.focus == L, live = (sr.live >> L) & 1u;
    uint32_t sig = su_mix(su_mix(sr.mute[L], su.locks[L]), su_mix(L * 4u + hot, su.info * 2u + live)), i;
    int32_t y = 82 + (int32_t)L * 16, m = sr.mute[L];
    for (i = 0; snd[i]; i++) sig = su_mix(sig, (uint8_t)snd[i]);
    if (!su_need(4 + L, sig)) return;
    su.sig[11 + L] = 0;                                                 /* (its meter drawn again on it) */
    cv_begin(188, 16, hot ? SU_PANEL : SU_BG);                         /* (to the meter: nothing of a page left) */
    if (hot) cv_rect(0, 0, 186, 1, live ? SU_ACC : SU_RULE);
    cv_rect(2, 2, 3, 12, m ? SU_FAINT : SU_LCOL[L]);
    su_t(10, 5, SU_LAYER[L], m ? SU_FAINT : SU_DIM, 1);
    cv_text(48, 0, &FONT_S, snd, m ? SU_FAINT : SU_TXT);
    if (su.locks[L]) su_t(176, 5, "@", SU_ACC, 1);
    if (live) { cv_rect(132, 3, 27, 11, SU_ACC); su_t(134, 5, "LIVE", SU_BG, 1); }   /* (0.8) in the hands */
    if (m) su_t(164, 5, "M", SU_ACC, 1);
    cv_blit(0, (uint32_t)y);
}
static void su_meter(uint32_t L)
{
    uint32_t lv = sr.mute[L] ? 99u : su_waits(L) ? 98u : su.meter[L] * 9u / 256u, k;
    int32_t y = 82 + (int32_t)L * 16, hot = su.focus == L;
    if (!su_need(11 + L, (lv + 1u) * 2u + (uint32_t)hot)) return;
    cv_begin(50, 16, hot ? SU_PANEL : SU_BG);
    if (lv == 99u) su_t(14, 5, "MUTE", SU_FAINT, 1);
    else if (lv == 98u) su_t(14, 5, "WAIT", SU_DIM, 1);
    else for (k = 0; k < 8u; k++)
        cv_rect((int32_t)k * 6, 5, 5, 6, k < lv ? (k >= 6u ? SU_ACC : SU_LCOL[L]) : SU_PANEL);
    cv_blit(188, (uint32_t)y);
}

/* ---- the knobs: as they act now (0.5): the DJ's four, or the strip of the layer whose key is held */
static void su_fval(su_s_t *s, int32_t f)   /* a filter's place: OPEN, LP n, HP n */
{
    if (!f) su_c(s, "OPEN");
    else { su_c(s, f < 0 ? "LP " : "HP "); su_u(s, (uint32_t)(f < 0 ? -f : f) * 100u / 60u, 1); }
}
static void su_knobs(void)
{
    int32_t L = su.info == SP_LFO ? (int32_t)su.focus : -1, glo = su.info == SP_GLO, v[4], k;
    uint32_t sig = su_mix(7u + (uint32_t)glo, (uint32_t)(L + 1));
    const char *kn[4];
    su_s_t t[4] = {{{0}, 0}, {{0}, 0}, {{0}, 0}, {{0}, 0}};
    if (L >= 0) {
        kn[0] = "VOLUME"; kn[1] = "FILTER"; kn[2] = "SPACE"; kn[3] = "VARIATION";
        v[0] = (su.lvol[L] + 30) * 1000 / 42; v[1] = 500 + su.lflt[L] * 500 / 60; v[2] = su.lspace[L] * 5; v[3] = sb.motif_ver[L] % 8u * 1000 / 7;
        if (su.lvol[L] >= 0) su_c(&t[0], "+");
        else su_c(&t[0], "-");
        su_u(&t[0], (uint32_t)(su.lvol[L] < 0 ? -su.lvol[L] : su.lvol[L]) / 2u, 1); su_c(&t[0], "DB");
        su_fval(&t[1], su.lflt[L]);
        su_u(&t[2], (uint32_t)su.lspace[L], 1); su_c(&t[2], "%");
        su_c(&t[3], "V"); su_u(&t[3], sb.motif_ver[L] + 1u, 1);
    } else if (glo) {                                                  /* (0.8) the settings' */
        kn[0] = "TENSION"; kn[1] = "MUTATION"; kn[2] = "INTENSITY"; kn[3] = "MELODY";
        v[0] = su.knob[2]; v[1] = su.knob[3]; v[2] = su.knob[0]; v[3] = su.knob[1];
        for (k = 0; k < 4; k++) su_u(&t[k], (uint32_t)v[k] / 10u, 1);
    } else {
        kn[0] = "FILTER"; kn[1] = "INTENSITY"; kn[2] = "SPACE"; kn[3] = "MELODY";
        v[0] = 500 + su.mfilt * 500 / 60; v[1] = su.knob[0]; v[2] = su.space * 5; v[3] = su.knob[1];
        su_fval(&t[0], su.mfilt);
        su_u(&t[1], (uint32_t)su.knob[0] / 10u, 1);
        su_u(&t[2], (uint32_t)su.space, 1); su_c(&t[2], "%");
        su_u(&t[3], (uint32_t)su.knob[1] / 10u, 1);
    }
    for (k = 0; k < 4; k++) { uint32_t i; sig = su_mix(sig, (uint32_t)v[k]); for (i = 0; i < t[k].n; i++) sig = su_mix(sig, (uint8_t)t[k].b[i]); }
    if (!su_need(18, sig)) return;
    cv_begin(240, 30, SU_BG);
    cv_rect(0, 0, 240, 1, L >= 0 ? SU_LCOL[L] : SU_RULE);
    for (k = 0; k < 4; k++) {
        int32_t x = 4 + k * 59, w = 54 * clamp(v[k], 0, 1000) / 1000, centred = !glo && k == (L >= 0 ? 1 : 0);
        su_t(x, 4, kn[k], L >= 0 ? SU_TXT : SU_DIM, 1);
        cv_rect(x, 14, 54, 4, SU_PANEL);
        if (centred) {                                                  /* (a filter: from the middle, left LP, right HP) */
            int32_t m = x + 27, e = x + w;
            cv_rect(e < m ? e : m, 14, e < m ? m - e : e - m, 4, SU_ACC);
            cv_rect(m, 12, 1, 8, SU_DIM);
        } else
            cv_rect(x, 14, w, 4, k == 1 && L < 0 && !glo ? SU_ACC : SU_TXT);
        su_t(x, 21, t[k].b, SU_TXT, 1);
    }
    cv_blit(0, 196);
}

/* ---- (0.8) a page: its name, a line of help, the 16 white keys as a grid (a number, a name; st: 0 plain, 1 on,
 * 2 empty, 3 marked, 4 to come) */
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
        uint16_t bg = t == 1u ? SU_ACC : t == 2u ? SU_BG : t == 3u || t == 4u ? SU_PANEL : RGB(12, 14, 19);
        cv_rect(x + 1, y, 58, 23, bg);
        if (t == 2u) { cv_rect(x + 1, y, 58, 1, SU_PANEL); cv_rect(x + 1, y + 22, 58, 1, SU_PANEL); }
        su_t(x + 3, y + 3, top[k], t == 1u ? SU_BG : t == 3u || t == 4u ? SU_ACC : t == 2u ? SU_FAINT : SU_DIM, 1);
        su_t(x + 3, y + 13, name[k], t == 1u ? SU_BG : t == 2u ? SU_FAINT : t == 4u ? SU_ACC : SU_TXT, 1);
    }
    cv_blit(0, 82);
}
static const char *const SU_NUM[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static void su_page_fx(void)                     /* FX: the effects, while a key is held */
{
    const char *nm[16]; uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) { nm[k] = SIREN_PX[sb.style % SB_NSTYLES][k].name; st[k] = su.px_key == (int8_t)k ? 1u : k >= 12u ? 3u : 0u; }
    su_grid("FX", "HOLD A WHITE KEY: ITS EFFECT", SU_NUM, nm, st, 4);
}
static void su_page_sel(void)                    /* SEL: the style */
{
    const char *nm[16], *tp[16]; uint8_t st[16]; uint32_t k;
    for (k = 0; k < 16u; k++) {
        tp[k] = k < 8u || k == 15u ? SU_NUM[k] : ""; nm[k] = k < 8u ? SU_STYLE[k] : ""; st[k] = 2;
        if (k < 8u) st[k] = k == sb.style % 8u ? 1u : sr.q_style == (int8_t)k ? 4u : 0u;
    }
    nm[15] = su.lock_style ? "KEPT" : "KEEP IT"; st[15] = su.lock_style ? 1u : 0u;
    su_grid("STYLE", sr.playing ? "IT COMES IN ON THE NEXT PHRASE" : "THE STYLE", tp, nm, st, 4);
}
static void su_page_env(void)                    /* ENV: the track's form */
{
    static const char *const TP[16] = {"1", "2", "3", "4", "5", "6 NEW", "7 NEW", "8 NEW", "9", "10 NEXT", "11", "", "", "", "", ""};
    static const char *const NM[16] = {"BREAK", "BUILD 4", "BUILD 16", "DROP", "FILL", "MELODY", "BASSLINE", "GROOVE", "KEY UP",
                                       "TRACK", "STAY", "", "", "", "", ""};
    uint8_t st[16], req[11] = {sb.break_req, sb.dive == 4u, sb.dive == 16u, sb.drop_req, (uint8_t)(sr.fill_req || sr.fill_on), 0, 0, 0,
                               sb.key_req, (uint8_t)(sr.q_next != 0), sb.stay};
    uint32_t k;
    for (k = 0; k < 16u; k++) st[k] = k < 11u ? (req[k] ? 1u : k == 3u ? 3u : 0u) : 2u;
    su_grid("FORM", "ON THE BEAT; LIT: TO COME", TP, NM, st, 4);
}
static void su_page_lfo(void)                    /* LFO: the layers, their strips (the knobs), on / off */
{
    static const char *const TP[16] = {"1", "2", "3", "4", "5", "6", "7", "", "9 DRUMS", "10 BASS", "11 ARP", "12 HOOK", "13 PAD",
                                       "14 STABS", "15 FX", "16"};
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
        if (su.fav_set[k]) { sb_code(su.fav[k], code[k]); nm[k] = code[k]; st[k] = su.fav[k] == sb.seed ? 1u : 0u; }
        else { nm[k] = "EMPTY"; st[k] = 2; }
    }
    su_grid("SEEDS", "TAP: KEEP / PLAY  HOLD: REPLACE", SU_NUM, nm, st, 4);
}
static void su_page_glo(void)                    /* GLO: the settings, and what the buttons do */
{
    static const char *const R[] = {"PAGES  FX  SEL STYLE  ENV FORM", "LFO LAYERS  SAVE SEEDS  GLO SETTINGS",
                                    "THE SAME BUTTON OR HOME: THE DECK", "DECK: THE KEYS PLAY THE LAYER (SELECT)",
                                    "HOME ON THE DECK: SIREN TAKES IT BACK", "ARP  OCT- OCT+  EDIT KEEP  SEQ REROLL",
                                    "REC A NEW RAVE            SIREN 0.8"};
    static const char *const TP[4] = {"1 KEEP", "2 MIX", "3 GIVE", ""};
    const char *nm[4]; uint8_t st[4]; uint32_t k;
    nm[0] = "STYLE"; nm[1] = "FLAT"; nm[2] = "ALL BACK"; nm[3] = "";
    st[0] = su.lock_style ? 1u : 0u; st[1] = 0; st[2] = sr.live ? 3u : 0u; st[3] = 2;
    su_grid("SETTINGS", "AND THE BUTTONS", TP, nm, st, 1);
    if (!su_need(5, 0x6105u)) return;
    cv_begin(240, 75, SU_BG);
    for (k = 0; k < sizeof R / sizeof *R; k++)
        su_t(4, 2 + (int32_t)k * 10, R[k], k == 6u ? SU_ACC : SU_TXT, 1);
    cv_blit(0, 119);
}

/* ---- the footer: the effect held, or what was just done; the load */
static void su_foot(void)
{
    uint32_t q = (uint32_t)song.cpu_q8 * 100u >> 8, fx = su.px_key >= 0;
    su_s_t nx = {{0}, 0};
    static const char *const HM[SP_N] = {"", "HOLD A KEY: ITS EFFECT", "1-8 THE STYLE  16 KEEP IT", "THE TRACK'S FORM",
                                         "KNOBS: THE LAYER'S STRIP", "16 SEEDS", "SIREN 0.8"};
    su_s_t hm = {{0}, 0};
    const char *m;
    if (su.info == SP_HOME) {                                            /* (0.8) the deck: the keyboard, the layer in the hands */
        uint32_t L = su.focus % 7u;
        if (sr.live) { su_c(&hm, "LIVE  HOME: GIVE IT BACK"); }
        else if (L == L_DRUMS) su_c(&hm, "THE KEYS PLAY THE KIT");
        else {
            su_c(&hm, "KEYS PLAY "); su_c(&hm, SU_LAYER[L]);
            if (su.oct) { su_c(&hm, su.oct < 0 ? " OCT-" : " OCT+"); su_u(&hm, (uint32_t)(su.oct < 0 ? -su.oct : su.oct), 1); }
            if (su.larp) su_c(&hm, " ARP");
        }
    } else su_c(&hm, HM[su.info % SP_N]);
    m = fx ? SIREN_PX[sb.style % SB_NSTYLES][su.px_key % 16].name : su.msg_t ? su.msg : hm.b;
    char kb[9];
    if (!fx && !su.msg_t && su.snd_t) {                                 /* (0.5) the sound just chosen */
        su_c(&nx, SU_LAYER[su.focus % 7u]); su_c(&nx, " > "); su_c(&nx, su_sound(su.focus % 7u, kb));
        m = nx.b;
    } else if (!fx && !su.msg_t && (sr.q_style >= 0 || sr.q_next)) {   /* (0.3) the next style, on the phrase (0.5: track) */
        su_c(&nx, "NEXT "); su_c(&nx, sr.q_style >= 0 ? SU_STYLE[sr.q_style % SB_NSTYLES] : "TRACK"); su_c(&nx, " IN ");
        su_u(&nx, 8u - sb.phrase_bar % 8u, 1); su_c(&nx, 8u - sb.phrase_bar % 8u > 1u ? " BARS" : " BAR");
        m = nx.b;
    } else if (!fx && !su.msg_t && sb.drop_req) m = "DROP AT THE NEXT BAR";
    else if (!fx && !su.msg_t && sb.key_req) m = "KEY UP AT THE DROP";
    uint32_t sig = su_mix(su_mix(q / 2u, sr.lean), fx * 2u + (su.msg_t ? 1u : 0u) + (nx.n ? 4u : 0u)), i;
    su_s_t s = {{0}, 0};
    for (i = 0; m[i]; i++) sig = su_mix(sig, (uint8_t)m[i]);
    if (!su_need(19, su_mix(sig, su.info))) return;
    cv_begin(240, 14, fx ? SU_ACC : SU_PANEL);
    if (fx) { su_t(4, 4, "FX", SU_BG, 1); su_t(22, 4, m, SU_BG, 1); }
    else su_t(4, 4, m, su.msg_t || nx.n ? SU_ACC : SU_DIM, 1);
    if (sr.lean) { char e[6] = "ECO 0"; e[4] = (char)('0' + sr.lean); su_t(164, 4, e, fx ? SU_BG : SU_ACC, 1); }
    su_c(&s, "CPU "); su_u(&s, q, 1); su_c(&s, "%");
    su_tr(236, 4, s.b, fx ? SU_BG : (q > 80u ? SU_ACC : SU_DIM), 1);
    cv_blit(0, 226);
}

static void siren_draw(void)
{
    uint32_t L;
    /* what happened since the last frame: kicks and notes light the meters */
    while (su.kicks_seen != sr.kicks) { su.kicks_seen++; su.meter[L_DRUMS] = 255; }
    if (sr.notes_n - su.notes_seen > 16u) su.notes_seen = sr.notes_n - 16u;
    while (su.notes_seen != sr.notes_n) {
        uint32_t q = su.notes_seen % 16u, l = sr.notes_l[q];
        su.notes_seen++;
        if (l == L_SUB) l = L_BASS;
        if (l < 7u) su.meter[l] = 255;
    }
    if (!sr.playing) su.play_ms = fm1_ms;
    if (su.page_was != su.info) {                                       /* a page change: all of it again */
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
    for (L = 0; L < 7u; L++) su.meter[L] = (uint8_t)(su.meter[L] * 7u / 8u);   /* (they fall) */
    if (su.msg_t) su.msg_t--;
    if (su.snd_t) su.snd_t--;
    su.frame++;
}

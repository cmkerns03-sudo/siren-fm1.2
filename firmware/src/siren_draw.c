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
    uint8_t info, style, lock_style, msg_t; char msg[20];
    int16_t knob[4];
    uint32_t fav[16]; uint8_t fav_set[16];
    uint8_t locks[7];
    uint8_t fx_was; int8_t px_key;               /* (0.2) FX held, the effect's key (-1) */
    /* (0.5) the hands on the mix: the layer chosen (SELECT), the layer whose key is held (-1) and whether the knobs or
     * encoders moved since (then no mute), the variation's clicks; the master's filter and space; each layer's strip;
     * the footer naming a sound just changed */
    uint8_t focus, held_used, snd_t; int8_t held, vacc, mfilt; int16_t space;
    int8_t lvol[7], lflt[7]; int16_t lspace[7];
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
    uint32_t hot = su.held == (int8_t)L ? 2u : su.focus == L ? 1u : 0u;
    uint32_t sig = su_mix(su_mix(sr.mute[L], su.locks[L]), su_mix(L * 4u + hot, su.info)), i;
    int32_t y = 82 + (int32_t)L * 16, m = sr.mute[L];
    for (i = 0; snd[i]; i++) sig = su_mix(sig, (uint8_t)snd[i]);
    if (!su_need(4 + L, sig)) return;
    su.sig[11 + L] = 0;                                                 /* (its meter drawn again on it) */
    cv_begin(186, 16, hot ? SU_PANEL : SU_BG);
    if (hot) cv_rect(0, 0, 186, 1, hot == 2u ? SU_ACC : SU_RULE);
    cv_rect(2, 2, 3, 12, m ? SU_FAINT : SU_LCOL[L]);
    su_t(10, 5, SU_LAYER[L], m ? SU_FAINT : SU_DIM, 1);
    cv_text(48, 0, &FONT_S, snd, m ? SU_FAINT : SU_TXT);
    if (su.locks[L]) su_t(176, 5, "@", SU_ACC, 1);
    if (m) su_t(164, 5, "M", SU_ACC, 1);
    cv_blit(0, (uint32_t)y);
}
static void su_meter(uint32_t L)
{
    uint32_t lv = sr.mute[L] ? 99u : su_waits(L) ? 98u : su.meter[L] * 9u / 256u, k;
    int32_t y = 82 + (int32_t)L * 16, hot = su.held == (int8_t)L || su.focus == L;
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
    int32_t L = su.held, v[4], k;
    uint32_t sig = su_mix(7u, (uint32_t)(L + 1));
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
        int32_t x = 4 + k * 59, w = 54 * clamp(v[k], 0, 1000) / 1000, centred = k == (L >= 0 ? 1 : 0);
        su_t(x, 4, kn[k], L >= 0 ? SU_TXT : SU_DIM, 1);
        cv_rect(x, 14, 54, 4, SU_PANEL);
        if (centred) {                                                  /* (a filter: from the middle, left LP, right HP) */
            int32_t m = x + 27, e = x + w;
            cv_rect(e < m ? e : m, 14, e < m ? m - e : e - m, 4, SU_ACC);
            cv_rect(m, 12, 1, 8, SU_DIM);
        } else
            cv_rect(x, 14, w, 4, k == 1 && L < 0 ? SU_ACC : SU_TXT);
        su_t(x, 21, t[k].b, SU_TXT, 1);
    }
    cv_blit(0, 196);
}

/* ---- the effects page (HOME): the 16 keys of FX + key, for this style */
static void su_fxpage(void)
{
    uint32_t k, sig = su_mix(sb.style, (uint32_t)(su.px_key + 2));
    if (!su_need(4, sig)) return;
    su.sig[18] = 0;                                                     /* (the knobs drawn again after) */
    for (k = 5; k < 18u; k++) su.sig[k] = 0;
    cv_begin(240, 112, SU_BG);
    su_t(4, 1, "FX + KEY", SU_ACC, 1);
    su_t(58, 1, "HOLD FX, PRESS A WHITE KEY", SU_DIM, 1);
    for (k = 0; k < 16u; k++) {
        int32_t x = (int32_t)(k % 4u) * 60, y = 12 + (int32_t)(k / 4u) * 25, on = su.px_key == (int8_t)k;
        su_s_t n = {{0}, 0};
        cv_rect(x + 1, y, 58, 23, on ? SU_ACC : k >= 12u ? SU_PANEL : RGB(12, 14, 19));
        su_u(&n, k + 1u, 1);
        su_t(x + 4, y + 3, n.b, on ? SU_BG : k >= 12u ? SU_ACC : SU_DIM, 1);
        su_t(x + 4, y + 13, SIREN_PX[sb.style % SB_NSTYLES][k].name, on ? SU_BG : SU_TXT, 1);
    }
    cv_blit(0, 82);
}

/* ---- (0.5) the keys page (HOME twice): what the black keys and the knobs do */
static void su_keyspage(void)
{
    static const char *const R[] = {"BLACK KEYS, LEFT TO RIGHT", "1 BREAK     2 BUILD 4   3 BUILD 16", "4 DROP      5 FILL",
                                    "6 NEW MELODY  7 NEW BASS  8 NEW GROOVE", "9 KEY UP  10 NEXT TRACK  11 STAY",
                                    "HOLD A LAYER KEY 1-7: ITS STRIP", "  VOLUME FILTER SPACE VARIATION",
                                    "TAP A LAYER KEY: ON / OFF", "SELECT LAYER  PRESET SOUND  ALGO MOTIF",
                                    "FX + WHITE KEY: THE EFFECTS  9-16 STYLE"};
    uint32_t k;
    if (!su_need(4, 0x5EC0u)) return;
    for (k = 5; k < 18u; k++) su.sig[k] = 0;
    cv_begin(240, 112, SU_BG);
    for (k = 0; k < sizeof R / sizeof *R; k++)
        su_t(4, 2 + (int32_t)k * 11, R[k], k == 0 || k == 5 || k == 7 ? SU_ACC : SU_TXT, 1);
    cv_blit(0, 82);
}

/* ---- the footer: the effect held, or what was just done; the load */
static void su_foot(void)
{
    uint32_t q = (uint32_t)song.cpu_q8 * 100u >> 8, fx = su.px_key >= 0;
    su_s_t nx = {{0}, 0};
    static const char *const HM[3] = {"HOME: THE EFFECTS", "HOME: THE KEYS", "HOME: BACK TO THE DECK"};
    const char *m = fx ? SIREN_PX[sb.style % SB_NSTYLES][su.px_key % 16].name : su.msg_t ? su.msg : HM[su.info % 3u];
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
    uint32_t sig = su_mix(su_mix(q / 2u, sr.lean), su_mix(fx * 2u + (su.msg_t ? 1u : 0u), (uint32_t)(uintptr_t)m)), i;
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
    if (su.info == 1u) { su_fxpage(); su_knobs(); }
    else if (su.info == 2u) { su_keyspage(); su_knobs(); }
    else {
        for (L = 0; L < 7u; L++) { su_layer(L); su_meter(L); }
        su_knobs();
    }
    su_foot();
    for (L = 0; L < 7u; L++) su.meter[L] = (uint8_t)(su.meter[L] * 7u / 8u);   /* (they fall) */
    if (su.msg_t) su.msg_t--;
    if (su.snd_t) su.snd_t--;
    su.frame++;
}

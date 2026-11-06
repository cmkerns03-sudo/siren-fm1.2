/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* (SIREN 0.10) SIREN STUDIO's SysEx (web/siren_studio.html): the live mode, the seeds, the banks of sounds.
 *   F0 7D 46 4C cmd args.. F7, as the editor's (editor.c: 7-bit bytes; ed_v: 14 bit, offset 8192). A u32: 5 bytes,
 *   7 bits each, low first; a bank (64-bit mask over a layer's universe, siren.c srb_*): 10 bytes, 7 bits each.
 *   100 INFO             -> the version, the protocol (1), styles 8, layers 7, each layer's universe size
 *   101 NAMES L, j0      -> L, j0, n (<= 20), then for each: the engine (or ""), the sound, the factory's styles (2 bytes)
 *   102 STATE            -> what plays (siren_studio.html reads it ~8 times a second)
 *   103 ACT kind a v     -> one of the FM-1's gestures (SA_*), without opening its page; -> 1 done, 0 not
 *   104 SEEDS            -> the 16 SAVE slots: set, style (bits 0-2) and ALL (bit 3), the seed
 *   105 SEEDS_PUT 16 x (set st seed) -> 1 (to flash at the next stop, as the FM-1's own)
 *   106 BANK s, L        -> s, L, n, the user's (1) or the factory's (0), the mask, the factory's mask
 *   107 BANK_PUT s L mask -> 1 (never empty: 0); from the next pick, to flash at the next stop
 *   108 BANK_RESET s L   -> 1 (s 127: every style and layer back to the factory's)
 *   109 AUDITION L j     -> 1: universe sound j on layer L now, the brain's choice back at its next
 *   110 PX s             -> s, the 16 effects of style s (the FX page's keys; 13-16 the style's own) */
enum { SR_INFO = 100, SR_NAMES, SR_STATE, SR_ACT, SR_SEEDS, SR_SEEDS_PUT, SR_BANK, SR_BANK_PUT, SR_BANK_RESET, SR_AUDITION, SR_PX };
enum { SA_PLAY, SA_REC, SA_FORM, SA_FX_DOWN, SA_FX_UP, SA_STYLE, SA_ALL, SA_KEEPSTYLE, SA_MUTE, SA_FLAT, SA_STRIP,
       SA_DJKNOB, SA_GLOKNOB, SA_FOCUS, SA_PRESET, SA_SOUND, SA_ALGO, SA_LOCK, SA_REROLL, SA_ARP, SA_OCT, SA_GIVEBACK,
       SA_NOTE_ON, SA_NOTE_OFF, SA_SEED_PLAY, SA_SEED_KEEP, SA_SEED_CLEAR, SA_PAGE, SA_N };
#define SR_PROTO 1u

static void sr_u32(uint32_t v) { uint32_t i; for (i = 0; i < 5u; i++) { ed_b(v); v >>= 7; } }
static uint32_t sr_ru32(const uint8_t *p) { uint32_t v = 0, i; for (i = 5; i--;) v = v << 7 | (p[i] & 0x7Fu); return v; }
static void sr_u64(uint64_t v) { uint32_t i; for (i = 0; i < 10u; i++) { ed_b((uint32_t)v & 0x7Fu); v >>= 7; } }
static uint64_t sr_ru64(const uint8_t *p) { uint64_t v = 0; uint32_t i; for (i = 10; i--;) v = v << 7 | (p[i] & 0x7Fu); return v; }
static uint64_t sr_universe(uint32_t L) { return srb_nuni[L] >= 64u ? ~(uint64_t)0 : SRB_BIT(srb_nuni[L]) - 1u; }
static int32_t sr_now(uint32_t L)                    /* the universe sound layer L plays, -1 none */
{
    int32_t k = sb.snd[L];
    if (k < 0 || !srb_all[L]) return -1;
    return srb_list[sb.style % SB_NSTYLES][L][(uint32_t)k % srb_all[L]];
}
static int sr_white(uint32_t bit, uint32_t *k)       /* a keyboard bit: a white key (its index), else black */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) if (SU_WHITE[i] == bit) { *k = i; return 1; }
    for (i = 0; i < 11u; i++) if (SU_BLACK[i] == bit) { *k = i; return 0; }
    *k = 0xFFu;
    return 0;
}

static int sr_act(uint32_t kind, uint32_t a, int32_t v)
{
    uint32_t L = a % 7u, k;
    switch (kind) {
    case SA_PLAY:
        if (sr.playing) { siren_cmd(SC_STOP, 0, 0, 0); transport_req = 2; } else { siren_cmd(SC_PLAY, 0, 0, 0); transport_req = 1; }
        return 1;
    case SA_REC: siren_cmd(SC_SEED, sb.style, 0, su_seed_new()); su_message("NEW RAVE"); return 1;
    case SA_FORM: if (a >= 11u) return 0; su_black(a); return 1;
    case SA_FX_DOWN:
        if (a >= 16u) return 0;
        if (su.px_key >= 0) su.vheld &= ~(1u << SU_WHITE[su.px_key]);
        su.px_key = (int8_t)a; su.vheld |= 1u << SU_WHITE[a];        /* (held until FX_UP: siren_input lets it go) */
        siren_cmd(SC_PUNCH, (int32_t)a, 0, 0);
        su_message(SIREN_PX[sb.style % SB_NSTYLES][a].name);
        return 1;
    case SA_FX_UP: if (su.px_key >= 0) su.vheld &= ~(1u << SU_WHITE[su.px_key]); return 1;
    case SA_STYLE:
        if (a >= SB_NSTYLES) return 0;
        su.style = (uint8_t)a;                                         /* (as SEL: on the next phrase; twice: now) */
        if (!sr.playing) siren_cmd(SC_STYLE, su.style, 0, 0); else siren_cmd(SC_STYLEQ, su.style, 0, 0);
        su_message(SU_STYLE[su.style]);
        return 1;
    case SA_ALL:
        siren_cmd(SC_ALLBANK, v != 0, 0, 0);
        su_message(v ? "ALL SOUNDS: NEXT TRACK" : "STYLE SOUNDS: NEXT TRACK");
        return 1;
    case SA_KEEPSTYLE: su_keep_style(); return 1;
    case SA_MUTE:
        sr.mute[L] ^= 1u; if (L == L_BASS) sr.mute[L_SUB] = sr.mute[L];
        su_message(sr.mute[L] ? "LAYER OFF" : "LAYER ON");
        return 1;
    case SA_FLAT: su_flat(); return 1;
    case SA_STRIP: if (a >= 28u) return 0; su_strip(a / 4u, a % 4u, v); return 1;
    case SA_DJKNOB: su_dj_knob(a & 3u, v); return 1;
    case SA_GLOKNOB: su_glo_knob(a & 3u, v); return 1;
    case SA_FOCUS: su.focus = (uint8_t)L; su.vacc = 0; siren_cmd(SC_MIX, SM_FOCUS, (int32_t)L, 0); return 1;
    case SA_PRESET: su_sound_step(L, v > 0 ? 1 : -1); return 1;
    case SA_SOUND: {                                                   /* universe sound v, kept on layer a */
        uint32_t st = sb.style % SB_NSTYLES;
        for (k = 0; k < srb_all[L]; k++)
            if (srb_list[st][L][k] == (uint32_t)v) break;
        if (k >= srb_all[L]) return 0;                                 /* (in no bank: AUDITION hears it) */
        siren_cmd(SC_SOUNDSET, (int32_t)L, (int32_t)k, 0);
        if (L != L_DRUMS) su.locks[L] = 1;
        su.msg_t = 0; su.snd_t = 60;
        return 1;
    }
    case SA_ALGO: siren_cmd(SC_REMOTIF, (int32_t)L, v > 0 ? 1 : -1, 0); su_message("NEW MOTIF"); return 1;
    case SA_LOCK:
        su.locks[L] ^= 1u; siren_cmd(SC_LOCK, (int32_t)L, su.locks[L], 0);
        su_message(su.locks[L] ? "LAYER KEPT" : "LAYER FREE");
        return 1;
    case SA_REROLL: siren_cmd(SC_REROLL, (int32_t)L, 0, 0); su_message("REROLL"); return 1;
    case SA_ARP: su.larp ^= 1u; siren_cmd(SC_LARP, su.larp, 0, 0); su_message(su.larp ? "ARP ON THE KEYS" : "ARP OFF"); return 1;
    case SA_OCT: su.oct = (int8_t)clamp(su.oct + (v > 0 ? 1 : -1), -2, 2); return 1;
    case SA_GIVEBACK: su_give_back(); return 1;
    case SA_NOTE_ON: {                                                 /* the keyboard: the layer chosen, as the deck */
        int w;
        if (a >= 27u || ((su.down >> a) & 1u)) return 0;
        w = sr_white(a, &k);
        if (k == 0xFFu || (!w && su.focus == L_DRUMS)) return 0;
        su.vheld |= 1u << a;
        su_key_down(su.focus, a, !w, k);
        return 1;
    }
    case SA_NOTE_OFF: if (a >= 27u) return 0; su.vheld &= ~(1u << a); return 1;
    case SA_SEED_PLAY: if (a >= 16u || !su.fav_set[a]) return 0; su_fav_play(a); return 1;
    case SA_SEED_KEEP: if (a >= 16u) return 0; su_fav_take(a); su_fav_kept(); return 1;
    case SA_SEED_CLEAR: if (a >= 16u) return 0; su.fav_set[a] = 0; su.fav_dirty = 1; return 1;
    case SA_PAGE: if (a >= SP_N) return 0; su_page(a); return 1;
    default: return 0;
    }
}

static void sr_state(void)
{
    uint32_t L, k, fav = 0, q = (uint32_t)song.cpu_q8 * 100u >> 8;
    uint32_t nx = sb.plan_at + 1u < sb.plan_n ? sb.plan_sec[sb.plan_at + 1u] + 1u : 0u;
    ed_b((sr.playing ? 1u : 0u) | (sr.allbank ? 2u : 0u) | (su.lock_style ? 4u : 0u) | (sr.live ? 8u : 0u) |
         (su.larp ? 16u : 0u) | (sb.stay ? 32u : 0u));
    ed_b(sr.lean);
    ed_b(sb.style); ed_b(su.style); ed_b((uint32_t)(sr.q_style + 1)); ed_b(sr.q_next);
    sr_u32(sb.seed);
    ed_v(sr.ev.bpm); ed_b(sr.ev.key); ed_b(sr.ev.scale);
    ed_b(sr.ev.sec); ed_v(sr.ev.sec_bar); ed_v(sr.ev.sec_len); ed_b(nx);
    ed_b((sb.drop_req ? 1u : 0u) | (sb.key_req ? 2u : 0u) | (sb.break_req ? 4u : 0u) | (sb.dive ? 8u : 0u));
    ed_b(su.info); ed_b(su.focus); ed_v(su.oct); ed_b((uint32_t)(su.px_key + 1));
    for (L = 0; L < 7u; L++) {
        int32_t j = sr_now(L);
        ed_b(sr.mute[L]); ed_b(sb.locks[L]); ed_b((uint32_t)(j + 1)); ed_b((uint32_t)(sr.sound_from[L] + 1));
        ed_v(su.lvol[L]); ed_v(su.lflt[L]); ed_v(su.lspace[L]);
    }
    ed_v(su.mfilt); ed_v(su.space);
    for (k = 0; k < 4u; k++) ed_v(su.knob[k]);
    ed_v((int32_t)q); ed_v(su.batt);
    for (k = 0; k < 16u; k++) fav |= (uint32_t)(su.fav_set[k] ? 1u : 0u) << k;
    ed_b(fav); ed_b(fav >> 7); ed_b(fav >> 14);
    ed_str(su.msg_t ? su.msg : "", 20);
}

/* the editor's dispatch (editor.c ed_handle): 1 handled */
static int ed_siren_handle(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, L, s;
    switch (cmd) {
    case SR_INFO:
        ed_str(FELUCCA_VERSION, 24);
        ed_b(SR_PROTO); ed_b(SB_NSTYLES); ed_b(7);
        for (L = 0; L < 7u; L++) ed_b(srb_nuni[L]);
        return 1;
    case SR_NAMES: {
        uint32_t j0, n;
        if (na < 2u || a[0] >= 7u) { ed_b(127); return 1; }
        L = a[0]; j0 = a[1];
        n = j0 < srb_nuni[L] ? srb_nuni[L] - j0 : 0u;
        if (n > 20u) n = 20u;
        ed_b(L); ed_b(j0); ed_b(n);
        for (i = j0; i < j0 + n; i++) {
            const char *e, *nm = srb_name(L, i, &e);
            char kb[9]; uint32_t f = 0, c;
            if (L == L_DRUMS) { for (c = 0; c < 8u && nm[c]; c++) kb[c] = nm[c]; kb[c] = 0; nm = kb; }
            for (s = 0; s < SB_NSTYLES; s++) if (srb_fact[s][L] & SRB_BIT(i)) f |= 1u << s;
            ed_str(e ? e : "", 8); ed_str(nm, 12); ed_b(f); ed_b(f >> 7);
        }
        return 1;
    }
    case SR_STATE: sr_state(); return 1;
    case SR_ACT:
        if (na < 4u) { ed_b(0); return 1; }
        ed_b((uint32_t)sr_act(a[0], a[1], ed_rv(a + 2)));
        return 1;
    case SR_SEEDS:
        for (i = 0; i < 16u; i++) { ed_b(su.fav_set[i]); ed_b(su.fav_st[i]); sr_u32(su.fav[i]); }
        return 1;
    case SR_SEEDS_PUT:
        if (na < 16u * 7u) { ed_b(0); return 1; }
        for (i = 0; i < 16u; i++) {
            const uint8_t *p = a + i * 7u;
            su.fav_set[i] = (uint8_t)(p[0] != 0); su.fav_st[i] = (uint8_t)(p[1] & 15u);
            su.fav[i] = su.fav_set[i] ? sr_ru32(p + 2) : 0u;
            if ((su.fav_st[i] & 7u) >= SB_NSTYLES) su.fav_st[i] &= 8u;
        }
        su.fav_dirty = 1; su_message(sr.playing ? "SEEDS IN: SAVED AT STOP" : "SEEDS IN");
        ed_b(1);
        return 1;
    case SR_BANK:
        if (na < 2u || a[0] >= SB_NSTYLES || a[1] >= 7u) { ed_b(127); return 1; }
        s = a[0]; L = a[1];
        ed_b(s); ed_b(L); ed_b(srb_nuni[L]); ed_b(srb_mask[s][L] != srb_fact[s][L]);
        sr_u64(srb_mask[s][L]); sr_u64(srb_fact[s][L]);
        return 1;
    case SR_BANK_PUT: {
        uint64_t m;
        if (na < 12u || a[0] >= SB_NSTYLES || a[1] >= 7u) { ed_b(0); return 1; }
        s = a[0]; L = a[1];
        m = sr_ru64(a + 2) & sr_universe(L);
        if (!m) { ed_b(0); return 1; }
        srb_mask[s][L] = m; srb_rebuild(); srb_dirty = 1;
        ed_b(1);
        return 1;
    }
    case SR_BANK_RESET:
        if (na < 2u) { ed_b(0); return 1; }
        for (s = 0; s < SB_NSTYLES; s++)
            for (L = 0; L < 7u; L++)
                if ((a[0] == 127u || a[0] == s) && (a[1] == 127u || a[1] == L)) srb_mask[s][L] = srb_fact[s][L];
        srb_rebuild(); srb_dirty = 1;
        ed_b(1);
        return 1;
    case SR_PX:
        s = na ? a[0] % SB_NSTYLES : 0u;
        ed_b(s);
        for (i = 0; i < 16u; i++) ed_str(SIREN_PX[s][i].name, 16);
        return 1;
    case SR_AUDITION:
        if (na < 2u || a[0] >= 7u || a[1] >= srb_nuni[a[0]]) { ed_b(0); return 1; }
        ed_b((uint32_t)siren_audition(a[0], a[1]));
        return 1;
    default:
        return 0;
    }
}

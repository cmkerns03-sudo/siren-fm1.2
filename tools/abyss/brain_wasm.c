/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (ABYSS) */
/* ABYSS's brain for the web: the FM-1's own abyss_brain.c compiled to WebAssembly, so the site plays exactly the
 * events the FM-1 plays. A few getters and setters for the audio worklet (js/worklet.js); no library.
 *   tools/abyss/build_brain_wasm.sh OUT.wasm */
#include "../../firmware/src/abyss_brain.c"
void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
static ab_brain_t B;
static ab_events_t E;
static ab_palette_t P;
#define X __attribute__((visibility("default")))
X void w_pal(int w, int L, int n) { if (w >= 0 && w < AB_NWORLDS && L >= 0 && L < AB_NLAYERS) { if (L == AL_DRUMS) P.nkits[w] = (uint8_t)n; else P.ncur[w][L] = (uint8_t)n; } }
X void w_init(void) { ab_init(&B, &P); }
X void w_seed(uint32_t seed, int world) { ab_set_seed(&B, seed, world); }
X void w_step(void) { ab_step16(&B, &E); }
X void w_knob(int k, int v) { ab_knob(&B, k, v); }
X void w_gesture(int g) { ab_gesture(&B, g); }
X void w_world(int w, int now) { if (now) ab_set_seed(&B, B.seed, w); else B.world_q = (uint8_t)w; }
X void w_reroll(int L) { ab_reroll(&B, L); }
X void w_lock(int L, int on) { ab_lock(&B, L, on); }
X void w_snd_step(int L, int d) {
    int n = L == AL_DRUMS ? P.nkits[B.world] : P.ncur[B.world][L];
    if (L < 0 || L >= AB_NLAYERS || n < 1) return;
    B.snd[L] = (int16_t)((B.snd[L] + d % n + n) % n); B.locks[L] = 1; B.sounds_changed = 1;
}
X uint32_t w_code(uint32_t s) { (void)s; return 0; }
/* the events of the last step */
X int w_n(void) { return E.nnotes; }
X int w_note(int i) { const ab_note_t *x = &E.notes[i]; return (x->layer << 16) | (x->note << 8) | x->vel; }
X int w_len(int i) { return E.notes[i].len; }
X int w_lvl(int L) { return E.lvl[L]; }
X int w_cut(int L) { return E.cut[L]; }
X int w_ev(int k)
{
    switch (k) {
    case 0: return E.gain; case 1: return E.verb; case 2: return E.lp; case 3: return E.hiss; case 4: return E.wind;
    case 5: return E.wind_f; case 6: return E.rumble; case 7: return E.sub_am; case 8: return E.drift; case 9: return E.frisson;
    case 10: return (int)E.bar; case 11: return E.step; case 12: return E.sec; case 13: return E.key; case 14: return E.mode;
    case 15: return E.chord; case 16: return E.chord_q; case 17: return E.e; case 18: return E.bpm; case 19: return E.sec_bar;
    case 20: return E.sec_len; case 21: return E.plan_at; case 22: return E.plan_n; case 23: return E.tex; default: return 0;
    }
}
/* the brain's state */
X int w_b(int k)
{
    switch (k) {
    case 0: return B.world; case 1: return B.world_q == 0xFF ? -1 : B.world_q; case 2: return (int)B.piece_no; case 3: return B.kind;
    case 4: return B.hold; case 5: return B.fade; case 6: return B.faded; case 7: return B.lift; case 8: return B.rise_req;
    case 9: return B.calm_req; case 10: return B.breath_req; case 11: return B.void_req; case 12: return B.chord_i; case 13: return B.nprog;
    case 14: return B.key; case 15: return B.mode; case 16: return B.bpm; case 17: return B.sounds_changed; case 18: B.sounds_changed = 0; return 0;
    default: return 0;
    }
}
X uint32_t w_seedv(void) { return B.seed; }
X int w_plan_sec(int i) { return B.plan_sec[i]; }
X int w_plan_len(int i) { return B.plan_len[i]; }
X int w_snd(int L) { return B.snd[L]; }
X int w_locked(int L) { return B.locks[L]; }
X int w_g(int L) { return B.g[L]; }
X int w_prog(int i) { return (B.prog[i].root & 0xFF) | (B.prog[i].q << 8) | (B.prog[i].inv << 16); }

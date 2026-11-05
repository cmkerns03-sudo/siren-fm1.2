/* SPDX-License-Identifier: GPL-3.0-only */
/* SIREN 0.9.1: OBERVAVE, the voice of OberVave (c) zvenson (https://github.com/zvenson/obervave, GPL-3.0-only), the
 * community's 8-voice analog-style polysynth for the FM-1, as one of SIREN's engines: its two oscillators (saw, pulse,
 * both; hard sync, cross-modulation, ENV > OSC 2), its noise, its input stage (DRIVE, a tanh), its filters (the 12 dB
 * state-variable LP / HP / BP / NOTCH / SEM morph, the 24 dB four-pole ladder), its two RC envelopes, its LFO (one
 * per part, SYNC / FREE / SPREAD, delayed per voice) and its VINTAGE, ported from obv_dsp.h and obv_engine.h. Its
 * programs (tools/siren/import_obervave.py -> obv_data.h) are SIREN's sounds; its arpeggiator, mixer and effects stay
 * OberVave's (SIREN has its own). OberVave runs its control at 16 frames: here two of its blocks a CTL (32), each
 * on half the samples. At half the rate (the pad, voice.c): the increments twice, the cutoff an octave up. */
#define OBV_PRESETS_HERE
#include "obv_data.h"
#include "obv_gain.h"
#define OBV_ONE (1 << 24)
#define OBV_OUT 3                              /* the voice into SIREN: x 8 (+18 dB: OberVave mixes its voices hot later; the layer's trim does the rest) */
#define OBV_FLOOR 2048                         /* -78 dB: a release below this frees the voice */
enum { OE_IDLE, OE_ATT, OE_DEC, OE_SUS, OE_REL };
typedef struct { uint32_t ph; int32_t prev; } obv_osc_t;
typedef struct { uint32_t stage; int32_t level; } obv_env_t;
typedef struct {
    obv_osc_t o1, o2;
    uint32_t noise, lfo_blk;
    obv_env_t fe, ae;
    int32_t ic1, ic2, lad[4];
    int32_t amp, drift, lfo_fade;
} obv_vc_t;
typedef struct {                               /* a part's numbers, once per block (obv_block) */
    uint32_t lfo_ph, rnd_a, rnd_b;
    int32_t lfo_val[2], cut_sm, reso_sm;
    int32_t fenv_amt, keytrk, velflt, lfo_pd, lfo_fd, lfo_pwd, lfo_ad, wheel_q, env_o2, det_fine8;
    int32_t lvl1, lvl2, lvln, drive_q12, reso_k, lad_k, vint, sus_f, sus_a, gain_q12, morph_q, aa, ar;
    uint32_t pw1_base, pw2_base, w1, w2, ftype, sync_on, xmod_q, prog, seen;
} obv_pt_t;
static obv_vc_t obv_v[NPART][NVOICE] __attribute__((section(".pool")));
static obv_pt_t obv_p[NPART] __attribute__((section(".pool")));   /* (zeroed at boot) */
static const int16_t OBV_VTUNE8[8] = {-20, 12, 7, -15, 18, -9, 3, -17};      /* the voice cards: +-8 cents */
static const int16_t OBV_VCUT8[8] = {-300, 250, 120, -380, 60, 330, -180, 220};   /* +-1.5 semitones */
static const int8_t OBV_VENV[8] = {10, -6, 14, -12, 3, -16, 8, -4};         /* /128: +-12 % on the times */

/* tanh(x / 32768) * 32767, interpolated */
static inline __attribute__((always_inline)) int32_t obv_tanh(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = OBV_TANH_Q15[256];
    else
        y = OBV_TANH_Q15[i] + (((OBV_TANH_Q15[i + 1] - OBV_TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
static inline __attribute__((always_inline)) int32_t obv_knee(int32_t x)   /* linear to +-32768, then soft */
{
    int32_t a = x < 0 ? -x : x;
    if (a <= 32768) return x;
    a = 32768 + obv_tanh(a - 32768);
    return x < 0 ? -a : a;
}
static inline uint32_t obv_xs(uint32_t *st)
{
    uint32_t s = *st;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return *st = s;
}
/* Q8 semitones -> the phase step a frame (44.1 kHz) */
static inline uint32_t obv_inc(int32_t p)
{
    uint32_t n, o;
    p = clamp(p, 0, 131 * 256 + 255);
    n = (uint32_t)p >> 8;
    o = n / 12u;
    return (uint32_t)(((uint64_t)OBV_INC_C9[n - o * 12u] * OBV_FINE_Q16[p & 255]) >> (26u - o));
}
/* the oscillators: naive saw / pulse, polyBLEP as discontinuity events one sample late (obv_dsp.h) */
static inline __attribute__((always_inline)) int32_t obv_naive(uint32_t ph, uint32_t pw, uint32_t wave)
{
    int32_t v = 0;
    if (wave & OW_SAW) v = (int32_t)(ph >> 16) - 32768;
    if (wave & OW_PLS) v += (int32_t)(ph >> 16) - (int32_t)((ph + pw) >> 16);
    return v;
}
static inline __attribute__((always_inline)) void obv_event(int32_t *prev, int32_t *cur, int32_t h, int32_t t)
{
    int32_t u = 32767 - t, h16 = h >> 4;
    *prev += (h16 * ((t * t) >> 15)) >> 12;
    *cur -= (h16 * ((u * u) >> 15)) >> 12;
}
static inline __attribute__((always_inline)) int32_t obv_osc(obv_osc_t *o, uint32_t inc, uint32_t d, uint32_t pw,
                                                             uint32_t wave, int32_t hw, int32_t reset_t)
{
    uint32_t old = o->ph, ph = old + inc;
    int32_t cur, out = o->prev;
    if (reset_t < 0 || !d) {
        cur = obv_naive(ph, pw, wave);
        if (ph < inc && d)
            obv_event(&out, &cur, hw, (int32_t)(ph / d));
        if ((wave & OW_PLS) && ph + pw < inc && d)
            obv_event(&out, &cur, 65536, (int32_t)((ph + pw) / d));
    } else {
        uint32_t at = ph - d * (uint32_t)reset_t;
        int32_t t;
        ph = d * (uint32_t)reset_t;
        cur = obv_naive(ph, pw, wave);
        if (at < old) {
            t = (int32_t)(at / d) + reset_t;
            obv_event(&out, &cur, hw, t > 32767 ? 32767 : t);
        }
        if ((wave & OW_PLS) && at + pw < old + pw) {
            t = (int32_t)((at + pw) / d) + reset_t;
            obv_event(&out, &cur, 65536, t > 32767 ? 32767 : t);
        }
        obv_event(&out, &cur, obv_naive(0, pw, wave) - obv_naive(at, pw, wave), reset_t);
    }
    o->ph = ph;
    o->prev = cur;
    return out;
}
/* the trapezoidal SVF (A. Simper), Q13; its band-pass state through a soft saturation */
typedef struct { int32_t a1, a2, a3, k; } obv_svf_t;
static inline int32_t obv_g(int32_t cut)
{
    int32_t i, g;
    cut = clamp(cut, 0, 136 << 8);
    i = cut >> 8;
    g = OBV_SVF_G[i];
    if (i < 136)
        g += ((OBV_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    return g;
}
static void obv_svf_coef(obv_svf_t *c, int32_t cut, int32_t k)
{
    int32_t g = obv_g(cut), den = 4096 + ((g * (g + k)) >> 12);
    if (den < 64) den = 64;
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
    c->k = k;
}
/* the 24 dB ladder: four TPT one-poles, the resonance around all four, a tanh in the loop */
typedef struct { int32_t G, G2, G3, G4, k, inv, gin; } obv_lad_t;
static void obv_lad_coef(obv_lad_t *c, int32_t cut, int32_t k10)
{
    int32_t g = obv_g(cut), G = (int32_t)(((uint32_t)g << 12) / (uint32_t)(4096 + g));
    c->G = G;
    c->G2 = (G * G) >> 12;
    c->G3 = (c->G2 * G) >> 12;
    c->G4 = (c->G3 * G) >> 12;
    c->k = k10;
    c->inv = (int32_t)((4096u << 12) / (uint32_t)(4096 + ((k10 * c->G4) >> 10)));
    c->gin = 4096 + 2 * k10;
}
static inline __attribute__((always_inline)) int32_t obv_lad(const obv_lad_t *c, int32_t *st, int32_t in)
{
    int32_t G = c->G, v, y, u;
    int32_t S1 = st[0] - ((G * st[0]) >> 12), S2 = st[1] - ((G * st[1]) >> 12);
    int32_t S3 = st[2] - ((G * st[2]) >> 12), S4 = st[3] - ((G * st[3]) >> 12);
    int32_t x = (in * c->gin) >> 12;
    int32_t acc = ((c->G3 * S1 + c->G2 * S2 + G * S3) >> 12) + S4;
    int32_t y4 = (int32_t)(((int64_t)(((c->G4 * x) >> 12) + acc) * c->inv) >> 12);
    u = obv_tanh((x - ((c->k * clamp(y4, -131072, 131071)) >> 10)) >> 1) << 1;
    v = ((u - st[0]) * G) >> 12; y = v + st[0]; st[0] = y + v;
    v = ((y - st[1]) * G) >> 12; y = v + st[1]; st[1] = y + v;
    v = ((y - st[2]) * G) >> 12; y = v + st[2]; st[2] = y + v;
    v = ((y - st[3]) * G) >> 12; y = v + st[3]; st[3] = y + v;
    return y;
}
/* the envelopes: analog RC curves, one step per 16 frames, level Q24 */
static void obv_env(obv_env_t *e, uint32_t a, uint32_t d, int32_t sus, uint32_t r, int32_t gate, int32_t vint)
{
    int32_t k;
    if (gate && e->stage >= OE_REL) e->stage = OE_ATT;
    if (!gate && e->stage && e->stage < OE_REL) e->stage = OE_REL;
    switch (e->stage) {
    case OE_ATT:
        k = (int32_t)OBV_ENV_K_ATT[a & 127u];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(OBV_ONE + (OBV_ONE * 3 >> 3) - e->level) * k) >> 24);
        if (e->level >= OBV_ONE) { e->level = OBV_ONE; e->stage = OE_DEC; }
        break;
    case OE_DEC:
        k = (int32_t)OBV_ENV_K_DEC[d & 127u];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(sus - e->level) * k) >> 24) - 16;
        if (e->level <= sus + 64) { e->level = sus; e->stage = OE_SUS; }
        break;
    case OE_SUS:
        e->level = sus;
        break;
    case OE_REL:
        k = (int32_t)OBV_ENV_K_DEC[r & 127u];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(0 - e->level) * k) >> 24) - 32;
        if (e->level < OBV_FLOOR) { e->level = 0; e->stage = OE_IDLE; }
        break;
    default:
        e->level = 0;
    }
}
static int32_t obv_lfo_shape(const obv_pt_t *g, uint32_t ph, uint32_t shape)
{
    int32_t p;
    switch (shape) {
    case OL_TRI:
        p = (int32_t)((ph + 0x40000000u) >> 15);
        return p < 65536 ? p - 32768 : 98303 - p;
    case OL_SAW: return 32767 - (int32_t)(ph >> 16);
    case OL_RAMP: return (int32_t)(ph >> 16) - 32768;
    case OL_SQR: return ph < 0x80000000u ? 32767 : -32767;
    case OL_SH: return (int32_t)(g->rnd_b >> 16) - 32768;
    default: {
        int32_t a = (int32_t)(g->rnd_a >> 16) - 32768, b = (int32_t)(g->rnd_b >> 16) - 32768;
        return a + (int32_t)(((int64_t)(b - a) * (ph >> 8)) >> 24);
    }
    }
}
static const uint8_t *obv_prog(const track_t *t) { return OBV_PROG[(uint32_t)clamp(t->p[P_E0], 0, OBV_NPROG - 1)]; }

/* once per block and part: the program with the EDIT offsets, the LFO's two steps */
static void obv_block(track_t *t)
{
    obv_pt_t *g = &obv_p[(uint32_t)(t - trk) % NPART];
    const uint8_t *pv = obv_prog(t);
    const int16_t *p = t->p;
    int32_t d, j;
    uint32_t k;
    g->prog = (uint32_t)clamp(p[P_E0], 0, OBV_NPROG - 1);
    if (g->seen != g->prog + 1u) {             /* a new program: its cutoff and resonance at once (no sweep from 0) */
        g->seen = g->prog + 1u;
        g->cut_sm = clamp((int32_t)pv[OP_CUTOFF] + p[P_E1], 0, 127) << 8;
        g->reso_sm = clamp((int32_t)pv[OP_RESO] + p[P_E2], 0, 127) << 8;
    }
    g->fenv_amt = clamp((int32_t)pv[OP_FENV_AMT] + p[P_E3], 0, 127) - 64;
    g->keytrk = pv[OP_KEYTRK];
    g->velflt = pv[OP_VEL_FLT];
    d = pv[OP_LFO_PITCH]; g->lfo_pd = (d * d) >> 7;
    d = pv[OP_LFO_FLT]; g->lfo_fd = (d * d) >> 7;
    d = pv[OP_LFO_PWM]; g->lfo_pwd = (d * d) >> 7;
    g->lfo_ad = pv[OP_LFO_AMP];
    g->wheel_q = pv[OP_MOD_DEPTH];
    g->env_o2 = (int32_t)pv[OP_ENV_O2] - 64;
    g->det_fine8 = ((int32_t)pv[OP_O2_FINE] - 64) * 2;
    d = pv[OP_O1_LVL]; g->lvl1 = (d * d) >> 7;
    d = pv[OP_O2_LVL]; g->lvl2 = (d * d) >> 7;
    d = pv[OP_NOISE]; g->lvln = (d * d) >> 7;
    d = clamp((int32_t)pv[OP_DRIVE] + p[P_E4], 0, 127); g->drive_q12 = 4096 + (d * d * 15 * 4096 / 16129);
    j = clamp((int32_t)pv[OP_CUTOFF] + p[P_E1], 0, 127);
    g->cut_sm += ((j << 8) - g->cut_sm) >> 5;   /* (two of OberVave's blocks: >> 5 for its >> 6) */
    j = clamp((int32_t)pv[OP_RESO] + p[P_E2], 0, 127);
    g->reso_sm += ((j << 8) - g->reso_sm) >> 5;
    g->reso_k = 8192 - g->reso_sm * 8400 / (127 * 256);
    g->lad_k = (8192 - g->reso_k) >> 1;
    g->vint = clamp((int32_t)pv[OP_VINTAGE] + p[P_E7], 0, 127);
    g->sus_f = pv[OP_FS] * (OBV_ONE / 127);
    g->sus_a = pv[OP_AS] * (OBV_ONE / 127);
    g->aa = clamp((int32_t)pv[OP_AA] + p[P_E5], 0, 127);
    g->ar = clamp((int32_t)pv[OP_AR] + p[P_E6], 0, 127);
    d = pv[OP_VOLUME];
    g->gain_q12 = (d * d) >> 2;
    g->pw1_base = ((uint32_t)pv[OP_O1_PW] << 25) + (1u << 24);
    g->pw2_base = ((uint32_t)pv[OP_O2_PW] << 25) + (1u << 24);
    g->w1 = pv[OP_O1_WAVE];
    g->w2 = pv[OP_O2_WAVE];
    g->ftype = pv[OP_FTYPE];
    g->morph_q = pv[OP_FMORPH];
    g->sync_on = pv[OP_SYNC];
    d = pv[OP_XMOD]; g->xmod_q = (uint32_t)((d * d) >> 7);
    for (k = 0; k < 2u; k++) {                 /* the LFO: OberVave's step a 16 frames, twice */
        uint32_t old = g->lfo_ph;
        g->lfo_ph += OBV_LFO_INC[pv[OP_LFO_RATE] & 127u];
        if (g->lfo_ph < old) { g->rnd_a = g->rnd_b; g->rnd_b = rng(); }
        g->lfo_val[k] = obv_lfo_shape(g, g->lfo_ph, pv[OP_LFO_SHAPE]);
    }
}

static void obv_note_on(track_t *t, voice_t *v)
{
    uint32_t pi = (uint32_t)(t - trk) % NPART, vi = (uint32_t)(v - t->v) % NVOICE;
    obv_vc_t *s = &obv_v[pi][vi];
    const uint8_t *pv = obv_prog(t);
    if (s->ae.stage == OE_IDLE) {              /* from silence: free-running phases, the filters clear */
        memset(s, 0, sizeof *s);
        s->o1.ph = rng(); s->o2.ph = rng();
        s->noise = rng() | 1u;
    }
    s->fe.stage = OE_ATT;
    s->ae.stage = OE_ATT;
    s->lfo_blk = 0;
    s->lfo_fade = pv[OP_LFO_DELAY] ? 0 : 32767;
}
static int obv_done(track_t *t, voice_t *v)
{
    return obv_v[(uint32_t)(t - trk) % NPART][(uint32_t)(v - t->v) % NVOICE].ae.stage == OE_IDLE;
}

static void obv_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t pi = (uint32_t)(t - trk) % NPART, card = (uint32_t)(v - t->v) % NVOICE, h, ns = n >> 1, i;
    obv_vc_t *s = &obv_v[pi][card];
    const obv_pt_t *g = &obv_p[pi];
    const uint8_t *pv = OBV_PROG[g->prog];
    int32_t vint = g->vint, vt = (vint * OBV_VENV[card & 7u]) >> 7, gate = v->gate;
    int32_t hw1 = (g->w1 & OW_SAW ? -65536 : 0) + (g->w1 & OW_PLS ? -65536 : 0);
    int32_t hw2 = (g->w2 & OW_SAW ? -65536 : 0) + (g->w2 & OW_PLS ? -65536 : 0);
    int32_t l1 = g->lvl1, l2 = g->lvl2, ln = g->lvln, drv = g->drive_q12, morph_q = g->morph_q;
    uint32_t ft = g->ftype, xq = g->xmod_q, sy = g->sync_on, ww1 = g->w1, ww2 = g->w2, ash = 4u - VH;
    /* the pitch from SIREN's (glide, bend, vibrato; its fine below 1/16 semitone), Q8 */
    int32_t base8 = (m->pitch16 << 4) + ((m->fine * 35) >> 5), gain = OBV_GAIN[g->prog];
    for (h = 0; h < 2u; h++, out += ns) {
        int32_t lfo, p8, p1, p2, cut, fenv, velg, trem, a, lfo_to, amp0, da;
        uint32_t inc1, inc2, pw1, pw2, d1, d2;
        obv_svf_t c1; obv_lad_t lc;
        obv_env(&s->fe, pv[OP_FA], pv[OP_FD], g->sus_f, pv[OP_FR], gate, vt);
        obv_env(&s->ae, (uint32_t)g->aa, pv[OP_AD], g->sus_a, (uint32_t)g->ar, gate, vt);
        fenv = s->fe.level >> 9;
        if (pv[OP_LFO_PHASE] == OLP_SPREAD)     /* (each voice card its own phase) */
            lfo = obv_lfo_shape(g, g->lfo_ph + (card << 29) - (h ? 0 : OBV_LFO_INC[pv[OP_LFO_RATE] & 127u]), pv[OP_LFO_SHAPE]);
        else
            lfo = g->lfo_val[h];
        if (s->lfo_fade < 32767) {
            uint32_t dl = OBV_LFO_DELAY_BLK[pv[OP_LFO_DELAY] & 127u];
            if (s->lfo_blk >= dl) { s->lfo_fade += 32767 / (int32_t)(dl / 2u + 1u) + 1; if (s->lfo_fade > 32767) s->lfo_fade = 32767; }
            s->lfo_blk++;
        }
        lfo = (lfo * s->lfo_fade) >> 15;
        s->drift += (int32_t)(obv_xs(&s->noise) & 31u) - 16;
        s->drift -= s->drift >> 11;
        p8 = base8 + ((OBV_VTUNE8[card & 7u] * vint) >> 7) + ((s->drift * vint) >> 10);
        p8 += (lfo * g->wheel_q * 4) >> 15;
        lfo_to = (lfo * g->lfo_pd * 24) >> 15;
        p1 = p8 + (pv[OP_LFO_PITCH_TO] != OTO_O2 ? lfo_to : 0);
        p2 = p8 + (pv[OP_LFO_PITCH_TO] != OTO_O1 ? lfo_to : 0) + ((int32_t)pv[OP_O2_SEMI] - 24) * 256 + g->det_fine8
           + ((fenv * g->env_o2 * 97) >> 15);
        inc1 = obv_inc(p1) << VH;
        inc2 = obv_inc(p2) << VH;
        a = lfo * g->lfo_pwd;
        {
            int32_t m1 = pv[OP_LFO_PWM_TO] != OTO_O2 ? a : 0, m2 = pv[OP_LFO_PWM_TO] != OTO_O1 ? a : 0;
            int64_t q1 = (int64_t)g->pw1_base + ((int64_t)m1 << 8), q2 = (int64_t)g->pw2_base + ((int64_t)m2 << 8);
            q1 = q1 < (1 << 26) ? (1 << 26) : q1 > 0xFBFFFFFFll ? 0xFBFFFFFFll : q1;
            q2 = q2 < (1 << 26) ? (1 << 26) : q2 > 0xFBFFFFFFll ? 0xFBFFFFFFll : q2;
            pw1 = (uint32_t)q1; pw2 = (uint32_t)q2;
        }
        cut = g->cut_sm + 8 * 256 + ((fenv * g->fenv_amt * 390) >> 15)
            + (((int32_t)m->pitch16 >> 4) - 60) * g->keytrk * 2 + (((int32_t)v->vel - 64) * g->velflt >> 1)
            + ((lfo * g->lfo_fd * 97) >> 15) + ((lfo * g->wheel_q * 48) >> 15) + ((OBV_VCUT8[card & 7u] * vint) >> 7)
            + ((s->drift * vint) >> 7) + ((m->cutoff * 7) >> 3) + (int32_t)VH * 12 * 256;   /* (SIREN's LFO, ENV) */
        if (ft == OF_LP24) obv_lad_coef(&lc, cut, g->lad_k);
        else obv_svf_coef(&c1, cut, g->reso_k);
        velg = 32767 - ((((127 - (int32_t)v->vel) * (int32_t)pv[OP_VEL_AMP]) >> 7) * 258);
        trem = 32767 - (((32767 - lfo) * g->lfo_ad) >> 8);
        amp0 = s->amp;
        a = (int32_t)(((int64_t)(s->ae.level >> 9) * velg >> 15) * trem >> 15);
        s->amp = (a * g->gain_q12) >> 12;
        da = s->amp - amp0;
        d1 = inc1 >> 15; d2 = inc2 >> 15;
        for (i = 0; i < ns; i++) {
            int32_t s1, s2, mix, lp, bp, hp, y, rt = -1;
            uint32_t ie = inc1;
            if (xq) {                          /* OSC 2 -> OSC 1 linear FM, the step never below inc / 16 */
                int32_t mm = (int32_t)(inc1 >> 8 > 0x200000u ? 0x200000u : inc1 >> 8) * (((s->o2.prev >> 8) * (int32_t)xq) >> 6);
                mm += (int32_t)inc1;
                ie = mm < (int32_t)(inc1 >> 4) ? inc1 >> 4 : (uint32_t)mm;
                d1 = ie >> 15;
            }
            s1 = obv_osc(&s->o1, ie, d1, pw1, ww1, hw1, -1);
            if (sy && s->o1.ph < ie && d1)
                rt = (int32_t)(s->o1.ph / d1);
            s2 = obv_osc(&s->o2, inc2, d2, pw2, ww2, hw2, rt);
            mix = s1 * l1 + s2 * l2;
            if (ln) mix += ((int32_t)(obv_xs(&s->noise) >> 16) - 32768) * ln;
            mix >>= 7;
            mix = obv_tanh(((((mix >> 3) * drv) >> 9) * 85) >> 8) * 3;   /* the input stage: DRIVE, a tanh */
            if (ft == OF_LP24)
                y = obv_lad(&lc, s->lad, mix);
            else {
                int32_t v3 = mix - s->ic2;
                int32_t v1 = (c1.a1 * s->ic1 + c1.a2 * v3) >> 13;
                int32_t v2 = s->ic2 + ((c1.a2 * s->ic1 + c1.a3 * v3) >> 13);
                int32_t n1 = 2 * v1 - s->ic1;
                s->ic1 = (uint32_t)(n1 + 24576) < 49152u ? n1 : obv_tanh(n1 >> 1) << 1;
                s->ic2 = 2 * v2 - s->ic2;
                lp = v2; bp = v1; hp = mix - ((c1.k * v1) >> 12) - v2;
                switch (ft) {
                case OF_HP12: y = hp; break;
                case OF_BP12: y = bp; break;
                case OF_NOTCH: y = hp + lp; break;
                case OF_SEM: y = lp + (((hp - lp) * morph_q) >> 7); break;
                default: y = lp; break;
                }
            }
            y = obv_knee(y);
            y = (y * (amp0 + ((da * (int32_t)i) >> ash))) >> 16;
            out[i] += (mulq15(y, amp_at(m, h * ns + i)) * gain) >> (8 - OBV_OUT);
        }
    }
}

static const engine_t ENG_OBV = {
    "OBERVAVE", {"PROG", "AMP"},
    {
        {"PROG", F_INT, 0, OBV_NPROG - 1, 0, 0, 0},
        {"CUT", F_BIPCT, -64, 63, 0, 0, 0},
        {"RES", F_BIPCT, -64, 63, 0, 0, 0},
        {"ENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"DRV", F_BIPCT, -64, 63, 0, 0, 0},
        {"ATK", F_BIPCT, -64, 63, 0, 0, 0},
        {"REL", F_BIPCT, -64, 63, 0, 0, 0},
        {"VINT", F_BIPCT, -64, 63, 0, 0, 0},
    },
    OBV_PRESETS, OBV_NPROG, -1, obv_note_on, obv_render,
    0xF800, {P_E1, P_E2, P_E3, P_E4}, 0, 0, 0, obv_block,
    .ownenv = 1, .done = obv_done,
};

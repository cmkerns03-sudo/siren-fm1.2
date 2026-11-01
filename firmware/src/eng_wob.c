/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (SIREN) */
/* WOB: SIREN's bass engine, for dubstep (and the reese of drum & bass). The firmwares' wobble sounds wobble on their own
 * clock; a dubstep riff needs its wobble locked to the tempo and changing rate hit by hit, and a growl. One voice (mono):
 * two detuned saws, a square, an FM pair (its index on the wobble: the tear), the four-pole ladder (its cutoff on the
 * wobble: the wob), two formants moving with it (the vowel: the growl, the "yoi"), a wavefolder, drive. The wobble is
 * not this engine's: SIREN writes it a block at a time (wob_lfo[], Q15 0..32767: the brain's rate, shape and depth for
 * each hit, siren.c); without SIREN it rests at 0.6. The prototype's worklet.js Wob, in fixed point.
 * EDIT 1  TYPE LO HI RES     EDIT 2  DET GRWL FOLD DRV
 * TYPE: the recipe (the oscillators' mix, the FM ratio and index, the vowels); LO, HI: the cutoff's range under the
 * wobble; GRWL: how much of the vowels; DET: the saws' detune (0..32 cents).
 * Voice state: ph[0], ph[1] the saws (ph[0] also the square and the FM carrier), ph[2] the FM modulator; s[0..3] the
 * ladder; s[4..7] the two formants. */
static const char *const N_WOB_TYPE[] = {"CLASSIC", "GROWL", "RIDDIM", "YOI", "TEAR", "REESE"};
static int32_t wob_lfo[NTRK] = {[0 ... NTRK - 1] = 19660};   /* Q15, per track (SIREN: siren.c, once a block) */

typedef struct { uint8_t saw, sq, fm; uint16_t ratio_q8, ilo_q8, ihi_q8; uint16_t va1, va2, vb1, vb2; } wob_recipe_t;   /* levels Q7 (128 = 1) */
static const wob_recipe_t WOB_R[6] = {
    {128, 45, 0, 256, 0, 0, 450, 800, 800, 1150},        /* CLASSIC: saws and a square, the filter */
    {77, 0, 128, 256, 102, 1024, 450, 800, 800, 1150},   /* GROWL: FM 1:1, the index 0.4..4, o -> a */
    {38, 128, 0, 256, 0, 0, 400, 700, 700, 1100},        /* RIDDIM: the square, hard */
    {128, 0, 77, 512, 51, 410, 325, 700, 270, 2300},     /* YOI: FM 2:1, u -> i */
    {51, 0, 128, 384, 256, 2048, 500, 1000, 700, 1800},  /* TEAR: FM 3:2, the index 1..8, folded */
    {128, 0, 0, 256, 0, 0, 450, 800, 800, 1150},         /* REESE: two saws, wide */
};
/* Hz -> the filters' cutoff index (0..127 << 8: 30 Hz .. 16 kHz), for the formants, from CUTOFF_HZ */
static int32_t wob_cut_of(int32_t hz)
{
    int32_t i = 0;
    while (i < 127 && CUTOFF_HZ[i + 1] <= hz) i++;
    if (i >= 127) return 127 << 8;
    return (i << 8) + (int32_t)(((hz - CUTOFF_HZ[i]) << 8) / (CUTOFF_HZ[i + 1] - CUTOFF_HZ[i] + 1));
}
/* the trapezoidal SVF's band-pass (tsvf_lp's v1) */
static inline int32_t wob_bp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v1;
}

static void wob_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {                    /* from silence: the filters clear (a slide keeps them) */
        v->ph[0] = 0; v->ph[1] = 0x40000000u; v->ph[2] = 0;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;
    }
}

static void wob_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const wob_recipe_t *R = &WOB_R[clamp(p[P_E0], 0, 5)];
    int32_t lfo = clamp(wob_lfo[(uint32_t)(t - trk) % NTRK], 0, 32767);
    int32_t lo = p[P_E1] << 8, hi = p[P_E2] << 8, cut = lo + (((hi - lo) * lfo) >> 15) + m->cutoff;
    int32_t idx = R->ilo_q8 + (((R->ihi_q8 - R->ilo_q8) * lfo) >> 15);                  /* the FM index, Q8 */
    int32_t form = p[P_E5] * 258, fold = p[P_E6], drive = 256 + p[P_E7] * 8;            /* form Q15; drive Q8 */
    uint32_t inc = m->inc, det = (uint32_t)p[P_E4] * 620000u;                            /* (2^(c/1200) - 1) x 2^32 per 1/4 cent */
    uint32_t incA = inc + (uint32_t)(((uint64_t)inc * det) >> 32), incB = inc - (uint32_t)(((uint64_t)inc * det) >> 32);
    uint32_t incM = (uint32_t)(((uint64_t)inc * R->ratio_q8) >> 8);
    uint32_t i, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t ls[4] = {v->s[0], v->s[1], v->s[2], v->s[3]};
    int32_t f1a = v->s[4], f1b = v->s[5], f2a = v->s[6], f2b = v->s[7];
    ladder_t lad; tsvf_t c1, c2;
    ladder_coef(&lad, cut, p[P_E3]);
    if (form) {                                      /* the vowels: from A to B as the wobble opens */
        tsvf_coef(&c1, wob_cut_of(R->va1 + (((int32_t)(R->vb1 - R->va1) * lfo) >> 15)), 120);
        tsvf_coef(&c2, wob_cut_of(R->va2 + (((int32_t)(R->vb2 - R->va2) * lfo) >> 15)), 120);
    }
    for (i = 0; i < n; i++) {
        int32_t x, y, soff = 0;
        if (R->fm) soff = (sine_i(ph2) * idx) >> 4;                                    /* the modulator, Q15 x Q8 >> 4 */
        x = ((hy_saw(ph0 + (uint32_t)(soff * 104), incA) + hy_saw(ph1, incB)) >> 1) * R->saw >> 7;
        if (R->sq) x += ((ph0 < 0x80000000u ? 19660 : -19660) * R->sq) >> 7;
        if (R->fm) x = ((x * (256 - R->fm)) >> 8) + ((sine_i(ph0 + (uint32_t)soff * 1304u) * 22938 >> 15) * R->fm >> 7);
        x = ladder_tick(&lad, x, ls);
        if (form) {
            int32_t b1 = wob_bp(&c1, x >> 1, &f1a, &f1b), b2 = wob_bp(&c2, x >> 1, &f2a, &f2b);
            x = mulq15(x, 32767 - ((form * 19661) >> 15)) + mulq15((b1 * 3) + ((b2 * 9) >> 2), form);
        }
        if (fold)                                    /* the wavefolder: sin(x (1 + 4 fold) 1.5), with the dry */
            x = (sine_i((uint32_t)((int64_t)x * (31292 + fold * 985))) * 26214 >> 15) + (x * 6553 >> 15);
        y = softclip((x * drive) >> 8);
        out[i] += mulq15(mulq15(y, amp_at(m, i)), VOICE_FS);
        ph0 += incA; ph1 += incB; ph2 += incM;
    }
    v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2;
    v->s[0] = ls[0], v->s[1] = ls[1], v->s[2] = ls[2], v->s[3] = ls[3];
    v->s[4] = f1a, v->s[5] = f1b, v->s[6] = f2a, v->s[7] = f2b;
}

static const preset_t WOB_PRESETS[] = {
    /* {TYPE, LO, HI, RES, DET, GRWL, FOLD, DRV}, {A D S R}, ENV -> FILTER 0, mono; glide (the riff's slides) */
    {"CLASSIC", {0, 26, 97, 76, 32, 0, 0, 19}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 0, 6), XP(P_GLMODE + 1, 1, P_GLIDE + 1, 20)},
    {"GROWL", {1, 45, 100, 44, 24, 108, 25, 64}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 0, 6), XP(P_GLMODE + 1, 1, P_GLIDE + 1, 20)},
    {"RIDDIM", {2, 31, 89, 91, 16, 38, 0, 96}, {0, 0, 127, 6}, 0, 1, FX(0, 0, 0, 4), XP(P_GLMODE + 1, 1, P_GLIDE + 1, 10)},
    {"YOI", {3, 51, 104, 38, 56, 127, 13, 32}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 0, 6), XP(P_GLMODE + 1, 1, P_GLIDE + 1, 20)},
    {"TEAR", {4, 64, 110, 32, 40, 51, 76, 64}, {0, 0, 127, 8}, 0, 1, FX(0, 0, 0, 6), XP(P_GLMODE + 1, 1, P_GLIDE + 1, 20)},
    {"REESE", {5, 36, 84, 25, 96, 0, 0, 16}, {0, 0, 127, 12}, 0, 1, FX(0, 8, 0, 8), XP(P_GLMODE + 1, 1, P_GLIDE + 1, 30)},
};

static const engine_t ENG_WOB = {
    "WOB", {"WOB", "TONE"},
    {
        {"TYPE", F_ENUM, 0, 5, 0, N_WOB_TYPE, 0},
        {"LO", F_CUTOFF, 0, 127, 26, 0, 0},
        {"HI", F_CUTOFF, 0, 127, 97, 0, 0},
        {"RES", F_PCT, 0, 127, 76, 0, 0},
        {"DET", F_PCT, 0, 127, 32, 0, 0},
        {"GRWL", F_PCT, 0, 127, 0, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"DRV", F_PCT, 0, 127, 19, 0, 0},
    },
    WOB_PRESETS, sizeof(WOB_PRESETS) / sizeof(WOB_PRESETS[0]), 0, wob_note_on, wob_render,
    0xF81F, {P_E1, P_E2, P_E5, P_E7}, 1,
};

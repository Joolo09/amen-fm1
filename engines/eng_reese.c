/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN-FM1: REESE, a drum & bass bass engine. */
/* Two detuned oscillators beating against each other (the reese), a sine sub under them, drive into a resonant
 * low-pass, and MOVE: a slow LFO that sweeps the detune and the cutoff so the bass breathes and phases.
 *
 *   DTUN   the detune, 0 .. +-50 cents between the two oscillators (the speed of the beating)
 *   SUB    the sine sub's level;  SUBO its octave under the note (-1 / -2)
 *   CUT, RESO   the low-pass (+ the track's filter envelope and modulation, m->cutoff)
 *   DRIVE  gain into a soft clip before the filter: growl
 *   MOVE   the LFO: 0 = still, up to ~3 Hz; it also sets how far it moves the detune and the cutoff
 *   WAVE   REESE (two saws), HOOVER (a pulse whose width the LFO moves and two saws), NEURO (the difference of the
 *          two saws, clipped: hollow, nasal, the neurofunk tone), SQUARE (two squares: darker, hollow)
 *
 * The host's ADSR shapes the amplitude. Fixed point only, 32-bit products only, no library calls.
 *
 * Voice state: ph[0], ph[1] the two oscillators, ph[2] the sub; s[0], s[1] the filter, s[4] HOOVER's third
 * oscillator, s[5] the LFO's phase (kept over a retrigger: a legato bass keeps moving). */

enum { RSE_REESE, RSE_HOOVER, RSE_NEURO, RSE_SQUARE };

static void rse_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = v->s[1] = 0;
    v->s[4] = (int32_t)(v->age * 2654435761u);     /* the third oscillator starts out of phase */
}

static void rse_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    tsvf_t flt;
    uint32_t i, wave = (uint32_t)clamp(p[P_E6], 0, 3), sub_shift = 1u + (uint32_t)clamp(p[P_E7], 0, 1);
    int32_t move = clamp(p[P_E5], 0, 127);
    uint32_t lph = (uint32_t)v->s[5] + 155841u + (uint32_t)move * 73625u;   /* 0.05 .. 3 Hz a block */
    int32_t lfo = sine_i(lph);                                                /* Q15 */
    int32_t depth = mulq15(lfo, move * 258);                                  /* Q15, 0 at MOVE 0 */
    int32_t ct = clamp(p[P_E0], 0, 127) * 50 / 127;
    int32_t cmod = ct + mulq15(ct, depth >> 1);                               /* +-50 % of the detune */
    uint32_t inc1 = cents_inc(m->pitch16, cmod, m->fine), inc2 = cents_inc(m->pitch16, -cmod, m->fine);
    uint32_t inc3 = m->inc, incs = m->inc >> sub_shift;
    uint32_t pw = 0x80000000u + (uint32_t)(depth * 49152);                    /* HOOVER: 50 % +- 37 % */
    int32_t sub = clamp(p[P_E1], 0, 127) * 258, drive = 4096 + clamp(p[P_E4], 0, 127) * 96;
    int32_t cut = (clamp(p[P_E2], 0, 127) << 8) + m->cutoff + (depth * clamp(p[P_E2], 0, 127) >> 11);
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], ph3 = (uint32_t)v->s[4];
    tsvf_coef(&flt, cut, clamp(p[P_E3], 0, 127));
    for (i = 0; i < n; i++) {
        int32_t a, b, x, y;
        switch (wave) {
        case RSE_HOOVER:
            a = osc_pulse(ph0, inc1, pw);
            b = osc_saw(ph1, inc2);
            x = (a * 2 + b + osc_saw(ph3, inc3)) * 8192 >> 15;
            break;
        case RSE_NEURO:
            x = softclip(osc_saw(ph0, inc1) - osc_saw(ph1, inc2));
            break;
        case RSE_SQUARE:
            x = (osc_pulse(ph0, inc1, 0x80000000u) + osc_pulse(ph1, inc2, 0x80000000u)) >> 1;
            break;
        default:
            x = (osc_saw(ph0, inc1) + osc_saw(ph1, inc2)) >> 1;
            break;
        }
        x = softclip(x * drive >> 12);
        x = tsvf_lp(&flt, x, &ic1, &ic2);
        y = softclip(x + mulq15(sine_i(ph2), sub));
        out[i] += voice_amp(y, m, i);
        ph0 += inc1;
        ph1 += inc2;
        ph2 += incs;
        ph3 += inc3 + (inc3 >> 9);                  /* the third oscillator: +3.4 cents */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[4] = (int32_t)ph3;
    v->s[5] = (int32_t)lph;
}

static const char *const N_RSE_WAVE[] = {"REESE", "HOOVER", "NEURO", "SQUARE"};
static const char *const N_RSE_SUBO[] = {"-1", "-2"};

static const preset_t REESE_PRESETS[] = {
    /* name, {DTUN SUB CUT RESO DRIVE MOVE WAVE SUBO}, {A D S R}, fenv, mono, FX(dist, chorus, delay, reverb) */
    {"REESE", {40, 70, 62, 20, 30, 30, RSE_REESE, 0}, {0, 90, 127, 24}, 0, 1, FX(0, 10, 0, 4), PAT(0)},
    {"DEEP", {22, 110, 46, 8, 8, 14, RSE_REESE, 0}, {2, 90, 127, 30}, 0, 1, FX(0, 0, 0, 0), PAT(0)},
    {"ROLLER", {34, 90, 54, 30, 40, 0, RSE_REESE, 0}, {0, 40, 70, 16}, 24, 1, FX(0, 0, 0, 0), PAT(0)},
    {"NEURO", {70, 40, 80, 70, 90, 60, RSE_NEURO, 0}, {0, 90, 127, 20}, 10, 1, FX(10, 0, 0, 4), PAT(0)},
    {"TECHSTP", {60, 50, 72, 54, 110, 22, RSE_NEURO, 1}, {0, 70, 110, 16}, 18, 1, FX(20, 0, 0, 0), PAT(0)},
    {"HOOVER", {90, 30, 96, 30, 50, 50, RSE_HOOVER, 0}, {4, 90, 127, 40}, 0, 1, FX(0, 30, 10, 20), PAT(0)},
    {"WOBBLE", {50, 60, 56, 84, 60, 112, RSE_REESE, 0}, {0, 90, 127, 24}, 0, 1, FX(0, 0, 0, 4), PAT(0)},
    {"SUB", {6, 127, 30, 0, 20, 0, RSE_SQUARE, 0}, {0, 90, 127, 30}, 0, 1, FX(0, 0, 0, 0), PAT(0)},
    {"JUNGLE", {14, 120, 40, 10, 50, 6, RSE_SQUARE, 1}, {0, 100, 90, 40}, 0, 1, FX(0, 0, 6, 6), PAT(0)},
};

static const engine_t ENG_REESE = {
    .name = "REESE",
    .page_title = {"OSC", "TONE"},
    .edit = {
        {"DTUN", F_PCT, 0, 127, 40, 0, 0},
        {"SUB", F_PCT, 0, 127, 70, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 62, 0, 0},
        {"RESO", F_PCT, 0, 127, 20, 0, 0},
        {"DRIV", F_PCT, 0, 127, 30, 0, 0},
        {"MOVE", F_PCT, 0, 127, 30, 0, 0},
        {"WAVE", F_ENUM, 0, 3, 0, N_RSE_WAVE, 0},
        {"SUBO", F_ENUM, 0, 1, 0, N_RSE_SUBO, 0},
    },
    .presets = REESE_PRESETS,
    .npresets = NELEM(REESE_PRESETS),
    .knob = {P_E0, P_E2, P_E4, P_E5},
    .keep = (1u << 0) | (1u << 1) | (1u << 4) | (1u << 5),
    .note_on = rse_note_on,
    .render = rse_render,
};

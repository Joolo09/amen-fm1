/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN-FM1: JUNGLE, a chopped-break engine for jungle, drum & bass and breakcore. */
/* A break-slicer that needs no samples: eight built-in two-bar breaks (jungle_data.h, tools/gen_breaks.py), each
 * cut into 32 sixteenths. A key plays one slice: the hits of that sixteenth of the break (kick, snare, ghost
 * snare, closed / open hat, ride, crash), synthesised as it starts. The lowest key is slice 0 (+ START), the next
 * key the next slice, as on a sampler's chop map, so the sequencer re-arranges the break step by step.
 *
 * Tails: a slice also holds what still rings from the sixteenths before it, as a cut of a recording does. The hits of
 * the nearest earlier sixteenth that has any (up to three back) carry on in it, their time moved on by that distance,
 * unless the slice hits the same drum again. So an empty sixteenth plays the tail of the kick before it instead of
 * silence, and a hat after a kick keeps the kick's boom under it.
 *
 * It plays like a sampled break, not like a drum machine:
 *   PITCH  speeds the whole slice up or down (a sampler's varispeed: +5 st is the classic jungle pitch-up). Every
 *          tone, the kick's sweep and every decay move together.
 *   MODE   CHOKE (the default): a new slice cuts the one before in 2 ms, the way one sampler voice plays a chopped
 *          break. GATE: CHOKE, and a slice also stops at its note-off (the sequencer's gate cuts it: stabs).
 *          POLY: slices ring over each other.
 *   REV    plays slices backwards: the sixteenth from its end to its hit (the swell into the hit, cut on the hit),
 *          ON, or at random on 25 % / 50 % of the notes (breakcore). A reversed slice is a sixteenth of the break at
 *          170 BPM long (x DECAY, / the PITCH speed), so it fits the step it is played on.
 *   CRUSH  drive into a soft clip, a lower sample rate (sample-and-hold, down to ~5.5 kHz) and fewer bits (down to 5):
 *          SP-1200 / early Akai grit at mid settings, broken digital at the top.
 *   DECAY  every hit's length, 0.25x .. 2.2x (a slice lasts at most ~0.9 s of break time, fading out over its last
 *          46 ms: a ride or crash rings no longer, and the part's voices free soon).   TONE  a low-pass over the slice (127 = open).
 *
 * The engine renders its own envelopes (ownenv): a slice ends when its hits have died away (done()), or when it
 * is choked. Velocity sets its level; accented eighths (the even steps) play their hats and rides louder.
 *
 * Fixed point only, 32-bit products only, no library calls (the FM-1 plugin rules).
 *
 * Voice state:
 *   ph[0]  the slice's time in samples, Q8, advanced by the speed (so the time is "break time")
 *   ph[1]  the kick's sine phase
 *   ph[2]  the hit mask (bits 0..7) | REV (bit 8) | choked (bit 9) | the tails' distance in sixteenths (bits 10..11)
 *          | the tails' hits (bits 12..18) | the slice's length in blocks of 32 samples (bits 19..31)
 *   s[0]   noise seed;  s[1], s[2] the last two noise samples (the hat and snare filters)
 *   s[3], s[4]  TONE's filter;  s[5] CRUSH's held sample << 8 | its counter;  s[6] the metal's low-pass
 *   s[7]   the choke gain, Q15 */
#include "jungle_data.h"
#ifndef JNG_DRUMS
#define JNG_DRUMS 1                  /* 1: the hits are recordings of a real kit (jungle_drums.h, tools/gen_drums.py, CC0
                                      * VCSL, 23 KB); 0: synthesised (the 8 KiB plugin, plugins/jungle.c) */
#endif
#if JNG_DRUMS
#include "jungle_drums.h"
#endif

enum { JK = 1, JS = 2, JG = 4, JH = 8, JO = 16, JR = 32, JC = 64, JA = 128 };
enum { JNG_REV_OFF, JNG_REV_ON, JNG_REV_R25, JNG_REV_R50 };
enum { JNG_POLY, JNG_CHOKE, JNG_GATE };
#define JNG_BASE 60                  /* the note of slice 0 (the lowest key, see jng_keys) */
#define JNG_F_REV (1u << 8)
#define JNG_F_CHOKED (1u << 9)
#define JNG_OFF_SHIFT 10
#define JNG_TAIL_SHIFT 12
#define JNG_LEN_SHIFT 19             /* the length in blocks of 32 samples */
#define JNG_LEN_MAX 40000u           /* a slice lasts at most ~0.9 s of break time (a voice frees soon) */
#define JNG_TAIL_BACK 3u             /* tails come from up to three sixteenths back */
#define JNG_FADE 2048u               /* a forward slice fades out over its last 46 ms */
#ifndef JNG_SIXTEENTH
#define JNG_SIXTEENTH 3891u          /* a sixteenth of break time: 170 BPM at PTCH 0 (a host may give its tempo's) */
#endif
#ifndef JNG_SPEED
#define JNG_SPEED(sp) (sp)           /* a host's speed on top of PTCH, Q12 in Q12 out (AMEN OS: TAPE STOP) */
#endif
#define JNG_CHOKE_STEP 372           /* the choke fade: Q15 a sample, ~2 ms */

/* the components and their time constants at DECAY 48 (samples at 44.1 kHz) */
enum { C_KBODY, C_KPITCH, C_KCLICK, C_STONE, C_SNOISE, C_HAT, C_OHAT, C_RIDE, C_CRASH, C_N };
static const uint16_t JNG_TAU[C_N] = {
    5300,   /* kick body: ~120 ms */
    520,    /* kick pitch sweep */
    70,     /* kick click */
    2300,   /* snare tone */
    5400,   /* snare noise: ~120 ms */
    1300,   /* closed hat */
    11000,  /* open hat */
    20000,  /* ride: ~450 ms */
    32000,  /* crash: ~730 ms */
};

/* phase increments a sample, divided by 256 (the time is Q8): f * 2^32 / 44100 / 256 */
#define JNG_F8(hz) ((uint32_t)(hz) * 3804u / 10u)
static const uint32_t JNG_METAL[6] = {   /* a cymbal's six square partials (inharmonic) */
    JNG_F8(410), JNG_F8(608), JNG_F8(739), JNG_F8(1045), JNG_F8(1080), JNG_F8(1600),
};

typedef struct {
    uint32_t r[C_N];      /* envelope rates: table index Q8 a sample * 16 */
    uint32_t tmax[C_N];   /* past this time the component is silent */
} jng_rates_t;

/* the rates at DECAY dec (0..127): time constants x (16 + dec) / 64; a ghost snare's are shorter */
static void jng_rates(jng_rates_t *q, int32_t dec, int ghost)
{
    uint32_t c, dk = (uint32_t)clamp(dec, 0, 127) + 16u;
    for (c = 0; c < C_N; c++) {
        uint32_t tau = (uint32_t)JNG_TAU[c] * dk >> 6;
        if (ghost && (c == C_STONE || c == C_SNOISE))
            tau = tau * 9u >> 4;
        if (tau < 8u)
            tau = 8u;
        q->r[c] = 2097152u / tau;               /* 32 table points a time constant, Q16 */
        q->tmax[c] = tau * 8u;                  /* the table's end: exp(-8) */
    }
}

/* the envelope of component c at time t (samples): exp(-t / tau), Q15 */
static inline int32_t jng_env(const jng_rates_t *q, uint32_t c, uint32_t t)
{
    uint32_t r = q->r[c], x, i;
    if (t >= q->tmax[c])
        return 0;
    x = ((t >> 4) * r >> 4) + ((t & 15u) * r >> 8);   /* the table index, Q8 (t < 8 tau: no overflow) */
    i = x >> 8;
    if (i >= 256u)
        return 0;
    return JNG_EXP[i] + (((JNG_EXP[i + 1] - JNG_EXP[i]) * (int32_t)(x & 255u)) >> 8);
}

/* the slice of a note: the lowest key (JNG_BASE) is slice 0, + START, around the 32 */
static uint32_t jng_slice(const track_t *t, uint32_t note)
{
    return (note + 4u * JNG_STEPS - JNG_BASE + (uint32_t)clamp(t->p[P_E1], 0, JNG_STEPS - 1)) & (JNG_STEPS - 1u);
}

/* the longest time constant among the hits of mask (at DECAY dec), in samples */
static uint32_t jng_tau_max(uint32_t mask, int32_t dec)
{
    uint32_t tau = 0, dk = (uint32_t)clamp(dec, 0, 127) + 16u;
    if (mask & JK) tau = JNG_TAU[C_KBODY];
    if ((mask & (JS | JG)) && JNG_TAU[C_SNOISE] > tau) tau = JNG_TAU[C_SNOISE];
    if ((mask & JH) && JNG_TAU[C_HAT] > tau) tau = JNG_TAU[C_HAT];
    if ((mask & JO) && JNG_TAU[C_OHAT] > tau) tau = JNG_TAU[C_OHAT];
    if ((mask & JR) && JNG_TAU[C_RIDE] > tau) tau = JNG_TAU[C_RIDE];
    if ((mask & JC) && JNG_TAU[C_CRASH] > tau) tau = JNG_TAU[C_CRASH];
    return tau * dk >> 6;
}

#if JNG_DRUMS
/* the length of drum d at DECAY dec in break time (44.1 kHz samples): the recording's own at DECAY 48 and up, shorter
 * below (x (16 + dec) / 64) */
static uint32_t jng_dlen(uint32_t d, int32_t dec)
{
    uint32_t n = ((uint32_t)JNG_DRUM[d].n << 16) / JNG_DRUM[d].r16, dk = (uint32_t)clamp(dec, 0, 127) + 16u;
    return dk < 64u ? n * dk >> 6 : n;
}
/* drum d at break time t8 (Q8 samples at 44.1 kHz; slow: r16 x 3 / 4), its level x lv (Q15), faded out over the last
 * 3/8 of its length L (DECAY; fg = 2^30 / that fade's length); 0 past it. Linear between the codes, so a slowed slice
 * stays smooth */
static int32_t jng_drum(uint32_t d, uint32_t t8, int32_t lv, uint32_t L, uint32_t fg, int slow)
{
    const jng_drum_t *k = &JNG_DRUM[d];
    uint32_t r = slow ? (uint32_t)k->r16 * 3u >> 2 : k->r16, t = t8 >> 8, pos, i;
    int32_t a, b, y;
    if (t >= L)
        return 0;
    pos = t * r + (((t8 & 255u) * r) >> 8);           /* Q16 in the recording (t < 40000 x 4/3: no overflow) */
    i = pos >> 16;
    if (i + 1u >= k->n)
        return 0;
    a = JNG_ULAW[k->d[i]];
    b = JNG_ULAW[k->d[i + 1u]];
    y = a + (((b - a) * (int32_t)((pos & 0xFFFFu) >> 1)) >> 15);
    if (t > L - (L * 3u >> 3))                         /* the fade: the last 3/8, (L - t) / fade in Q15 */
        y = mulq15(y, (int32_t)((L - t) * fg >> 15));
    return mulq15(y, mulq15(k->level, lv));
}
#endif

#if JNG_DRUMS
/* the longest of the drums of mask at DECAY dec (the crash: the ride slowed to 3/4, so 4/3 its length) */
static uint32_t jng_hits_len(uint32_t mask, int32_t dec)
{
    uint32_t l = 0, x;
    if ((mask & JK) && (x = jng_dlen(JD_KICK, dec)) > l) l = x;
    if ((mask & JS) && (x = jng_dlen(JD_SNARE, dec)) > l) l = x;
    if ((mask & JG) && (x = jng_dlen(JD_GHOST, dec)) > l) l = x;
    if ((mask & JH) && (x = jng_dlen(JD_HAT, dec)) > l) l = x;
    if ((mask & JO) && (x = jng_dlen(JD_OHAT, dec)) > l) l = x;
    if ((mask & JR) && (x = jng_dlen(JD_RIDE, dec)) > l) l = x;
    if ((mask & JC) && (x = jng_dlen(JD_RIDE, dec) * 4u / 3u) > l) l = x;
    return l;
}
#endif

static void jng_note_on(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    const uint8_t *brk = JNG_BRK[clamp(p[P_E0], 0, JNG_NBRK - 1)];
    uint32_t i, sl = jng_slice(t, v->note), mask = brk[sl], tail = 0, off = 0;
    uint32_t rnd = (v->age * 2654435761u) >> 30, rev = 0, len, tau;
    switch (clamp(p[P_E5], 0, 3)) {
    case JNG_REV_ON: rev = 1; break;
    case JNG_REV_R25: rev = rnd == 0; break;
    case JNG_REV_R50: rev = rnd < 2; break;
    default: break;
    }
    for (i = 1; i <= JNG_TAIL_BACK; i++) {          /* the tails: the nearest earlier sixteenth with hits */
        uint32_t prev = brk[(sl - i) & (JNG_STEPS - 1u)] & 127u;
        if (prev) {
            tail = prev & ~mask & 127u;
            off = i;
            break;
        }
    }
    if (!tail)
        off = 0;
    if (!(mask & 127u) && !tail)
        mask = 0;                                  /* nothing hits or rings: silence (done at once) */
#if JNG_DRUMS
    tau = jng_hits_len(mask & 127u, p[P_E3]);       /* forward: until its drums have rung out, the tails' too */
    {
        uint32_t tl = jng_hits_len(tail, p[P_E3]), o = off * JNG_SIXTEENTH;
        if (tl > o && tl - o > tau)
            tau = tl - o;
    }
#else
    tau = jng_tau_max(mask | tail, p[P_E3]) * 5u;   /* forward: until the hits are down 43 dB */
#endif
    if (rev)                                       /* reversed: the sixteenth played from its end to its hit */
        len = JNG_SIXTEENTH * ((uint32_t)clamp(p[P_E3], 0, 127) + 16u) >> 6;
    else
        len = tau;
    if (len > JNG_LEN_MAX)
        len = JNG_LEN_MAX;
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = mask | (rev ? JNG_F_REV : 0u) | (off << JNG_OFF_SHIFT) | (tail << JNG_TAIL_SHIFT) |
               (((len + 31u) >> 5) << JNG_LEN_SHIFT);
    v->s[0] = (int32_t)(v->age * 1103515245u + 12345u) | 1;
    v->s[1] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = v->s[6] = 0;
    v->s[7] = 32767;
    if (clamp(p[P_E6], 0, 2) != JNG_POLY)          /* CHOKE / GATE: the slices before fade out */
        for (i = 0; i < NVOICE; i++) {
            voice_t *u = &t->v[i];
            if (u != v && u->active)
                u->ph[2] |= JNG_F_CHOKED;
        }
}

static void jng_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
#if JNG_DRUMS
    uint32_t dl[JD_N + 1], fg[JD_N + 1];            /* (JD_N: the crash) */
#else
    jng_rates_t q;
#endif
    tsvf_t flt;
    uint32_t i, flags = v->ph[2], mask = flags & 255u, rev = flags & JNG_F_REV;
    uint32_t tail = (flags >> JNG_TAIL_SHIFT) & 127u, off = ((flags >> JNG_OFF_SHIFT) & 3u) * JNG_SIXTEENTH;
    uint32_t all = (mask | tail) & 127u, sm = (mask & (JS | JG)) ? mask : tail;   /* the snare's own or a tail's */
    uint32_t len = (flags >> JNG_LEN_SHIFT) << 5, sp = JNG_SPD[clamp(p[P_E2], -12, 12) + 12];
    uint32_t step, tq = v->ph[0], kph = v->ph[1];
    int32_t crush = clamp(p[P_E4], 0, 127), tone = clamp(p[P_E7], 0, 127);
    int32_t drive = 4096 + crush * 40, hold = 1 + (crush >> 4), drop = crush / 12;
    int32_t hn = v->s[5] >> 8, hc = v->s[5] & 255, mlp = v->s[6], gain = v->s[7];
    int32_t n1 = v->s[1], n2 = v->s[2], ic1 = v->s[3], ic2 = v->s[4];
    int32_t vel = v->vel * 258, acc = (mask & JA) ? 32767 : 19660;
#if !JNG_DRUMS
    uint32_t k;
    int32_t sn = (sm & JS) ? 32767 : 9830;         /* a ghost snare: -10 dB */
#endif
    sp = JNG_SPEED(sp);
    step = sp >> 4;
    if (!all)
        return;
    if (clamp(p[P_E6], 0, 2) == JNG_GATE && !v->gate)
        flags |= JNG_F_CHOKED;                      /* GATE: the note-off cuts the slice */
#if JNG_DRUMS
    {
        uint32_t d;
        for (d = 0; d <= JD_N; d++) {               /* each drum's length at this DECAY, its fade's reciprocal */
            dl[d] = d < JD_N ? jng_dlen(d, p[P_E3]) : jng_dlen(JD_RIDE, p[P_E3]) * 4u / 3u;
            fg[d] = (1u << 30) / ((dl[d] * 3u >> 3) | 1u);
        }
    }
#else
    jng_rates(&q, p[P_E3], !(sm & JS));
#endif
    if (tone < 127)
        tsvf_coef(&flt, tone << 8, 0);
    for (i = 0; i < n; i++) {
        uint32_t tt = tq >> 8;
        int32_t y = 0;
#if JNG_DRUMS
        {   /* the kit: each hit its recording at its own time (a tail's moved on by its distance) */
            uint32_t te8 = rev ? (tq < (len << 8) ? (len << 8) - tq : 0u) : tq, to8 = te8 + (off << 8);
#define JNG_T8(bits) ((mask & (bits)) ? te8 : to8)
            if (all & JK)
                y += jng_drum(JD_KICK, JNG_T8(JK), 32767, dl[JD_KICK], fg[JD_KICK], 0);
            if (all & (JS | JG)) {
                uint32_t d = (sm & JS) ? JD_SNARE : JD_GHOST;
                y += jng_drum(d, JNG_T8(JS | JG), 32767, dl[d], fg[d], 0);
            }
            if (all & JH)
                y += jng_drum(JD_HAT, JNG_T8(JH), acc, dl[JD_HAT], fg[JD_HAT], 0);
            if (all & JO)
                y += jng_drum(JD_OHAT, JNG_T8(JO), acc, dl[JD_OHAT], fg[JD_OHAT], 0);
            if (all & JR)
                y += jng_drum(JD_RIDE, JNG_T8(JR), acc, dl[JD_RIDE], fg[JD_RIDE], 0);
            if (all & JC)                            /* the crash: the ride slower, louder */
                y += jng_drum(JD_RIDE, JNG_T8(JC), 32767, dl[JD_N], fg[JD_N], 1);
#undef JNG_T8
        }
#else
        uint32_t te = tt, to;
        int32_t nz, d1, d2;
        if (rev)
            te = tt < len ? len - tt : 0;
        to = te + off;                               /* the tails' time */
#define JNG_T(bits) ((mask & (bits)) ? te : to)
        nz = (int32_t)noise32(&v->s[0]) >> 17;      /* +-16384 */
        d1 = nz - n1;                                /* bright */
        d2 = (nz - 2 * n1 + n2) >> 1;                /* brighter: hats */
        n2 = n1;
        n1 = nz;
        if (all & JK) {                              /* kick: a sine swept 200 -> 48 Hz, a click */
            uint32_t tk = JNG_T(JK);
            int32_t pe = jng_env(&q, C_KPITCH, tk);
            uint32_t inc = 4674797u + (uint32_t)pe * 446u;
            y += softclip(mulq15(sine_i(kph), jng_env(&q, C_KBODY, tk)) * 3 >> 1);
            y += mulq15(nz, jng_env(&q, C_KCLICK, tk));
            kph += (inc >> 12) * sp;
        }
        if (all & (JS | JG)) {                       /* snare: two drum-head tones and the wires */
            uint32_t ts = JNG_T(JS | JG);
            int32_t tone_ = (sine_i((uint32_t)mul_wrap((int32_t)tq, JNG_F8(185))) * 3 +
                             sine_i((uint32_t)mul_wrap((int32_t)tq, JNG_F8(330))) * 2) * 6554 >> 15;   /* / 5 */
            int32_t wires = mulq15(d1, jng_env(&q, C_SNOISE, ts));
            int32_t sy = mulq15(mulq15(tone_, jng_env(&q, C_STONE, ts)), 14746) +            /* x 0.45 */
                         wires + mulq15(wires, 9830);                                         /* x 1.3 */
            y += mulq15(sy, sn);
        }
        if (all & (JR | JC)) {                      /* ride and crash: six squares, high-passed, and sizzle */
            int32_t metal = 0, hp;
            for (k = 0; k < 6; k++)
                metal += (int32_t)(mul_wrap((int32_t)tq, (int32_t)JNG_METAL[k]) >> 31 | 1) * 5461;
            mlp += (metal - mlp) >> 2;
            hp = metal - mlp;
            if (all & JR)
                y += mulq15(mulq15(hp, 11000) + mulq15(d2, 6000), mulq15(jng_env(&q, C_RIDE, JNG_T(JR)), acc));
            if (all & JC)
                y += mulq15(mulq15(hp, 9000) + mulq15(d2, 16000), jng_env(&q, C_CRASH, JNG_T(JC)));
        }
        if (all & JH)                                /* closed hat */
            y += mulq15(mulq15(d2, 14000), mulq15(jng_env(&q, C_HAT, JNG_T(JH)), acc));
        if (all & JO)                                /* open hat */
            y += mulq15(mulq15(d2, 13000), mulq15(jng_env(&q, C_OHAT, JNG_T(JO)), acc));
#undef JNG_T
#endif
        y = softclip(y * drive >> 12);               /* the drive (CRUSH) and a ceiling */
        if (crush) {                                 /* fewer samples, fewer bits */
            if (--hc <= 0) {
                hc = hold;
                hn = shl(y >> drop, (uint32_t)drop);
            }
            y = hn;
        }
        if (tone < 127)
            y = tsvf_lp(&flt, y, &ic1, &ic2);
        if (!rev && tt + JNG_FADE > len)             /* the end of a forward slice: no click at the cut */
            y = tt < len ? y * (int32_t)(len - tt) >> 11 : 0;
        if (flags & JNG_F_CHOKED) {
            gain -= JNG_CHOKE_STEP;
            if (gain < 0)
                gain = 0;
        }
        y = mulq15(mulq15(y, gain), vel);
        out[i] += voice_amp(y, m, i);
        tq += step;
    }
    v->ph[0] = tq;
    v->ph[1] = kph;
    v->ph[2] = flags;
    v->s[1] = n1;
    v->s[2] = n2;
    v->s[3] = ic1;
    v->s[4] = ic2;
    v->s[5] = (int32_t)((uint32_t)hn << 8) | (hc & 255);
    v->s[6] = mlp;
    v->s[7] = gain;
}

/* the slice is over: its hits have died away, it was choked to silence, or it had none */
static int jng_done(track_t *t, voice_t *v)
{
    uint32_t flags = v->ph[2];
    (void)t;
    if (!(flags & 127u) && !((flags >> JNG_TAIL_SHIFT) & 127u))
        return 1;
    if ((flags & JNG_F_CHOKED) && v->s[7] <= 0)
        return 1;
    return (v->ph[0] >> 8) >= (flags >> JNG_LEN_SHIFT) << 5;
}

/* the keys: chromatic from slice 0, no scale (a chop map) */
static int32_t jng_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(JNG_BASE + (int32_t)k, 0, 127);
}

static const char *const N_JNG_REV[] = {"OFF", "ON", "R25%", "R50%"};
static const char *const N_JNG_MODE[] = {"POLY", "CHOKE", "GATE"};

static const preset_t JUNGLE_PRESETS[] = {
    /* name, {BRK START PTCH DCAY CRSH REV MODE TONE}, {A D S R}, fenv, mono, FX(dist, chorus, delay, reverb) */
    {"AMEN", {0, 0, 0, 48, 0, 0, JNG_CHOKE, 127}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 10), PAT(0)},
    {"AMEN+5", {0, 0, 5, 44, 10, 0, JNG_CHOKE, 127}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 10), PAT(0)},
    {"SP1200", {0, 0, 3, 48, 72, 0, JNG_CHOKE, 104}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 8), PAT(0)},
    {"THINK", {1, 0, 2, 46, 24, 0, JNG_CHOKE, 120}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 10), PAT(0)},
    {"FUNKY", {2, 0, 1, 50, 30, 0, JNG_CHOKE, 116}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 12), PAT(0)},
    {"APACHE", {3, 0, 4, 52, 40, 0, JNG_CHOKE, 110}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 14), PAT(0)},
    {"ROLLER", {4, 0, 0, 40, 0, 0, JNG_CHOKE, 127}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 8), PAT(0)},
    {"HALFTME", {5, 0, -2, 64, 20, 0, JNG_CHOKE, 118}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 10, 20), PAT(0)},
    {"RAGGA", {7, 0, 3, 46, 30, 0, JNG_CHOKE, 120}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 8, 12), PAT(0)},
    {"REVERSE", {0, 0, 0, 56, 0, JNG_REV_ON, JNG_CHOKE, 127}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 24), PAT(0)},
    {"BRKCORE", {6, 0, 7, 30, 96, JNG_REV_R25, JNG_GATE, 127}, {0, 127, 127, 20}, 0, 0, FX(20, 0, 0, 6), PAT(0)},
    {"GABBER", {6, 0, -5, 80, 118, 0, JNG_CHOKE, 96}, {0, 127, 127, 20}, 0, 0, FX(60, 0, 0, 4), PAT(0)},
    {"STABS", {0, 0, 2, 36, 50, 0, JNG_GATE, 127}, {0, 127, 127, 20}, 0, 0, FX(0, 0, 0, 10), PAT(0)},
    {"WASH", {0, 0, -3, 110, 0, JNG_REV_R50, JNG_POLY, 90}, {0, 127, 127, 20}, 0, 0, FX(0, 20, 30, 50), PAT(0)},
};

static const engine_t ENG_JUNGLE = {
    .name = "JUNGLE",
    .page_title = {"BREAK", "CHOP"},
    .edit = {
        {"BRK", F_ENUM, 0, JNG_NBRK - 1, 0, JNG_BRK_NAMES, 0},
        {"START", F_INT, 0, JNG_STEPS - 1, 0, 0, 0},
        {"PTCH", F_SEMI, -12, 12, 0, 0, 0},
        {"DCAY", F_PCT, 0, 127, 48, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},
        {"REV", F_ENUM, 0, 3, 0, N_JNG_REV, 0},
        {"MODE", F_ENUM, 0, 2, JNG_CHOKE, N_JNG_MODE, 0},
        {"TONE", F_INT, 0, 127, 127, 0, 0},
    },
    .presets = JUNGLE_PRESETS,
    .npresets = NELEM(JUNGLE_PRESETS),
    .knob = {P_E0, P_E2, P_E3, P_E4},
    .sampled = 1,
    .keys = jng_keys,
    .note_on = jng_note_on,
    .render = jng_render,
    .ownenv = 1,
    .done = jng_done,
};

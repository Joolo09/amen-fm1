/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS core: the three tracks' voices, the sequencer, the performance effects, the mix.
 *
 * Tracks: 0 BRK A and 1 BRK B (the JUNGLE engine), 2 BASS (the REESE engine). Each has its sound (an engine's eight
 * parameters, from a preset), a level, a delay send and a mute. The sequencer plays one of eight scenes (A..H): a
 * pattern of up to 32 sixteenths for each track. The audio ISR runs app_audio: events from the keys and MIDI, the
 * sequencer, the voices, the mix. Everything fixed point; 32-bit products only. */

/* ------------------------------------------------------------------------------------------ the engines --- */
#define NVOICE 6
#define NPART 3
enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_DEC, P_SUS, P_COUNT };
typedef struct track track_t;
#include "felucca_engine.h"
struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
};
static track_t trk[NPART];
static uint32_t amen_sixteenth = 3891u;            /* a sixteenth at the tempo, samples (JUNGLE's reverse, its tails) */
static uint32_t amen_speed = 4096u;                /* TAPE STOP: the speed of everything, Q12 */
#define JNG_SIXTEENTH amen_sixteenth
#define JNG_SPEED(sp) (((uint32_t)(sp) * amen_speed) >> 12)
#include "dsp.c"
#include "eng_jungle.c"
#include "eng_reese.c"

enum { T_BRKA, T_BRKB, T_BASS };
static const engine_t *const ENG[NPART] = {&ENG_JUNGLE, &ENG_JUNGLE, &ENG_REESE};
static const char *const TRACK_NAME[NPART] = {"BRK-A", "BRK-B", "BASS"};
#define IS_BRK(p) ((p) < T_BASS)

/* ---------------------------------------------------------------------------------------- the project --- */
#define NSCENE 8
#define NSTEP 32
typedef struct {
    uint8_t on;                  /* 1: the step plays (a BASS tie: on with F_TIE) */
    uint8_t val;                 /* BRK: the slice 0..31; BASS: the MIDI note */
    uint8_t roll;                /* BRK: 1..4 hits in the step */
    uint8_t fx;                  /* BRK: FX_* */
    uint8_t chance;              /* 0: 100 %, 1: 75 %, 2: 50 %, 3: 25 % */
    uint8_t flags;               /* F_ACC | BASS: F_TIE F_SLIDE */
} step_t;
enum { FX_NONE, FX_REV, FX_UP, FX_DN, FX_CUT, FX_N };
#define F_ACC 1u
#define F_TIE 2u
#define F_SLIDE 4u
enum { ST_JUNGLE, ST_ROLLER, ST_HALF, ST_BRKCORE, ST_N };
static const char *const STYLE_NAME[ST_N] = {"JUNGLE", "ROLLER", "HALFTIME", "BRKCORE"};

#define PROJ_MAGIC 0x414D4E31u     /* "AMN1" */
typedef struct {
    uint32_t magic, size;
    uint8_t bpm, swing, len, style;            /* BPM 60..220, swing 0..50 %, 16 / 32 steps, MANGLE's style */
    uint8_t dly_time, dly_fb, filter, scene;   /* DLY 0: 1/8 1: 3/16 2: 1/4; feedback 0..100; DJ filter 0..127 (64 off) */
    uint8_t midich, rsv[3];
    struct {
        int16_t p[P_COUNT];
        uint8_t preset, level, send, mute;     /* level 0..127, delay send 0..127 */
    } snd[NPART];
    step_t pat[NSCENE][NPART][NSTEP];
    uint32_t crc;
} project_t;
static project_t prj;

static uint32_t crc32(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static uint32_t rnd_state = 0x1A2B3C4Du;
static uint32_t rnd(uint32_t n)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return n ? rnd_state % n : 0u;
}

static const engine_t *eng_of(uint32_t part) { return ENG[part % NPART]; }
static uint32_t npresets(uint32_t part) { return eng_of(part)->npresets; }
static void sound_preset(uint32_t part, uint32_t k)
{
    const preset_t *pr = &eng_of(part)->presets[k % npresets(part)];
    uint32_t i;
    prj.snd[part].preset = (uint8_t)(k % npresets(part));
    for (i = 0; i < 8u; i++)
        prj.snd[part].p[P_E0 + i] = pr->e[i];
    prj.snd[part].p[P_ATK] = pr->env[0];
    prj.snd[part].p[P_DEC] = pr->env[1];
    prj.snd[part].p[P_SUS] = pr->env[2];
    prj.snd[part].p[P_REL] = pr->env[3];
}

/* ------------------------------------------------------------------------------------------- the voices --- */
static uint32_t vage;
static int8_t vx_pitch[NPART][NVOICE];             /* a step's UP / DN: semitones for that voice */
static int8_t vx_gate[NPART][NVOICE];              /* a step's CUT: the voice is cut at its note-off */
static int32_t last_amp[NPART][NVOICE];
static int32_t bass_fenv;                          /* the accent's filter envelope, 0..127 << 8 */

/* the engine reads its parameters from trk[part].p: the sound's, with a voice's overrides around each call */
static void track_sync(uint32_t part)
{
    memcpy(trk[part].p, prj.snd[part].p, sizeof trk[part].p);
}

static void voice_start(uint32_t part, uint32_t note, uint32_t vel, uint32_t fx, int legato)
{
    track_t *t = &trk[part];
    voice_t *v = 0;
    uint32_t k, oldest = 0xFFFFFFFFu;
    track_sync(part);
    if (IS_BRK(part)) {
        for (k = 0; k < NVOICE && !v; k++)
            if (!t->v[k].active)
                v = &t->v[k];
        if (!v) {
            for (k = 0; k < NVOICE; k++)
                if (t->v[k].age < oldest) {
                    oldest = t->v[k].age;
                    v = &t->v[k];
                }
        }
        k = (uint32_t)(v - t->v);
        vx_pitch[part][k] = (int8_t)(fx == FX_UP ? 12 : fx == FX_DN ? -12 : 0);
        vx_gate[part][k] = (int8_t)(fx == FX_CUT);
        if (fx == FX_REV)
            t->p[P_E5] = JNG_REV_ON;
        memset(v->ph, 0, sizeof v->ph);
        memset(v->s, 0, sizeof v->s);
        v->note = (uint8_t)note;
        v->vel = (uint8_t)vel;
        v->gate = 1;
        v->active = 1;
        v->stage = 1;
        v->age = ++vage;
        v->env = 1 << 24;
        last_amp[part][k] = 32767;
        ENG[part]->note_on(t, v);
        t->p[P_E5] = prj.snd[part].p[P_E5];
        return;
    }
    /* BASS: one voice, legato on a slide (the pitch glides there, the envelope goes on) */
    v = &t->v[0];
    if (legato && v->active && v->gate) {
        v->note = (uint8_t)note;
        v->pitch16 = (int32_t)note * 16;
        v->gstep = 0;                                  /* glide: voice_tick moves pitch_cur */
        if (vel > 110u)
            bass_fenv = 48 << 8;
        return;
    }
    if (!v->active)
        v->env = 0;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    if (!v->ph[0])
        memset(v->s, 0, sizeof v->s);
    ENG[part]->note_on(t, v);
    bass_fenv = vel > 110u ? 56 << 8 : 16 << 8;
}

static void voice_release(uint32_t part, int note)  /* note -1: every voice */
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++) {
        voice_t *v = &trk[part].v[k];
        if (v->active && v->gate && (note < 0 || v->note == (uint32_t)note)) {
            v->gate = 0;
            if (!IS_BRK(part))
                v->stage = 3;
        }
    }
}

/* the BASS envelope a block: ATK / DEC to SUS / REL from the sound (0..127), Q24 -> Q15 */
static int32_t bass_env(voice_t *v, const int16_t *p)
{
    int32_t a = 1 + p[P_ATK] * p[P_ATK] / 40, d = 2 + p[P_DEC] / 4, r = 2 + p[P_REL] / 4;
    int32_t sus = p[P_SUS] * (1 << 17);                /* 127 -> ~full */
    switch (v->stage) {
    case 1:
        v->env += (1 << 24) / a;
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env -= (v->env - sus) / d;
        break;
    case 3:
        v->env -= v->env / r + 64;
        if (v->env <= 0) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env >> 9;
}

/* 1/16 semitones of a speed s / 64 (TAPE STOP on the bass): 192 log2(s / 64) */
static const int16_t SPEED_P16[64] = {
    -1152, -960, -848, -768, -706, -656, -613, -576, -543, -514, -488, -464, -442, -421, -402, -384, -367, -351, -336,
    -322, -309, -296, -283, -272, -260, -250, -239, -229, -219, -210, -201, -192, -183, -175, -167, -159, -152, -144,
    -137, -130, -123, -117, -110, -104, -98, -91, -86, -80, -74, -68, -63, -58, -52, -47, -42, -37, -32, -27, -23, -18,
    -13, -9, -4, 0};

static void part_render(uint32_t part, int32_t *out)
{
    track_t *t = &trk[part];
    const engine_t *e = ENG[part];
    uint32_t k;
    track_sync(part);
    if (!IS_BRK(part) && bass_fenv > 0)                /* the accent's filter envelope */
        bass_fenv -= bass_fenv / 24 + 1;
    for (k = 0; k < NVOICE; k++) {
        voice_t *v = &t->v[k];
        vmod_t m;
        int32_t a;
        if (!v->active)
            continue;
        memset(&m, 0, sizeof m);
        if (IS_BRK(part)) {
            if (e->done(t, v)) {
                v->active = 0;
                continue;
            }
            t->p[P_E2] = (int16_t)clamp(prj.snd[part].p[P_E2] + vx_pitch[part][k], -12, 12);
            t->p[P_E6] = vx_gate[part][k] ? JNG_GATE : prj.snd[part].p[P_E6];
            a = 32767;
        } else {
            int32_t p16, sp = (int32_t)(amen_speed >> 6);
            if (v->pitch_cur != v->pitch16) {          /* the slide's glide: ~60 ms */
                int32_t d = v->pitch16 - v->pitch_cur, st = d / 6;
                v->pitch_cur += st ? st : d > 0 ? 1 : -1;
            }
            a = mulq15(bass_env(v, t->p), v->vel * 258);
            if (!v->active)
                continue;
            p16 = v->pitch_cur + SPEED_P16[clamp(sp, 1, 64) - 1];
            m.pitch16 = clamp(p16, 0, 2047);
            m.inc = pitch_inc((uint32_t)m.pitch16);
            m.cutoff = bass_fenv;
        }
        m.amp0 = last_amp[part][k];
        m.amp1 = a;
        last_amp[part][k] = a;
        e->render(t, v, out, CTL, &m);
        if (IS_BRK(part)) {
            t->p[P_E2] = prj.snd[part].p[P_E2];
            t->p[P_E6] = prj.snd[part].p[P_E6];
        }
    }
}

/* the audio is late (a platform's ISR measured it): the oldest sounding voice goes, a break's first */
static uint32_t shed_count;
static void audio_shed(void)
{
    uint32_t part, k, best_age = 0xFFFFFFFFu;
    voice_t *best = 0;
    for (part = 0; part < NPART && !best; part++)
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &trk[part].v[k];
            if (v->active && v->age < best_age) {
                best_age = v->age;
                best = v;
            }
        }
    if (best) {
        best->active = 0;
        shed_count++;
    }
}

static int part_sounding(uint32_t part)
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++)
        if (trk[part].v[k].active)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------------- events from the keys and MIDI --- */
enum { EV_ON, EV_OFF };
typedef struct { uint8_t type, part, note, vel; } ev_t;
#define EVQ 32u
static ev_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
static void ev_post(uint32_t type, uint32_t part, uint32_t note, uint32_t vel)
{
    if (ev_w - ev_r >= EVQ)
        return;
    evq[ev_w % EVQ] = (ev_t){(uint8_t)type, (uint8_t)part, (uint8_t)note, (uint8_t)vel};
    __asm__ volatile("" ::: "memory");
    ev_w++;
}

/* ------------------------------------------------------------------------------------------ the sequencer --- */
static struct {
    volatile uint8_t playing, rec;
    volatile uint8_t queued;                       /* a scene to go to at the bar's end, 0xFF none */
    volatile uint8_t repeat;                       /* REPEAT held: 0 off, 1..4: 1/4 1/8 1/16 1/32 */
    volatile uint8_t tape;                         /* TAPE STOP held */
    volatile uint8_t restart;
    uint8_t step;                                  /* the step playing */
    uint32_t pos_q;                                /* in the step, samples Q12 */
    uint32_t len_q;                                /* this step's length, Q12 */
    uint8_t roll_left[NPART];                      /* a roll's hits to come */
    uint32_t roll_at[NPART], roll_per[NPART];      /* Q12 in the step */
    uint8_t cut_at_half[NPART];
    uint8_t last_note[NPART], last_fx[NPART];      /* REPEAT plays these */
    uint32_t rep_q, rep_per;
    int8_t bass_held;                              /* the BASS note held by the sequencer, -1 none */
    uint8_t lit[NPART];                            /* the step that last played something, per track (the screens) */
    uint8_t mute_seen[NPART];
} sq;

static uint32_t sixteenth(void) { return 44100u * 15u / (prj.bpm ? prj.bpm : 170u); }
static uint32_t step_len_q(uint32_t step)        /* swing: the even sixteenth longer, the odd one shorter */
{
    uint32_t l = sixteenth(), s = l * prj.swing / 100u;
    return ((step & 1u) ? l - s : l + s) << 12;
}
static const step_t *cur_step(uint32_t part, uint32_t i) { return &prj.pat[prj.scene % NSCENE][part][i % NSTEP]; }
static int chance_ok(uint32_t c) { return c == 0u || rnd(4) < 4u - c; }

static void brk_hit(uint32_t part, uint32_t slice, uint32_t vel, uint32_t fx)
{
    sq.last_note[part] = (uint8_t)slice;
    sq.last_fx[part] = (uint8_t)fx;
    if (!prj.snd[part].mute)
        voice_start(part, JNG_BASE + (slice + JNG_STEPS - (uint32_t)clamp(prj.snd[part].p[P_E1], 0, 31)) % JNG_STEPS, vel,
                    fx, 0);
}

static void seq_step_start(void)
{
    uint32_t part, i = sq.step;
    for (part = 0; part < NPART; part++) {
        const step_t *s = cur_step(part, i);
        sq.roll_left[part] = 0;
        sq.cut_at_half[part] = 0;
        if (IS_BRK(part)) {
            if (!s->on || sq.repeat || !chance_ok(s->chance))
                continue;
            sq.lit[part] = (uint8_t)i;
            brk_hit(part, s->val, (s->flags & F_ACC) ? 127u : 100u, s->fx);
            if (s->roll > 1u) {
                sq.roll_left[part] = (uint8_t)(s->roll - 1u);
                sq.roll_per[part] = sq.len_q / s->roll;
                sq.roll_at[part] = sq.roll_per[part];
            }
            if (s->fx == FX_CUT)
                sq.cut_at_half[part] = 1;
        } else {
            int tie = s->on && (s->flags & F_TIE);
            if (tie && sq.bass_held >= 0)
                continue;                              /* the note goes on */
            if (s->on && !tie && chance_ok(s->chance) && !prj.snd[part].mute) {
                const step_t *pv = cur_step(part, (i + NSTEP - 1u) % (prj.len ? prj.len : 16u));
                int legato = sq.bass_held >= 0 && pv->on && (pv->flags & F_SLIDE);
                sq.lit[part] = (uint8_t)i;
                voice_start(part, s->val, (s->flags & F_ACC) ? 127u : 96u, 0, legato);
                sq.bass_held = (int8_t)s->val;
            } else if (sq.bass_held >= 0) {
                voice_release(part, -1);
                sq.bass_held = -1;
            }
        }
    }
}

static void seq_block(uint32_t n)
{
    uint32_t part, adv = n * amen_speed;
    if (sq.restart) {
        sq.restart = 0;
        sq.step = 0;
        sq.pos_q = 0;
        sq.len_q = step_len_q(0);
        sq.rep_q = 0;
        seq_step_start();
    }
    if (!sq.playing)
        return;
    sq.pos_q += adv;
    for (part = 0; part < NPART; part++) {           /* rolls, CUT */
        while (sq.roll_left[part] && sq.pos_q >= sq.roll_at[part]) {
            sq.roll_left[part]--;
            sq.roll_at[part] += sq.roll_per[part];
            brk_hit(part, sq.last_note[part], 90u, sq.last_fx[part]);
        }
        if (sq.cut_at_half[part] && sq.pos_q >= sq.len_q / 2u) {
            sq.cut_at_half[part] = 0;
            voice_release(part, -1);
        }
    }
    if (sq.repeat) {                                 /* REPEAT: the breaks' last slices again and again */
        sq.rep_q += adv;
        if (sq.rep_q >= sq.rep_per) {
            sq.rep_q -= sq.rep_per;
            for (part = 0; part < NPART; part++)
                if (IS_BRK(part))
                    brk_hit(part, sq.last_note[part], 110u, sq.last_fx[part]);
        }
    }
    while (sq.pos_q >= sq.len_q) {
        uint32_t len = prj.len ? prj.len : 16u;
        sq.pos_q -= sq.len_q;
        sq.step = (uint8_t)((sq.step + 1u) % len);
        if (sq.step == 0u && sq.queued < NSCENE) {   /* the bar's end: the queued scene */
            prj.scene = sq.queued;
            sq.queued = 0xFF;
        }
        sq.len_q = step_len_q(sq.step);
        seq_step_start();
    }
}

static void seq_play(int on)
{
    if (on) {
        sq.playing = 1;
        sq.restart = 1;
    } else {
        sq.playing = 0;
        sq.rec = 0;
    }
}

static void seq_repeat(uint32_t rate)             /* 0 off, 1..4: 1/4 1/8 1/16 1/32 */
{
    static const uint8_t PER16[5] = {0, 8, 4, 2, 1}; /* in 1/32 notes... (x 1/2 sixteenth) */
    if (rate && rate != sq.repeat) {
        sq.rep_per = (sixteenth() << 12) / 2u * PER16[rate];
        sq.rep_q = sq.rep_per;                       /* the first hit at once */
    }
    sq.repeat = (uint8_t)rate;
}

/* a key, live: plays the track (and with REC, writes the step) */
static void live_key(uint32_t part, uint32_t val, int on)
{
    if (!on) {
        ev_post(EV_OFF, part, val, 0);
        return;
    }
    ev_post(EV_ON, part, val, 110);
    if (sq.rec && sq.playing) {                       /* the nearest sixteenth */
        uint32_t len = prj.len ? prj.len : 16u, i = sq.step;
        step_t *s;
        if (sq.pos_q > sq.len_q / 2u)
            i = (i + 1u) % len;
        s = &prj.pat[prj.scene][part][i];
        memset(s, 0, sizeof *s);
        s->on = 1;
        s->val = (uint8_t)val;
        s->roll = 1;
    }
}

static void events_take(void)
{
    while (ev_r != ev_w) {
        ev_t e = evq[ev_r % EVQ];
        __asm__ volatile("" ::: "memory");
        ev_r++;
        if (e.type == EV_ON) {
            if (IS_BRK(e.part))
                brk_hit(e.part, e.note, e.vel, FX_NONE);
            else {
                voice_start(e.part, e.note, e.vel, 0, 0);
            }
        } else if (!IS_BRK(e.part)) {
            voice_release(e.part, e.note);
        }
    }
}

/* ------------------------------------------------------------------------------------------------ the mix --- */
#define DL_N 12288u                                  /* the delay line at half rate: 0.557 s */
static int16_t dl[DL_N];
static uint32_t dl_w;
static int32_t dl_in_acc, dl_lp, dl_out0, dl_out1, dl_half;
static int32_t fic1, fic2;                           /* the DJ filter */
static uint8_t meter[NPART];                         /* a peak per track for the screens, 0..15 */
static volatile int32_t master_q15 = 20000;          /* MASTER (the pot, ui.c), after the limiter */

static uint32_t dly_samples(void)                    /* the delay's time: 1/8, 3/16, 1/4 at the tempo (half rate) */
{
    static const uint8_t N[3] = {2, 3, 4};
    uint32_t t = sixteenth() * N[prj.dly_time % 3u] / 2u;
    return t >= DL_N - 2u ? DL_N - 2u : t;
}

static void mix_block(int32_t *out, uint32_t n)
{
    static int32_t pb[NPART][CTL];
    int32_t dry[CTL], send[CTL];
    uint32_t i, part, f = prj.filter, dt = dly_samples();
    int32_t fb = prj.dly_fb * 300;                    /* Q15: 0 .. ~0.92 */
    tsvf_t flt;
    int32_t k = 5600, gain = amen_speed >= 4096u ? 32767 : (int32_t)(amen_speed * 8u);   /* TAPE STOP: the fade */
    for (i = 0; i < n; i++)
        dry[i] = send[i] = 0;
    for (part = 0; part < NPART; part++) {
        int32_t lv = prj.snd[part].level * 258, sd = prj.snd[part].send * 258, pk = 0;
        memset(pb[part], 0, sizeof pb[part]);
        part_render(part, pb[part]);
        for (i = 0; i < n; i++) {
            int32_t x = mulq15(pb[part][i], lv), a = x < 0 ? -x : x;
            dry[i] += x;
            send[i] += mulq15(x, sd);
            if (a > pk)
                pk = a;
        }
        pk >>= 11;
        meter[part] = (uint8_t)(pk > 15 ? 15 : pk > meter[part] ? pk : meter[part] > 0 ? meter[part] - 1 : 0);
    }
    if (f != 64u)
        tsvf_coef_k(&flt, f < 64u ? (int32_t)(30 + f * 97 / 64) << 8 : (int32_t)((f - 64u) * 100 / 63) << 8, k);
    for (i = 0; i < n; i++) {
        int32_t y = dry[i], w;
        /* the dub delay: half rate, darkened and soft-clipped in its feedback */
        dl_in_acc += send[i];
        if (dl_half ^= 1) {
            uint32_t rd = (dl_w + DL_N - dt) % DL_N;
            int32_t tap = dl[rd];
            dl_lp += (tap - dl_lp) >> 2;
            w = softclip((dl_in_acc >> 1) + mulq15(dl_lp, fb));
            dl[dl_w] = (int16_t)w;
            dl_w = (dl_w + 1u) % DL_N;
            dl_in_acc = 0;
            dl_out0 = dl_out1;
            dl_out1 = tap;
            w = dl_out0;
        } else {
            w = (dl_out0 + dl_out1) >> 1;
        }
        y += w;
        if (f != 64u) {                              /* the DJ filter: low-pass left of the middle, high-pass right */
            int32_t v3 = y - fic2, v1 = (flt.a1 * fic1 + flt.a2 * v3) >> 13, v2 = fic2 + ((flt.a2 * fic1 + flt.a3 * v3) >> 13);
            fic1 = clamp(2 * v1 - fic1, -150000, 150000);
            fic2 = clamp(2 * v2 - fic2, -150000, 150000);
            y = f < 64u ? v2 : y - ((k * v1) >> 12) - v2;
        }
        y = mulq15(mulq15(softclip(y), gain), master_q15);   /* the limiter, MASTER */
        out[2u * i] = y;
        out[2u * i + 1u] = y;
    }
}

/* ----------------------------------------------------------------------------------------- the audio ISR --- */
static void audio_blocks(int32_t *out, uint32_t n)
{
    uint32_t b;
    for (b = 0; b < n; b += CTL) {
        uint32_t target = sq.tape ? 0u : 4096u;       /* TAPE STOP: down in ~1 s, back in ~0.25 s */
        if (amen_speed > target)
            amen_speed = amen_speed > 3u ? amen_speed - 3u : 0u;
        else if (amen_speed < target)
            amen_speed = amen_speed + 12u > 4096u ? 4096u : amen_speed + 12u;
        amen_sixteenth = sixteenth();
        events_take();
        seq_block(CTL);
        mix_block(out + 2u * b, CTL);
    }
}

/* ---------------------------------------------------------------------------------------- MANGLE: edits --- */
static uint32_t brk_pick(uint32_t part, uint32_t bits, uint32_t near)   /* a slice of the break with any of bits */
{
    const uint8_t *brk = JNG_BRK[clamp(prj.snd[part].p[P_E0], 0, JNG_NBRK - 1)];
    uint32_t i, best = near % JNG_STEPS, bd = 99, d, n = 0;
    for (i = 0; i < JNG_STEPS; i++) {
        if (!(brk[i] & bits))
            continue;
        n++;
        d = i > near ? i - near : near - i;
        if (near >= JNG_STEPS)
            d = rnd(32);
        if (d < bd || (d == bd && rnd(2))) {
            bd = d;
            best = i;
        }
    }
    return n ? best : near % JNG_STEPS;
}
static void put_brk(step_t *s, uint32_t slice, uint32_t roll, uint32_t fx, uint32_t chance, uint32_t acc)
{
    memset(s, 0, sizeof *s);
    s->on = 1;
    s->val = (uint8_t)(slice % JNG_STEPS);
    s->roll = (uint8_t)(roll ? roll : 1u);
    s->fx = (uint8_t)fx;
    s->chance = (uint8_t)chance;
    s->flags = acc ? F_ACC : 0u;
}
static void mangle_brk(uint32_t part, uint32_t style, step_t *pat, uint32_t len)
{
    const uint8_t *brk = JNG_BRK[clamp(prj.snd[part].p[P_E0], 0, JNG_NBRK - 1)];
    uint32_t i, b;
    memset(pat, 0, sizeof(step_t) * NSTEP);
    for (i = 0; i < len; i++) {
        b = i % 16u;
        switch (style) {
        case ST_ROLLER:                                /* two-step from the break's own hits */
            if (b == 0u || b == 10u) put_brk(&pat[i], brk_pick(part, JK, b), 1, 0, 0, 1);
            else if (b == 4u || b == 12u) put_brk(&pat[i], brk_pick(part, JS, b), 1, 0, 0, 1);
            else if (b == 7u || b == 15u) put_brk(&pat[i], brk_pick(part, JG, b), b == 15u && rnd(2) ? 2u : 1u, 0, 1, 0);
            else put_brk(&pat[i], brk_pick(part, JH | JR | JO, b), 1, 0, (b & 1u) ? 1u : 0u, 0);
            break;
        case ST_HALF:                                  /* halftime: kick on 1, the snare on 3, space */
            if (b == 0u || b == 6u || (b == 11u && rnd(2))) put_brk(&pat[i], brk_pick(part, JK, b), 1, 0, 0, b == 0u);
            else if (b == 8u) put_brk(&pat[i], brk_pick(part, JS, 4), 1, 0, 0, 1);
            else if (b % 2u == 0u) put_brk(&pat[i], brk_pick(part, JH | JR, b), 1, 0, 0, 0);
            else if (rnd(4) == 0) put_brk(&pat[i], brk_pick(part, JG, b), 1, 0, 2, 0);
            break;
        case ST_BRKCORE: {                              /* thrown about: rolls, rests, stutters, reverse */
            uint32_t r = rnd(16);
            if (r < 2u) break;
            if (r < 5u) put_brk(&pat[i], brk_pick(part, JS, 99), 3u + rnd(2), rnd(3) == 0 ? FX_UP : 0, 0, 1);
            else if (r < 8u) put_brk(&pat[i], brk_pick(part, JK, 99), 1u + rnd(2), 0, 0, 1);
            else if (r < 10u && i) pat[i] = pat[i - 1u];
            else put_brk(&pat[i], rnd(JNG_STEPS), 1u + (rnd(4) == 0), rnd(5) == 0 ? FX_REV : rnd(6) == 0 ? FX_CUT : 0,
                         rnd(3), 0);
            break;
        }
        default:                                       /* JUNGLE: the break as it plays, the classic edits */
            put_brk(&pat[i], i % JNG_STEPS, 1, 0, 0, (brk[i % JNG_STEPS] & JS) != 0u);
            break;
        }
    }
    if (style == ST_JUNGLE)
        for (i = 0; i < len; i += 16u) {
            uint32_t k;
            if (rnd(2))                                /* beats 3-4 as 1-2 again */
                for (k = 8; k < 12u && i + k < len; k++)
                    put_brk(&pat[i + k], k - 8u + (i % JNG_STEPS), 1, 0, 0, k == 8u);
            if (rnd(3) == 0 && i + 15u < len) {        /* a snare stutter on the last beat */
                uint32_t s = brk_pick(part, JS, (i + 12u) % JNG_STEPS);
                put_brk(&pat[i + 12u], s, 1, 0, 0, 1);
                put_brk(&pat[i + 13u], s + 1u, 1, 0, 0, 0);
                put_brk(&pat[i + 14u], s, 1, 0, 0, 1);
                put_brk(&pat[i + 15u], s, 2, 0, 0, 0);
            } else if (rnd(2) && i + 15u < len) {      /* a roll into the next bar */
                put_brk(&pat[i + 15u], brk_pick(part, JS | JG, (i + 15u) % JNG_STEPS), 2u + rnd(2), 0, 0, 0);
            }
            if (rnd(4) == 0 && i + 7u < len)           /* a reversed slice into beat 3 */
                pat[i + 7u].fx = FX_REV;
        }
}
/* the bass: a minor root (E1 / F1 / G1 / A1) held, a slide up or down to the fifth / the seventh / the octave */
static void mangle_bass(uint32_t style, step_t *pat, uint32_t len)
{
    static const uint8_t ROOTS[4] = {28, 29, 31, 33};
    static const int8_t MOVE[6] = {7, 10, 12, 3, -2, 5};
    uint32_t root = ROOTS[rnd(4)], i;
    memset(pat, 0, sizeof(step_t) * NSTEP);
    for (i = 0; i < len; i++) {
        uint32_t b = i % 16u;
        step_t *s = &pat[i];
        if (style == ST_HALF) {                        /* long notes */
            s->on = 1;
            s->val = (uint8_t)root;
            s->flags = b == 0u ? F_ACC : F_TIE;
            if (b == 12u && rnd(2)) { s->flags = F_SLIDE; s->val = (uint8_t)(root + (uint32_t)MOVE[rnd(3)]); }
            continue;
        }
        if (b == 0u) { s->on = 1; s->val = (uint8_t)root; s->flags = F_ACC; }
        else if (b < 6u) { s->on = 1; s->val = (uint8_t)root; s->flags = F_TIE; }
        else if (b == 6u || b == 10u || (style == ST_BRKCORE && rnd(3) == 0)) {
            s->on = 1;
            s->val = (uint8_t)((int32_t)root + MOVE[rnd(6)]);
            s->flags = (rnd(2) ? F_SLIDE : 0u) | (rnd(3) == 0 ? F_ACC : 0u);
        } else if (b == 7u || b == 11u) {
            s->on = 1; s->val = pat[i - 1u].val; s->flags = F_TIE;
        } else if (style == ST_ROLLER && (b == 14u)) {
            s->on = 1; s->val = (uint8_t)(root + 12u); s->flags = F_SLIDE;
        }
    }
}
static void mangle(uint32_t part, uint32_t style)
{
    uint32_t len = prj.len ? prj.len : 16u;
    if (IS_BRK(part))
        mangle_brk(part, style, prj.pat[prj.scene][part], len);
    else
        mangle_bass(style, prj.pat[prj.scene][part], len);
}

/* ---------------------------------------------------------------------------------------- the default --- */
static void project_default(void)
{
    uint32_t sc, part;
    static const uint8_t SC_STYLE[NSCENE] = {ST_JUNGLE, ST_ROLLER, ST_HALF, ST_BRKCORE, ST_JUNGLE, ST_ROLLER, ST_JUNGLE,
                                             ST_BRKCORE};
    memset(&prj, 0, sizeof prj);
    prj.magic = PROJ_MAGIC;
    prj.size = sizeof prj;
    prj.bpm = 170;
    prj.swing = 0;
    prj.len = 16;
    prj.filter = 64;
    prj.dly_time = 1;
    prj.dly_fb = 45;
    sound_preset(T_BRKA, 1);                         /* AMEN+5 */
    sound_preset(T_BRKB, 3);                         /* THINK */
    sound_preset(T_BASS, 0);                         /* REESE */
    prj.snd[T_BRKA].level = 120;
    prj.snd[T_BRKB].level = 90;
    prj.snd[T_BASS].level = 110;
    prj.snd[T_BRKB].send = 30;
    rnd_state = 0x5EED1234u;                         /* the same demo scenes every time */
    for (sc = 0; sc < NSCENE; sc++) {
        prj.scene = (uint8_t)sc;
        for (part = 0; part < NPART; part++) {
            if (part == T_BRKB && (sc == 0u || sc == 2u))
                continue;                            /* scenes A and C: BRK B empty, for your edits */
            mangle(part, SC_STYLE[sc]);
        }
    }
    prj.scene = 0;
    prj.style = ST_JUNGLE;
}

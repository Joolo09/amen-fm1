/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN-FM1: a desktop host for the engines. It plays them the way Felucca does (a part's voices, the engine
 * hooks, an envelope a block, the same tables), so they can be tested and heard without an FM-1.
 *
 *   amen_host test              checks every preset and mode (exit status 1 on a failure)
 *   amen_host demo OUT.wav      renders the demo: eight bars of chopped breaks and a reese at 170 BPM
 *   amen_host break N OUT.wav   one break (0..7) played straight, both bars, at 170 BPM
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVOICE 8
#define NPART 4
#include "fe_tables.h"
#include "felucca_engine.h"

enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
    const engine_t *e;
    uint8_t mono;
    int32_t last[NVOICE];
} track_t;
static track_t trk[NPART];

#include "dsp.c"
#include "eng_jungle.c"
#include "eng_reese.c"

/* ------------------------------------------------------------------------------------------- voices --- */
static uint32_t vage;

static void part_load(track_t *t, const engine_t *e, const preset_t *pr)
{
    uint32_t k;
    memset(t, 0, sizeof *t);
    t->e = e;
    t->mono = pr->mono;
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = pr->e[k];
    t->p[P_ATK] = pr->env[0];
    t->p[P_REL] = pr->env[3];
}

static void note_on(track_t *t, uint32_t note, uint32_t vel)
{
    const engine_t *e = t->e;
    voice_t *v = 0;
    uint32_t k, oldest = 0xFFFFFFFFu;
    int sounding;
    uint32_t ph[3];
    int32_t s[8];
    if (t->mono) {
        v = &t->v[0];
    } else {
        for (k = 0; k < NVOICE && !v; k++)
            if (!t->v[k].active)
                v = &t->v[k];
        for (k = 0; k < NVOICE && !v; k++)          /* none free: steal the oldest */
            if (t->v[k].age < oldest)
                oldest = t->v[k].age;
        for (k = 0; k < NVOICE && !v; k++)
            if (t->v[k].age == oldest)
                v = &t->v[k];
    }
    sounding = v->active && v->stage != 0;
    memcpy(ph, v->ph, sizeof ph);
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    if (!sounding) {
        v->env = v->env_out = 0;
        t->last[v - t->v] = 0;
    }
    e->note_on(t, v);
    if (sounding && !e->sampled) {                   /* a legato retrigger keeps the phases and the keep slots */
        memcpy(v->ph, ph, sizeof ph);
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u))
                v->s[k] = s[k];
    }
}

static void note_off(track_t *t, uint32_t note)
{
    uint32_t k;
    for (k = 0; k < NVOICE; k++)
        if (t->v[k].active && t->v[k].gate && t->v[k].note == note) {
            t->v[k].gate = 0;
            t->v[k].stage = 3;
        }
}

/* an ADSR a block: attack from ATK, full sustain, release from REL (Q24 -> Q15) */
static int32_t env_tick(track_t *t, voice_t *v)
{
    int32_t atk = 1 << 24, rel;
    if (t->p[P_ATK] > 0)
        atk = (1 << 24) / (1 + t->p[P_ATK] * 4);
    rel = 2 + t->p[P_REL] / 8;                       /* release: falls by 1 / 2^(rel / 4) a block, roughly */
    switch (v->stage) {
    case 1:
        v->env += atk;
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 3:
        v->env -= v->env / (rel < 2 ? 2 : rel);
        if (v->env < (1 << 24) / 2000) {
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

/* one block of a part, added to out[CTL] */
static void part_render(track_t *t, int32_t *out)
{
    const engine_t *e = t->e;
    uint32_t k;
    if (!e)
        return;
    if (e->block)
        e->block(t);
    for (k = 0; k < NVOICE; k++) {
        voice_t *v = &t->v[k];
        vmod_t m;
        int32_t a;
        uint32_t base;
        if (!v->active)
            continue;
        if (e->ownenv) {
            if (e->done(t, v)) {
                v->active = 0;
                v->stage = 0;
                continue;
            }
            a = 32767;
        } else {
            a = mulq15(env_tick(t, v), v->vel * 258);
            if (!v->active)
                continue;
        }
        base = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
        memset(&m, 0, sizeof m);
        m.pitch16 = v->pitch16;
        m.inc = base;
        m.amp0 = t->last[k];
        m.amp1 = a;
        m.envq15 = e->ownenv ? 0 : a;
        t->last[k] = a;
        e->render(t, v, out, CTL, &m);
    }
}

static uint32_t part_voices(const track_t *t)
{
    uint32_t k, n = 0;
    for (k = 0; k < NVOICE; k++)
        n += t->v[k].active;
    return n;
}

/* ------------------------------------------------------------------------------------------ the WAV --- */
typedef struct {
    int16_t *pcm;
    uint32_t n, cap;
    int32_t peak;
} wav_t;

static void wav_put(wav_t *w, int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a > w->peak)
        w->peak = a;
    x >>= 1;                                         /* two parts at the voice level: headroom */
    x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 1 << 16;
        w->pcm = realloc(w->pcm, w->cap * sizeof *w->pcm);
        if (!w->pcm)
            exit(2);
    }
    w->pcm[w->n++] = (int16_t)x;
}

static int wav_write(const wav_t *w, const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = w->n * 2u, rate = FS, brate = FS * 2u, v;
    uint16_t s;
    if (!f)
        return 1;
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f);                      /* PCM */
    s = 1; fwrite(&s, 2, 1, f);                      /* mono */
    fwrite(&rate, 4, 1, f);
    fwrite(&brate, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(w->pcm, 2, w->n, f);
    return fclose(f) != 0;
}

/* ------------------------------------------------------------------------------------ the sequencer --- */
#define BPM 170
#define STEP_SAMPLES (FS * 60 / BPM / 4)            /* a sixteenth: 3891 samples at 170 BPM */

/* an event a sixteenth (or a 32nd: half steps): part, note (-1 = none), velocity, gate in half-steps */
typedef struct { uint16_t at; uint8_t part; int8_t note; uint8_t vel, gate; } ev_t;

typedef struct {
    ev_t *ev;
    uint32_t n, cap;
} song_t;

static void song_add(song_t *s, uint32_t half, uint32_t part, int32_t note, uint32_t vel, uint32_t gate)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        s->ev = realloc(s->ev, s->cap * sizeof *s->ev);
        if (!s->ev)
            exit(2);
    }
    s->ev[s->n++] = (ev_t){(uint16_t)half, (uint8_t)part, (int8_t)note, (uint8_t)vel, (uint8_t)gate};
}

/* renders the song's events (sorted by time) over halves 32nds, plus a tail */
static void song_play(const song_t *s, uint32_t halves, wav_t *w)
{
    uint32_t total = halves * (STEP_SAMPLES / 2) + FS, pos = 0, e = 0, k;
    int32_t out[CTL];
    /* pending note-offs: part, note, sample */
    struct { uint8_t part, note; uint32_t at; } off[64];
    uint32_t noff = 0;
    while (pos < total) {
        uint32_t i;
        while (e < s->n && (uint32_t)s->ev[e].at * (STEP_SAMPLES / 2) <= pos) {
            const ev_t *x = &s->ev[e++];
            if (x->note >= 0) {
                note_on(&trk[x->part], (uint32_t)x->note, x->vel);
                if (noff < 64) {
                    off[noff].part = x->part;
                    off[noff].note = (uint8_t)x->note;
                    off[noff].at = ((uint32_t)x->at + x->gate) * (STEP_SAMPLES / 2) - CTL;
                    noff++;
                }
            }
        }
        for (k = 0; k < noff;) {
            if (off[k].at <= pos) {
                note_off(&trk[off[k].part], off[k].note);
                off[k] = off[--noff];
            } else {
                k++;
            }
        }
        memset(out, 0, sizeof out);
        for (k = 0; k < NPART; k++)
            part_render(&trk[k], out);
        for (i = 0; i < CTL; i++)
            wav_put(w, out[i]);
        pos += CTL;
    }
}

static const preset_t *preset_named(const engine_t *e, const char *name)
{
    uint32_t i;
    for (i = 0; i < e->npresets; i++)
        if (!strcmp(e->presets[i].name, name))
            return &e->presets[i];
    fprintf(stderr, "no preset %s in %s\n", name, e->name);
    exit(2);
}

/* the slice notes of the demo's edits: the slice index (+60 is the key), -1 a rest; 32 sixteenths a line */
static const int8_t EDIT_STRAIGHT[32] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                         16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
static const int8_t EDIT_CHOP[32] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15,
                                     16, 17, 2, 3, 4, 5, 22, 23, 4, 5, 4, 5, 12, 13, 12, 13};
static const int8_t EDIT_DNB[32] = {0, 1, 6, 7, 4, 5, 6, 7, 10, 11, 2, 3, 12, 13, 14, 15,
                                    0, 1, 6, 7, 4, 5, 14, 15, 10, 11, 10, 3, 12, 13, 28, 31};

static void demo(const char *path)
{
    song_t s = {0};
    wav_t w = {0};
    uint32_t bar, i;
    static const int8_t BASS[8] = {28, 28, 31, 26, 28, 28, 33, 31};   /* E1 E1 G1 D1 E1 E1 A1 G1 */
    part_load(&trk[0], &ENG_JUNGLE, preset_named(&ENG_JUNGLE, "AMEN"));
    part_load(&trk[1], &ENG_REESE, preset_named(&ENG_REESE, "REESE"));
    part_load(&trk[2], &ENG_JUNGLE, preset_named(&ENG_JUNGLE, "BRKCORE"));
    for (bar = 0; bar < 8; bar++) {
        const int8_t *ed = bar < 2 ? EDIT_STRAIGHT : bar < 4 ? EDIT_CHOP : EDIT_DNB;
        uint32_t base = bar * 32u;                   /* in 32nds */
        if (bar < 6) {
            for (i = 0; i < 16; i++) {
                int32_t sl = ed[(bar & 1) * 16 + i];
                uint32_t vel = (i & 3) == 0 ? 120 : (i & 1) ? 84 : 104;
                if (bar >= 4 && (i == 14 || i == 15) && (bar & 1)) {   /* a 32nd roll into the next bar */
                    song_add(&s, base + i * 2u, 0, 60 + 4, vel, 1);
                    song_add(&s, base + i * 2u + 1u, 0, 60 + 4, vel - 20, 1);
                } else if (sl >= 0) {
                    song_add(&s, base + i * 2u, 0, 60 + sl, vel, 2);
                }
            }
        } else {                                     /* the last two bars: breakcore over the break */
            for (i = 0; i < 16; i++) {
                uint32_t r = (bar * 16u + i) * 2654435761u >> 27;   /* 0..31 */
                song_add(&s, base + i * 2u, 0, 60 + (int32_t)EDIT_DNB[(bar & 1) * 16 + i], 100, 2);
                if (r & 1u)
                    song_add(&s, base + i * 2u + 1u, 2, 60 + (int32_t)(r & 31u), 90, 1);
            }
        }
        if (bar >= 2) {                              /* the reese from bar 3: two notes a bar */
            song_add(&s, base, 1, BASS[bar], 110, 22);
            song_add(&s, base + 24u, 1, BASS[bar] + ((bar & 1) ? 3 : 0), 100, 8);
        }
    }
    /* the events must be in time order */
    for (i = 1; i < s.n; i++) {
        ev_t x = s.ev[i];
        uint32_t j = i;
        while (j > 0 && s.ev[j - 1].at > x.at) {
            s.ev[j] = s.ev[j - 1];
            j--;
        }
        s.ev[j] = x;
    }
    song_play(&s, 8u * 32u, &w);
    if (wav_write(&w, path)) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    printf("%s: %.1f s, peak %d\n", path, (double)w.n / FS, w.peak);
    free(s.ev);
    free(w.pcm);
}

static void one_break(uint32_t brk, const char *path)
{
    song_t s = {0};
    wav_t w = {0};
    uint32_t i;
    preset_t pr = *preset_named(&ENG_JUNGLE, "AMEN");
    pr.e[0] = (int8_t)brk;
    part_load(&trk[0], &ENG_JUNGLE, &pr);
    for (i = 0; i < 64; i++)
        song_add(&s, i * 2u, 0, 60 + (int32_t)(i & 31u), 110, 2);
    song_play(&s, 128, &w);
    if (wav_write(&w, path))
        exit(2);
    printf("%s: %s, peak %d\n", path, JNG_BRK_NAMES[brk], w.peak);
    free(s.ev);
    free(w.pcm);
}

/* -------------------------------------------------------------------------------------------- tests --- */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

typedef struct { uint64_t h; int32_t peak; uint32_t blocks_to_silence; } run_t;

/* plays notes on part 0: a note every `every` blocks, `count` of them, each held `hold` blocks */
static run_t run(const engine_t *e, const preset_t *pr, const uint8_t *notes, uint32_t count, uint32_t every,
                 uint32_t hold, uint32_t blocks)
{
    run_t r = {14695981039346656037ull, 0, 0};
    uint32_t b, i, k, held = 0;
    int32_t out[CTL];
    vage = 0;
    for (k = 0; k < NPART; k++)
        memset(&trk[k], 0, sizeof trk[k]);
    part_load(&trk[0], e, pr);
    for (b = 0; b < blocks; b++) {
        if (b % every == 0 && b / every < count) {
            held = notes[b / every];
            note_on(&trk[0], held, 110);
        }
        if (b % every == hold && b / every < count)
            note_off(&trk[0], held);
        memset(out, 0, sizeof out);
        part_render(&trk[0], out);
        for (i = 0; i < CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            uint32_t x = (uint32_t)out[i];
            for (k = 0; k < 4u; k++) {
                r.h ^= (x >> (8u * k)) & 0xFFu;
                r.h *= 1099511628211ull;
            }
            if (a > r.peak)
                r.peak = a;
        }
        if (part_voices(&trk[0]) && b >= every * count)
            r.blocks_to_silence = b - every * count + 1;
    }
    return r;
}

static void test_presets(const engine_t *e, uint32_t max_tail)
{
    static const uint8_t NOTES[8] = {60, 64, 62, 72, 33, 40, 45, 52};
    static const uint8_t BASS[8] = {28, 31, 33, 26, 40, 28, 35, 24};
    uint32_t j;
    for (j = 0; j < e->npresets; j++) {
        const preset_t *pr = &e->presets[j];
        run_t a = run(e, pr, e->ownenv ? NOTES : BASS, 8, 40, 30, 6000);
        run_t b = run(e, pr, e->ownenv ? NOTES : BASS, 8, 40, 30, 6000);
        printf("  %-7s %-8s peak %6d  tail %4u blocks  %016llx\n", e->name, pr->name, a.peak, a.blocks_to_silence,
               (unsigned long long)a.h);
        CHECK(a.h == b.h, "%s %s: not deterministic", e->name, pr->name);
        CHECK(a.peak > 2000, "%s %s: silent (peak %d)", e->name, pr->name, a.peak);
        CHECK(a.peak < 4 * VOICE_FS, "%s %s: too loud (peak %d)", e->name, pr->name, a.peak);
        CHECK(a.blocks_to_silence < max_tail, "%s %s: still sounding %u blocks after the last note", e->name,
              pr->name, a.blocks_to_silence);
    }
}

/* every value of every parameter on the defaults: no overflow, nothing stuck */
static void test_params(const engine_t *e)
{
    static const uint8_t NOTES[4] = {60, 76, 30, 69};
    uint32_t k;
    preset_t pr;
    memset(&pr, 0, sizeof pr);
    pr.name = "params";
    pr.env[1] = 100;
    pr.env[2] = 127;
    pr.env[3] = 30;
    for (k = 0; k < 8u; k++) {
        int32_t v;
        uint32_t j;
        for (j = 0; j < 8u; j++)
            pr.e[j] = (int8_t)e->edit[j].def;
        for (v = e->edit[k].min; v <= e->edit[k].max; v++) {
            run_t r;
            pr.e[k] = (int8_t)v;
            r = run(e, &pr, NOTES, 4, 60, 20, 4000);
            CHECK(r.peak < 4 * VOICE_FS, "%s %s=%d: too loud (%d)", e->name, e->edit[k].label, v, r.peak);
            CHECK(r.blocks_to_silence < 2900, "%s %s=%d: stuck (%u blocks)", e->name, e->edit[k].label, v,
                  r.blocks_to_silence);
        }
    }
}

/* CHOKE: one slice sounds at a time, within the 2 ms fade; POLY lets them ring */
static void test_choke(void)
{
    preset_t pr = *preset_named(&ENG_JUNGLE, "AMEN");
    uint32_t b, most = 0, mode;
    int32_t out[CTL];
    for (mode = 0; mode < 2; mode++) {
        pr.e[6] = mode ? JNG_CHOKE : JNG_POLY;
        pr.e[3] = 127;                               /* long hits: they would overlap */
        vage = 0;
        part_load(&trk[0], &ENG_JUNGLE, &pr);
        most = 0;
        for (b = 0; b < 1200; b++) {
            if (b % 60 == 0)
                note_on(&trk[0], 60 + (b / 60) % 32, 110);
            memset(out, 0, sizeof out);
            part_render(&trk[0], out);
            if (b % 60 == 10 && part_voices(&trk[0]) > most)
                most = part_voices(&trk[0]);
        }
        if (mode)
            CHECK(most == 1, "CHOKE: %u slices sound together", most);
        else
            CHECK(most > 1, "POLY: slices do not overlap (%u)", most);
    }
}

/* GATE: a note-off cuts the slice; REV: the reversed slice swells (its end louder than its start) */
static void test_gate_rev(void)
{
    preset_t pr = *preset_named(&ENG_JUNGLE, "AMEN");
    int32_t out[CTL];
    uint32_t b, i;
    int32_t early = 0, late = 0;
    pr.e[6] = JNG_GATE;
    vage = 0;
    part_load(&trk[0], &ENG_JUNGLE, &pr);
    note_on(&trk[0], 60 + 4, 110);                   /* slice 4 of AMEN: a snare */
    for (b = 0; b < 20; b++) {
        if (b == 5)
            note_off(&trk[0], 64);
        memset(out, 0, sizeof out);
        part_render(&trk[0], out);
    }
    CHECK(part_voices(&trk[0]) == 0, "GATE: the slice still sounds after its note-off");

    pr.e[6] = JNG_CHOKE;
    pr.e[5] = JNG_REV_ON;
    pr.e[0] = 1;                                     /* THINK: slice 4 is a snare and a hat (no ride) */
    pr.e[2] = 0;
    pr.e[3] = 48;
    vage = 0;
    part_load(&trk[0], &ENG_JUNGLE, &pr);
    note_on(&trk[0], 60 + 4, 110);
    {
        int32_t pk[400] = {0};
        uint32_t nb = 0;
        for (b = 0; b < 400 && part_voices(&trk[0]); b++, nb++) {
            memset(out, 0, sizeof out);
            part_render(&trk[0], out);
            for (i = 0; i < CTL; i++)
                if ((out[i] < 0 ? -out[i] : out[i]) > pk[b])
                    pk[b] = out[i] < 0 ? -out[i] : out[i];
        }
        for (b = 0; b < nb; b++) {
            if (b < nb / 4 && pk[b] > early)
                early = pk[b];
            if (b >= nb - nb / 8 && pk[b] > late)
                late = pk[b];
        }
        CHECK(nb > 100 && nb < 140, "REV: the slice lasts %u blocks, not a sixteenth (~122)", nb);
    }
    CHECK(late * 5 > early * 6, "REV: no swell (start %d, end %d)", early, late);
}

/* every slice of every break: a slice with hits sounds, an empty one is silent at once */
static void test_slices(void)
{
    preset_t pr = *preset_named(&ENG_JUNGLE, "AMEN");
    uint32_t brk, sl, b, i;
    int32_t out[CTL];
    for (brk = 0; brk < JNG_NBRK; brk++)
        for (sl = 0; sl < JNG_STEPS; sl++) {
            int32_t peak = 0;
            pr.e[0] = (int8_t)brk;
            vage = 0;
            part_load(&trk[0], &ENG_JUNGLE, &pr);
            note_on(&trk[0], 60 + sl, 110);
            for (b = 0; b < 60; b++) {
                memset(out, 0, sizeof out);
                part_render(&trk[0], out);
                for (i = 0; i < CTL; i++)
                    if ((out[i] < 0 ? -out[i] : out[i]) > peak)
                        peak = out[i] < 0 ? -out[i] : out[i];
            }
            {
                uint32_t k, rings = 0;                /* hits, or hits up to three sixteenths back (tails) */
                for (k = 0; k <= JNG_TAIL_BACK; k++)
                    rings |= JNG_BRK[brk][(sl - k) & (JNG_STEPS - 1u)] & 127u;
                if (JNG_BRK[brk][sl] & 127u)
                    CHECK(peak > 1000, "%s slice %u: silent (%d)", JNG_BRK_NAMES[brk], sl, peak);
                else if (rings)                        /* a tail only: it sounds, quietly if it was a hat */
                    CHECK(peak > 0, "%s slice %u: its tail is silent", JNG_BRK_NAMES[brk], sl);
                else
                    CHECK(peak == 0, "%s slice %u: nothing rings but it sounds", JNG_BRK_NAMES[brk], sl);
            }
        }
    {   /* AMEN slice 1 is empty: it plays slice 0's kick and ride, a sixteenth on */
        int32_t peak = 0;
        pr.e[0] = 0;
        vage = 0;
        part_load(&trk[0], &ENG_JUNGLE, &pr);
        note_on(&trk[0], 61, 110);
        for (b = 0; b < 60; b++) {
            memset(out, 0, sizeof out);
            part_render(&trk[0], out);
            for (i = 0; i < CTL; i++)
                if ((out[i] < 0 ? -out[i] : out[i]) > peak)
                    peak = out[i] < 0 ? -out[i] : out[i];
        }
        CHECK(peak > 3000, "AMEN slice 1: the kick's tail is missing (%d)", peak);
    }
}

/* PITCH: a pitched-up slice is shorter (break time runs faster) */
static void test_pitch(void)
{
    preset_t pr = *preset_named(&ENG_JUNGLE, "AMEN");
        pr.e[0] = 1;                                     /* THINK: slice 4 is a snare and a hat */
    {
        uint32_t b, nlo = 0, nhi = 0;
        int32_t out[CTL];
        for (pr.e[2] = -12; pr.e[2] <= 12; pr.e[2] += 24) {
            uint32_t *cnt = pr.e[2] < 0 ? &nlo : &nhi;
            vage = 0;
            part_load(&trk[0], &ENG_JUNGLE, &pr);
            note_on(&trk[0], 64, 110);
            for (b = 0; b < 4000 && part_voices(&trk[0]); b++) {
                memset(out, 0, sizeof out);
                part_render(&trk[0], out);
                (*cnt)++;
            }
        }
        CHECK(nlo > nhi * 3, "PITCH: -12 lasts %u blocks, +12 %u (expected ~4x)", nlo, nhi);
    }
}

static int tests(void)
{
    printf("presets:\n");
    test_presets(&ENG_JUNGLE, 1800);   /* 0.9 s of break time; PTCH -5 (GABBER) stretches it to ~1.2 s */
    test_presets(&ENG_REESE, 400);
    printf("parameters, chokes, gates, reverse, slices, pitch\n");
    test_params(&ENG_JUNGLE);
    test_params(&ENG_REESE);
    test_choke();
    test_gate_rev();
    test_slices();
    test_pitch();
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "test"))
        return tests();
    if (argc >= 3 && !strcmp(argv[1], "demo")) {
        demo(argv[2]);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "break")) {
        one_break((uint32_t)atoi(argv[2]) % JNG_NBRK, argv[3]);
        return 0;
    }
    fprintf(stderr, "usage: %s test | demo OUT.wav | break N OUT.wav\n", argv[0]);
    return 2;
}

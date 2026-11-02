/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN: the screens of a jungle firmware (included by ui_graph.c, after its helpers).
 *
 * BREAK MAP (a JUNGLE track: HOME and its EDIT pages): the break's 32 sixteenths as two bars of columns, each drawn
 *   from what hits on it (jungle_data.h JNG_BRK): a kick a block on the floor, a snare a block over it (a ghost a
 *   short one), hats ticks at the top (open: two), a ride a dot, a crash a cross. The slice sounding now is lit; the
 *   27 keys' window (START .. START + 26) is the line under the columns.
 * REESE (a REESE track): the two oscillators' beating drawn as it sounds: the saw's cycles inside the envelope the
 *   detune beats at (DTUN: more beats), its filter's darkness (CUT: steeper edges dimmed), the sub under it (SUB, SUBO).
 * CHOP (a JUNGLE track's sequencer page, in place of the piano roll): the bank's 16 steps as two rows of eight
 *   cells: the slice's number and its hits in miniature; a roll (RATCH) its parts as bars, a chance under 100 % the
 *   cell dimmed with its percent, an accent a bar on top, a tie a dash, a rest a dot. The cursor framed, the step
 *   playing filled. */

static int amen_jungle(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_JUNGLE; }
static int amen_reese(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_REESE; }

/* what a slice holds, in a word: its loudest hit (a kick and a snare together: K+S) */
static const char *amen_hit_name(uint32_t m)
{
    if ((m & JK) && (m & JS)) return "K+S";
    if (m & JK) return "KIK";
    if (m & JS) return "SNR";
    if (m & JC) return "CRS";
    if (m & JG) return "GHO";
    if (m & JO) return "OHH";
    if (m & JR) return "RDE";
    if (m & JH) return "HAT";
    return "---";
}
/* the slice of step st on t (its first note), its hits */
static uint32_t amen_step_hits(const track_t *t, const step_t *st)
{
    return JNG_BRK[clamp(t->p[P_E0], 0, JNG_NBRK - 1)][jng_slice(t, st->note[0])];
}

/* the slice sounding on t (its newest voice), -1 = none */
static int32_t amen_slice_now(const track_t *t)
{
    uint32_t k, best = 0;
    int32_t s = -1;
    for (k = 0; k < NVOICE; k++) {
        const voice_t *v = &t->v[k];
        if (v->active && v->age >= best) {
            best = v->age;
            s = (int32_t)jng_slice(t, v->note);
        }
    }
    return s;
}

/* a slice's hits in a box w x h at (x, y): kick floor, snare over it, hats at the top */
static void amen_hits(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t m, uint16_t kc, uint16_t sc, uint16_t hc,
                      uint16_t bg)
{
    int32_t kh = h * 3 / 10, sh = h * 3 / 10, cx = x + w / 2;
    (void)bg;
    if (m & JK)
        cv_rect(x, y + h - kh, w, kh, (m & JA) ? kc : kc);
    if (m & JS)
        cv_rect(x, y + h - kh - sh - 2, w, sh, sc);
    else if (m & JG)
        cv_rect(x + 1, y + h - kh - sh / 2 - 2, w - 2, sh / 2, hc);
    if (m & JH)
        cv_rect(x, y, w, 2, hc);
    if (m & JO) {
        cv_rect(x, y, w, 2, hc);
        cv_rect(x, y + 4, w, 2, hc);
    }
    if (m & JR)
        cv_rect(cx - 1, y + 1, 3, 3, hc);
    if (m & JC) {
        cv_line(cx - 3, y, cx + 3, y + 6, sc);
        cv_line(cx + 3, y, cx - 3, y + 6, sc);
    }
    if (!(m & 127u))                                  /* an empty sixteenth: its floor */
        cv_rect(cx, y + h - 2, 1, 2, T_DIM);
}

/* BREAK MAP: a slice drawn as its waveform's transient, mirrored round the row's middle: an attack as loud as its
 * loudest hit, a decay, a tail (three bars); a kick in the theme, a snare in the accent, the rest in MID; the tails of
 * the sixteenths before it a low hum */
#define BM_X0 16                                     /* the first column */
#define BM_CW 13                                     /* column pitch */
static int32_t amen_level(uint32_t m)                /* the loudest hit, 0 .. 100 */
{
    return (m & JK) ? 100 : (m & JS) ? 90 : (m & JC) ? 80 : (m & JO) ? 50 : (m & JG) ? 42 : (m & JR) ? 36 : (m & JH) ? 26 : 0;
}
static void graph_break(const track_t *t, uint16_t c)
{
    const uint8_t *brk = JNG_BRK[clamp(t->p[P_E0], 0, JNG_NBRK - 1)];
    int32_t now = amen_slice_now(t), start = clamp(t->p[P_E1], 0, JNG_STEPS - 1), row, i, k;
    int32_t gh = (int32_t)graph_h(), rh = (gh - 16) / 2;
    char b[8];
    for (row = 0; row < 2; row++) {
        int32_t y0 = 5 + row * (rh + 6), mid = y0 + rh / 2 - 2, amp = rh / 2 - 4;
        cv_text_on(6, mid - 8, &AF_S, row ? "2" : "1", T_DIM, T_SURF);   /* the bar */
        cv_rect(BM_X0 - 2, mid, 16 * BM_CW, 1, T_GRID);
        for (i = 0; i < 16; i++) {
            int32_t sl = row * 16 + i, x = BM_X0 + i * BM_CW, lit = sl == now, lv;
            uint32_t m = brk[sl];
            uint16_t col = lit ? T_TEXT : (m & JK) ? c : (m & JS) ? T_ACCENT : T_MID;
            if (lit)
                cv_rrect(x - 2, y0 - 1, BM_CW + 1, rh, 2, T_RAISE, T_SURF);
            lv = amen_level(m);
            if (!lv) {                                /* a sixteenth with no hit: the tails' hum */
                cv_rect(x, mid - 1, BM_CW - 3, 3, lit ? T_TEXT : T_DIM);
            } else {
                for (k = 0; k < 3; k++) {             /* attack, decay, tail */
                    int32_t hh = amp * lv / 100 * (k == 0 ? 10 : k == 1 ? 6 : 3) / 10;
                    if (hh < 1)
                        hh = 1;
                    cv_rect(x + k * 3, mid - hh, 3, 2 * hh + 1, col);
                }
                if ((m & (JH | JO | JR)) && (m & (JK | JS)))   /* a hat over a kick / snare: a tick on top */
                    cv_rect(x + 9, mid - amp / 3, 1, 2 * (amp / 3) + 1, lit ? T_TEXT : T_MID);
            }
            /* the keys' window: the 27 slices the keys play, a line under them (beats brighter) */
            if (((sl - start + JNG_STEPS) % JNG_STEPS) < 27)
                cv_rect(x - 1, y0 + rh - 2, BM_CW - 1, 1, (i & 3) == 0 ? T_MID : T_DIM);
        }
    }
    if (now >= 0) {                                   /* the slice's number and what it is */
        b[0] = 'S';
        b[1] = (char)('0' + now / 10);
        b[2] = (char)('0' + now % 10);
        b[3] = ' ';
        str_cpy(b + 4, amen_hit_name(brk[now]), 4);
        cv_text_r(232, 2, &AF_S, b, T_TEXT, T_SURF);
    }
}

/* REESE */
static void graph_reese(const track_t *t, uint16_t c)
{
    static const char *const WN[4] = {"REESE", "HOOVER", "NEURO", "SQUARE"};
    int32_t gh = (int32_t)graph_h(), mid = gh / 2 - 8, amp = gh / 2 - 22, x;
    int32_t dt = clamp(t->p[P_E0], 0, 127), sub = clamp(t->p[P_E1], 0, 127), cut = clamp(t->p[P_E2], 0, 127);
    int32_t wave = clamp(t->p[P_E6], 0, 3), beats = 1 + dt / 24;           /* the envelope's beats across the panel */
    int32_t cyc = 26, py = mid, sy0 = gh - 14, sa = 2 + sub / 22;
    uint32_t sph = 0;
    cv_rect(6, mid, 228, 1, T_GRID);
    for (x = 0; x < 226; x++) {
        int32_t env = sine_i((uint32_t)(x * beats) * (0x80000000u / 226u)), y, ph, s;   /* |cos| as a sine of half turns */
        if (env < 0)
            env = -env;
        env = 6000 + (env * 26767 >> 15);
        ph = (x * cyc * 65536 / 226) & 0xFFFF;                           /* the saw's phase, Q16 */
        switch (wave) {
        case 1: s = ph < 0x6000 ? 26000 : -26000; break;                 /* HOOVER: a narrow pulse */
        case 2: s = ph < 0x8000 ? ph - 0x4000 : 0xC000 - ph; s = s * 2 - 32767 / 2; break;   /* NEURO: a fold */
        case 3: s = ph < 0x8000 ? 26000 : -26000; break;                 /* SQUARE */
        default: s = 32767 - ph; break;                                   /* REESE: the saw */
        }
        s = s * (cut + 40) / 167;                                         /* CUT: a darker filter, less edge */
        y = mid - (s * env >> 15) * amp / 32767;
        if (x)
            cv_line(5 + x, py, 6 + x, y, c);
        py = y;
    }
    /* the sub: a sine along the floor, as loud as SUB */
    for (x = 0; x < 226; x += 2) {
        int32_t y = sy0 - (sine_i(sph) * sa >> 15);
        cv_rect(6 + x, y, 2, 2, T_ACCENT);
        sph += (1u << 25) >> clamp(t->p[P_E7], 0, 1);   /* (a shift: the build lints hex addresses) */
    }
    cv_text_on(9, 3, &AF_S, WN[wave], T_MID, T_SURF);
    cv_text_r(231, 3, &AF_S, t->p[P_E7] ? "SUB -2" : "SUB -1", T_DIM, T_SURF);
}

/* CHOP: the sequencer of a JUNGLE track */
#define CH_W 28
#define CH_X0 8
static void graph_chop(const track_t *t, uint16_t c)
{
    const uint8_t *brk = JNG_BRK[clamp(t->p[P_E0], 0, JNG_NBRK - 1)];
    uint32_t i, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int32_t gh = (int32_t)graph_h(), chh = (gh - 12) / 2;
    char b[4];
    for (i = 0; i < 16u; i++) {
        uint32_t si = base + i;
        int32_t x = CH_X0 + (int32_t)(i % 8u) * CH_W, y = 4 + (int32_t)(i / 8u) * (chh + 4);
        const step_t *st;
        int playing = song.playing && t->seq_idx == si, cur = si == ui.cursor;
        uint16_t fill = playing ? T_SEL : T_RAISE, ink = playing ? T_INK : T_TEXT;
        if (si >= len) {
            cv_rect(x + CH_W / 2 - 1, y + chh / 2, 2, 2, T_DIM);
            continue;
        }
        st = &seq_steps(t)[si];
        if (st->time == ST_NOTE && st->n) {
            uint32_t sl = jng_slice(t, st->note[0]), r = step_ratchet(st), ch = step_chance(st);
            uint16_t hk = playing ? T_INK : c, hs = playing ? T_INK : T_ACCENT, hh = playing ? T_INK : T_MID;
            cv_rrect(x, y, CH_W - 3, chh, 3, fill, T_SURF);
            b[0] = (char)('0' + sl / 10);
            b[1] = (char)('0' + sl % 10);
            b[2] = 0;
            cv_text_on(x + 3, y + 1, &AF_S, b, ink, fill);
            if (st->flags & SF_ACCENT)
                cv_rect(x + 3, y + 2, CH_W - 9, 1, hs);
            amen_hits(x + 4, y + 17, CH_W - 11, chh - 27, brk[sl], hk, hs, hh, fill);
            if (r > 1u) {                             /* a roll: its parts */
                uint32_t k;
                int32_t pw = (CH_W - 9) / (int32_t)r;
                for (k = 0; k < r; k++)
                    cv_rect(x + 3 + (int32_t)k * pw, y + chh - 6, pw - 1, 3, hs);
            }
            if (ch < 100u && ch > 0u)                 /* a chance: a bar as long as it */
                cv_rect(x + 3, y + chh - 2, (int32_t)ch * (CH_W - 9) / 100, 1, hh);
        } else if (st->time == ST_TIE) {
            cv_rect(x + 4, y + chh / 2, CH_W - 11, 2, T_MID);
        } else {
            cv_rect(x + CH_W / 2 - 2, y + chh / 2, 2, 2, T_DIM);
        }
        if (cur)
            cv_frame(x - 1, y - 1, CH_W - 1, chh + 2, T_TEXT);
    }
}


/* BASSLINE (a REESE track's sequencer page, in place of the piano roll): the bank's 16 steps as one line: each note
 * a thick bar at its pitch (the line's own range, at least an octave), a tie its bar carried on, a slide a glide
 * down / up into the next note, an accent the bar brighter and taller; the notes' names along the floor (the start
 * of each; odd steps a row lower, so a sharp never runs into the next); the cursor framed, the step playing filled */
#define BL_X0 10
#define BL_CW 14
static void graph_bass(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int32_t gh = (int32_t)graph_h(), top = 6, bot = gh - 34, lo = 127, hi = 0, prev = -1, py = 0, cur_note = -1;
    char nb[6];
    for (i = 0; i < 16u && base + i < len; i++) {      /* the line's range */
        const step_t *st = &seq_steps(t)[base + i];
        if (st->time == ST_NOTE && st->n) {
            if (st->note[0] < lo) lo = st->note[0];
            if (st->note[0] > hi) hi = st->note[0];
        }
    }
    if (hi < lo) { lo = 28; hi = 40; }
    if (hi - lo < 12) { lo -= (12 - (hi - lo)) / 2; hi = lo + 12; }
    for (i = 0; i <= (uint32_t)(hi - lo); i++)         /* the octaves' lines (C), quietly */
        if ((lo + (int32_t)i) % 12 == 0)
            cv_rect(BL_X0, bot - (int32_t)i * (bot - top) / (hi - lo), 16 * BL_CW - 2, 1, T_GRID);
    for (i = 0; i < 16u; i++) {
        uint32_t si = base + i;
        int32_t x = BL_X0 + (int32_t)i * BL_CW, y;
        const step_t *st;
        int playing = song.playing && t->seq_idx == si;
        if (playing)
            cv_rect(x, top - 4, BL_CW - 1, bot - top + 8, T_RAISE);
        if (si >= len) {
            cv_rect(x + BL_CW / 2 - 1, bot + 6, 2, 2, T_DIM);
            prev = -1;
            continue;
        }
        st = &seq_steps(t)[si];
        if (st->time == ST_NOTE && st->n) {
            int acc = (st->flags & SF_ACCENT) != 0u, th = acc ? 6 : 4;
            cur_note = st->note[0];
            y = bot - (cur_note - lo) * (bot - top) / (hi - lo);
            if (prev >= 0 && (seq_steps(t)[si - 1u].flags & SF_SLIDE))   /* a slide into this note: the glide */
                cv_line_t(x - 3, py, x + 2, y, acc ? T_ACCENT : c, 2);
            cv_rrect(x + 1, y - th / 2, BL_CW - 2, th, 2, acc ? T_ACCENT : c, playing ? T_RAISE : T_SURF);
            note_name(nb, (uint32_t)cur_note);
            if (nb[1] == '#') nb[2] = 0; else nb[1] = 0;   /* the name without its octave */
            cv_text_on(x + 1, bot + 3 + (int32_t)(i & 1u) * 12, &AF_S, nb, si == ui.cursor ? T_TEXT : T_MID, T_SURF);   /* (two rows) */
            prev = cur_note;
            py = y;
        } else if (st->time == ST_TIE && prev >= 0) {
            cv_rect(x - 1, py - 1, BL_CW, 3, c);
        } else {
            cv_rect(x + BL_CW / 2 - 1, bot + 10, 2, 2, T_DIM);
            prev = -1;
        }
        if (si == ui.cursor)
            cv_frame(x, top - 4, BL_CW - 1, gh - top - 4, T_TEXT);
    }
}

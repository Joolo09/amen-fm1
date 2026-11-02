/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN: the edit makers of SEQ TOOLS on a JUNGLE track (included by ui_tools.c, after its TL_* and step helpers). */
/* ---------------------------------------------------------------------------------------- the edit makers ---
 * SEQ TOOLS on a JUNGLE track (ui_tools.c tl_do): RANDOM is JUNGLE, COOK is BRKCORE, BEAT is ROLLER. They know the
 * break: its kicks, snares, ghosts and hats (jungle_data.h). A note is a slice through START (jng_slice): the notes
 * written are the slices meant whatever START is. Each press differs (rng); one undo, as the other tools. */
static uint32_t amen_rnd(uint32_t n) { return n ? rng() % n : 0u; }
/* the slices of t's break that have any of bits (none: every slice) */
static uint32_t amen_pick(const track_t *t, uint32_t bits, uint32_t near)
{
    const uint8_t *brk = JNG_BRK[clamp(t->p[P_E0], 0, JNG_NBRK - 1)];
    uint32_t list[JNG_STEPS], n = 0, i;
    for (i = 0; i < JNG_STEPS; i++)
        if (brk[i] & bits)
            list[n++] = i;
    if (!n)
        return near % JNG_STEPS;
    if (near < JNG_STEPS) {                          /* the one nearest a slice (keeps the edit in the groove) */
        uint32_t best = list[0], bd = 99, d;
        for (i = 0; i < n; i++) {
            d = list[i] > near ? list[i] - near : near - list[i];
            if (d < bd || (d == bd && amen_rnd(2))) { bd = d; best = list[i]; }
        }
        return best;
    }
    return list[amen_rnd(n)];
}
static void amen_put(track_t *t, uint32_t i, uint32_t slice, uint32_t ratch, uint32_t chance, uint32_t acc)
{
    step_t *st = &t->step[i];
    uint32_t start = (uint32_t)clamp(t->p[P_E1], 0, JNG_STEPS - 1);
    step_clear(st);
    st->note[0] = (uint8_t)(JNG_BASE + (slice + JNG_STEPS - start) % JNG_STEPS);
    st->n = 1;
    st->time = ST_NOTE;
    st->vel = 100;
    st->flags = acc ? SF_ACCENT : 0u;
    step_set_ratchet(st, ratch ? ratch : 1u);
    st->probability = chance >= 100u ? 0u : (uint8_t)chance;
}
/* JUNGLE: the break as it plays, then the classic edits: the first half-bar again, the snare's stutter, a roll into
 * the next bar, a ghost for a hat */
static void amen_make_jungle(track_t *t, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        amen_put(t, i, i % JNG_STEPS, 1, 100, (JNG_BRK[clamp(t->p[P_E0], 0, JNG_NBRK - 1)][i % JNG_STEPS] & JS) != 0u);
    for (i = 0; i < len; i += 16u) {
        uint32_t k, bar = i;
        if (amen_rnd(2))                              /* beats 3-4 as beats 1-2 again */
            for (k = 8; k < 12u && bar + k < len; k++)
                amen_put(t, bar + k, (bar + k - 8u) % JNG_STEPS, 1, 100, k == 8u);
        if (amen_rnd(3) == 0 && bar + 15u < len) {    /* a snare stutter on the last beat */
            uint32_t s = amen_pick(t, JS, (bar + 12u) % JNG_STEPS);
            amen_put(t, bar + 12u, s, 1, 100, 1);
            amen_put(t, bar + 13u, (s + 1u) % JNG_STEPS, 1, 100, 0);
            amen_put(t, bar + 14u, s, 1, 100, 1);
            amen_put(t, bar + 15u, s, 2, 100, 0);
        } else if (amen_rnd(2) && bar + 15u < len) {  /* a roll into the next bar */
            amen_put(t, bar + 15u, amen_pick(t, JS | JG, (bar + 15u) % JNG_STEPS), 2u + amen_rnd(2), 100, 0);
        }
        for (k = 0; k < 16u && bar + k < len; k++)    /* a ghost for a hat, now and then */
            if (amen_rnd(8) == 0 && !(JNG_BRK[clamp(t->p[P_E0], 0, JNG_NBRK - 1)][(bar + k) % JNG_STEPS] & (JK | JS)))
                amen_put(t, bar + k, amen_pick(t, JG, (bar + k) % JNG_STEPS), 1, 100, 0);
    }
}
/* ROLLER: drum & bass two-step from the break's own hits: kick on 1 and the '3 and', snare on 2 and 4, ghosts and
 * hats between (some by chance) */
static void amen_make_roller(track_t *t, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t b = i % 16u;
        if (b == 0u || b == 10u)
            amen_put(t, i, amen_pick(t, JK, b), 1, 100, 1);
        else if (b == 4u || b == 12u)
            amen_put(t, i, amen_pick(t, JS, b), 1, 100, 1);
        else if (b == 7u || b == 15u)
            amen_put(t, i, amen_pick(t, JG, b), b == 15u && amen_rnd(2) ? 2u : 1u, 85, 0);
        else
            amen_put(t, i, amen_pick(t, JH | JR | JO, b), 1, b & 1u ? 70 : 100, 0);
    }
}
/* BRKCORE: the slices thrown about: snares and kicks at random, rolls x3 / x4, chances, rests, a slice held over */
static void amen_make_brkcore(track_t *t, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t r = amen_rnd(16);
        if (r < 2u) {                                 /* a rest */
            step_clear(&t->step[i]);
            t->step[i].time = ST_REST;
        } else if (r < 5u) {                          /* a roll */
            amen_put(t, i, amen_pick(t, JS, 99), 3u + amen_rnd(2), 100, 1);
        } else if (r < 8u) {
            amen_put(t, i, amen_pick(t, JK, 99), 1u + amen_rnd(2), 100, 1);
        } else if (r < 10u && i) {                    /* the step before, again (a stutter) */
            t->step[i] = t->step[i - 1u];
        } else {
            amen_put(t, i, amen_rnd(JNG_STEPS), 1u + (amen_rnd(4) == 0), 60u + amen_rnd(41), 0);
        }
    }
}
/* the tool's name on a JUNGLE track (0: Felucca's) */
static const char *amen_tool_name(const track_t *t, uint32_t p)
{
    if (!amen_jungle(t))
        return 0;
    return p == TL_RANDOM ? "JUNGLE" : p == TL_COOK ? "BRKCORE" : p == TL_BEAT ? "ROLLER" : 0;
}

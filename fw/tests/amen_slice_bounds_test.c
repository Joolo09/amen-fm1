/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN: SLICE reads only inside the zones of its material (fw/AMEN.md, "SLICE: reads inside the material").
 * Same sources as the firmware, through hostsim.c, built strict: ASan + UBSan, no exclusions
 * (tools/amen_slice_check.sh). USR1 is a slot image of two zones whose data end where the image ends, so a read past
 * the last zone's data is an ASan global-buffer-overflow; reading a user slot as SMP_DATA[off] is UBSan's "index out
 * of bounds for type 'uint8_t [N]'" (Felucca 1.9.0; smp_bytes since).
 * 1. every slice of USR1 (DIV 4 / 8 / 16 / 32 / AUTO; forwards and REV; ONE, and LOOP across the zones) plays and
 *    a ONE slice ends by itself.
 * 2. a held voice given a bad state stops within two blocks, reading nothing outside its zones: a position past the
 *    material (its end too), a wrapped one, a zone index past the zones, a reverse position past the end; a step
 *    index past IMA_STEP is brought into range (the voice plays on).
 * 3. a voice left on USR1 while its material is replaced by a shorter one with fewer zones, with no block between
 *    (the host can; the device always renders one meanwhile: AMEN.md) stops (REV: once its decoded window, in its
 *    own RAM, is played out). */
#include <stdarg.h>
#include <stdint.h>
#define SLOT_HDR 512u                                    /* SMP_USER_DATA (checked below) */
#define SLOT_DATA 1501u                                  /* zone A: 2000 samples at 0, zone B: 1001 at 1000 */
static uint8_t slot0[SLOT_HDR + SLOT_DATA] __attribute__((aligned(4)));   /* USR1: nothing after its data */
static uint32_t slot_none[2][SLOT_HDR / 4u];                             /* USR2, USR3: empty */
#define SMP_USER_XIP(k) ((k) ? (const uint8_t *)slot_none[((k) - 1u) & 1u] : (const uint8_t *)slot0)
#define main hostsim_main
#include "hostsim.c"
#undef main
#if !FELUCCA_SLICE
#error "amen_slice_bounds_test needs the SLICE engine (FELUCCA_SLICE=1, the default)"
#endif

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("amen slice: %-74s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

static void blocks(uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        mix_block(o, CTL);
}
static void voices_off(void)
{
    uint32_t i, k;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            trk[k].v[i].active = 0;
}
static voice_t *voice_of(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    return 0;
}
static uint32_t slice_eng(void)
{
    uint32_t i;
    for (i = 0; i < NENGINES; i++)
        if (ENGINES[i] == &ENG_SLICE)
            return i;
    return 0;
}

/* USR1: nz zones (1: A only, n0 samples; 2: A and B), random IMA codes in bursts (so AUTO finds onsets) */
static void slot_make(uint32_t nz, uint32_t n0)
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)slot0;
    uint8_t *d = slot0 + SLOT_HDR;
    uint32_t i, r = 2468u;
    memset(slot0, 0, sizeof slot0);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = (uint8_t)nz;
    memcpy(h->name, "BOUNDS", 6);
    h->data_len = SLOT_DATA;
    for (i = 0; i < SLOT_DATA; i++) {
        r = r * 1103515245u + 12345u;
        d[i] = i % 250u < 40u ? (uint8_t)(r >> 16) : 0x80u;
    }
    h->zone[0] = (smp_zone_t){.off = 0, .n = n0, .le = n0 - 1u, .rate = 32768u, .root16 = 60 * 16, .lo = 0, .hi = 63};
    h->zone[1] = (smp_zone_t){.off = 1000, .n = 1001, .le = 1000, .rate = 32768u, .root16 = 72 * 16, .lo = 64, .hi = 127};
}

/* play every slice of DIV div on t (MODE mode, REV rev): ONE until it ends (at most 400 blocks), LOOP 300 blocks held */
static uint32_t play_all(track_t *t, uint32_t div, uint32_t mode, uint32_t rev, uint32_t *ended)
{
    uint32_t j, n, k, played = 0;
    t->p[P_E1] = (int16_t)div;
    t->p[P_E4] = (int16_t)mode;
    t->p[P_E5] = (int16_t)rev;
    n = slc_count(slc_get(1), div);
    for (j = 0; j < n; j++) {
        voice_t *v;
        voices_off();
        trk_note_on(t, 60u + j, 100);
        blocks(1);
        v = voice_of(t, 60u + j);
        played += v != 0;
        for (k = 0; v && v->active && k < (mode == SLC_LOOP ? 300u : 400u); k++)
            blocks(1);
        if (mode != SLC_LOOP)
            *ended += v && !v->active;
        trk_note_off(t, 60u + j);
        blocks(8);
    }
    return played == n;
}

/* a held GATE voice on slice j (DIV 4), primed; then f gives it a bad state; 1 = it stopped within two blocks */
static int bad_state(track_t *t, uint32_t j, uint32_t rev, void (*f)(voice_t *, const slc_src_t *), uint32_t *alive)
{
    voice_t *v;
    int stopped;
    voices_off();
    t->p[P_E1] = 0;
    t->p[P_E4] = SLC_GATE;
    t->p[P_E5] = (int16_t)rev;
    trk_note_on(t, 60u + j, 100);
    blocks(1);
    v = voice_of(t, 60u + j);
    if (!v)
        return 0;
    f(v, slc_get(1));
    blocks(2);
    stopped = !v->active;
    if (alive)
        *alive = v->active && v->s[6] != 2;
    trk_note_off(t, 60u + j);
    blocks(8);
    return stopped;
}
static void past_end(voice_t *v, const slc_src_t *s) { v->ph[0] = s->len; v->ph[2] = s->len + 5000u; }
static void far_past(voice_t *v, const slc_src_t *s) { v->ph[0] = s->len + 1000u; v->ph[2] = s->len + 5000u; }
static void wrapped(voice_t *v, const slc_src_t *s) { (void)s; v->ph[0] = 0xFFFFFF00u; v->ph[2] = 0xFFFFFFFFu; }
static void zone_past(voice_t *v, const slc_src_t *s)
{
    (void)s;
    v->s[4] = (int32_t)(((uint32_t)v->s[4] & ~(15u << 3)) | 9u << 3);
}
static void rev_past(voice_t *v, const slc_src_t *s) { v->ph[0] = s->len + 300u; v->s[0] = 0x7FFFFFFF; }
static void step_past(voice_t *v, const slc_src_t *s) { (void)s; v->s[1] = 5000; }

int main(void)
{
    track_t *t = &trk[0];
    uint32_t eng = slice_eng(), ok, div, ended = 0, alive = 0;
    if (SMP_USER_DATA != SLOT_HDR || (uintptr_t)slot0 < (uintptr_t)SMP_DATA ||
        (uintptr_t)slot0 - (uintptr_t)SMP_DATA > 0xF0000000u || !eng) {
        printf("amen slice: the slot image must lie above SMP_DATA within 4 GiB (32-bit offsets), SLICE built\n");
        return 1;
    }
    slot_make(2, 2000);
    smp_user_scan(0);
    check("USR1: two zones, one material of 3001 samples, its slices", usr_nz[0] == 2 && slc_get(1) &&
          slc_usr[0].nseg == 2 && slc_usr[0].len == 3001u && slc_usr[0].nauto > 1u,
          "%u zones, %u samples, %u AUTO slices", usr_nz[0], slc_usr[0].len, slc_usr[0].nauto);

    /* 1 */
    host_tracks_init();
    host_preset(t, eng, 0);
    t->p[P_E0] = 1;                                      /* SRC USR1 */
    t->p[P_E2] = 0;
    t->p[P_E3] = 0;
    t->p[P_E6] = 127;                                    /* no DCAY: a slice plays to its end */
    for (ok = 1, div = 0; div <= SLC_DIV_AUTO; div++)
        ok &= play_all(t, div, SLC_ONE, 0, &ended) & play_all(t, div, SLC_ONE, 1, &ended);
    check("every slice, DIV 4 / 8 / 16 / 32 / AUTO, forwards and REV (ONE): played, each ended", ok &&
          ended == 2u * (4u + 8u + 16u + 32u + slc_usr[0].nauto), "%u ended", ended);
    ok = play_all(t, 0, SLC_LOOP, 0, &ended) & play_all(t, 0, SLC_LOOP, 1, &ended);
    check("LOOP (DIV 4, forwards and REV): every slice held over its end, across the zones", ok, 0);

    /* 2 */
    check("a position at the material's end, its slice end past it: the voice stops", bad_state(t, 3, 0, past_end, 0), 0);
    check("a position far past the material: the voice stops", bad_state(t, 1, 0, far_past, 0), 0);
    check("a wrapped position (0xFFFFFF00): the voice stops", bad_state(t, 2, 0, wrapped, 0), 0);
    check("a zone index past the material's zones (9 of 2): the voice stops", bad_state(t, 0, 0, zone_past, 0), 0);
    check("REV: a position past the material: the voice stops", bad_state(t, 2, 1, rev_past, 0), 0);
    ok = !bad_state(t, 1, 0, step_past, &alive) && alive;
    check("a step index past IMA_STEP (5000): in range, the voice plays on", ok, 0);

    /* 3: slice 3 of 4 is in zone B; the new material is zone A's first 500 samples. Forwards it stops at the next
     * block; REV first plays out the window it decoded before (its own RAM, SLC_RB samples: 4 blocks at this rate) */
    voices_off();
    t->p[P_E1] = 0;
    t->p[P_E4] = SLC_GATE;
    for (ok = 1, div = 0; div < 2u; div++) {
        voice_t *v;
        uint32_t k;
        slot_make(2, 2000);
        smp_user_scan(0);
        t->p[P_E5] = (int16_t)div;
        trk_note_on(t, 63, 100);
        blocks(1);
        v = voice_of(t, 63);
        ok &= v != 0;
        slot_make(1, 500);
        smp_user_scan(0);
        ok &= slc_get(1) && slc_usr[0].len == 500u && slc_usr[0].nseg == 1u;
        for (k = 0; v && v->active && k < 8u; k++)
            blocks(1);
        ok &= v && !v->active && k <= (div ? 6u : 2u);
        trk_note_off(t, 63);
        blocks(8);
        voices_off();
    }
    check("a voice in zone B, the material replaced by 500 samples of zone A: it stops (fwd, REV)", ok, 0);

    printf("amen slice: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}

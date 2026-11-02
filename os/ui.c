/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS: the screen and the controls (docs/AMEN-OS.md).
 *
 * The screen is drawn in bands of 16 rows: each frame every band is drawn into one band buffer (the primitives clip
 * to it), and only a band whose pixels changed goes to the display. No frame buffer: 7.5 KB of RAM. */
#include "font.h"

/* ------------------------------------------------------------------------------------------- the colours --- */
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BG RGB(6, 6, 8)
#define C_PANEL RGB(20, 20, 22)
#define C_RAISE RGB(38, 38, 40)
#define C_GRID RGB(60, 60, 58)
#define C_DIM RGB(100, 100, 92)
#define C_MID RGB(160, 160, 148)
#define C_TEXT RGB(240, 238, 226)
#define C_ACID RGB(204, 255, 26)
#define C_PINK RGB(255, 46, 136)
#define C_CYAN RGB(40, 220, 255)
#define C_RED RGB(255, 60, 50)
#define C_INK RGB(6, 6, 8)

/* --------------------------------------------------------------------------------------- the band renderer --- */
#define SW 240
#define SH 240
#define BAND 16
static uint16_t band[SW * BAND];
static int32_t by0;                                   /* the band being drawn */
static uint32_t band_hash[SH / BAND];

static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h, yy, xx;
    if (x < 0) x = 0;
    if (x1 > SW) x1 = SW;
    if (y < by0) y = by0;
    if (y1 > by0 + BAND) y1 = by0 + BAND;
    for (yy = y; yy < y1; yy++) {
        uint16_t *r = &band[(yy - by0) * SW];
        for (xx = x; xx < x1; xx++)
            r[xx] = c;
    }
}
static void frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y, t, h, c);
    rect(x + w - t, y, t, h, c);
}
static int32_t text_w(const char *s, int32_t sc) { int32_t n = (int32_t)am_strlen(s); return n ? n * 6 * sc - sc : 0; }
static int32_t text(int32_t x, int32_t y, const char *s, int32_t sc, uint16_t c)
{
    if (y + 7 * sc <= by0 || y >= by0 + BAND)          /* (outside the band: only its width) */
        return x + text_w(s, sc) + sc;
    for (; *s; s++, x += 6 * sc) {
        uint32_t ch = (uint8_t)*s, r, b;
        const uint8_t *g;
        if (ch >= 'a' && ch <= 'z') ch -= 32u;
        if (ch < 32u || ch > 126u) ch = '?';
        g = FONT[ch - 32u];
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (g[r] & (0x10u >> b))
                    rect(x + (int32_t)b * sc, y + (int32_t)r * sc, sc, sc, c);
    }
    return x;
}
static void text_c(int32_t cx, int32_t y, const char *s, int32_t sc, uint16_t c) { text(cx - text_w(s, sc) / 2, y, s, sc, c); }
static void text_r(int32_t xr, int32_t y, const char *s, int32_t sc, uint16_t c) { text(xr - text_w(s, sc), y, s, sc, c); }
static void num(char *b, int32_t v)
{
    char t[12];
    int n = 0, neg = v < 0;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (neg) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}
static void num2(char *b, uint32_t v) { b[0] = (char)('0' + v / 10u % 10u); b[1] = (char)('0' + v % 10u); b[2] = 0; }

/* ------------------------------------------------------------------------------------------- the UI state --- */
enum { SC_MAIN, SC_STEP, SC_SOUND, SC_FX, SC_SET, SC_N };
static const char *const SC_NAME[SC_N] = {"MAIN", "STEP", "SOUND", "FX", "SET"};
static struct {
    uint8_t screen, part, sel, bar, page, window, oct;
    uint32_t btn, keys;                               /* last frame's */
    char msg[24];
    uint32_t msg_until;
    uint8_t hot;                                      /* the knob turned last (1..4), its highlight */
    uint32_t hot_until;
    uint8_t live_key[NKEYS];                          /* the value a key played (its note-off) */
    uint8_t live_part[NKEYS];
} ui;
static uint32_t ui_now;

static void say(const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < sizeof ui.msg - 1u; i++)
        ui.msg[i] = s[i];
    ui.msg[i] = 0;
    ui.msg_until = ui_now + 1200u;
}

static uint32_t plen(void) { return prj.len == 32u ? 32u : 16u; }
static step_t *sel_step(void) { return &prj.pat[prj.scene][ui.part][ui.sel % NSTEP]; }
static uint32_t brk_mask(uint32_t part, uint32_t slice)
{
    return JNG_BRK[clamp(prj.snd[part].p[P_E0], 0, JNG_NBRK - 1)][slice % JNG_STEPS];
}
static const char *hit_name(uint32_t m)
{
    if ((m & JK) && (m & JS)) return "K+S";
    if (m & JK) return "KICK";
    if (m & JS) return "SNARE";
    if (m & JC) return "CRASH";
    if (m & JG) return "GHOST";
    if (m & JO) return "OPEN";
    if (m & JR) return "RIDE";
    if (m & JH) return "HAT";
    return "TAIL";
}
static uint16_t hit_col(uint32_t m) { return (m & JK) ? C_ACID : (m & JS) ? C_PINK : (m & 127u) ? C_MID : C_DIM; }
static const char *const NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void note_str(char *b, uint32_t n)
{
    am_strcpy(b, NOTE_N[n % 12u]);
    num(b + am_strlen(b), (int32_t)(n / 12u) - 1);
}
static const char *const FX_NAME[FX_N] = {"--", "REV", "UP", "DN", "CUT"};
static const char *const CH_NAME[4] = {"100%", "75%", "50%", "25%"};

/* -------------------------------------------------------------------------------------- shared pieces --- */
/* the top bar: the screen's tag, the track, the scene, the tempo and the transport */
static void draw_top(void)
{
    char b[16];
    uint16_t tc = ui.part == T_BASS ? C_CYAN : C_ACID;
    rect(0, 0, SW, 24, C_BG);
    rect(0, 0, 64, 22, C_PINK);
    text_c(32, 4, SC_NAME[ui.screen], 2, C_INK);
    rect(66, 0, 66, 22, C_PANEL);
    rect(66, 0, 3, 22, tc);
    text(72, 4, TRACK_NAME[ui.part], 2, prj.snd[ui.part].mute ? C_DIM : C_TEXT);
    if (ui.msg_until > ui_now) {                       /* a message over the right part */
        rect(136, 0, SW - 136, 22, C_TEXT);
        text_c(188, 8, ui.msg, 1, C_INK);
        return;
    }
    b[0] = (char)('A' + prj.scene);
    b[1] = 0;
    rect(136, 0, 22, 22, C_RAISE);
    text_c(147, 4, b, 2, C_ACID);
    if (sq.queued < NSCENE) {
        b[0] = '>';
        b[1] = (char)('A' + sq.queued);
        b[2] = 0;
        text(161, 8, b, 1, C_PINK);
    }
    num(b, prj.bpm);
    text_r(212, 4, b, 2, C_TEXT);
    text(216, 4, sq.playing ? "[" : "]", 2, sq.playing ? C_ACID : C_DIM);
    if (sq.rec)
        text(228, 4, "{", 1, C_RED);
}

/* the knob row: four labelled values, the one turned lit */
static void draw_knobs(const char *const lab[4], const char *const val[4], const int32_t bar[4])
{
    int32_t k;
    rect(0, 196, SW, 44, C_BG);
    for (k = 0; k < 4; k++) {
        int32_t x = k * 60, hot = ui.hot == k + 1 && ui.hot_until > ui_now;
        uint16_t f = hot ? C_ACID : C_PANEL, tc = hot ? C_INK : C_TEXT;
        rect(x + 2, 198, 56, 40, f);
        text(x + 6, 202, lab[k], 1, hot ? C_INK : C_MID);
        if (text_w(val[k], 2) <= 50)                    /* (a long value in the small size) */
            text(x + 6, 214, val[k], 2, tc);
        else
            text(x + 6, 218, val[k], 1, tc);
        if (bar[k] >= 0) {                              /* 0..100 */
            rect(x + 6, 233, 48, 2, hot ? C_INK : C_RAISE);
            rect(x + 6, 233, 48 * bar[k] / 100, 2, hot ? C_INK : C_ACID);
        }
    }
}

/* a slice's transient, w wide, mirrored round mid (as the break map draws it) */
static int32_t lvl(uint32_t m)
{
    return (m & JK) ? 100 : (m & JS) ? 90 : (m & JC) ? 80 : (m & JO) ? 50 : (m & JG) ? 42 : (m & JR) ? 36 : (m & JH) ? 26 : 0;
}
static void transient(int32_t x, int32_t mid, int32_t amp, uint32_t m, uint16_t c)
{
    int32_t k, lv = lvl(m);
    if (!lv) {
        rect(x, mid, 6, 1, C_DIM);
        return;
    }
    for (k = 0; k < 3; k++) {
        int32_t hh = amp * lv / 100 * (k == 0 ? 10 : k == 1 ? 6 : 3) / 10;
        rect(x + k * 2, mid - hh, 2, 2 * hh + 1, c);
    }
}

/* ------------------------------------------------------------------------------------------ MAIN --- */
static void draw_main(void)
{
    int32_t part, i, len = (int32_t)plen(), bar = sq.playing ? sq.step / 16u : ui.bar;
    char b[16];
    for (part = 0; part < NPART; part++) {
        int32_t y = 30 + part * 52, sel = part == ui.part, mute = prj.snd[part].mute;
        uint16_t tc = part == T_BASS ? C_CYAN : C_ACID;
        rect(0, y, SW, 50, sel ? C_PANEL : C_BG);
        rect(0, y, 4, 50, mute ? C_DIM : tc);
        text(8, y + 4, part == T_BRKA ? "A" : part == T_BRKB ? "B" : "BS", 2, mute ? C_DIM : C_TEXT);
        rect(8, y + 22, 24, 24 - meter[part] * 3 / 2, C_RAISE);           /* the meter */
        rect(8, y + 46 - meter[part] * 3 / 2, 24, meter[part] * 3 / 2, mute ? C_DIM : tc);
        for (i = 0; i < 16; i++) {
            int32_t si = bar * 16 + i, x = 38 + i * 12, play = sq.playing && sq.step == si;
            const step_t *s = cur_step((uint32_t)part, (uint32_t)si);
            if (si >= len) break;
            if (play) rect(x - 1, y + 1, 12, 48, C_RAISE);
            if (!s->on) {
                rect(x + 4, y + 24, 2, 2, (i & 3) ? C_GRID : C_DIM);
                continue;
            }
            if (IS_BRK(part)) {
                uint32_t m = brk_mask((uint32_t)part, s->val);
                transient(x + 2, y + 25, 18, m, mute ? C_DIM : play ? C_TEXT : hit_col(m));
                if (s->roll > 1u)
                    rect(x, y + 46, 10, 2, C_PINK);
            } else if (s->flags & F_TIE) {
                rect(x - 1, y + 46 - (int32_t)(s->val % 24u), 12, 3, mute ? C_DIM : C_CYAN);
            } else {
                rect(x, y + 45 - (int32_t)(s->val % 24u), 10, 5, mute ? C_DIM : (s->flags & F_ACC) ? C_PINK : C_CYAN);
            }
        }
        if (mute)
            text_r(234, y + 4, "MUTE", 1, C_PINK);
    }
    am_strcpy(b, "MANGLE ");
    am_strcat(b, STYLE_NAME[prj.style % ST_N]);
    text(8, 187, b, 1, C_DIM);
    {
        static const char *const L[4] = {"LVL A", "LVL B", "LVL BS", "FILTER"};
        char v[4][8];
        const char *vp[4];
        int32_t bars[4], k;
        for (k = 0; k < 3; k++) {
            num(v[k], prj.snd[k].level);
            bars[k] = prj.snd[k].level * 100 / 127;
        }
        if (prj.filter == 64u) am_strcpy(v[3], "OFF");
        else { am_strcpy(v[3], prj.filter < 64u ? "LP" : "HP"); num(v[3] + 2, prj.filter < 64u ? 64 - prj.filter : prj.filter - 64); }
        bars[3] = prj.filter * 100 / 127;
        for (k = 0; k < 4; k++) vp[k] = v[k];
        draw_knobs(L, vp, bars);
    }
}

/* ------------------------------------------------------------------------------------------ STEP --- */
static void draw_step(void)
{
    int32_t i, part = ui.part, len = (int32_t)plen();
    char b[24];
    for (i = 0; i < 16; i++) {
        int32_t si = ui.bar * 16 + i, x = 4 + (i % 8) * 29, y = 30 + (i / 8) * 64;
        const step_t *s = cur_step((uint32_t)part, (uint32_t)si);
        int32_t play = sq.playing && sq.step == si, sel = ui.sel == si;
        uint16_t fill = play ? C_RAISE : C_PANEL;
        if (si >= len) {
            rect(x, y, 26, 60, C_BG);
            continue;
        }
        rect(x, y, 26, 60, fill);
        if (s->on) {
            if (IS_BRK(part)) {
                uint32_t m = brk_mask((uint32_t)part, s->val);
                num2(b, s->val);
                text(x + 2, y + 3, b, 2, C_TEXT);
                transient(x + 9, y + 38, 13, m, hit_col(m));
                if (s->roll > 1u) {                      /* a roll: its hits as dots */
                    uint32_t k;
                    for (k = 0; k < s->roll; k++)
                        rect(x + 3 + (int32_t)k * 6, y + 54, 4, 3, C_PINK);
                }
                if (s->fx)
                    text(x + 2, y + 20, FX_NAME[s->fx], 1, C_CYAN);
                if (s->chance)
                    rect(x + 22, y + 22 + s->chance * 6, 2, 30 - s->chance * 6, C_DIM);
            } else {
                if (s->flags & F_TIE) {
                    text(x + 7, y + 22, "$", 2, C_CYAN);
                } else {
                    note_str(b, s->val);
                    text(x + 2, y + 3, b, 1, C_TEXT);
                    rect(x + 3, y + 50 - (int32_t)(s->val % 36u), 20, 4, (s->flags & F_ACC) ? C_PINK : C_CYAN);
                    if (s->flags & F_SLIDE)
                        text(x + 15, y + 14, "`", 1, C_ACID);
                }
            }
            if (s->flags & F_ACC)
                rect(x, y, 26, 2, C_PINK);
        } else {
            rect(x + 12, y + 29, 2, 2, (i & 3) ? C_GRID : C_DIM);
        }
        if (sel)
            frame(x - 2, y - 2, 30, 64, 2, C_ACID);
    }
    {   /* the selected step's knobs */
        const step_t *s = sel_step();
        static const char *const LB[4] = {"SLICE", "ROLL", "CHANCE", "FX"};
        static const char *const LS[4] = {"NOTE", "TIE", "SLIDE", "ACCENT"};
        char v[4][8];
        const char *vp[4];
        int32_t bars[4] = {-1, -1, -1, -1}, k;
        if (IS_BRK(part)) {
            if (s->on) { num2(v[0], s->val); v[1][0] = 'X'; num(v[1] + 1, s->roll); }
            else { am_strcpy(v[0], "--"); am_strcpy(v[1], "--"); }
            am_strcpy(v[2], CH_NAME[s->chance & 3u]);
            am_strcpy(v[3], FX_NAME[s->fx % FX_N]);
        } else {
            if (s->on) note_str(v[0], s->val); else am_strcpy(v[0], "--");
            am_strcpy(v[1], (s->flags & F_TIE) ? "ON" : "--");
            am_strcpy(v[2], (s->flags & F_SLIDE) ? "ON" : "--");
            am_strcpy(v[3], (s->flags & F_ACC) ? "ON" : "--");
        }
        for (k = 0; k < 4; k++) vp[k] = v[k];
        draw_knobs(IS_BRK(part) ? LB : LS, vp, bars);
        b[0] = 0;
        am_strcpy(b, "STEP ");
        num(b + 5, ui.sel + 1);
        if (IS_BRK(part) && s->on) {
            am_strcat(b, "  ");
            am_strcat(b, hit_name(brk_mask((uint32_t)part, s->val)));
        }
        text(6, 162 + 20, b, 1, C_MID);
        if (len > 16) {
            text_r(234, 182, ui.bar ? "BAR 2/2" : "BAR 1/2", 1, C_DIM);
        }
    }
}

/* ------------------------------------------------------------------------------------------ SOUND --- */
static void param_str(char *b, const param_desc_t *d, int32_t v)
{
    if (d->fmt == F_ENUM && d->names) am_strcpy(b, d->names[clamp(v, d->min, d->max) - d->min]);
    else if (d->fmt == F_SEMI) { if (v > 0) { b[0] = '+'; num(b + 1, v); } else num(b, v); }
    else num(b, v);
}
static void draw_sound(void)
{
    const engine_t *e = eng_of(ui.part);
    const int16_t *p = prj.snd[ui.part].p;
    int32_t i;
    char b[24];
    text(8, 30, e->presets[prj.snd[ui.part].preset % e->npresets].name, 3, C_TEXT);
    text_r(234, 34, ui.page ? "2/2" : "1/2", 1, C_DIM);
    text(8, 56, e->name, 1, C_DIM);
    if (IS_BRK(ui.part)) {                                 /* the break map: 32 slices, the one sounding lit */
        int32_t now = -1, row;
        uint32_t k, best = 0;
        for (k = 0; k < NVOICE; k++)
            if (trk[ui.part].v[k].active && trk[ui.part].v[k].age >= best) {
                best = trk[ui.part].v[k].age;
                now = (int32_t)jng_slice(&trk[ui.part], trk[ui.part].v[k].note);
            }
        text(8 + text_w(e->name, 1) + 8, 56, JNG_BRK_NAMES[clamp(p[P_E0], 0, JNG_NBRK - 1)], 1, C_ACID);
        for (row = 0; row < 2; row++)
            for (i = 0; i < 16; i++) {
                int32_t sl = row * 16 + i, x = 10 + i * 14, mid = 92 + row * 50;
                uint32_t m = brk_mask(ui.part, (uint32_t)sl);
                if (sl == now)
                    rect(x - 2, mid - 22, 12, 45, C_RAISE);
                transient(x, mid, 20, m, sl == now ? C_TEXT : hit_col(m));
                if ((i & 3) == 0)
                    rect(x, mid + 23, 1, 2, C_DIM);
            }
    } else {                                              /* the reese: two saws beating, the sub */
        int32_t x, py = 110, dt = clamp(p[P_E0], 0, 127), beats = 1 + dt / 24;
        for (x = 0; x < 224; x++) {
            int32_t env = sine_i((uint32_t)(x * beats) * (0x80000000u / 224u)), ph, s, y;
            if (env < 0) env = -env;
            env = 8000 + (env * 24767 >> 15);
            ph = (x * 22 * 65536 / 224) & 0xFFFF;
            s = 32767 - ph;
            y = 110 - (s * env >> 15) * 34 / 32767;
            if (x) {
                int32_t a = py < y ? py : y, h = (py < y ? y - py : py - y) + 1;
                rect(8 + x, a, 1, h, C_CYAN);
            }
            py = y;
        }
        for (x = 0; x < 224; x += 3) {
            int32_t y = 166 - (sine_i((uint32_t)x * (0x10000000u / 224u) * 16u) * (2 + p[P_E1] / 20) >> 15);
            rect(8 + x, y, 2, 2, C_PINK);
        }
    }
    {
        const char *lab[4], *vp[4];
        char v[4][12];
        int32_t bars[4], k;
        for (k = 0; k < 4; k++) {
            const param_desc_t *d = &e->edit[ui.page * 4 + k];
            int32_t val = p[P_E0 + ui.page * 4 + k];
            lab[k] = d->label;
            param_str(v[k], d, val);
            vp[k] = v[k];
            bars[k] = d->fmt == F_ENUM ? -1 : (val - d->min) * 100 / (d->max - d->min ? d->max - d->min : 1);
        }
        draw_knobs(lab, vp, bars);
    }
    (void)b;
}

/* ------------------------------------------------------------------------------------------ FX --- */
static void draw_fx(void)
{
    static const char *const T[3] = {"1/8", "3/16", "1/4"};
    int32_t i, n = 1 + prj.dly_time % 3u, f = prj.filter;
    text(8, 30, "DUB DELAY", 2, C_TEXT);
    for (i = 0; i < 8; i++) {                             /* its echoes */
        int32_t h = 50, k;
        for (k = 0; k < i; k++) h = h * prj.dly_fb / 100;
        rect(12 + i * 26 * (n + 1) / 3, 104 - h, 8, h + 1, i ? C_CYAN : C_TEXT);
    }
    text(8, 114, "DJ FILTER", 2, C_TEXT);
    for (i = 0; i < 220; i += 2) {                        /* its curve */
        int32_t cut = f < 64 ? 220 * (f + 4) / 68 : 220 * (f - 60) / 68, y;
        if (f == 64) y = 150;
        else if (f < 64) y = i < cut ? 150 : 150 + (i - cut) / 3;
        else y = i > cut ? 150 : 150 + (cut - i) / 3;
        rect(10 + i, y > 186 ? 186 : y, 2, 2, f == 64 ? C_DIM : C_PINK);
    }
    {
        static const char *const L[4] = {"SEND", "TIME", "FEEDBK", "FILTER"};
        char v[4][8];
        const char *vp[4];
        int32_t bars[4], k;
        num(v[0], prj.snd[ui.part].send);
        bars[0] = prj.snd[ui.part].send * 100 / 127;
        am_strcpy(v[1], T[prj.dly_time % 3u]);
        bars[1] = -1;
        num(v[2], prj.dly_fb);
        bars[2] = prj.dly_fb;
        if (f == 64) am_strcpy(v[3], "OFF");
        else { am_strcpy(v[3], f < 64 ? "LP" : "HP"); num(v[3] + 2, f < 64 ? 64 - f : f - 64); }
        bars[3] = f * 100 / 127;
        for (k = 0; k < 4; k++) vp[k] = v[k];
        draw_knobs(L, vp, bars);
    }
}

/* ------------------------------------------------------------------------------------------ SET --- */
static void draw_set(void)
{
    char b[16];
    num(b, prj.bpm);
    text_c(120, 40, b, 6, C_ACID);
    text_c(120, 90, "BPM", 2, C_MID);
    text(8, 126, AMEN_NAME " " AMEN_VERSION, 1, C_DIM);
    {
        static uint32_t cpu, cpu_t;
        if (ui_now - cpu_t > 500u) { cpu = plat_cpu(); cpu_t = ui_now; }
        am_strcpy(b, "CPU ");
        num(b + 4, (int32_t)cpu);
        am_strcat(b, "%");
        text_r(232, 126, b, 1, cpu > 85u ? C_RED : C_DIM);
    }
    text(8, 140, "SCENE", 1, C_MID);
    b[0] = (char)('A' + prj.scene);
    b[1] = 0;
    text(52, 140, b, 1, C_TEXT);
    text(8, 156, "MIDI IN: BRK A, BRK B, BASS", 1, C_DIM);
    am_strcpy(b, "CH ");
    num(b + 3, prj.midich + 1);
    am_strcat(b, "+");
    text(8, 168, b, 1, C_DIM);
    {
        static const char *const L[4] = {"BPM", "SWING", "LENGTH", "MANGLE"};
        char v[4][10];
        const char *vp[4];
        int32_t bars[4], k;
        num(v[0], prj.bpm);
        bars[0] = (prj.bpm - 60) * 100 / 160;
        num(v[1], prj.swing);
        am_strcat(v[1], "%");
        bars[1] = prj.swing * 2;
        am_strcpy(v[2], plen() == 32u ? "2 BAR" : "1 BAR");
        bars[2] = -1;
        am_strcpy(v[3], STYLE_NAME[prj.style % ST_N]);
        bars[3] = -1;
        for (k = 0; k < 4; k++) vp[k] = v[k];
        draw_knobs(L, vp, bars);
    }
}

/* ---------------------------------------------------------------------------------------- the frame --- */
static void draw_band(void)
{
    rect(0, by0, SW, BAND, C_BG);
    switch (ui.screen) {
    case SC_STEP: draw_step(); break;
    case SC_SOUND: draw_sound(); break;
    case SC_FX: draw_fx(); break;
    case SC_SET: draw_set(); break;
    default: draw_main(); break;
    }
    draw_top();
}
static void screen_update(void)
{
    uint32_t b, i, h;
    for (b = 0; b < SH / BAND; b++) {
        by0 = (int32_t)(b * BAND);
        draw_band();
        h = 2166136261u;
        for (i = 0; i < SW * BAND; i += 3u)
            h = (h ^ band[i] ^ (uint32_t)band[i + 1u] << 16) * 16777619u;
        h ^= band[SW * BAND - 1u];
        if (h != band_hash[b]) {
            band_hash[b] = h;
            plat_lcd_band(b * BAND, BAND, band);
        }
    }
}

/* ------------------------------------------------------------------------------------------- the splash --- */
static void splash(void)
{
    uint32_t b, i;
    for (b = 0; b < SH / BAND; b++) {
        by0 = (int32_t)(b * BAND);
        rect(0, by0, SW, BAND, C_BG);
        text_c(120, 64, "AMEN", 7, C_ACID);
        rect(36, 116, 168, 6, C_PINK);
        for (i = 0; i < 16; i++) {
            uint32_t m = JNG_BRK[0][i];
            transient(28 + (int32_t)i * 12, 150, 16, m, hit_col(m));
        }
        text_c(120, 184, "JUNGLE / DNB / BREAKCORE", 1, C_TEXT);
        text_c(120, 200, "OS " AMEN_VERSION, 1, C_DIM);
        plat_lcd_band(b * BAND, BAND, band);
        band_hash[b] = 0;
    }
}

/* ------------------------------------------------------------------------------------------ the controls --- */
static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* from F3 */
static int white_of(uint32_t key)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (WHITE[i] == key)
            return (int)i;
    return -1;
}
static void knob(uint32_t k, int32_t d)            /* K1..K4 (k 0..3) on the screen shown */
{
    int16_t *p = prj.snd[ui.part].p;
    step_t *s = sel_step();
    ui.hot = (uint8_t)(k + 1u);
    ui.hot_until = ui_now + 900u;
    switch (ui.screen) {
    case SC_MAIN:
        if (k < 3u) prj.snd[k].level = (uint8_t)clamp(prj.snd[k].level + d * 2, 0, 127);
        else prj.filter = (uint8_t)clamp(prj.filter + d * 2, 0, 127);
        if (k == 3u && prj.filter > 61u && prj.filter < 67u) prj.filter = 64;   /* a notch at OFF */
        break;
    case SC_STEP:
        if (IS_BRK(ui.part)) {
            if (k == 0u) {
                if (!s->on) { s->on = 1; s->roll = 1; s->val = (uint8_t)(ui.sel % JNG_STEPS); }
                else s->val = (uint8_t)((s->val + JNG_STEPS * 4u + (uint32_t)d) % JNG_STEPS);
            } else if (k == 1u && s->on) s->roll = (uint8_t)clamp(s->roll + d, 1, 4);
            else if (k == 2u) s->chance = (uint8_t)clamp(s->chance + d, 0, 3);
            else if (k == 3u) s->fx = (uint8_t)clamp(s->fx + d, 0, FX_N - 1);
        } else {
            if (k == 0u) {
                if (!s->on) { s->on = 1; s->val = 28; }
                else s->val = (uint8_t)clamp(s->val + d, 24, 72);
                s->flags &= (uint8_t)~F_TIE;
            } else {
                uint8_t f = k == 1u ? F_TIE : k == 2u ? F_SLIDE : F_ACC;
                if (d > 0) s->flags |= f; else s->flags &= (uint8_t)~f;
                if (k == 1u && d > 0) s->on = 1;
            }
        }
        break;
    case SC_SOUND: {
        const param_desc_t *dd = &eng_of(ui.part)->edit[ui.page * 4u + k];
        int16_t *v = &p[P_E0 + ui.page * 4u + k];
        int32_t step = dd->fmt == F_ENUM || dd->max - dd->min < 40 ? d : d * 2;
        *v = (int16_t)clamp(*v + step, dd->min, dd->max);
        break;
    }
    case SC_FX:
        if (k == 0u) prj.snd[ui.part].send = (uint8_t)clamp(prj.snd[ui.part].send + d * 2, 0, 127);
        else if (k == 1u) prj.dly_time = (uint8_t)clamp(prj.dly_time + d, 0, 2);
        else if (k == 2u) prj.dly_fb = (uint8_t)clamp(prj.dly_fb + d * 2, 0, 95);
        else { prj.filter = (uint8_t)clamp(prj.filter + d * 2, 0, 127); if (prj.filter > 61u && prj.filter < 67u) prj.filter = 64; }
        break;
    default:
        if (k == 0u) prj.bpm = (uint8_t)clamp(prj.bpm + d, 60, 220);
        else if (k == 1u) prj.swing = (uint8_t)clamp(prj.swing + d, 0, 50);
        else if (k == 2u) prj.len = d > 0 ? 32 : 16;
        else prj.style = (uint8_t)((prj.style + ST_N + (d > 0 ? 1u : ST_N - 1u)) % ST_N);
        break;
    }
}

static void save(void)
{
    prj.magic = PROJ_MAGIC;
    prj.size = sizeof prj;
    prj.crc = crc32(&prj, sizeof prj - 4u);
    say(plat_store_save(&prj, sizeof prj) == 0 ? "SAVED" : "SAVE FAILED");
}

static void controls(void)
{
    plat_in_t in;
    uint32_t pressed, released, kon, koff, k;
    plat_input(&in);
    pressed = in.buttons & ~ui.btn;
    released = ui.btn & ~in.buttons;
    kon = in.keys & ~ui.keys;
    koff = ui.keys & ~in.keys;
    ui.btn = in.buttons;
    ui.keys = in.keys;
    master_q15 = (int32_t)((uint32_t)in.pot * in.pot / 32u);   /* MASTER: a square law, 0 .. ~full */

    /* the screens */
    if (pressed & (1u << B_HOME)) ui.screen = SC_MAIN;
    if (pressed & (1u << B_SEQ)) ui.screen = SC_STEP;
    if (pressed & (1u << B_EDIT)) { if (ui.screen == SC_SOUND) ui.page ^= 1u; ui.screen = SC_SOUND; }
    if (pressed & (1u << B_FX)) ui.screen = SC_FX;
    if (pressed & (1u << B_GLO)) ui.screen = SC_SET;
    /* the transport */
    if (pressed & (1u << B_PLAY)) seq_play(!sq.playing);
    if (pressed & (1u << B_REC)) { sq.rec ^= 1u; say(sq.rec ? "REC ON" : "REC OFF"); }
    /* the tools */
    if (pressed & (1u << B_ENV)) {
        mangle(ui.part, prj.style);
        say(STYLE_NAME[prj.style % ST_N]);
    }
    if (pressed & (1u << B_SCL)) {
        prj.snd[ui.part].mute ^= 1u;
        if (prj.snd[ui.part].mute) voice_release(ui.part, -1);
        say(prj.snd[ui.part].mute ? "MUTE" : "UNMUTE");
    }
    if (pressed & (1u << B_SAVE)) save();
    if (pressed & (1u << B_ARP)) seq_repeat(3);       /* REPEAT: 1/16 until a white key picks another */
    if (released & (1u << B_ARP)) seq_repeat(0);
    if (pressed & (1u << B_LFO)) sq.tape = 1;
    if (released & (1u << B_LFO)) sq.tape = 0;
    if (pressed & ((1u << B_OCTDN) | (1u << B_OCTUP))) {
        int up = (pressed >> B_OCTUP) & 1u;
        if (ui.screen == SC_STEP && plen() == 32u) { ui.bar = (uint8_t)up; ui.sel = (uint8_t)(ui.bar * 16u + ui.sel % 16u); }
        else if (ui.screen == SC_SOUND) ui.page = (uint8_t)up;
        else if (IS_BRK(ui.part)) { ui.window = up ? 5 : 0; say(up ? "SLICES 05-31" : "SLICES 00-26"); }
        else { ui.oct = (uint8_t)clamp(ui.oct + (up ? 1 : -1), 0, 2); say(ui.oct == 0 ? "OCT 1" : ui.oct == 1 ? "OCT 2" : "OCT 3"); }
    }

    /* the encoders */
    if (in.enc[E_SELECT]) {
        ui.part = (uint8_t)((ui.part + NPART * 4u + (uint32_t)(in.enc[E_SELECT] > 0 ? 1 : -1)) % NPART);
        ui.page = 0;
    }
    if (in.enc[E_ALGO]) {
        uint32_t sc = ((sq.queued < NSCENE ? sq.queued : prj.scene) + NSCENE + (in.enc[E_ALGO] > 0 ? 1u : NSCENE - 1u)) % NSCENE;
        if (sq.playing) sq.queued = (uint8_t)sc;
        else prj.scene = (uint8_t)sc;
    }
    if (in.enc[E_PRESET]) {
        uint32_t n = npresets(ui.part);
        sound_preset(ui.part, (prj.snd[ui.part].preset + n * 4u + (uint32_t)(in.enc[E_PRESET] > 0 ? 1 : -1)) % n);
        say(eng_of(ui.part)->presets[prj.snd[ui.part].preset].name);
    }
    for (k = 0; k < 4u; k++)
        if (in.enc[E_K1 + k])
            knob(k, in.enc[E_K1 + k]);

    /* the keys */
    for (k = 0; k < NKEYS; k++) {
        int w = white_of(k);
        if (kon & (1u << k)) {
            if ((in.buttons >> B_ARP) & 1u) {          /* REPEAT's rate on the first four white keys */
                if (w >= 0 && w < 4) seq_repeat((uint32_t)w + 1u);
                continue;
            }
            if (ui.screen == SC_STEP && w >= 0) {      /* the steps */
                uint32_t si = ui.bar * 16u + (uint32_t)w;
                step_t *s = &prj.pat[prj.scene][ui.part][si];
                if (si >= plen()) continue;
                if (!s->on) {
                    memset(s, 0, sizeof *s);
                    s->on = 1;
                    s->roll = 1;
                    s->val = IS_BRK(ui.part) ? (uint8_t)(si % JNG_STEPS) : (uint8_t)(28u + ui.oct * 12u);
                } else if (ui.sel == si) {
                    s->on = 0;
                }
                ui.sel = (uint8_t)si;
                continue;
            }
            {                                           /* play the track */
                uint32_t val = IS_BRK(ui.part) ? (k + ui.window) % JNG_STEPS : 29u + k + ui.oct * 12u;   /* BASS: F1 up */
                ui.live_key[k] = (uint8_t)val;
                ui.live_part[k] = ui.part;
                live_key(ui.part, val, 1);
            }
        }
        if (koff & (1u << k)) {
            if (!(ui.screen == SC_STEP && w >= 0))
                live_key(ui.live_part[k], ui.live_key[k], 0);
        }
    }

    /* MIDI in: channel midich + 0 / 1 / 2 -> BRK A / BRK B / BASS; a break's note 60 is slice 0 */
    {
        uint8_t m[3];
        while (plat_midi_get(m)) {
            uint32_t ch = (uint32_t)(m[0] & 15u), part = (ch - prj.midich) & 15u, st = m[0] & 0xF0u;
            if (part >= NPART || (st != 0x90u && st != 0x80u)) continue;
            if (st == 0x90u && m[2])
                ev_post(EV_ON, part, IS_BRK(part) ? ((uint32_t)m[1] + JNG_STEPS * 4u - 60u) % JNG_STEPS : m[1], m[2]);
            else if (!IS_BRK(part))
                ev_post(EV_OFF, part, m[1], 0);
        }
    }
}

/* ------------------------------------------------------------------------------------------------ the LEDs --- */
static void leds(void)
{
    uint32_t k, blink = (ui_now / 120u) & 1u;
    plat_led(B_PLAY, sq.playing && (sq.step & 3u) == 0u ? 1 : sq.playing ? blink : 0);
    plat_led(B_REC, sq.rec);
    plat_led(B_SCL, prj.snd[ui.part].mute);
    plat_led(B_HOME, ui.screen == SC_MAIN);
    plat_led(B_SEQ, ui.screen == SC_STEP);
    plat_led(B_EDIT, ui.screen == SC_SOUND);
    plat_led(B_FX, ui.screen == SC_FX);
    plat_led(B_GLO, ui.screen == SC_SET);
    plat_led(B_ARP, sq.repeat != 0u);
    plat_led(B_LFO, sq.tape);
    plat_led(B_ENV, 0);
    plat_led(B_SAVE, ui.msg_until > ui_now && !am_strcmp(ui.msg, "SAVED"));
    plat_led(B_OCTDN, ui.screen == SC_STEP ? ui.bar == 0u : ui.window == 0u && ui.oct == 0u);
    plat_led(B_OCTUP, ui.screen == SC_STEP ? ui.bar == 1u : ui.window != 0u || ui.oct != 0u);
    for (k = 0; k < NKEYS; k++) {
        int w = white_of(k), on = (int)((ui.keys >> k) & 1u);
        if (ui.screen == SC_STEP && w >= 0) {          /* the steps on, the one playing blinking */
            uint32_t si = ui.bar * 16u + (uint32_t)w;
            on = si < plen() && cur_step(ui.part, si)->on;
            if (sq.playing && sq.step == si) on = !on;
        } else if (IS_BRK(ui.part) && sq.playing) {    /* the slice playing, on its key */
            on |= (int)(((uint32_t)sq.last_note[ui.part] + JNG_STEPS - ui.window) % JNG_STEPS == k);
        }
        plat_led(14u + k, on);
    }
}

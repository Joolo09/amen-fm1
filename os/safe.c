/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS: the SAFE MODE screen (a platform's boot guard fills safe_info_t; os/fm1/main.c). Included after ui.c. */
typedef struct {
    int forced;                                 /* HOME held at power-on (not a failed boot) */
    uint32_t stage;                             /* how far the boot before got (STAGE_NAME) */
    int crash, watchdog;                        /* the cause: a crash (the record below), the watchdog (a hang) */
    uint32_t pc, vec, rets, dbg, emu;           /* the crash record */
    uint32_t in_audio, in_ui;                   /* where it was */
    uint32_t halves, frames, worst_pct, sheds;  /* how long it ran; the audio's worst half, % of its time */
} safe_info_t;

static const char *const STAGE_NAME[] = {"NOTHING", "START", "SCREEN", "INPUT", "USB", "INTERRUPTS", "PROJECT",
                                         "AUDIO START", "RUNNING", "RUNNING 5 S", "SAFE MODE"};
static char *hex8(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
    return b;
}
static void kv(char *b, const char *a, uint32_t v, const char *z)   /* appends a, the number v, z */
{
    char n[12];
    num(n, (int32_t)v);
    b += am_strlen(b);
    am_strcpy(b, a);
    am_strcat(b, n);
    am_strcat(b, z);
}
static void safe_draw(const safe_info_t *si)
{
    uint32_t b, ls = si->stage < sizeof STAGE_NAME / sizeof STAGE_NAME[0] ? si->stage : 0u;
    char t[48], h[12];
    for (b = 0; b < SH / BAND; b++) {
        int32_t y;
        by0 = (int32_t)(b * BAND);
        rect(0, by0, SW, BAND, C_BG);
        rect(0, 0, SW, 30, C_PINK);
        text_c(120, 6, "SAFE MODE", 3, C_INK);
        y = 38;
        if (si->forced) {
            text(6, y, "HOME HELD AT POWER-ON", 1, C_TEXT);
            y += 12;
        } else {
            text(6, y, "THE LAST BOOT STOPPED AT:", 1, C_MID);
            y += 11;
            text(6, y, STAGE_NAME[ls], 2, C_ACID);
            y += 18;
            text(6, y, si->crash ? "CAUSE: A CRASH" : si->watchdog ? "CAUSE: THE WATCHDOG (A HANG)" : "CAUSE: A RESET",
                 1, C_RED);
            y += 11;
            if (si->crash) {
                am_strcpy(t, "PC ");
                am_strcat(t, hex8(h, si->pc));
                kv(t, " VEC ", si->vec, " RETS ");
                am_strcat(t, hex8(h, si->rets));
                text(6, y, t, 1, C_TEXT);
                y += 11;
                am_strcpy(t, "DBG ");
                am_strcat(t, hex8(h, si->dbg));
                am_strcat(t, " EMU ");
                am_strcat(t, hex8(h, si->emu));
                text(6, y, t, 1, C_TEXT);
                y += 11;
            }
            text(6, y, si->in_audio ? "IT WAS IN: THE AUDIO" : si->in_ui ? "IT WAS IN: THE SCREEN" : "IT WAS IN: THE LOOP",
                 1, C_TEXT);
            y += 11;
            t[0] = 0;
            kv(t, "AUDIO HALVES ", si->halves, "");
            kv(t, "  WORST ", si->worst_pct, "%");
            text(6, y, t, 1, C_TEXT);
            y += 11;
            t[0] = 0;
            kv(t, "FRAMES ", si->frames, "");
            kv(t, "  SHED ", si->sheds, "");
            text(6, y, t, 1, C_TEXT);
            y += 11;
        }
        y += 6;
        rect(6, y, 228, 1, C_GRID);
        y += 8;
        text(6, y, "USB: THE INSTALLER WORKS NOW", 1, C_CYAN);
        y += 12;
        text(6, y, "PLAY: TRY AMEN OS AGAIN", 1, C_ACID);
        y += 12;
        text(6, y, "OCT- + OCT+ 5 S: UBOOT", 1, C_MID);
        y += 16;
        text(6, y, "PLEASE TAKE A PHOTO OF THIS", 1, C_DIM);
        y += 11;
        text(6, y, "SCREEN FOR A BUG REPORT.", 1, C_DIM);
        plat_lcd_band(b * BAND, BAND, band);
        band_hash[b] = 0;
    }
}

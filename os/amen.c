/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN OS: the app. A platform includes this file after os/amen.h and its plat_* functions. */
#include "core.c"
#include "ui.c"

static void app_init(void)
{
    memset(&sq, 0, sizeof sq);
    sq.queued = 0xFF;
    sq.bass_held = -1;
    if (plat_store_load(&prj, sizeof prj) != 0 || prj.magic != PROJ_MAGIC || prj.size != sizeof prj ||
        prj.crc != crc32(&prj, sizeof prj - 4u))
        project_default();
    memset(&ui, 0, sizeof ui);
    ui_now = plat_ms();
    splash();
}

static void app_audio(int32_t *out, uint32_t n) { audio_blocks(out, n); }

static void app_frame(void)
{
    ui_now = plat_ms();
    controls();
    leds();
    screen_update();
}

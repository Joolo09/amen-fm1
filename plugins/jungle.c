/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN-FM1: the JUNGLE engine as a Felucca plugin (slot 0 on the FM-1). */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#define JNG_DRUMS 0                  /* the plugin: synthesised hits (the recorded kit, 23 KB, does not fit its 8 KiB slot) */
#include "eng_jungle.c"
FE_PLUGIN(ENG_JUNGLE, FE_ID('j', 'N', 'G', 'L'), 1, FE_NEW, 0)

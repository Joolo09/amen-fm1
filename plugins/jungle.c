/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN-FM1: the JUNGLE engine as a Felucca plugin (slot 0 on the FM-1). */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_jungle.c"
FE_PLUGIN(ENG_JUNGLE, FE_ID('j', 'N', 'G', 'L'), 1, FE_NEW, 0)

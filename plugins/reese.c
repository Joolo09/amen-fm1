/* SPDX-License-Identifier: GPL-3.0-only
 * AMEN-FM1: the REESE engine as a Felucca plugin (slot 1 on the FM-1). */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_reese.c"
FE_PLUGIN(ENG_REESE, FE_ID('r', 'E', 'E', 'S'), 1, FE_NEW, 0)

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_DEMO_H
#define KREISEL_DEMO_H
#include "kreisel.h"

/* COLDWIRE (examples/coldwire.json) at start:
 *   2 (the default, as released): only when there is nothing of the user's to open: no song saved (the FM-1), no session and
 *     no song saved (a desktop). A first start plays something; after that the user's own song comes back.
 *   1: always, instead of the saved song or the session (while developing)
 *   0: never (an empty song when nothing is saved) */
#ifndef KREISEL_DEMO_SONG
#define KREISEL_DEMO_SONG 2
#endif

int k_demo_load(KApp *a);   /* COLDWIRE into the app (0, or -1: it does not read) */
#endif

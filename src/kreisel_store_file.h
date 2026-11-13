/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_STORE_FILE_H
#define KREISEL_STORE_FILE_H
/* SAVE's slots as files under a folder (the SDL build: ~/.kreisel). Not for the FM-1. */
#include "kreisel_save.h"

const KStore *k_store_file(const char *dir);   /* 0 when the path is too long; the folders are made on the first write */

#endif

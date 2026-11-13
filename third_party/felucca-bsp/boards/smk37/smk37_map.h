/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The M-VAVE SMK-37 Pro (and Elite): its flash map and its names, for a build of the FM-1's code on its board. Given to every
 * compile of the SMK-37 build first (`-include`, platforms/fm1hw/build.sh with KREISEL_BOARD=smk37), before any other header, so
 * the FM-1 files' defaults (#ifndef) take these values instead. The FM-1 build never sees this file.
 *
 * Facts (addresses, sizes, names) from fm1-to-smk37, docs/01-hardware.md section 2 and docs/02-update-and-recovery.md
 * (https://github.com/genkigenki/fm1-to-smk37, CC BY-NC 4.0: facts only are used here, none of its text).
 *
 *   0x00000-0x03FFF  header, SPL, boot config         never written
 *   0x04000-0x9CFFF  the app area (PRCT)               the update loader only
 *   0x9D000-0xBAFFF  Fune's store: 9 objects, each an A/B pair of 4 KiB sectors (songs 1..6, presets 1..24: 72 KiB, 0x9D000..
 *                    0xAEFFF); 0xAF000-0xBAFFF stays free (0xB0000 is the stock firmware's VM data)
 *   0xBB000-0xBFFFF  update staging: the staged loader and its record (0xBFF00, where the stock firmware puts it)
 *   0xC0000          BTIF, Bluetooth pairing            NEVER
 *   0xC1000          USRTRIM, the factory calibration   NEVER (read only: losing it breaks the pads and wheels, even on stock)
 *   0xC2000-0xFEFFF  stock data and resources           not written
 *   0xFF000          key_mac                            NEVER
 * No outside plugins: their FM-1 region does not exist here (an empty region). */
#pragma once

#define FM1_BOARD_SMK37 1

/* The model: M-VAVE's updater takes only a unit's own model's package, so each has its own (build.sh KREISEL_SMK_MODEL): "SMK-37 Pro"
 * or "SMK-37 Elite" (the same board). The identity the installer sees (the reference port's custom range is 9xx): the app's, ==
 * the package's; the update loader's. */
#ifndef SMK_MODEL_NAME
#define SMK_MODEL_NAME "SMK-37 Pro"
#endif
#if SMK_LOADER
#define FELUCCA_ID "ota-" SMK_MODEL_NAME "_913"
#define LDR_LCD_HW "../boards/smk37/fm1_lcd_hw.h"
#else
#define FELUCCA_ID SMK_MODEL_NAME "_913"
#endif
/* the name the app writes into the update record for the loader: the stock one (V016), so M-VAVE's updater knows its own loader
 * when it goes back to stock (the Pro's measured; the Elite's taken to follow it) */
#define OTA_REC_NAME "ota-" SMK_MODEL_NAME "_016"

/* the flash parts: 0xEB6014 on the unit measured; the FM-1's two (the same generic commands) are let through as well */
#define FM1_JEDEC_OK(id) ((id) == 0xEB6014u || (id) == 0x856014u || (id) == 0xB36014u)

/* update staging (chip/ota.c): the stock firmware's place, its record at 0xBFF00 */
#define OTA_AREA 0x000BB000u
#define OTA_AREA_LEN 0x5000u

/* the store (platforms/fm1hw/fm1_store.c) */
#define KS_LO 0x0009D000u
#define KS_HI 0x000AF000u
#define KS_PAIR_AT {0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, 0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u}
#define KS_RANGE_TEXT "0x9D000..0xAEFFF"

/* no outside plugins (platforms/fm1hw/fm1_plugins.c): an empty region, and the smallest RAM pool */
#define PL_LO 0x00097000u
#define PL_HI 0x00097000u

/* the boot check (platforms/fm1hw/fm1_bootcheck.c): places a unit has filled, read directly and through the plain XIP window:
 * the Bluetooth pairing, the calibration (its marker "trimdat" at 0xC1224), song 1, key_mac */
#define BOOT_SAMPLE_AT {0xC0000u, 0xC1000u, 0xC1200u, 0x9D000u, 0xFF000u, 0xFFF00u}

/* what the flash driver may write (chip/fm1_flash.h FL_RANGE_OK). The app: the store and the staging, [0x9D000, 0xC0000). The
 * update loader (SMK_LOADER, build.sh): the app area and the staging (its records), never the store. */
#define SMK_APP_LO 0x00004000u
#define SMK_APP_HI 0x0009D000u
#define SMK_DATA_LO 0x0009D000u
#define SMK_STAGE_LO 0x000BB000u
#define SMK_STAGE_HI 0x000C0000u
#if SMK_LOADER
#define FL_RANGE_OK(off, n) (FL_IN(off, n, SMK_APP_LO, SMK_APP_HI) || FL_IN(off, n, SMK_STAGE_LO, SMK_STAGE_HI))
/* loader/ldr_core.c: what it writes and where it looks for update records */
#define LDR_APP_LO SMK_APP_LO
#define LDR_APP_HI SMK_APP_HI
#define LDR_REC_LO SMK_STAGE_LO
#define LDR_REC_HI SMK_STAGE_HI
#define LDR_DATA_LO SMK_STAGE_HI                 /* (no store between the records: an empty range) */
#define LDR_DATA_HI SMK_STAGE_HI
#else
#define FL_RANGE_OK(off, n) FL_IN(off, n, SMK_DATA_LO, SMK_STAGE_HI)
#endif

/* the panel's extras (pads, faders, wheels, the outer keys, the four buttons the FM-1 has not): platforms/smk37/smk37_panel.c,
 * included by platforms/fm1hw/fm1_main.c */
#define FM1_BOARD_EXTRAS "../smk37/smk37_panel.c"

#if !SMK_LOADER
_Static_assert(KS_HI <= SMK_STAGE_LO && KS_LO >= SMK_DATA_LO, "the store between the app and the staging");
_Static_assert(SMK_STAGE_HI <= 0xC0000u, "never the Bluetooth pairing (0xC0000) or the calibration (0xC1000)");
#endif

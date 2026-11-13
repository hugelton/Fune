/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The SMK-37 has no MIDI IN jack: chip/fm1_uart.h's calls (the FM-1's TRS MIDI IN on PH8) do nothing here. PH8 is a row of the
 * SMK's key matrix: driving it as the FM-1's UART input would fight the matrix (fm1-to-smk37 docs/01-hardware.md section 9).
 * The SMK's jack is MIDI OUT on PC7 (UART1 TX), not confirmed on a unit there: not driven by this build. Found ahead of chip/ by
 * the SMK build's include order (platforms/fm1hw/build.sh). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

FM1_INLINE void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t len)
{
    (void)ring;
    (void)len;
}
FM1_INLINE uint32_t fm1_uart1_rx_take(void) { return 0; }

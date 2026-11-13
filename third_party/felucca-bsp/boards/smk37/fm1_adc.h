/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The SMK-37's analog inputs, behind the FM-1's ADC calls (boards/fm1/fm1_adc.h). Three 8:1 muxes share one address, bits 5..3
 * of the key matrix's 595 word (fm1_input.h shifts it out with each column):
 *
 *   address  mux A (ch 1, PA8)  mux B (ch 8, PC9)  mux C (ch 2, PA10)
 *   0..7     the top pads       the bottom pads    faders 1, 2, 3 on 0, 1, 2 and fader 0 on 3; 4 the pitch wheel, 5 the mod
 *                                                  wheel, 6 the pedal jack, 7 the battery
 *   pads on the addresses 3, 0, 1, 2, 4, 6, 7, 5 for pads 0..7 (8..15 on mux B the same way)
 *
 * The scan (fm1_input.h's tick) owns the converter: it starts one conversion a tick, on the mux address latched with that tick's
 * word, and takes it at the next. The SMK's ADC timing (CON 0x400D: ~40 us a conversion; the FM-1's slower setting does not finish
 * within a tick here). The readings land in smk_an; fm1_adc_read gives the FM-1 code what it asks for:
 *   FM1_ADC_MASTER   the MASTER level, which the SMK has no pot for: K1 turns it (a virtual pot, 0..1023)
 *   FM1_ADC_BATT     the battery (mux C, address 7)
 * PB1 is never touched (held low 8 s it resets the chip; the SMK's charger line), nor PB6 (the LCD's clock here).
 *
 * Facts from fm1-to-smk37 docs/01-hardware.md section 6 (verified there; https://github.com/genkigenki/fm1-to-smk37, CC BY-NC 4.0:
 * facts only). */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

#define FM1_ADC_CON_R (*(volatile uint32_t *)0x13100u)
#define FM1_ADC_RES (*(volatile uint32_t *)0x13104u)
#define FM1_WLA_CON0 (*(volatile uint32_t *)0x11900u)
enum { SMK_ADC_A = 1, SMK_ADC_C = 2, SMK_ADC_B = 8 };   /* the muxes' channels */
enum { FM1_ADC_BATT = 100, FM1_ADC_MASTER = 101 };      /* (not channels here: what fm1_adc_read is asked for) */
#define SMK_ADC_CON 0x400Du
#define SMK_PAD_ADDR {3u, 0u, 1u, 2u, 4u, 6u, 7u, 5u}   /* pad p % 8 sits on this address */
#define SMK_FADER_ADDR {3u, 0u, 1u, 2u}                 /* fader f on mux C */
enum { SMK_C_PITCH = 4, SMK_C_MOD = 5, SMK_C_PEDAL = 6, SMK_C_BATT = 7 };

static volatile struct {
    uint16_t a[8], b[8], c[8];  /* the last reading of each mux address (0..1023) */
    uint16_t master;            /* the virtual MASTER pot, 0..1023 (fm1_input.h: K1) */
    uint16_t timeouts;          /* conversions not done after a tick */
    uint32_t sweeps;            /* whole passes over the 24 readings */
} smk_an = {.master = 820u};    /* (80 % at power-on, as the core's own default) */

static void fm1_adc_init(void)
{
    uint32_t a = (1u << 8) | (1u << 10), c = 1u << 9;   /* PA8, PA10, PC9: analog, no pulls, no input buffer */
    FM1_PR(FM1_PA, FM1_DIE) &= ~a;
    FM1_PR(FM1_PA, FM1_PU) &= ~a;
    FM1_PR(FM1_PA, FM1_PD) &= ~a;
    FM1_PR(FM1_PA, FM1_DIR) |= a;
    FM1_PR(FM1_PC, FM1_DIE) &= ~c;
    FM1_PR(FM1_PC, FM1_PU) &= ~c;
    FM1_PR(FM1_PC, FM1_PD) &= ~c;
    FM1_PR(FM1_PC, FM1_DIR) |= c;
}

static void smk_adc_start(uint32_t ch)
{
    FM1_ADC_CON_R = 0;
    if (FM1_WLA_CON0 & (1u << 14))
        FM1_WLA_CON0 &= ~(1u << 14);
    FM1_ADC_CON_R = SMK_ADC_CON | ((ch & 0xFu) << 8);
    FM1_ADC_CON_R |= 0x10u;
    FM1_ADC_CON_R |= 0x40u;
}
static inline int smk_adc_done(void) { return (FM1_ADC_CON_R & 0x80u) != 0; }
static inline uint32_t smk_adc_take(void)
{
    uint32_t v = FM1_ADC_RES & 0x3FFu;
    FM1_ADC_CON_R = 0x40u;
    FM1_ADC_CON_R = 0;
    return v;
}

/* what the FM-1 code reads: never blocks (the scan has the converter) */
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_MASTER)
        return smk_an.master;
    if (ch == FM1_ADC_BATT)
        return smk_an.sweeps ? (int32_t)smk_an.c[SMK_C_BATT] : -1;
    return -1;
}

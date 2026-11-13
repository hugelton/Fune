/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The SMK-37's panel, behind the FM-1's input calls (boards/fm1/fm1_input.h: the same names and the same ids, so the FM-1's code
 * reads it as it is). Written for Fune from the hardware facts alone.
 *
 * The matrix: 16 columns x 8 rows. Four 74HC595 take a 32-bit word from SPI2 (pin group C: CLK PA3, DO PA4; 4 bytes by DMA, the
 * word little-endian in RAM) and show it on a PA1 pulse; PA0 (their output enable) is held low. In the word, bits 31..24 select
 * columns 0..7 and 23..16 columns 8..15 (low, one at a time), bits 15..0 are the button LED lines (low: lit) but for 5..3, the
 * analog muxes' address (fm1_adc.h). The rows come back low = closed on PA2, PA5, PA6, PA7, PA9 (rows 0..4: the key bed), PH6,
 * PH8 (5, 6: the buttons) and PH9 (7: the encoders), with pull-ups. A contact is named col << 4 | row: the rows read while the
 * word for column c is latched are those of contact column c + 1 (mod 16), as the facts were taken.
 *
 * One tick (TIMER5, 10 kHz, fm1_input_tick):
 *   1. the conversion started last tick is taken (fm1_adc.h)
 *   2. the rows are read: the contacts of the column after the latched one
 *   3. the word shifted last tick is latched (PA1); at column 0 a new frame begins (16 ticks, 1.6 ms): the other LED group's common
 *   4. a conversion starts on the mux address now latched (24 readings: 8 addresses x 3 muxes, one a tick, 2.4 ms a sweep)
 *   5. the next column's word goes out by DMA (with the LED lines of its frame's group and the next reading's address)
 *
 * What it gives (the FM-1's ids, as fm1_main.c reads them):
 *   fm1_in.notes         the 27 middle keys F3..G5 (keys 5..31 of the 37), bit n = note key n, debounced
 *   fm1_in.buttons       the 14 of the 18 buttons the FM-1 has (SMK_BTN_AS_FM1): bit = the FM-1's button id
 *   fm1_input_edges(), fm1_input_note_edges(), fm1_enc_take(0..6): as the FM-1's (encoders by the FM-1's ids: SMK_ENC_AS_FM1)
 *   fm1_adc_read(FM1_ADC_MASTER): K1, a virtual MASTER pot (SMK_MASTER_STEP a detent)
 * and the SMK's own (smk_in, for platforms/smk37/smk37_panel.c): every key of the 37 (smk_in.keys, edges, velocity), the four
 * buttons the FM-1 has not, the 16 pads (hits, velocity), and the raw faders, wheels, pedal and battery (smk_an).
 *
 * Keys: two contacts each, the first early in the stroke, the second near the bottom. The time between them (in ticks, 0.1 ms)
 * T = 10 + elapsed is the velocity: x = 1023 - T through the key's 5-point curve (the unit's calibration record when it is sound,
 * else the stock defaults) gives 1..1023, MIDI max(1, v >> 3). A note starts when the second contact has been closed two frames;
 * it ends when the first has been open SMK_KEY_RELEASE frames (the second alone opening keeps it).
 * Buttons: SMK_BTN_PRESS frames closed, SMK_BTN_RELEASE frames open.
 * Encoders: the stock method: a state (A | B << 1) taken when two frames saw it, the last three in a 6-bit history; a count when
 * the last two transitions went the same way (4 a quadrature cycle); a detent is a whole cycle, counted when the knob comes back
 * to its rest state (the one seen at power-on), a lost count forgiven.
 * Pads: a slowly tracking baseline (from 100: down 1 when the reading is under it, up 2 while it is under the curve's first point);
 * a hit from baseline + SMK_PAD_ON, its velocity the peak of the next SMK_PAD_PEAK readings through the pad's curve; let go under
 * baseline + SMK_PAD_OFF twice.
 * Button LEDs: 19 on two commons (bits 17 and 18 of 0x16A0C, the second USB PHY's pads as GPIO, low: on), one group a frame; each
 * LED's level (fm1_led_lv, 0..64 | FM1_ANIM_GLOW, as the FM-1's) as lit frames by a first order sigma-delta.
 *
 * Facts (contacts, pins, registers, the velocity and encoder methods, the calibration record) from fm1-to-smk37,
 * docs/01-hardware.md sections 2.1, 5 and 6 (marked verified there but for the LED table's spare entries;
 * https://github.com/genkigenki/fm1-to-smk37, CC BY-NC 4.0: facts only, none of its text or code). */
#pragma once
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_gpio.h"
#include "fm1_adc.h"

#define SMK_NCOL 16u
#define SMK_NKEY 37u
#define SMK_NBTN 18u
#define SMK_NENC 8u
#define SMK_NPAD 16u
#define SMK_NLED 19u
#define SMK_KEY0 5u                 /* the FM-1's note key 0 (F3) is key 5 of the 37 (C3 = 0) */
#define FM1_NCOL SMK_NCOL           /* (the console's `inp`: per column) */
#define FM1_NKEY 41u                /* the FM-1's LED ids: 0..13 buttons, 14..40 the keys F3..G5 */
#define FM1_NENC 7u
#define FM1_ANIM_FULL 64u
#define FM1_ANIM_GLOW 0x80u
#define SMK_KEY_RELEASE 3u          /* frames the first contact is open: the note ends (~5 ms) */
#define SMK_KEY_STRIKE 2u           /* frames the second contact is closed: the note starts */
#define SMK_BTN_PRESS 2u
#define SMK_BTN_RELEASE 3u
#define SMK_PAD_ON 24u
#define SMK_PAD_OFF 12u
#define SMK_PAD_PEAK 3u
#define SMK_MASTER_STEP 24          /* K1: of 1024 a detent (~40 detents from 0 to full) */
#define SMK_GLOW 2u                 /* the glow under a dark LED (FM1_ANIM_GLOW), of 64 */
#ifndef FM1_INPUT_NOW
#define FM1_INPUT_NOW() fm1_ticks()
#endif

/* the contacts, col << 4 | row: each key's first and second, the buttons, each encoder's A and B */
static const uint8_t SMK_KEY_C1[SMK_NKEY] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xC0, 0xD0, 0xE0, 0xF0, 0xB0, 0x11, 0x21, 0x31, 0x41,
    0x51, 0x61, 0x71, 0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1, 0xF1, 0x64, 0x74, 0x84, 0x94, 0xA4, 0xB4, 0xC4};
static const uint8_t SMK_KEY_C2[SMK_NKEY] = {
    0x52, 0x72, 0x92, 0xB2, 0xD2, 0xF2, 0xE2, 0xC2, 0xA2, 0x82, 0x62, 0x42, 0x22, 0x12, 0x32, 0x33, 0x43, 0x53, 0x63,
    0x73, 0x83, 0x93, 0xA3, 0xB3, 0xC3, 0xD3, 0xE3, 0x23, 0x13, 0x14, 0x24, 0x34, 0x44, 0x54, 0xD4, 0xF4, 0xE4};
/* the buttons in this order: KNOB BANK, FADER BANK, <, >, OCTAVE-, OCTAVE+, ARP, NOTE REPEAT, SCALE, CHORD, GLOBE, BT, PATCH,
 * PARA, FX, SEQ, SEQ PLAY, SEQ REC */
enum { SMK_B_KNOB_BANK, SMK_B_FADER_BANK, SMK_B_LEFT, SMK_B_RIGHT, SMK_B_OCT_DN, SMK_B_OCT_UP, SMK_B_ARP, SMK_B_NOTE_REPEAT,
       SMK_B_SCALE, SMK_B_CHORD, SMK_B_GLOBE, SMK_B_BT, SMK_B_PATCH, SMK_B_PARA, SMK_B_FX, SMK_B_SEQ, SMK_B_PLAY, SMK_B_REC };
static const uint8_t SMK_BTN_AT[SMK_NBTN] = {0x26, 0x76, 0x96, 0x56, 0x66, 0x36, 0x75, 0x65, 0x25, 0x45, 0x15, 0x86, 0x95, 0x55,
                                             0x85, 0x35, 0x16, 0x46};
static const uint8_t SMK_ENC_AT[SMK_NENC][2] = {{0x97, 0xA7}, {0x37, 0x47}, {0x17, 0x27}, {0xB7, 0xC7},
                                                {0x57, 0x67}, {0x77, 0x87}, {0xD7, 0xE7}, {0xF7, 0x07}};
/* the button LEDs (LED k is button k; 18 a spare): group << 4 | the line's bit in the word */
static const uint8_t SMK_LED_AT[SMK_NLED] = {0x0C, 0x12, 0x00, 0x10, 0x0D, 0x1D, 0x02, 0x01, 0x09, 0x08, 0x0B, 0x0A, 0x11,
                                             0x19, 0x18, 0x1A, 0x1C, 0x1E, 0x1B};

/* The FM-1's controls on the SMK's (fm1-to-smk37 docs/04-controls-mapping.md: every FM-1 control keeps a home). The FM-1's button
 * ids: 0 OCT- 1 OCT+ 2 FX 3 SEL 4 ENV 5 LFO 6 EDIT 7 GLO 8 HOME 9 SAVE 10 ARP 11 SEQ 12 PLAY 13 REC; -1: one the FM-1 has not
 * (platforms/smk37/smk37_panel.c). Its encoder ids: 0 SELECT 1 ALGORITHM 2..5 KNOB 1..4 6 PRESETS; -1: K1, the MASTER level. */
static const int8_t SMK_BTN_AS_FM1[SMK_NBTN] = {-1, -1, -1, -1, 0, 1, 10, 5, 3, 4, 7, 9, 8, 6, 2, 11, 12, 13};
static const int8_t SMK_ENC_AS_FM1[SMK_NENC] = {-1, 0, 1, 6, 2, 3, 4, 5};

/* the calibration record (USRTRIM, flash 0xC1000; read only): 5 rising points for each pad, each key and the pedal */
#define SMK_TRIM_PADS 0x008u
#define SMK_TRIM_KEYS 0x0A8u
#define SMK_TRIM_PEDAL 0x21Au
#define SMK_TRIM_MARK 0x224u
static const uint16_t SMK_CURVE_KEY[5] = {5, 640, 800, 920, 980};
static const uint16_t SMK_CURVE_PAD[5] = {50, 550, 720, 850, 950};
static const uint8_t *smk_trim;     /* the record in the plain XIP window once checked (smk_trim_use), else 0: the defaults */

/* ---- what the FM-1 code reads (boards/fm1/fm1_input.h's names) ---- */
static volatile struct {
    uint32_t notes;              /* debounced: bit n = note key n (0 = F3 .. 26 = G5) */
    uint32_t buttons;            /* debounced: bit i = the FM-1's button i */
    uint32_t pressed, released;  /* button edges since the last fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since the last fm1_input_note_edges() */
    int16_t enc_steps[FM1_NENC]; /* + = clockwise */
    uint32_t frames;
} fm1_in;
/* ---- the SMK's own ---- */
static volatile struct {
    uint64_t keys, keys_pressed;        /* all 37 (bit k = key k, C3 = 0), sounding; their press edges */
    uint8_t vel[SMK_NKEY];              /* each key's last strike, MIDI 1..127 */
    uint32_t xbtn, xpressed, xreleased; /* the buttons the FM-1 has not (bit = SMK_B_*), and their edges */
    uint32_t btn;                       /* all 18 (bit = SMK_B_*) */
    uint16_t pads, pads_pressed;        /* hit (bit p = hardware pad p: 0..7 the top row, 8..15 the bottom), their edges */
    uint8_t pad_vel[SMK_NPAD];          /* each pad's last hit, MIDI 1..127 */
    uint16_t pad_base[SMK_NPAD];
    int16_t master_steps;               /* K1's detents (the MASTER pot takes them) */
} smk_in;
static volatile uint8_t fm1_led_lv[FM1_NKEY];   /* each FM-1 LED's level (the main loop's): 0..64 | FM1_ANIM_GLOW */
static volatile uint8_t smk_led_x[SMK_NLED];    /* the LEDs of the buttons the FM-1 has not, by SMK LED number: 0..64 */
static volatile uint32_t smk_btn_led[SMK_NLED]; /* (diagnostics) frames each LED was lit */

#define FM1_GAP_BINS 8u
static volatile struct {
    uint32_t last, gap_max, gap_hist[FM1_GAP_BINS];
    uint32_t on[FM1_NCOL], on_max[FM1_NCOL];
    uint32_t cost_sum, cost_max, ticks;
    uint32_t dim_n, dim_sum;     /* (the FM-1's dim pulses: none here) */
    uint32_t dma_waits;          /* ticks that found the last word still going out */
    uint32_t enc_moves[SMK_NENC];
} fm1_in_stat;

#define SMK_SPI2_CON (*(volatile uint32_t *)0x11E00u)
#define SMK_SPI2_BAUD (*(volatile uint32_t *)0x11E04u)
#define SMK_SPI2_ADR (*(volatile uint32_t *)0x11E0Cu)
#define SMK_SPI2_CNT (*(volatile uint32_t *)0x11E10u)
#define SMK_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
#define SMK_LED_COM (*(volatile uint32_t *)0x16A0Cu)

static int8_t smk__at[SMK_NCOL][8];     /* what each contact is: 0..36 a key's first, 37..73 its second, 74..91 a button, 92.. an
                                         * encoder's A (even) or B (odd); -1 none */
#define SMK_AT_C2 37
#define SMK_AT_BTN 74
#define SMK_AT_ENC 92
static uint32_t smk__word __attribute__((aligned(4)));   /* the word going out by DMA */
static uint8_t smk__sel_sh = 15, smk__sel_lat = 0xFF;    /* the column of the word shifted, of the word latched */
static uint8_t smk__grp_sh, smk__grp_lat;                /* the LED group of each */
static uint8_t smk__ad_sh, smk__ad_lat;                  /* the reading (0..23) each word's address is for */
static uint8_t smk__ad_next, smk__ad_pend = 0xFF;        /* the next reading to address; the conversion running */
static uint16_t smk__led_lines[2];                       /* each group's lit lines this frame (bits of the word, high: lit) */
static uint8_t smk__led_acc[SMK_NLED];
static uint8_t smk__glow = SMK_GLOW;
/* keys */
static uint8_t smk__kst[SMK_NKEY], smk__kc1[SMK_NKEY], smk__kc2[SMK_NKEY];   /* state 0 up, 1 first contact, 2 sounding */
static uint16_t smk__kt0[SMK_NKEY], smk__kt2[SMK_NKEY];
static uint16_t smk__tick;                               /* ticks (0.1 ms), wrapping */
/* buttons, encoders */
static uint8_t smk__bcnt[SMK_NBTN];
static uint8_t smk__eab[SMK_NENC], smk__ecand[SMK_NENC], smk__est[SMK_NENC], smk__ehist[SMK_NENC], smk__erest[SMK_NENC];
static int8_t smk__eacc[SMK_NENC];
/* pads */
static uint8_t smk__pst[SMK_NPAD], smk__pn[SMK_NPAD];
static uint16_t smk__ppk[SMK_NPAD];

static void fm1_led_dim_level(uint32_t lo) { smk__glow = lo ? 1u : SMK_GLOW; }
static uint8_t smk__levels_on;
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }   /* (no power-on sweep here) */
static int fm1_led_anim_on(void) { return 0; }
static void fm1_led_anim_stop(void) {}
static void fm1_led_levels_start(void) { smk__levels_on = 1; }
static int fm1_led_levels_on(void) { return smk__levels_on; }
/* the button LEDs dark (the flash being written stops the scan: no LED left lit on one column meanwhile) */
static void fm1__led_lines(uint32_t rowmask)
{
    (void)rowmask;
    SMK_LED_COM = SMK_LED_COM | 0x1000Fu | (3u << 17);
}

static void smk__word_out(uint32_t w)
{
    smk__word = w;
    SMK_SPI2_CON |= 0x4000u;
    SMK_SPI2_ADR = (uint32_t)(uintptr_t)&smk__word;
    SMK_SPI2_CNT = 4u;
}
static void smk__latch(void)
{
    FM1_PR(FM1_PA, FM1_OUT) |= 1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}
static uint32_t smk__rows(void)
{
    uint32_t na = ~FM1_PR(FM1_PA, FM1_IN), nh = ~FM1_PR(FM1_PH, FM1_IN);
    return ((na >> 2) & 1u) | ((na >> 4) & 0x0Eu) | ((na >> 5) & 0x10u) | ((nh >> 1) & 0x20u) | ((nh >> 2) & 0xC0u);
}

static void fm1_input_init(void)
{
    const uint32_t rows_a = (1u << 2) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 9), rows_h = (1u << 6) | (1u << 8) | (1u << 9);
    const uint32_t outs_a = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4);   /* OE, latch, the 595s' clock and data */
    uint32_t i, c, r;
    for (c = 0; c < SMK_NCOL; c++)
        for (r = 0; r < 8u; r++)
            smk__at[c][r] = -1;
    for (i = 0; i < SMK_NKEY; i++) {
        smk__at[SMK_KEY_C1[i] >> 4][SMK_KEY_C1[i] & 7u] = (int8_t)i;
        smk__at[SMK_KEY_C2[i] >> 4][SMK_KEY_C2[i] & 7u] = (int8_t)(SMK_AT_C2 + i);
    }
    for (i = 0; i < SMK_NBTN; i++)
        smk__at[SMK_BTN_AT[i] >> 4][SMK_BTN_AT[i] & 7u] = (int8_t)(SMK_AT_BTN + i);
    for (i = 0; i < SMK_NENC; i++) {
        smk__at[SMK_ENC_AT[i][0] >> 4][SMK_ENC_AT[i][0] & 7u] = (int8_t)(SMK_AT_ENC + 2u * i);
        smk__at[SMK_ENC_AT[i][1] >> 4][SMK_ENC_AT[i][1] & 7u] = (int8_t)(SMK_AT_ENC + 2u * i + 1u);
        smk__ecand[i] = smk__est[i] = 0xFF;
    }
    for (i = 0; i < SMK_NPAD; i++)
        smk_in.pad_base[i] = 100u;
    FM1_PR(FM1_PA, FM1_DIE) |= rows_a;
    FM1_PR(FM1_PA, FM1_DIR) |= rows_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~rows_a;
    FM1_PR(FM1_PA, FM1_PU) |= rows_a;
    FM1_PR(FM1_PH, FM1_DIE) |= rows_h;
    FM1_PR(FM1_PH, FM1_DIR) |= rows_h;
    FM1_PR(FM1_PH, FM1_PD) &= ~rows_h;
    FM1_PR(FM1_PH, FM1_PU) |= rows_h;
    FM1_PR(FM1_PA, FM1_PU) &= ~outs_a;
    FM1_PR(FM1_PA, FM1_PD) &= ~outs_a;
    FM1_PR(FM1_PA, FM1_OUT) &= ~outs_a;
    FM1_PR(FM1_PA, FM1_DIR) &= ~outs_a;
    fm1__led_lines(0);                                   /* both LED groups off */
    SMK_IOMAP_CON1 = (SMK_IOMAP_CON1 & ~(3u << 16)) | (2u << 16);   /* SPI2 on pin group C */
    SMK_SPI2_CON = 0x4020u;                              /* master, transmit */
    SMK_SPI2_BAUD = 5u;
    SMK_SPI2_CON |= 1u;
    smk__word_out(0xFFFFFFFFu);                          /* no column, every LED line dark */
    {
        uint32_t n;
        for (n = 0; n < 100000u && !(SMK_SPI2_CON & 0x8000u); n++)
            ;
    }
    smk__latch();
}

/* the calibration record (flash 0xC1000, as plain XIP: fm1_flash.h fl_plain_window_init first), taken when its marker is there and
 * every curve rises strictly, as the stock firmware takes it; else the defaults. Never written. */
static int smk_trim_use(const uint8_t *rec)
{
    static const char MARK[8] = "trimdat";
    uint32_t i, j;
    for (i = 0; i < 8u; i++)
        if (rec[SMK_TRIM_MARK + i] != (uint8_t)MARK[i])
            return 0;
    for (i = 0; i < SMK_NPAD + SMK_NKEY + 1u; i++) {
        const uint8_t *q = rec + SMK_TRIM_PADS + i * 10u;
        for (j = 1; j < 5u; j++)
            if ((q[2u * j] | q[2u * j + 1u] << 8) <= (q[2u * j - 2u] | q[2u * j - 1u] << 8))
                return 0;
    }
    smk_trim = rec;
    return 1;
}
/* x (0..1023) through a 5-point curve: 1..1023 */
static uint32_t smk__curve(const uint16_t *def, uint32_t off, uint32_t x)
{
    uint32_t t[5], i;
    for (i = 0; i < 5u; i++)
        t[i] = smk_trim ? (uint32_t)(smk_trim[off + 2u * i] | smk_trim[off + 2u * i + 1u] << 8) : def[i];
    if (x <= t[0])
        return 1;
    if (x > t[4])
        return 1023;
    for (i = 1; i < 4u && x > t[i]; i++)
        ;
    x = (i - 1u) * 256u + (x - t[i - 1u]) * 256u / (t[i] - t[i - 1u]);
    return x ? x : 1u;
}
static uint8_t smk__midi(uint32_t v) { return (uint8_t)(v >> 3 ? v >> 3 : 1u); }

static void smk__key(uint32_t k, uint32_t second, uint32_t closed)
{
    uint8_t *st = &smk__kst[k];
    if (!second) {
        if (closed) {
            smk__kc1[k] = 0;
            if (!*st) {
                *st = 1;
                smk__kt0[k] = smk__tick;
                smk__kc2[k] = 0;
            }
        } else if (*st && ++smk__kc1[k] >= SMK_KEY_RELEASE) {
            uint64_t m = (uint64_t)1 << k;
            *st = 0;
            smk__kc1[k] = 0;
            smk_in.keys &= ~m;
            if (k >= SMK_KEY0 && k < SMK_KEY0 + 27u)
                fm1_in.notes &= ~(1u << (k - SMK_KEY0));
        }
        return;
    }
    if (*st != 1u)
        return;
    if (!closed) {
        smk__kc2[k] = 0;
        return;
    }
    if (!smk__kc2[k])
        smk__kt2[k] = smk__tick;
    if (++smk__kc2[k] >= SMK_KEY_STRIKE) {           /* struck: T in 0.1 ms, x = 1023 - T through the key's curve */
        uint32_t d = (uint16_t)(smk__kt2[k] - smk__kt0[k]), T = d < SMK_NCOL ? 10u : 10u + d;
        uint64_t m = (uint64_t)1 << k;
        smk_in.vel[k] = smk__midi(smk__curve(SMK_CURVE_KEY, SMK_TRIM_KEYS + k * 10u, T > 1022u ? 1u : 1023u - T));
        *st = 2;
        smk_in.keys |= m;
        smk_in.keys_pressed |= m;
        if (k >= SMK_KEY0 && k < SMK_KEY0 + 27u) {
            fm1_in.notes |= 1u << (k - SMK_KEY0);
            fm1_in.notes_pressed |= 1u << (k - SMK_KEY0);
        }
    }
}

static void smk__button(uint32_t b, uint32_t closed)
{
    uint32_t m = 1u << b, on = (smk_in.btn & m) != 0u;
    int f = SMK_BTN_AS_FM1[b];
    if (closed == on) {
        smk__bcnt[b] = 0;
        return;
    }
    if (++smk__bcnt[b] < (on ? SMK_BTN_RELEASE : SMK_BTN_PRESS))
        return;
    smk__bcnt[b] = 0;
    smk_in.btn ^= m;
    if (f >= 0) {
        fm1_in.buttons ^= 1u << f;
        if (on)
            fm1_in.released |= 1u << f;
        else
            fm1_in.pressed |= 1u << f;
    } else {
        smk_in.xbtn ^= m;
        if (on)
            smk_in.xreleased |= m;
        else
            smk_in.xpressed |= m;
    }
}

static void smk__frame(void)                         /* every 16 ticks: the encoders, the LED lines of the next frame */
{
    uint32_t e, k;
    for (e = 0; e < SMK_NENC; e++) {
        uint32_t cur = smk__eab[e];
        if (cur != smk__ecand[e]) {                  /* seen once: a candidate */
            smk__ecand[e] = (uint8_t)cur;
            continue;
        }
        if (smk__est[e] == 0xFFu) {                  /* first: the knob rests here */
            smk__est[e] = smk__erest[e] = (uint8_t)cur;
            smk__ehist[e] = (uint8_t)(cur * 0x15u);
            continue;
        }
        if (cur == smk__est[e])
            continue;
        smk__est[e] = (uint8_t)cur;                  /* seen twice: taken */
        smk__ehist[e] = (uint8_t)(((smk__ehist[e] << 2) | cur) & 0x3Fu);
        k = smk__ehist[e];
        if (k == 0x0Bu || k == 0x12u || k == 0x2Du || k == 0x34u)   /* clockwise (the two last transitions alike) */
            smk__eacc[e]++;
        else if (k == 0x07u || k == 0x1Eu || k == 0x21u || k == 0x38u)
            smk__eacc[e]--;
        fm1_in_stat.enc_moves[e]++;
        if (cur == smk__erest[e]) {                  /* the detent: whole cycles (4 counts), one lost forgiven */
            int32_t a = smk__eacc[e], n = a < 0 ? -a : a, f = SMK_ENC_AS_FM1[e];
            n = n >= 2 ? (n + 2) / 4 : 0;
            if (a < 0)
                n = -n;
            smk__eacc[e] = 0;
            if (n && f >= 0)
                fm1_in.enc_steps[f] = (int16_t)(fm1_in.enc_steps[f] + n);
            else if (n) {
                int32_t v = (int32_t)smk_an.master + n * SMK_MASTER_STEP;
                smk_an.master = (uint16_t)(v < 0 ? 0 : v > 1023 ? 1023 : v);
                smk_in.master_steps = (int16_t)(smk_in.master_steps + n);
            }
        } else if (smk__eacc[e] > 60 || smk__eacc[e] < -60) {
            smk__eacc[e] = 0;
        }
    }
    {   /* the button LEDs: each level as lit frames, the group of the frame ahead (its frames are every other one) */
        uint32_t lines[2] = {0, 0};
        for (k = 0; k < SMK_NLED; k++) {
            int f = k < SMK_NBTN ? SMK_BTN_AS_FM1[k] : -1;
            uint32_t q = smk__levels_on ? (f >= 0 ? fm1_led_lv[f] : smk_led_x[k]) : 0u, a, lv = q & 0x7Fu;
            if ((q & FM1_ANIM_GLOW) && lv < smk__glow)
                lv = smk__glow;
            a = smk__led_acc[k] + lv;
            if (a >= FM1_ANIM_FULL) {
                a -= FM1_ANIM_FULL;
                lines[SMK_LED_AT[k] >> 4] |= 1u << (SMK_LED_AT[k] & 15u);
                smk_btn_led[k]++;
            }
            smk__led_acc[k] = (uint8_t)a;
        }
        smk__led_lines[0] = (uint16_t)lines[0];
        smk__led_lines[1] = (uint16_t)lines[1];
    }
    fm1_in.frames++;
}

static void smk__pad(uint32_t p, uint32_t v)
{
    uint32_t base = smk_in.pad_base[p], m = 1u << p, t0 = smk_trim ? (uint32_t)(smk_trim[SMK_TRIM_PADS + p * 10u] |
                    smk_trim[SMK_TRIM_PADS + p * 10u + 1u] << 8) : SMK_CURVE_PAD[0];
    switch (smk__pst[p]) {
    case 0:                                          /* idle: the baseline follows; a hit starts over it */
        if (v >= base + SMK_PAD_ON) {
            smk__pst[p] = 1;
            smk__pn[p] = 0;
            smk__ppk[p] = (uint16_t)v;
        } else if (v < base) {
            smk_in.pad_base[p] = (uint16_t)(base - 1u);
        } else if (base < t0 && v > base) {
            smk_in.pad_base[p] = (uint16_t)(base + 2u);
        }
        break;
    case 1:                                          /* the hit's peak */
        if (v > smk__ppk[p])
            smk__ppk[p] = (uint16_t)v;
        if (++smk__pn[p] >= SMK_PAD_PEAK) {
            smk_in.pad_vel[p] = smk__midi(smk__curve(SMK_CURVE_PAD, SMK_TRIM_PADS + p * 10u, smk__ppk[p]));
            smk__pst[p] = 2;
            smk__pn[p] = 0;
            smk_in.pads |= (uint16_t)m;
            smk_in.pads_pressed |= (uint16_t)m;
        }
        break;
    default:                                         /* held: let go under the lower threshold twice */
        if (v < base + SMK_PAD_OFF) {
            if (++smk__pn[p] >= 2u) {
                smk__pst[p] = 0;
                smk_in.pads &= (uint16_t)~m;
            }
        } else {
            smk__pn[p] = 0;
        }
        break;
    }
}
/* reading r (0..23): the mux address r / 3, the mux r % 3 (A, B, C) */
static void smk__an_take(uint32_t r)
{
    static const uint8_t PAD_OF_ADDR[8] = {1, 2, 3, 0, 4, 7, 5, 6};   /* (the inverse of SMK_PAD_ADDR) */
    uint32_t ad = r / 3u, mx = r % 3u, v;
    if (!smk_adc_done()) {
        FM1_ADC_CON_R = 0x40u;
        FM1_ADC_CON_R = 0;
        smk_an.timeouts++;
        return;
    }
    v = smk_adc_take();
    if (mx == 0u) {
        smk_an.a[ad] = (uint16_t)v;
        smk__pad(PAD_OF_ADDR[ad], v);
    } else if (mx == 1u) {
        smk_an.b[ad] = (uint16_t)v;
        smk__pad(8u + PAD_OF_ADDR[ad], v);
    } else {
        smk_an.c[ad] = (uint16_t)v;
        if (ad == 7u)
            smk_an.sweeps++;
    }
}

static void fm1_input_tick(void)
{
    static const uint8_t CH[3] = {SMK_ADC_A, SMK_ADC_B, SMK_ADC_C};
    uint32_t t0 = fm1_ticks(), g = t0 - fm1_in_stat.last, b = 0, rows, n;
    fm1_in_stat.last = t0;
    while (b < FM1_GAP_BINS - 1u && g >= (150u * FM1_TICKS_PER_US << b))
        b++;
    fm1_in_stat.gap_hist[b]++;
    if (g > fm1_in_stat.gap_max)
        fm1_in_stat.gap_max = g;
    smk__tick++;
    if (smk__ad_pend != 0xFFu) {                     /* 1. last tick's conversion */
        smk__an_take(smk__ad_pend);
        smk__ad_pend = 0xFF;
    }
    rows = smk__rows();                              /* 2. the contacts of the column after the latched one */
    if (smk__sel_lat != 0xFFu) {
        uint32_t cc = (smk__sel_lat + 1u) & 15u, r;
        fm1_in_stat.on[cc] += g;
        if (g > fm1_in_stat.on_max[cc])
            fm1_in_stat.on_max[cc] = g;
        for (r = 0; r < 8u; r++) {
            int id = smk__at[cc][r];
            uint32_t closed = (rows >> r) & 1u;
            if (id < 0)
                continue;
            if (id < SMK_AT_BTN)
                smk__key(id < SMK_AT_C2 ? (uint32_t)id : (uint32_t)id - SMK_AT_C2, id >= SMK_AT_C2, closed);
            else if (id < SMK_AT_ENC)
                smk__button((uint32_t)id - SMK_AT_BTN, closed);
            else {
                uint32_t e = ((uint32_t)id - SMK_AT_ENC) >> 1, bit = 1u << (((uint32_t)id - SMK_AT_ENC) & 1u);
                smk__eab[e] = (uint8_t)(closed ? smk__eab[e] | bit : smk__eab[e] & ~bit);
            }
        }
        if (cc == 0u)
            smk__frame();
    }
    for (n = 0; n < 2000u && !(SMK_SPI2_CON & 0x8000u); n++)   /* (4 bytes at ~8 MHz: long gone) */
        ;
    if (n)
        fm1_in_stat.dma_waits++;
    smk__latch();                                    /* 3. the word shifted last tick */
    smk__sel_lat = smk__sel_sh;
    smk__ad_lat = smk__ad_sh;
    if (smk__sel_lat == 0u && smk__grp_lat != smk__grp_sh) {   /* a new frame: its LED group's common */
        smk__grp_lat = smk__grp_sh;
        SMK_LED_COM = (SMK_LED_COM & ~(3u << 17)) | 0x1000Fu | (1u << (18u - smk__grp_lat));
    }
    smk_adc_start(CH[smk__ad_lat % 3u]);             /* 4. on the address now latched */
    smk__ad_pend = smk__ad_lat;
    {                                                /* 5. the next column's word */
        uint32_t c = (smk__sel_sh + 1u) & 15u, w, ad = smk__ad_next;
        if (c == 0u)
            smk__grp_sh ^= 1u;
        w = ~(c < 8u ? 0x01000000u << c : 0x00010000u << (c - 8u));
        w &= ~(uint32_t)smk__led_lines[smk__grp_sh];
        w = (w & ~0x38u) | (ad / 3u) << 3;
        smk__sel_sh = (uint8_t)c;
        smk__ad_sh = (uint8_t)ad;
        smk__ad_next = (uint8_t)(ad + 1u >= 24u ? 0u : ad + 1u);
        smk__word_out(w);
    }
    g = fm1_ticks() - t0;
    fm1_in_stat.cost_sum += g;
    fm1_in_stat.ticks++;
    if (g > fm1_in_stat.cost_max)
        fm1_in_stat.cost_max = g;
}

/* main-loop critical section against fm1_input_tick (main loop only: it re-enables interrupts unconditionally) */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

/* detent steps turned since the last call, + = clockwise (the FM-1's encoder ids) */
static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released)
        *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}
/* the SMK's own edges: every key's presses, the extra buttons' presses and releases, the pads' hits */
static void smk_input_edges(uint64_t *keys, uint32_t *xpressed, uint32_t *xreleased, uint32_t *pads)
{
    uint32_t k = fm1__lock();
    *keys = smk_in.keys_pressed;
    *xpressed = smk_in.xpressed;
    *xreleased = smk_in.xreleased;
    *pads = smk_in.pads_pressed;
    smk_in.keys_pressed = 0;
    smk_in.xpressed = smk_in.xreleased = 0;
    smk_in.pads_pressed = 0;
    fm1__unlock(k);
}

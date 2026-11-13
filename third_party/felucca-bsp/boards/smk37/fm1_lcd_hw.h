/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The SMK-37's LCD, behind the FM-1's LCD calls (boards/fm1/fm1_lcd_hw.h: the same names, so the FM-1's code drives it as it is):
 * an ST7789V, 240 x 240, no window offset, on SPI1 pin group A (IOMAP_CON1 bit 4 clear): CLK PB6, DO PB7, CS PC8, D/C PB5 (low:
 * a command). No reset pin (the panel's power-on reset; SWRESET works), no backlight pin (lit whenever the unit is on).
 * The same SPI1 drives the 16 RGB pad LEDs on pin group B (PC10, bit 4 set): smk_pads_send, between two LCD transfers.
 *
 *   fm1_lcd_hw_init()          pins, SPI1 master at BAUD 4 (~12 MHz)
 *   fm1_lcd_tune(cmd)          the panel's own settings, sent by the FM-1 code's lcd_init (FM1_LCD_HAS_TUNE)
 *   fm1_lcd_baud(b)            SPI1 clock = lsb / (b + 1)
 *   fm1_lcd_send_cmd(c)        D/C low, CS low, one byte (then fm1_lcd_wait)
 *   fm1_lcd_send_data(p, n)    D/C high, CS low, DMA n bytes from RAM p
 *   fm1_lcd_wait()             SPI done (or timeout, counted), pending cleared
 *   fm1_lcd_deselect()         CS high
 *   fm1_lcd_backlight(on)      nothing: there is no backlight control
 *   smk_pads_send(p, n)        a pad LED frame (one SPI byte a bit) on group B at BAUD 8, then group A and BAUD 4 again; the LCD
 *                              must be idle (its CS high)
 *
 * Pins, the bus and the panel's settings from fm1-to-smk37 docs/01-hardware.md sections 3 and 4 (marked verified there;
 * https://github.com/genkigenki/fm1-to-smk37, CC BY-NC 4.0: facts only). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_gpio.h"

#define FM1_LCD_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
#define FM1_LCD_SPI_CON (*(volatile uint32_t *)0x11D00u)
#define FM1_LCD_SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define FM1_LCD_SPI_BUF (*(volatile uint32_t *)0x11D08u)
#define FM1_LCD_SPI_ADR (*(volatile uint32_t *)0x11D0Cu)
#define FM1_LCD_SPI_CNT (*(volatile uint32_t *)0x11D10u)
#define SMK_LCD_DC (1u << 5)     /* PB5 */
#define SMK_LCD_CLK (1u << 6)    /* PB6 */
#define SMK_LCD_DO (1u << 7)     /* PB7 */
#define SMK_LCD_CS (1u << 8)     /* PC8 */
#define SMK_PAD_DO (1u << 10)    /* PC10: the pad LEDs' data (group B's DO) */
#define SMK_LCD_BAUD 4u
#define SMK_PAD_BAUD 8u          /* ~6.7 MHz: 1.2 us an LED bit, one SPI byte each */
#define FM1_LCD_HAS_TUNE 1

static uint32_t fm1_lcd_timeouts;

FM1_INLINE void fm1_lcd_hw_init(void)
{
    FM1_LCD_IOMAP_CON1 &= ~0x10u;                     /* group A */
    FM1_PR(FM1_PC, FM1_OUT) |= SMK_LCD_CS;
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_LCD_CS;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_PAD_DO;           /* the pad LEDs' line low between frames (their latch) */
    FM1_PR(FM1_PC, FM1_DIR) &= ~SMK_PAD_DO;
    FM1_PR(FM1_PB, FM1_OUT) &= ~(SMK_LCD_DC | SMK_LCD_CLK | SMK_LCD_DO);
    FM1_PR(FM1_PB, FM1_DIR) &= ~(SMK_LCD_DC | SMK_LCD_CLK | SMK_LCD_DO);
    FM1_LCD_SPI_CON = 0x4021u;
    FM1_LCD_SPI_BAUD = SMK_LCD_BAUD;
}

FM1_INLINE void fm1_lcd_backlight(uint32_t on) { (void)on; }

FM1_INLINE void fm1_lcd_baud(uint32_t b) { FM1_LCD_SPI_BAUD = b; }

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_PR(FM1_PC, FM1_OUT) |= SMK_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_PR(FM1_PB, FM1_OUT) &= ~SMK_LCD_DC;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_PR(FM1_PB, FM1_OUT) |= SMK_LCD_DC;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

/* the panel's settings as it was tuned (after SLPOUT, before INVON and DISPON): porch, gate, VCOM, power, frame rate, gamma */
static void fm1_lcd_tune(void (*cmd)(uint8_t c, const uint8_t *d, uint32_t n))
{
    static const uint8_t T[] = {
        0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
        0xB7, 1, 0x35,
        0xBB, 1, 0x32,
        0xC2, 1, 0x01,
        0xC3, 1, 0x15,
        0xC4, 1, 0x20,
        0xC6, 1, 0x0F,
        0xD0, 2, 0xA4, 0xA1,
        0xE0, 14, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34,
        0xE1, 14, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34,
    };
    uint32_t i = 0;
    while (i < sizeof T) {
        cmd(T[i], &T[i + 2], T[i + 1]);
        i += 2u + T[i + 1];
    }
}

/* a pad LED frame: the LCD idle (CS high), group B and the slower clock for its bytes, then the LCD's again */
static void smk_pads_send(const uint8_t *p, uint32_t n)
{
    FM1_PR(FM1_PC, FM1_OUT) |= SMK_LCD_CS;
    FM1_LCD_SPI_BAUD = SMK_PAD_BAUD;
    FM1_LCD_IOMAP_CON1 |= 0x10u;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
    fm1_lcd_wait();
    FM1_LCD_IOMAP_CON1 &= ~0x10u;
    FM1_PR(FM1_PC, FM1_OUT) &= ~SMK_PAD_DO;
    FM1_LCD_SPI_BAUD = SMK_LCD_BAUD;
}

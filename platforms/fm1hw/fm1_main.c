/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel on the M-VAVE FM-1: the device layer, and only that. The core (src/kreisel.c), the UI (src/ui/) and the
 * audio (src/kreisel_audio.c) are compiled as they are for every platform; this file gives them the FM-1:
 *
 *   screen   240x240 ST7789: the UI canvas (0xAARRGGBB) is sent in 8-row bands, only the bands that changed
 *   input    14 buttons, 7 encoders, the MASTER pot and 27 keys -> the same KActions the SDL panel sends
 *            (platforms/sdl/panel.c is the reference for the mapping)
 *   audio    ALNK0 I2S, double-buffered DMA; k_audio_render in the half-buffer interrupt
 *   LEDs     the buttons of the page in hand, PLAY and REC lit; the lit keys of the page; the rest a dim glow
 *   update   the BSP's update entry (usb.c / ota.c): the installer can replace this firmware over USB-MIDI, and two
 *            boots in a row that do not reach the healthy point drop into UBOOT (as examples/hwtest)
 *
 * One compilation unit, like the BSP's own firmware (chip/libc.c, the BSP's memset / memcpy / memcmp, is not linked: it clashes
 * with newlib's string.h, which the core and the UI include, and newlib's own are there): the hardware headers define static functions and state, so only
 * this file includes them. The hardware layer is third_party/felucca-bsp (MPL-2.0). The structure (boot guard, timer
 * interrupt, update hooks, ST7789 bring-up) follows third_party/felucca-bsp/examples/hwtest/hwtest.c.
 *
 * Build: platforms/fm1hw/build.sh. Not yet run on a device. */
#include <stdint.h>
#define FELUCCA_CDC 1
#define FELUCCA_OTA 1             /* the update entry: the installer can replace this firmware over USB-MIDI */
#ifndef FELUCCA_ID                /* (another board's own: its map header, e.g. boards/smk37/smk37_map.h) */
#define FELUCCA_ID "FM-1_1300"    /* the identity the installer sees, == the package's (fm1pkg.py --product) */
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_timer.h"
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_audio.h"
#include "kreisel_mul.h"   /* (the fade-in's products out of line: the CPU erratum) */
#include "fm1_lcd_hw.h"
#include "fm1_flash.h"
#include "fm1_uart.h"
#define AUDIO_RATE 44117u         /* 24 MHz / 544 TIMER4 ticks per I2S frame (what the I2S really runs at) */
#define HALF_FRAMES 256u          /* frames per half buffer: ~5.8 ms */
#define HALF_WORDS (HALF_FRAMES * 2u)
#if KREISEL_FM1_AUDIO && !defined(FELUCCA_UAC)
#define FELUCCA_UAC 1                           /* the USB audio input: the computer records the FM-1's output over the cable (usb.c) */
#define FELUCCA_UAC_48K 0                       /* (44.1 kHz only: the 48 kHz resampler's table is Felucca's generated uac_fir.h) */
#ifndef FELUCCA_UAC_DEEP
#define FELUCCA_UAC_DEEP 1                      /* a half more in the USB audio ring (4 KB, +5.8 ms): a render may take its whole time and the
                                                 * recording does not crackle (without it, over ~40 % load it could) */
#endif
#endif
#include "usb.c"
#include "ota.c"

#include "kreisel.h"
#include "kreisel_plugin.h"
#include "kreisel_audio.h"
#include "kreisel_clock.h"
#include "kreisel_io.h"
#include "kreisel_song.h"
#include "kreisel_save.h"
#include "draw.h"
#include "gfx.h"
#include "kreisel_version.h"

#ifndef KREISEL_FM1_DIAG
#define KREISEL_FM1_DIAG 0        /* 1: the diagnostics SysEx while running (fm1_diag.py); off, it held MIDI out back
                                   * (the console has stat and crash) */
#endif
#ifndef KREISEL_FM1_AUDIO
#define KREISEL_FM1_AUDIO 1       /* 0: no sound (the UI and the LEDs only) */
#endif
#define OUT_SCALE 4194304.0f      /* 1.0 -> 2^22: 24-bit samples with a -6 dBFS ceiling (as Felucca) */

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];   /* code that runs from RAM: the flash driver (fm1_flash.h) */

static volatile uint32_t ms;
static uint32_t boots_ok;
static struct { uint32_t magic, pending, failed; } guard __attribute__((section(".noinit")));
/* Bring-up diagnostics (they survive a reset, not a power-off): how far the last boot got (stage), the last fault's
 * record, and SAFE mode. A boot that faults, or two that never get healthy, start the next one in SAFE mode: the
 * screen, USB and the update only, so the installer (tools/fm1_install.py, over USB-MIDI, no UBOOT) can always put a
 * new build on; the stage and the record go out every 500 ms as SysEx F0 7D 4B 44 <stage> <safe> <reason>
 * <fault record: 11 words, 5 x 7 bits each> F7 (platforms/fm1hw/fm1_diag.py reads them). A new install leaves it. */
enum { KS_CSTART = 1, KS_LCD, KS_INPUT, KS_USB, KS_IRQ, KS_UI_INIT, KS_K_INIT, KS_AUDIO, KS_LOOP, KS_FRAME, KS_HEALTHY };
static struct {
    uint32_t magic, stage, safe, reason, safe_pending, safe_failed;   /* reason: 1 a fault, 2 a boot loop */
    fm1_crash_t rec;
    uint32_t t_us[12];                          /* when each stage began (us since cstart), the last boot's */
    uint32_t frame_max_us;                      /* the longest main-loop frame (ui_draw + present) */
    uint32_t reset;                             /* this boot's reset: p3_rst | rst_src bit 5 << 8 | wdt_con << 16
                                                 * (p3_rst bit 0 power-on, 2 WDT, 6 soft) */
    uint32_t prev_stage, prev_reset;            /* the boot before: where it stopped, why it reset */
    uint32_t frames;                            /* main-loop frames drawn this boot */
    uint32_t why, prev_why;                     /* a reset this firmware asked for: 1 a fault, 2 an update, 3 UBOOT
                                                 * (0: none of ours: a watchdog, the BSP, power); the boot before's */
    uint32_t frame_last_us, frame_avg_us;       /* the last frame, and the mean (1/8 a frame) after the first */
} kdiag __attribute__((section(".noinit")));
#define KDIAG_MAGIC (0x4B440000u | (uint32_t)sizeof kdiag)   /* "KD" + its size: a build that changes the layout
                                                 * does not read the last one's fields as its own */
static uint32_t prof_n, prof_sum;             /* the console's timing window: frames and their total us since `prof` */
static uint32_t kdiag_t0 __attribute__((section(".noinit")));   /* (set before .bss is cleared) */
static void kstage(uint32_t n)                  /* a stage begins: noted, and the watchdog fed (start-up runs long) */
{
    kdiag.stage = n;
    if (n < 12u)
        kdiag.t_us[n] = (fm1_ticks() - kdiag_t0) / FM1_TICKS_PER_US;
    fm1_wdt_feed();
}
#define KSTAGE(n) kstage(n)

/* ---- MIDI in on the TRS jack (UART1 RX DMA ring, polled from the timer interrupt; the BSP's fm1_uart.h) ----
 * Channel messages go into the BSP's USB-MIDI packet queue (source 2) next to the USB ones; the main loop takes both. */
#define UM_RING 128u
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct { uint32_t rd; uint8_t st, need, got, d0, sysex; } um;
static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {                           /* realtime: the clock, start, continue, stop go to the queue as a CIN 0xF packet (source 2, */
        if (b == 0xF8u || (b >= 0xFAu && b <= 0xFCu))   /* stamped there) for the core's clock follower; the rest (sensing, reset) is dropped */
            midi_enqueue(0xFu | b << 8, 2u);
        return;                                 /* (it does not disturb a running status) */
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u ? (uint8_t)b : 0;
        um.need = ((b & 0xE0u) == 0xC0u) ? 1u : 2u;
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        um.got = 0;
        midi_enqueue((um.st >> 4) | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24, 2u);
    }
}
static void uart_midi_poll(void)
{
    uint32_t n = fm1_uart1_rx_take();
    if (n > UM_RING) {                          /* lost bytes: wait for a new status */
        um.rd = (um.rd + n - UM_RING) & (UM_RING - 1u);
        n = UM_RING;
        um.st = um.got = um.sysex = 0;
    }
    while (n--) {
        um_byte(um_ring[um.rd]);
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
    }
}

/* ---- the 10 kHz interrupt: key scan, the millisecond clock, USB at 2 kHz ---- */
void fm1_timer5_irq(void)
{
    static uint32_t sub, last, acc;
    uint32_t t = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t - last;
    last = t;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        ms++;
        fm1_ms = ms;                            /* the BSP's clock (usb.c stamps MIDI in with it) */
    }
    if (++sub == 5u) {
        sub = 0;
        usb_poll();
        uart_midi_poll();
    }
}
extern void isr_timer5(void);
extern void isr_alnk0(void);

/* ---- screen ---- */
#define SW 240u
#define BAND 8u                   /* rows per transfer */
static uint16_t band_px[2][SW * BAND] __attribute__((aligned(4)));   /* RGB565, high byte first; DMA reads from RAM */
#define CBLK 8u                   /* a band's columns in blocks of SW / CBLK (30 px): only the changed span of a band is sent */
static uint32_t band_sum[SW / BAND][CBLK];
static uint8_t band_valid;
static uint32_t lcd_gamma = 1u;           /* the colour mode (lcd_lut_init) */
static int32_t lcd_sat = 256;             /* saturation, /256 (console `sat N`): the panel shows colours pale */
static void lcd_panel_gamma(void);
static uint8_t lcd_pending;       /* a pixel DMA may still run; CS is low */

static void lcd_sync(void)
{
    if (!lcd_pending)
        return;
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_pending = 0;
}
static void lcd_cmd(uint8_t c, const uint8_t *d, uint32_t n)
{
    static uint8_t buf[16];
    uint32_t i;
    if (n > sizeof buf)
        n = sizeof buf;
    lcd_sync();
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    if (!n)
        return;
    for (i = 0; i < n; i++)
        buf[i] = d[i];
    fm1_lcd_send_data(buf, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}
static void lcd_init(void)
{
    static const uint8_t colmod = 0x55, madctl = 0x00;
    fm1_lcd_hw_init();
    fm1_lcd_backlight(0);                       /* dark until the first frame: the panel's RAM after a reset shows white
                                                 * for a moment (2026-11-07); KS_IRQ lights it after the fill */
    fm1_delay_ms(50);
    lcd_cmd(0x01, 0, 0);                        /* SWRESET */
    fm1_delay_ms(150);
    lcd_cmd(0x11, 0, 0);                        /* SLPOUT */
    fm1_delay_ms(120);
    lcd_cmd(0x3A, &colmod, 1);                  /* RGB565 */
    lcd_cmd(0x36, &madctl, 1);                  /* top-left origin, RGB */
#ifdef FM1_LCD_HAS_TUNE
    fm1_lcd_tune(lcd_cmd);                      /* the board's own panel settings (the SMK-37's: its fm1_lcd_hw.h) */
#endif
    lcd_cmd(0x21, 0, 0);                        /* INVON: this panel is IPS */
    lcd_cmd(0x13, 0, 0);                        /* NORON */
    if (lcd_gamma >= 3u)
        lcd_panel_gamma();
    lcd_cmd(0x29, 0, 0);                        /* DISPON */
    fm1_lcd_baud(4);                            /* 12 MHz */
}
static void lcd_window_x(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1)   /* columns x0..x1, rows y0..y1 */
{
    uint8_t a[4];
    a[0] = 0; a[1] = (uint8_t)x0; a[2] = 0; a[3] = (uint8_t)x1;
    lcd_cmd(0x2A, a, 4);
    a[0] = (uint8_t)(y0 >> 8); a[1] = (uint8_t)y0; a[2] = (uint8_t)(y1 >> 8); a[3] = (uint8_t)y1;
    lcd_cmd(0x2B, a, 4);
    lcd_cmd(0x2C, 0, 0);
}
static void lcd_window(uint32_t y0, uint32_t y1)    /* the full width, rows y0..y1 */
{
    uint8_t a[4];
    a[0] = 0; a[1] = 0; a[2] = 0; a[3] = (uint8_t)(SW - 1u);
    lcd_cmd(0x2A, a, 4);
    a[0] = (uint8_t)(y0 >> 8); a[1] = (uint8_t)y0; a[2] = (uint8_t)(y1 >> 8); a[3] = (uint8_t)y1;
    lcd_cmd(0x2B, a, 4);
    lcd_cmd(0x2C, 0, 0);
}
static void lcd_fill(uint16_t rgb565)
{
    uint32_t i, y;
    uint16_t sw = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
    for (i = 0; i < SW * BAND; i++)
        band_px[0][i] = sw;
    lcd_window(0, SW - 1u);
    for (y = 0; y < SW; y += BAND) {
        lcd_sync();
        fm1_lcd_send_data(band_px[0], SW * BAND * 2u);
        lcd_pending = 1;
    }
    lcd_sync();
}
/* The canvas to the panel. A band whose pixels did not change since it was last sent is skipped: the UI mostly moves
 * a little at a time, and the link (12 MHz, ~77 ms for a whole frame) is the slow part. While one band goes out by DMA
 * the next is converted into the other buffer. */
/* The panel's own curve is flat (colours came out pale, red towards orange): the darker half of each channel is pulled down
 * (out = (3*v*v/255 + v) / 4, about gamma 1.8) while the 5/6/5-bit pack is made. Tables are built once. */
static uint16_t lut_r[32], lut_g[64], lut_b[32];
/* the colour mode (console `gamma N`, while the right one is chosen on the device): 0 none, 1 soft ~1.4, 2 soft ~1.8,
 * 3 the panel's own gamma (a common ST7789 IPS table, PVGAMCTRL/NVGAMCTRL), 4 the panel's and soft ~1.4 */
static void lcd_lut_init(void)
{
    uint32_t i, k = lcd_gamma == 1u || lcd_gamma == 4u ? 2u : lcd_gamma == 2u ? 3u : 0u;   /* k/4 of v*v/255, the rest v */
    for (i = 0; i < 64u; i++) {
        uint32_t v = i * 255u / 63u, o = (v * v / 255u * k + v * (4u - k)) / 4u, q = (o * 63u + 127u) / 255u;
        lut_g[i] = (uint16_t)(q << 5);
        if (i < 32u) {
            v = i * 255u / 31u;
            o = (v * v / 255u * k + v * (4u - k)) / 4u;
            q = (o * 31u + 127u) / 255u;
            lut_r[i] = (uint16_t)(q << 11);
            lut_b[i] = (uint16_t)q;
        }
    }
}
static void lcd_panel_gamma(void)
{
    static const uint8_t PV[14] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    static const uint8_t NV[14] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
    lcd_cmd(0xE0, PV, 14);
    lcd_cmd(0xE1, NV, 14);
}
/* the frame's parts, timed (console `stat`: part_us): the ground, the header, the page, the cards, the cursor and hint, the LCD */
static uint32_t mark_t0, mark_last, part_us[16], acc_us[8];   /* (marks 20 ..: added up over the frame) */
static void fm1_mark(int k)
{
    const uint32_t t = fm1_ticks();
    if (k == 1) { uint32_t q; mark_last = mark_t0; for (q = 0; q < 8; q++) acc_us[q] = 0; }
    if (k >= 1 && k <= 15) part_us[k - 1] = (t - mark_last) / FM1_TICKS_PER_US;
    if (k >= 21 && k <= 28) acc_us[k - 21] += (t - mark_last) / FM1_TICKS_PER_US;
    mark_last = t;
}
static void ui_mark_fm1(int k) { fm1_mark(k); }
uint32_t fm1_part_us(int k) { return (unsigned)k < 16u ? part_us[k] : k >= 20 && k < 28 ? acc_us[k - 20] : 0u; }
static void lcd_present(const GCanvas *cv)
{
    /* A band (BAND rows) is cut into CBLK column blocks, each summed: only the span from the first changed block to the last is
     * converted and sent (2026-11-12: a playhead, a vertical line, changed every band, and each band went out whole: ~77 ms a
     * frame on ROLL and STEP while playing; now a column or two). */
    const uint32_t BW = SW / CBLK;
    uint32_t b, i, buf = 0;
    for (b = 0; b < SW / BAND; b++) {
        const gpix_t *src = cv->px + b * BAND * SW;
        uint32_t k, lo = CBLK, hi = 0;
        for (k = 0; k < CBLK; k++) {
            uint32_t s = 0x9E3779B9u + k, t = 0, r;              /* two sums, by add (carries: alike changes do not cancel) and by */
            for (r = 0; r < BAND; r++) {                         /* position (no multiply: it is slow here) */
                const gpix_t *q = src + r * SW + k * BW;
                for (i = 0; i < BW; i += 2) {
                    s = ((s << 5) | (s >> 27)) + q[i];     t += s;
                    s = ((s << 5) | (s >> 27)) + q[i + 1]; t += s;
                }
            }
            s ^= (t << 16) | (t >> 16);
            if (band_valid && s == band_sum[b][k]) continue;
            band_sum[b][k] = s;
            if (k < lo) lo = k;
            hi = k;
        }
        if (lo > hi) continue;
        {
            const uint32_t x0 = lo * BW, x1 = (hi + 1) * BW - 1u, w = x1 - x0 + 1u;
            uint16_t *d = band_px[buf];
            uint32_t r;
            for (r = 0; r < BAND; r++)
                for (i = 0; i < w; i++) {
                    uint32_t p = GPIX_RGB(src[r * SW + x0 + i]);
                    uint16_t c;
                    if (lcd_sat != 256) {                        /* saturation: each channel away from the grey of the pixel */
                        int32_t rr = (int32_t)(p >> 16 & 255u), g = (int32_t)(p >> 8 & 255u), bl = (int32_t)(p & 255u);
                        if (rr != g || g != bl) {
                            int32_t y = (rr * 77 + g * 150 + bl * 29) >> 8;
                            rr = y + (((rr - y) * lcd_sat) >> 8); g = y + (((g - y) * lcd_sat) >> 8); bl = y + (((bl - y) * lcd_sat) >> 8);
                            rr = rr < 0 ? 0 : rr > 255 ? 255 : rr; g = g < 0 ? 0 : g > 255 ? 255 : g; bl = bl < 0 ? 0 : bl > 255 ? 255 : bl;
                            p = (uint32_t)rr << 16 | (uint32_t)g << 8 | (uint32_t)bl;
                        }
                    }
                    c = (uint16_t)(lut_r[(p >> 19) & 31u] | lut_g[(p >> 10) & 63u] | lut_b[(p >> 3) & 31u]);
                    d[r * w + i] = (uint16_t)((c >> 8) | (c << 8));   /* the panel wants the high byte first */
                }
            lcd_sync();                                          /* the last band is out: the next window may start */
            lcd_window_x(x0, x1, b * BAND, b * BAND + BAND - 1u);
            fm1_lcd_send_data(band_px[buf], w * BAND * 2u);
            lcd_pending = 1;
            buf ^= 1u;
        }
    }
    lcd_sync();
    band_valid = 1;
}

/* ---- audio ---- */
#if KREISEL_FM1_AUDIO
static KAudio audio;
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint8_t audio_on;          /* rendering; the load guard below turns it off */
static volatile uint32_t audio_halves, audio_max_us, audio_over, audio_peak;
static volatile uint32_t audio_pk[2];        /* the header's meter: left and right peaks since the main loop last took them, 0..32767 */
static volatile uint32_t tone_half;          /* console `tone N`: a square of period 2N frames straight to the I2S (tests the output path), 0 = off */
static uint32_t tone_ph;
static volatile uint32_t audio_gap_max_us, audio_last_t, audio_same_half;   /* the longest time between two halves, and halves that came out twice in a row */
static uint32_t sync_max_us;                  /* the longest core -> audio sync (the audio interrupt held off meanwhile) */
static uint32_t audio_prev_half = 2u;
static uint32_t audio_fade;                   /* Q16: 0 at power-on, to 1 over the first 4400 frames */
#define AUDIO_BUDGET_TICKS (HALF_FRAMES * 1000000u / AUDIO_RATE * 90u / 100u * FM1_TICKS_PER_US)
#define AUDIO_HALVES_2S (2u * AUDIO_RATE / HALF_FRAMES)
static volatile uint32_t audio_quiet, audio_off_halves, audio_offs, audio_step_wait;   /* the load guard's: halves with room, halves off, times it went off */
static volatile uint8_t audio_enabled_guard;   /* the guard may turn the sound back on (not while the flash is written: audio_silence) */

void fm1_alnk0_irq(void)                   /* via isr_alnk0 (bsp/chip/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), i, t0 = fm1_ticks(), dt, us;
        if (audio_last_t) {
            uint32_t g = (t0 - audio_last_t) / FM1_TICKS_PER_US;
            if (g > audio_gap_max_us)
                audio_gap_max_us = g;
        }
        audio_last_t = t0;
        if (half == audio_prev_half)
            audio_same_half++;
        audio_prev_half = half;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (tone_half) {
            for (i = 0; i < HALF_WORDS; i += 2u) {
                o[i] = o[i + 1u] = (tone_ph < tone_half) ? (1 << 20) : -(1 << 20);
                if (++tone_ph >= 2u * tone_half)
                    tone_ph = 0;
            }
        } else if (audio_on) {
#if FELUCCA_UAC
            uac_render_start();                              /* (the USB audio input: its ring primed for this render) */
#endif
            k_audio_render_s32(&audio, o, HALF_FRAMES);     /* L,R interleaved, 24-bit left-justified, ceiling 2^22 */
            if (audio_fade < 65536u) {                       /* the first ~0.1 s faded in: no step on the speaker */
                for (i = 0; i < HALF_WORDS; i += 2u) {
                    o[i] = k_mulsh(o[i], (int32_t)audio_fade, 16);
                    o[i + 1u] = k_mulsh(o[i + 1u], (int32_t)audio_fade, 16);
                    audio_fade = audio_fade + 15u > 65536u ? 65536u : audio_fade + 15u;
                }
            }
#if FELUCCA_UAC
            if (uac.feed)                                    /* the output to the computer, while it records: at full level, */
                uac_tap(o, HALF_FRAMES);                     /* whatever the MASTER knob (the knob at 0 sent silence) */
#endif
            {   const int32_t mg = k_audio_master_q12(&audio);   /* MASTER: the headphones and the line out alone (k_audio_master_outside) */
                if (mg < 4096)
                    for (i = 0; i < HALF_WORDS; i++)
                        o[i] = k_mulsh(o[i], mg, 12);
            }
            {   uint32_t pl = audio_pk[0], pr = audio_pk[1];
                for (i = 0; i < HALF_WORDS; i += 2u) {       /* the meter for the diagnostics, and the header's (left, right) */
                    int32_t li = o[i], ri = o[i + 1u];
                    uint32_t m = (uint32_t)(li < 0 ? -li : li) >> 8, ml = (uint32_t)(li < 0 ? -li : li) >> 7, mr = (uint32_t)(ri < 0 ? -ri : ri) >> 7;
                    if (m > audio_peak)
                        audio_peak = m;
                    if (ml > pl) pl = ml;   /* (>> 7: the ceiling 2^22 is 32768) */
                    if (mr > pr) pr = mr;
                }
                audio_pk[0] = pl; audio_pk[1] = pr; }
        } else {
            for (i = 0; i < HALF_WORDS; i++)
                o[i] = 0;
#if FELUCCA_UAC
            if (uac.feed) { uac_render_start(); uac_tap(o, HALF_FRAMES); }   /* (silence to the computer too: its ring kept fed) */
#endif
        }
        fm1_audio_ack_half();
        dt = fm1_ticks() - t0;
        us = dt / FM1_TICKS_PER_US;
        audio_halves++;
        if (us > audio_max_us)
            audio_max_us = us;
        /* The load guard. A half over 90 % of its time: one voice a track fewer (k_audio_voice_cap), at most one step every
         * 8 halves (46 ms), so the released voices have time to end and one burst does not take the cap to the floor; two
         * seconds with no half over: one more again. Still over at one voice, six halves in a row: the sound off (the screen
         * and the keys kept alive), and on again two seconds later at one voice. */
        if (audio_on) {
            if (audio_step_wait) audio_step_wait--;
            if (dt > AUDIO_BUDGET_TICKS) {
                const int cap = k_audio_voice_cap_now();
                audio_quiet = 0;
                if (cap > 1) {
                    audio_over = 0;   /* (counted at one voice only) */
                    if (!audio_step_wait) { k_audio_voice_cap(cap - 1); audio_step_wait = 8u; }
                } else if (++audio_over >= 6u) { audio_on = 0; audio_off_halves = 0; audio_offs++; }
            } else {
                audio_over = 0;
                if (++audio_quiet >= AUDIO_HALVES_2S) {
                    audio_quiet = 0;
                    if (k_audio_voice_cap_now() < 6) k_audio_voice_cap(k_audio_voice_cap_now() + 1);
                }
            }
        } else if (audio_offs && ++audio_off_halves >= AUDIO_HALVES_2S && audio_enabled_guard) {
            k_audio_voice_cap(1);
            audio_over = 0;
            audio_fade = 0;   /* (faded in again: no step) */
            audio_on = 1;   /* (back after the pause, thinned; the voices come back as there is room) */
        }
    }
}
static void audio_silence(void)            /* before the flash is written: its code runs from XIP, which is off then */
{
    audio_enabled_guard = 0;                  /* (the load guard must not turn it back on) */
    audio_on = 0;
    fm1_audio_stop();
}
#else
void fm1_alnk0_irq(void) {}
static void audio_silence(void) {}
#endif

/* ---- the update (ota.c): the installer's M-UPGRADE session over USB-MIDI ---- */
static uint8_t flash_ok;                        /* the expected flash part: the update may write it */
#ifndef FM1_JEDEC_OK
#define FM1_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)
#endif
static uint32_t flash_id;                       /* its JEDEC ID (the console's songinfo) */
static uint32_t ota_now_ms(void) { return ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint8_t *d = p;
    while (n) {                                 /* 256-byte windows with interrupts off */
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void ota_show(uint32_t step, int32_t code)   /* the steps as a bar across the bottom; red on a failure */
{
    uint32_t i, w = step < 9u ? step * 26u : SW;
    uint16_t c = step < 9u ? 0xFFFFu : code == 1 ? 0x07E0u : 0xF800u;
    c = (uint16_t)((c >> 8) | (c << 8));
    for (i = 0; i < SW * BAND; i++)
        band_px[0][i] = i % SW < w ? c : 0;
    lcd_window(SW - BAND, SW - 1u);
    lcd_sync();
    fm1_lcd_send_data(band_px[0], SW * BAND * 2u);
    lcd_pending = 1;
    lcd_sync();
    band_valid = 0;
}
static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;                          /* an intentional reset, not a failed boot */
    guard.failed = 0;
    kdiag.safe = 0;                             /* a new build: a normal boot again */
    kdiag.safe_pending = kdiag.safe_failed = 0;
    usb_detach();
    fm1_delay_ms(30);
    kdiag.why = 2u;
    fm1_enter_update(parm);
}

/* a fault: red screen with the record in hex, then reset; the record in fm1_crash survives (hwtest's `c` shows it).
 * Lines, top to bottom: pc, rets, dbg, sp, vec (0 without a fault record), then the failed boot's reset, this boot's
 * reset, and stage << 16 | reason << 8 | record valid (white 3x5 digits drawn 3x, 8 per line) */
static const uint16_t HEX35[16] = {   /* 3 x 5, rows top to bottom, bit 2 the left column */
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717, 025755, 065656, 034443, 065556,
    074647, 074644};
static void lcd_crash(const fm1_crash_t *c)
{
    const uint32_t v[8] = {c->pc, c->rets, c->dbg, c->sp, c->vec, c->emu, c->uptime_ms, c->count};
    uint32_t y, r, x;
    uint16_t red = (uint16_t)((0xF800u >> 8) | (0xF800u << 8)), white = 0xFFFFu;
    lcd_window(0, SW - 1u);
    for (y = 0; y < SW; y += BAND) {
        lcd_sync();
        for (r = 0; r < BAND; r++)
            for (x = 0; x < SW; x++) {
                uint32_t yy = y + r, line = (yy - 20u) / 24u, gy = (yy - 20u) % 24u, col = (x - 12u) / 12u, gx = (x - 12u) % 12u;
                uint16_t px = red;
                if (yy >= 20u && line < 8u && gy < 15u && x >= 12u && col < 8u && gx < 9u) {
                    uint32_t d = (v[line] >> (28u - 4u * col)) & 15u, bits = HEX35[d] >> (3u * (4u - gy / 3u));
                    if ((bits >> (2u - gx / 3u)) & 1u)
                        px = white;
                }
                band_px[0][r * SW + x] = px;
            }
        fm1_lcd_send_data(band_px[0], SW * BAND * 2u);
        lcd_pending = 1;
    }
    lcd_sync();
}
static void fm1_fault(const fm1_crash_t *c)
{
#if KREISEL_FM1_AUDIO
    fm1_audio_stop();
#endif
    kdiag.rec = *c;
    kdiag.reason = 1u;
    kdiag.safe = 1u;                            /* the next boot: SAFE mode (the record on screen and over MIDI) */
    guard.pending = 0;
    guard.failed = 0;
    lcd_fill(0xF800u);
    fm1_delay_ms(300);
    kdiag.why = 1u;
    fm1_reboot();
}

/* ---- controls: the FM-1's matrix ids, as Felucca's panel table has them (bsp keys 0..13 buttons, 14..40 keys) ---- */
enum { MB_OCTDN = 0, MB_OCTUP = 1, MB_FX = 2, MB_SEL = 3, MB_ENV = 4, MB_LFO = 5, MB_EDIT = 6, MB_GLO = 7,
       MB_HOME = 8, MB_SAVE = 9, MB_ARP = 10, MB_SEQ = 11, MB_PLAY = 12, MB_REC = 13 };
enum { ME_SELECT = 0, ME_ALGO = 1, ME_K1 = 2, ME_K2 = 3, ME_K3 = 4, ME_K4 = 5, ME_PRESETS = 6 };
#define NNOTE 27u
#define NWHITE 16u
static const int8_t BTN_GROUP[14] = {      /* the page family a button opens, -1: it does one thing */
    -1, -1, K_GROUP_FX, K_GROUP_SOUND, K_GROUP_ENV, K_GROUP_LFO, K_GROUP_EDIT, K_GROUP_GLO,
    -1, -1, K_GROUP_ARP, K_GROUP_PATTERN, -1, -1};
/* key n (0 = F3 .. 26 = G5, one a semitone) -> the id the core uses: the white keys 0..15 left to right, then the
 * black ones 16..26. Black: the semitones 1, 3, 5, 8, 10 of each octave from F. */
static uint8_t key_id[NNOTE];
static void keymap_init(void)
{
    uint32_t n, w = 0, b = NWHITE;
    for (n = 0; n < NNOTE; n++) {
        uint32_t pc = n % 12u;
        int black = pc == 1u || pc == 3u || pc == 5u || pc == 8u || pc == 10u;
        key_id[n] = (uint8_t)(black ? b++ : w++);
    }
}

static KApp app;
static uint32_t notes_down;                 /* the keys last sent as down (bit n = note key n) */
static uint32_t btn_down;
static uint8_t input_seen;                  /* something changed since the screen was last drawn */
#ifndef KREISEL_FM1_BOOTCHECK
#define KREISEL_FM1_BOOTCHECK 1                 /* 0 only for an emulator (fm1-emu boots the package's own boot file): never handed out */
#endif
#include "fm1_bootcheck.c"                      /* where it was started from: only M-VAVE's own bootloader, mapped at 0x4000 */
#include "fm1_store.c"                          /* SAVE's songs and presets in flash (A/B sector pairs), ported from Felucca */
#include "fm1_plugins.c"                        /* outside plugins: images in flash, run in place (kreisel_plugin_image.h) */

/* the main loop: every packet that came in (USB or TRS) to the core; any channel */
static void act(KActionType t, int a, int b, uint32_t now);
/* MIDI out over USB (kreisel_io.c's sender): a message as a USB-MIDI event packet, cable 0, into the BSP's out ring (main loop only:
 * its one writer) */
static void usb_midi_send(const uint8_t *m, int n)
{
    const uint32_t st = m[0];
    if (n < 1 || st < 0x80u || (st >= 0xF0u && st < 0xF8u)) return;   /* (channel messages, and the clock's realtime bytes: CIN F, a byte) */
    midi_out_event((st >> 4) | st << 8 | (uint32_t)(n > 1 ? m[1] : 0) << 16 | (uint32_t)(n > 2 ? m[2] : 0) << 24);
}
static void midi_service(void)
{
    if (midi_in_overflow) {                     /* a broken stream: drop it all, stop what sounds */
        mi_r = mi_w;
        midi_in_overflow = 0;
        return;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = pkt >> 8 & 0xFFu;
        uint32_t src = midi_in_source[mi_r % MQ] ? midi_in_source[mi_r % MQ] : 1u, at_ms = midi_in_ms[mi_r % MQ];
        const uint8_t msg[3] = {(uint8_t)st, (uint8_t)(pkt >> 16 & 0x7Fu), (uint8_t)(pkt >> 24 & 0x7Fu)};
        mi_r++;
        /* a packet's message, through the input layer (kreisel_io.c, its FM-1 table: notes, the clock, CC 100 a page, CC 102 play) */
        k_io_midi(&app, (int)src, msg, st >= 0xF8u ? 1 : (st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u ? 2 : 3, at_ms);
        input_seen = 1;
    }
}


static uint32_t last_input_ms;               /* the panel's last input (the LEDs' idle breath waits for quiet) */
static void act(KActionType t, int a, int b, uint32_t now)
{
    k_dispatch(&app, (KAction){t, a, b, now});
    input_seen = 1;
    if (t != K_ACT_MASTER_DELTA) last_input_ms = now;   /* (the pot's drift is no input) */
}

/* a panel button going down / up and an encoder turning, as the core hears them (poll_input and the USB console both) */
static void btn_event(uint32_t i, int down, uint32_t now)
{
    int g = BTN_GROUP[i];
    if (down) {
        if (g >= 0) act(K_ACT_PAGE_GROUP_DOWN, g, 0, now);
        else if (i == MB_HOME) act(K_ACT_HOME_DOWN, 0, 0, now);   /* a short press is HOME, held it opens the settings */
        else if (i == MB_SAVE) act(K_ACT_SAVE, 0, 0, now);   /* a menu row confirmed, or the SAVE page (songs, presets: fm1_store.c) */
        else if (i == MB_PLAY) act(K_ACT_PLAY, 0, 0, now);
        else if (i == MB_REC) act(K_ACT_REC, 0, 0, now);
        else if (i == MB_OCTDN) act(K_ACT_OCTAVE_DELTA, -1, 0, now);
        else if (i == MB_OCTUP) act(K_ACT_OCTAVE_DELTA, 1, 0, now);
    } else if (g >= 0) {
        act(K_ACT_PAGE_GROUP_UP, g, 0, now);
    } else if (i == MB_HOME) {
        act(K_ACT_HOME_UP, 0, 0, now);
    }
}
/* one detent = one step, clockwise +; the mapping of the SDL panel (panel_wheel) */
static void enc_event(uint32_t e, int32_t s, uint32_t now)
{
    if (e == ME_SELECT) act(K_ACT_SELECT_DELTA, s, 0, now);         /* SELECT: the cursor (clockwise: down, the next row); HOME held: the track */
    else if (e == ME_PRESETS) act(K_ACT_SOUND_DELTA, s, 0, now);    /* PRESETS: the user presets */
    else if (e == ME_ALGO) act(K_ACT_TYPE_DELTA, s, 0, now);        /* ALGORITHM: the engine, or the effect in hand on FX */
    else if (e >= ME_K1 && e < ME_K1 + 4u) act(K_ACT_PARAM_DELTA, (int)(e - ME_K1), (int)s * 2, now);
}

static void poll_input(uint32_t now)
{
    uint32_t pr, i, cur, edges;
    int32_t s;
    pr = fm1_input_edges(0);
    for (i = 0; i < 14u; i++) {
        uint32_t m = 1u << i;
        if (pr & m) {
            btn_down |= m;
            btn_event(i, 1, now);
        }
        if ((btn_down & m) && !(fm1_in.buttons & m)) {
            btn_down &= ~m;
            btn_event(i, 0, now);
        }
    }
    /* keys: a press edge counts even if the key is already up again (a tap shorter than one pass of the loop) */
    edges = fm1_input_note_edges();
    cur = fm1_in.notes;
    for (i = 0; i < NNOTE; i++) {
        uint32_t m = 1u << i;
        if ((edges & m) && !(notes_down & m)) {
            act(K_ACT_KEY, key_id[i], 1, now);
            notes_down |= m;
        }
        if ((notes_down & m) && !(cur & m)) {
            act(K_ACT_KEY, key_id[i], 0, now);
            notes_down &= ~m;
        }
    }
    for (i = 0; i < 7u; i++)
        if ((s = fm1_enc_take(i)) != 0) enc_event(i, s, now);
}

/* the MASTER pot's reading (0..1023) as the level, 0..100: the bottom ~6 % of its turn is 0 (the pot's end does not always reach
 * a clean 0) and the top ~2 % is 100, the rest even between */
#define MASTER_ZERO 24                          /* (the taper does the rest: a small dead zone is enough) */
#define MASTER_FULL 1003
static int master_of(int32_t raw)
{
    if (raw <= MASTER_ZERO) return 0;
    if (raw >= MASTER_FULL) return 100;
    return (int)((raw - MASTER_ZERO) * 100 / (MASTER_FULL - MASTER_ZERO));
}
/* the MASTER pot is absolute, the core's level is a delta: send the difference, with a dead band for the ADC's noise */
static void poll_master(uint32_t now)
{
    static uint32_t last;
    int32_t v;
    if (now - last < 40u)
        return;
    last = now;
    v = fm1_adc_read(FM1_ADC_MASTER);
    if (v < 0)
        return;
    v = master_of(v);
    if (v - app.master >= 2 || app.master - v >= 2 || (v == 0 && app.master != 0) || (v == 100 && app.master != 100))   /* (the ends: always reached, under the dead band too) */
        act(K_ACT_MASTER_DELTA, v - app.master, 0, now);
}

/* the battery (after Felucca's, the owner's own: its divider on ADC ch3, a slow mean, the stock thresholds 531 / 561 / 591 for
 * a third, two thirds, full, shown as 1, 3 and 4 bars of 4), and USB power as the charging battery: there is no charger line,
 * a host on the USB is taken for power. Read once a second, between the MASTER pot's readings. */
static int32_t batt_raw;                        /* (the console's stat: batt_raw, batt) */
static void poll_battery(uint32_t now)
{
    static uint32_t last, acc;   /* acc: the average x 8 (Q3: a /8 step on the plain value never closed the last 7 counts) */
    static uint8_t tried;
    int32_t raw, b, lvl;
    if (tried && now - last < 1000u)   /* (once a second, a failed read too: the read blocks) */
        return;
    tried = 1;
    last = now;
    b = fm1_adc_read(FM1_ADC_BATT);
    if (b <= 0)
        return;
    acc = acc ? acc + (uint32_t)b - acc / 8u : (uint32_t)b * 8u;
    raw = (int32_t)((acc + 4u) / 8u);
    batt_raw = raw;
    lvl = raw >= 591 ? 4 : raw >= 561 ? 3 : raw >= 531 ? 1 : 0;
    k_set_battery(&app, lvl, usb.config && !usb.suspended);
}

#ifdef FM1_BOARD_EXTRAS
#include FM1_BOARD_EXTRAS                       /* another board's controls past the FM-1's (the SMK-37's: platforms/smk37/smk37_panel.c) */
#endif

/* the page family a page belongs to (its button lit) */
static int group_of_page(KPage p)
{
    return K_IS_HOME(p) || p == K_PAGE_MIXER ? -1 :   /* HOME (the mixer, the macros, TRACKS) is no family: HOME lit below */
           p == K_PAGE_STEP || p == K_PAGE_DRUM || p == K_PAGE_ROLL ? K_GROUP_PATTERN :
           p == K_PAGE_EDIT ? K_GROUP_EDIT :
           p == K_PAGE_SONG || p == K_PAGE_BUS1 || p == K_PAGE_BUS2 ? K_GROUP_GLO :   /* (GLO: the song's own) */
           p == K_PAGE_LFO ? K_GROUP_LFO : p == K_PAGE_ARP || p == K_PAGE_SCL ? K_GROUP_ARP :
           p == K_PAGE_SOUND ? K_GROUP_SOUND : p == K_PAGE_FX ? K_GROUP_FX : K_GROUP_ENV;
}
/* The LEDs as levels (the BSP's fm1_led_lv, drawn by the scan as the power-on sweep: lit frames /64 over the glow), from
 * here every 20 ms. Each LED aims at a level and moves to it: up at once, down by a fade (~0.15 s to a tenth), so a press
 * flashes and dies away rather than blinking off. Aims: the glow under every LED; the page's family button, PLAY and REC
 * running, HOME on HOME, the keys the page lights (steps, the lane) lit; a button or key held lit; OCT- / OCT+ by how far
 * the octave is shifted (a half and full). Quiet for IDLE_MS, stopped: everything breathes together, slowly in and out */
#ifndef FM1_LED_FADE
#define FM1_LED_FADE 0              /* 1: the LEDs fade down (the buttons ~0.13 s, the keys ~60 ms); 0: off at once */
#endif
#define LV_FULL 64u
#define LV_IDLE_MS 20000u
#define LV_FLOOR 4u                         /* of 64: below it (~5 %) an LED is off, not a flickering trickle of PWM */
#define LV_BREATH_MS 4096u                  /* a breath (a power of 2) */
static uint16_t lv_cur[41];                 /* each LED's level, x 256 */
/* the power-on wave: the panel lit from the top down, a row at a time, each row up at once and fading out:
 * FX SEL ENV LFO EDIT GLO, then OCT- OCT+, then HOME SAVE ARP SEQ, then the black keys, then the white ones; LV_WAVE_GAP_MS
 * between the rows. Its level for an LED, 0 once it is over (or for PLAY and REC, not in it). */
#define LV_WAVE_GAP_MS 140u
#define LV_WAVE_RISE_MS 260u                /* (softer: each row comes up gently, then a long tail) */
#define LV_WAVE_FADE_MS 700u
static uint32_t lv_wave(uint32_t i, uint32_t t)
{
    uint32_t row, x;
    if (i < 14u) row = i == MB_OCTDN || i == MB_OCTUP ? 1u : i >= MB_FX && i <= MB_GLO ? 0u : i >= MB_HOME && i <= MB_SEQ ? 2u : 9u;
    else { const uint32_t pc = (i - 14u) % 12u; row = pc == 1u || pc == 3u || pc == 5u || pc == 8u || pc == 10u ? 3u : 4u; }
    if (row == 9u || t < row * LV_WAVE_GAP_MS) return 0;
    x = t - row * LV_WAVE_GAP_MS;
    if (x < LV_WAVE_RISE_MS) {   /* up: a smoothstep */
        x = x * 256u / LV_WAVE_RISE_MS;
        return LV_FULL * (x * x * (768u - 2u * x) >> 16) / 256u;
    }
    x -= LV_WAVE_RISE_MS;
    if (x >= LV_WAVE_FADE_MS) return 0;
    x = (LV_WAVE_FADE_MS - x) * 256u / LV_WAVE_FADE_MS;   /* down: a smoothstep too, slow at both ends */
    return LV_FULL * (x * x * (768u - 2u * x) >> 16) / 256u;
}
static void leds_update(uint32_t now)
{
    static uint32_t wave_t0;
    static uint8_t wave_on = 1;
    uint8_t aim[41];
    uint32_t i, br = 0;
    const int idle = 0;   /* (no breath while idle) */
    const uint32_t leds = app.set_v[K_SET_LEDS];   /* LEDS: 0 ON, 1 DIM (a third), 2 OFF (the glow too) */
    if (fm1_led_anim_on() && !fm1_led_levels_on())   /* the power-on sweep has the LEDs; then they are ours */
        return;
    if (!fm1_led_levels_on()) {
        for (i = 0; i < 41u; i++) lv_cur[i] = 0;
        fm1_led_levels_start();
    }
    if (idle) {   /* the breath: a smoothstep of a triangle, 4 .. 26 /64 */
        uint32_t x = (now - last_input_ms - LV_IDLE_MS) & (LV_BREATH_MS - 1u);
        x = (x & (LV_BREATH_MS / 2u) ? LV_BREATH_MS - 1u - x : x) * 256u / (LV_BREATH_MS / 2u);   /* 0..255 */
        x = x * x * (768u - 2u * x) >> 16;
        br = LV_FLOOR + x * 22u / 255u;   /* (from the floor up: the breath never dips into the flicker) */
    }
    for (i = 0; i < 41u; i++) aim[i] = 0;
    for (i = 0; i < 14u; i++) {
        int g = BTN_GROUP[i];
        if ((g >= 0 && group_of_page(app.page) == g) || (i == MB_PLAY && app.playing) || (i == MB_REC && app.recording) ||
            (i == MB_HOME && (app.page == K_PAGE_HOME || app.page == K_PAGE_MIXER)) || (btn_down & (1u << i)))
            aim[i] = LV_FULL;
    }
    if (app.octave < 0) aim[MB_OCTDN] = (uint8_t)(app.octave <= -2 ? LV_FULL : LV_FULL / 3u);
    if (app.octave > 0) aim[MB_OCTUP] = (uint8_t)(app.octave >= 2 ? LV_FULL : LV_FULL / 3u);
    {   /* a yes or no waits: its keys blink, 0.3 s on, 0.3 s off (OCT+ yes, OCT- no; SAVE: overwrite?) */
        const int ask = k_asks(&app), on = (now / 300u) & 1u ? 0 : 1;
        if (ask == K_ASK_OCT) { aim[MB_OCTUP] = on ? LV_FULL : 0; aim[MB_OCTDN] = on ? LV_FULL : 0; }
        if (ask == K_ASK_SAVE) aim[MB_SAVE] = on ? LV_FULL : 0;
    }
    const int arp_on = !k_is_drum(&app, app.track) && app.params[app.track][K_PAGE_ARP][0] != 0;
    /* a drum track, on every page: its white keys are the lane's sixteen steps, in quarters of the light: a step on
     * 75 %, a step off dark but the beats' (1, 5, 9, 13) at 25 %; the step playing turns it over (on: dark, off: full) */
    const int drum = k_is_drum(&app, app.track);
    for (i = 0; i < NNOTE; i++) {
        const int id = key_id[i];
        if ((notes_down & (1u << i)) && !arp_on) { aim[14u + i] = LV_FULL; continue; }   /* (the arp on: the keys follow the note it plays, not the ones held) */
        if (drum && id < K_STEPS) {
            const int on = k_key_lit(&app, id);
            aim[14u + i] = (uint8_t)(k_key_playing(&app, id) ? (on ? 0u : LV_FULL) : on ? LV_FULL * 3u / 4u : (id & 3) == 0 ? LV_FULL / 4u : 0u);
        } else if (k_key_lit(&app, id))
            aim[14u + i] = LV_FULL;
    }
    if (wave_on) {   /* the power-on wave over whatever else is lit */
        if (!wave_t0) wave_t0 = now ? now : 1u;
        if (now - wave_t0 >= 4u * LV_WAVE_GAP_MS + LV_WAVE_RISE_MS + LV_WAVE_FADE_MS) wave_on = 0;
        else for (i = 0; i < 41u; i++) { const uint32_t w = lv_wave(i, now - wave_t0); if (w > aim[i]) aim[i] = (uint8_t)w; }
    }
    for (i = 0; i < 41u; i++) {
        uint32_t a = (uint32_t)(idle && aim[i] < br ? br : aim[i]) << 8, c = lv_cur[i];
#if FM1_LED_FADE
        c = a >= c ? a : c - ((c - a) * (i >= 14u ? 141u : 77u) >> 8) - 1u;   /* (down: the buttons x 0.7 a step, ~0.13 s to a tenth; the
                                                 * keys faster, x 0.45, ~60 ms) */
#else
        c = a;   /* no fade down: off at once (the drum steps running over the keys read better hard) */
#endif
        if (c < a) c = a;
        lv_cur[i] = (uint16_t)c;
        {   uint32_t lv = leds ? (c >> 8) / 3u : c >> 8;
            if (lv < LV_FLOOR) lv = 0;   /* (the lowest levels flicker, a lit frame now and then: from ~5 % down they are off) */
            fm1_led_lv[i] = leds >= 2u ? 0u : (uint8_t)(lv | FM1_ANIM_GLOW); }   /* (the glow under all: the panel found in the dark) */
    }
}

/* ---- main ---- */
static KCursorVisual cursor;

/* SAFE mode: the screen shows the record (lcd_crash) with the stage on its last line, USB and the update run, nothing
 * else (no UI, no audio); every 500 ms the diagnostics SysEx */
/* the audio interrupt's load for the diagnostics: 0 its worst time (us), 1 the halves rendered, 2 the over-budget run,
 * 3 rendering on (the load guard turns it off after three over budget) */
static uint32_t diag_audio(uint32_t k)
{
#if KREISEL_FM1_AUDIO
    return k == 0u ? audio_max_us : k == 1u ? audio_halves : k == 2u ? audio_over : (audio_on | (audio_peak << 1));
#else
    (void)k;
    return 0;
#endif
}
static void diag_audio_reset(void)
{
#if KREISEL_FM1_AUDIO
    audio_max_us = 0;
    audio_gap_max_us = 0;
    sync_max_us = 0;
    audio_same_half = 0;
#endif
}
#include "fm1_console.c"                        /* the USB serial console (cdc_task), ported from Felucca */
static void diag_send(void)
{
    uint8_t m[8 + 36 * 5];
    const uint32_t *w = (const uint32_t *)&kdiag.rec;
    uint32_t n = 0, i, k;
    m[n++] = 0xF0; m[n++] = 0x7D; m[n++] = 0x4B; m[n++] = 0x44;
    m[n++] = (uint8_t)(kdiag.stage & 0x7Fu); m[n++] = (uint8_t)(kdiag.safe & 0x7Fu); m[n++] = (uint8_t)(kdiag.reason & 0x7Fu);
    for (i = 0; i < 36u; i++) {                 /* the record's 11 words, the 12 stage times, the longest frame,
                                                 * this boot's reset, the boot before's stage and reset, frames, ms */
        uint32_t v = i < 11u ? w[i] : i < 23u ? kdiag.t_us[i - 11u] : i == 23u ? kdiag.frame_max_us :
                     i == 24u ? kdiag.reset : i == 25u ? kdiag.prev_stage : i == 26u ? kdiag.prev_reset :
                     i == 27u ? kdiag.frames : i == 28u ? ms : i == 29u ? kdiag.prev_why :
                     i == 30u ? kdiag.frame_last_us : i == 31u ? kdiag.frame_avg_us : diag_audio(i - 32u);
        for (k = 0; k < 5u; k++)
            m[n++] = (uint8_t)((v >> (28u - 7u * k)) & 0x7Fu);
    }
    m[n++] = 0xF7;
    (void)ota_wire_send(m, n);
}
static void safe_main(void)
{
    uint32_t last = 0;
    fm1_crash_t shown = kdiag.rec;
    lcd_init();
    if (kdiag.rec.magic != FM1_CRASH_MAGIC) {   /* no fault record (a boot loop): zeros, not what .noinit held */
        uint32_t q, *z = (uint32_t *)&shown;
        for (q = 0; q < sizeof shown / 4u; q++)
            z[q] = 0;
    }
    /* lines 6..8 say why SAFE: the failed boot's reset (p3_rst, bit 2 WDT), its stage and SAFE's reason (1 a fault,
     * 2 a boot loop), and the reset of this boot */
    shown.emu = kdiag.prev_reset & 0xFFFFFFu;
    shown.uptime_ms = kdiag.reset & 0xFFFFFFu;
    shown.count = (kdiag.prev_stage & 0xFFu) << 16 | (kdiag.reason & 0xFFu) << 8 | (kdiag.rec.magic == FM1_CRASH_MAGIC);
    lcd_crash(&shown);
    usb_start();
    fm1_led_dim_level(0);
    fm1_led_anim_start(1);                        /* the power-on LED sweep (the scan runs it, ~0.7 s) */
    fm1_uart1_midi_init(um_ring, UM_RING);        /* TRS MIDI in: before the timer starts (PORTH) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_lcd_backlight(1);
    for (;;) {
        uint32_t now = ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req) {
            guard.pending = 0;
            guard.failed = 0;
            usb_detach();
            fm1_delay_ms(30);
            kdiag.why = 3u;
            fm1_enter_uboot();
        }
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();
        }
        if (now > 2000u && kdiag.safe_pending) {   /* SAFE mode itself runs */
            kdiag.safe_pending = 0;
            kdiag.safe_failed = 0;
            guard.pending = 0;
            guard.failed = 0;
        }
        if (now - last >= 500u) {
            last = now;
            diag_send();
        }
    }
}

/* the FM-1's own profile (240x240, knobs and keys); k_profile_default() is the handheld's 320x180, which the band
 * transfer below (240 pixels a row) would show skewed */
static const KProfile *fm1_profile(void)
{
    int i;
    for (i = 0; i < k_profile_count(); i++) {
        const KProfile *p = k_profile_at(i);
        if (p->w == (int)SW && p->h == (int)SW && p->name[0] == 'f' && p->name[1] == 'm')
            return p;
    }
    for (;;) fm1_wdt_feed();                    /* no such profile: stay (the boot guard counts) */
}

/* not started from M-VAVE's own bootloader, or not mapped at 0x4000 (fm1_bootcheck.c): nothing is written to the flash
 * (no song, no settings, no plugins, no update through Fune); the screen says so and USB stays for the way back (the
 * console read-only, the soft key to the chip's own download mode). Never returns. */
static void boot_locked_main(void)
{
    static const char *const WHY[5] = {"", "Unknown bootloader", "Unknown flash layout", "Started from elsewhere", "Flash unread"};
    GCanvas *cv = (GCanvas *)ui_draw_canvas();
    uint32_t last = 0;
    g_fill(cv, 0, 0, (int)SW, (int)SW, 0xFF000000u);
    g_text_box(cv, 0, 70, (int)SW, 24, GF_M, WHY[boot_info.verdict < 5u ? boot_info.verdict : 4u], 0xFFFF5040u, 0);
    g_text_box(cv, 0, 104, (int)SW, 18, GF_S, "Fune writes nothing here", 0xFFC2C6CCu, 0);
    g_text_box(cv, 0, 124, (int)SW, 18, GF_S, "M-VAVE's own bootloader only", 0xFFC2C6CCu, 0);
    band_valid = 0;
    lcd_present(cv);
    for (;;) {
        uint32_t now = ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req) {                    /* the soft key: the chip's own download mode (M-Flasher) */
            guard.pending = 0;
            guard.failed = 0;
            usb_detach();
            fm1_delay_ms(30);
            kdiag.why = 3u;
            fm1_enter_uboot();
        }
        ota_service();                          /* (the installer's handshake is answered; its update is not taken) */
        usb.ota_req = 0;
        if (now > 2000u) {                      /* it runs: not a boot loop */
            guard.pending = 0;
            guard.failed = 0;
        }
        cdc_task();                             /* the console: con_exec refuses what writes (boot_locked) */
        if (now - last >= 1000u) {
            last = now;
            lcd_present(cv);
        }
    }
}

static void app_main(void)
{
    uint32_t last_draw = 0, last_led = 0;
#if KREISEL_FM1_DIAG
    uint32_t last_diag = 0;
#endif
    int audio_enabled = KREISEL_FM1_AUDIO, home_held = 0;
    {
        uint32_t f = irq_save();
        const uint32_t id = FL_FAR(fl_jedec_ram)();
        flash_ok = FM1_JEDEC_OK(id);   /* the FM-1's 1 MiB parts: Puya P25Q80H, and UCUN UC25HQ80 on newer units (the same
                                                         * commands: 0x20 erase, 0x02 program, 0x0B read, WEL and WIP in the status) */
        flash_id = id;
        irq_restore(f);
        if (flash_ok)
            ota_boot_cleanup();                 /* what an interrupted update left behind */
    }
    pl_mark_boot();                             /* (a run with plugins that failed: the next start skips them, SAFE's included) */
    if (kdiag.safe) {
        safe_main();
        return;
    }
    KSTAGE(KS_LCD);
    lcd_init();
    lcd_lut_init();
    lcd_fill(0x0000u);                          /* black until the splash */
#if KREISEL_FM1_AUDIO
    /* The codec and the I2S up early, playing zeros (audio_on is 0), as Felucca does: started late, after the
     * USB and the splash, the speaker clicked at power-on (2026-11-07). The sound itself comes in at KS_AUDIO. */
    {
        uint32_t i;
        for (i = 0; i < 2u * HALF_WORDS; i++)
            abuf[i] = 0;
        fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);   /* (attaches ALNK0's vector: before the top is locked) */
    }
#endif
    KSTAGE(KS_INPUT);
    keymap_init();
    fm1_input_init();
    fm1_adc_init();
    KSTAGE(KS_USB);
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
    KSTAGE(KS_IRQ);
    fm1_irq_enable_all();
    fm1_lcd_backlight(1);
    fm1_delay_ms(40);                           /* the scan has debounced what is held */
    if (fm1_in.buttons & (1u << MB_HOME)) {     /* HOME held at power-on: no sound (a way out if the load guard is not enough), and no saved song */
        audio_enabled = 0;
        home_held = 1;
    }
    KSTAGE(KS_UI_INIT);
    if (!ui_draw_init(fm1_profile(), K_THEME_DARK))
        for (;;) fm1_wdt_feed();                /* no memory for the canvas: stay, the boot guard counts */
    band_valid = 0;                             /* (no splash: black, then HOME; the LEDs sweep on their own) */
    KSTAGE(KS_K_INIT);
    k_init(&app);
    k_set_shown(&app, fm1_profile()->shown);
    k_set_layout(&app, k_profile_layout(fm1_profile()));
    ui_mark = ui_mark_fm1;
    k_io_send_to(usb_midi_send);                /* the key bed's notes out over USB MIDI */
    {   /* the room for the inserts and outside plugins: 44.1 kHz, 40 KB of state (K_FX_POOL_BYTES: a 0.27 s delay is 24 KB), 120 us a track a block (of 725), no FPU use */
        static const KDeviceCaps caps = {44100u, 40960u, 120u, K_AUDIO_VOICES, 0u};
        k_plugin_set_caps(&caps);
    }
    k_set_battery(&app, -1, 0);                 /* no icon until the first reading (poll_battery) */
    {                                           /* MASTER as the pot stands, before the audio starts (as Felucca's master_boot):
                                                 * k_init's 80 % played a burst at power-on with the knob at zero */
        uint32_t i;
        (void)fm1_adc_read(FM1_ADC_MASTER);     /* the first conversion after the pin setup is dropped */
        for (i = 0; i < 3u; i++) {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                app.master = master_of(a);
                break;
            }
        }
    }
    boot_inspect(&boot_info);                   /* where it was started from (fm1_bootcheck.c): before the first flash read or write */
    if (boot_info.verdict != BOOT_OK && KREISEL_FM1_BOOTCHECK) {
        boot_locked = 1;
        boot_locked_main();
    }
    pl_boot(home_held);                         /* the outside plugins in flash (fm1_plugins.c): before the song, which may name them */
    {   /* the last saved song, unless the boot before failed: a fault (kdiag.prev_why 1), a watchdog reset (p3_rst bit 2), or a boot
         * that never got healthy (guard.failed). Before the audio starts, which then reads the song (k_audio_sync). */
        int clean = !guard.failed && !(kdiag.reset & 4u) && kdiag.prev_why != 1u;
        ks_boot(clean, home_held);
    }
#if KREISEL_FM1_AUDIO
    KSTAGE(KS_AUDIO);
    if (audio_enabled) {
        uint32_t f;
        k_audio_init(&audio, AUDIO_RATE);
        k_audio_master_outside(&audio, !app.set_v[K_SET_USB_LEVEL]);   /* MASTER after the USB audio tap (the render loop), unless USB level is Volume */
        f = irq_save();
        k_audio_sync(&audio, &app);
        audio_fade = 0;
        audio_on = 1;
        audio_enabled_guard = 1;
        irq_restore(f);
    }
#endif
    fm1_guard_lock_top();                       /* after the last fm1_irq_attach (fm1_guard.h): the vectors, boot info
                                                 * and mailbox write-protected; locked before audio's, its vector write
                                                 * faulted (dbg 0x2000, a write error, 2026-11-04) */
    (void)audio_enabled;
    KSTAGE(KS_LOOP);
    for (;;) {
        uint32_t now = ms, busy;
        static uint8_t slow_anim;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req) {                    /* the SysEx soft key */
            guard.pending = 0;
            guard.failed = 0;
            usb_detach();
            fm1_delay_ms(30);
            kdiag.why = 3u;
            fm1_enter_uboot();
        }
        ota_service();                          /* the installer's handshake */
        if (usb.ota_req) {                      /* and its update: commits and resets, or comes back */
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();
            band_valid = 0;
            input_seen = 1;
        }
        if (!boots_ok && now > 2000u && kdiag.stage >= KS_FRAME) {   /* it runs and has drawn: the next boot is not a loop */
            boots_ok = 1;
            guard.pending = 0;
            guard.failed = 0;
            KSTAGE(KS_HEALTHY);
            ks_boot_late();                     /* (a boot that skipped the store: it now) */
        }
        poll_input(now);
#ifdef FM1_BOARD_EXTRAS
        board_poll(now);
#endif
        poll_master(now);
        poll_battery(now);
        midi_service();
        cdc_task();                             /* the USB serial console */
        k_tick(&app, now);
#if KREISEL_FM1_AUDIO
        if (audio_enabled && audio_on) {
            int playhead;
            KNoteEv ne;
            uint32_t t0 = fm1_ticks(), us;
            /* Only the audio interrupt is held off while the core's state goes to the audio (it must not render a half-updated
             * KAudio). Not every interrupt: the sync takes a few hundred us (floats on a part without an FPU), and the key / LED
             * scan (TIMER5, 10 kHz) stalled with it lit its column longer: the LEDs flickered. TIMER5 never touches KAudio. */
            fm1_irq_mask(FM1_IRQ_ALNK0);
            k_audio_sync(&audio, &app);
            k_audio_master_outside(&audio, !app.set_v[K_SET_USB_LEVEL]);   /* USB level: Fixed (MASTER after the USB tap) or Volume */
            k_audio_clock_tick(&audio, k_clock_take(&app));   /* (after the sync: a Start has set it up) */
            while (k_take_note(&app, &ne))
                k_audio_note(&audio, ne.track, ne.note, ne.vel, ne.vel > 0);
            playhead = audio.current_step;
            k_audio_song_follow(&audio, &app);   /* (a song: its scene and patterns; stopped at its end) */
            {   static uint32_t arp_seen[K_TRACKS];   /* the arp's notes to the screen (the ARP page, its keys lit) */
                int t2;
                for (t2 = 0; t2 < K_TRACKS; t2++) {
                    uint32_t cnt;
                    const int an = k_audio_arp_last(&audio, t2, &cnt);
                    if (an >= 0 && cnt != arp_seen[t2]) k_arp_note(&app, t2, an, now);
                    arp_seen[t2] = cnt;
                } }
            fm1_irq_unmask(FM1_IRQ_ALNK0);
            k_io_send_audio(&audio);           /* the sequencer's notes out over USB MIDI (its ring: no lock) */
            us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
            if (us > sync_max_us)
                sync_max_us = us;
            if (app.playing && playhead >= 0) { /* the sample clock is the playhead while sound runs */
                app.step = playhead;
                app.beat = (playhead / 4) & 3;
            }
        } else if (audio_enabled) {   /* the sound paused (the load guard): its notes taken still, the offs given (no render runs) */
            KNoteEv ne;
            while (k_take_note(&app, &ne))
                if (!ne.vel) k_audio_note(&audio, ne.track, ne.note, 0, 0);
        }
#endif
        if (now - last_led >= 20u) {
            last_led = now;
            leds_update(now);
            {   /* SCREEN OFF: the backlight out after that long with no input (playing or not); any input lights it again */
                static const uint32_t MIN[5] = {0u, 5u, 15u, 30u, 60u};
                static uint8_t dark;
                const uint32_t m = MIN[app.set_v[K_SET_SCREEN_OFF] < 5u ? app.set_v[K_SET_SCREEN_OFF] : 0u];
                const int off = m && now - last_input_ms >= m * 60000u;
                if (off != dark) { dark = (uint8_t)off; fm1_lcd_backlight(!off); }
            }
        }
#if KREISEL_FM1_DIAG
        if (now - last_diag >= (now < 30000u ? 1000u : 10000u)) {   /* bring-up: every second for 30 s, then every 10 s */
            last_diag = now;
            diag_send();
        }
#endif
        if (input_seen || app.playing || now - last_draw >= (slow_anim ? 100u : 250u)) {   /* (slow_anim: the row in hand breathing) */
            input_seen = 0;
            last_draw = now;
            {
                uint32_t f0 = fm1_ticks(), fd;
                fm1_wdt_feed();
                mark_t0 = fm1_ticks();
#if KREISEL_FM1_AUDIO
                {   const uint32_t pl = audio_pk[0], pr = audio_pk[1];   /* (taken, then cleared: a half between the two reads is lost to the meter, no more) */
                    audio_pk[0] = audio_pk[1] = 0;
                    k_set_out_peak(&app, (int)(pl > 32767u ? 32767u : pl), (int)(pr > 32767u ? 32767u : pr), now);
                    k_audio_take_peaks(&audio, &app, now, 0); }   /* (the tracks' meters: the header's has its own, above) */
#endif
                busy = (uint32_t)ui_draw(&app, &cursor, now);
                fm1_mark(5);
                lcd_present(ui_draw_canvas());
                fm1_mark(6);
                fd = (fm1_ticks() - f0) / FM1_TICKS_PER_US;
                if (fd > kdiag.frame_max_us)
                    kdiag.frame_max_us = fd;
                kdiag.frame_last_us = fd;
                kdiag.frame_avg_us = kdiag.frames ? kdiag.frame_avg_us + ((int32_t)(fd - kdiag.frame_avg_us) >> 3) : fd;
                kdiag.frames++;
                prof_n++;
                prof_sum += fd;
            }
            if (kdiag.stage < KS_FRAME)
                KSTAGE(KS_FRAME);
            slow_anim = busy == 2u;
            if (busy == 1u)
                input_seen = 1;                 /* an animation: again at once */
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_reset_reason();                         /* before the watchdog is armed: why this boot began */
    fm1_wdt_arm(0x0D);
    if (guard.magic != 0x4B525331u || (fm1_boot.p3_rst & 1u)) {   /* "KRS1"; a power-on (p3_rst bit 0) is no failed boot:
                                                 * the RAM may keep the magic over a short power-off with its counts stale
                                                 * (2026-11-05: SAFE reason 2 on battery after unplugging USB). A crash loop
                                                 * resets by the watchdog or in software, and is still counted. */
        guard.magic = 0x4B525331u;
        guard.failed = 0;
        guard.pending = 0;
    }
    if (kdiag.magic != KDIAG_MAGIC) {           /* a cold start or another layout: all of it zero, then used */
        uint32_t q, *z = (uint32_t *)&kdiag;
        for (q = 0; q < sizeof kdiag / 4u; q++)
            z[q] = 0;
        kdiag.magic = KDIAG_MAGIC;
    }
    if (guard.pending)
        guard.failed++;
    guard.pending = 1;
    if (kdiag.safe) {                           /* SAFE mode: it counts its own failures */
        guard.pending = 0;
        guard.failed = 0;
        if (kdiag.safe_pending)
            kdiag.safe_failed++;
        kdiag.safe_pending = 1;
        if (kdiag.safe_failed >= 2u) {          /* SAFE mode itself never got going: UBOOT, the last way out */
            kdiag.safe = kdiag.safe_pending = kdiag.safe_failed = 0;
            fm1_enter_uboot();
        }
    } else if (guard.failed >= 2u) {            /* two boots that never got healthy: SAFE mode (not UBOOT: the
                                                 * installer reaches it over USB-MIDI) */
        guard.failed = 0;
        guard.pending = 0;
        kdiag.safe = 1u;
        kdiag.reason = 2u;
    }
    kdiag_t0 = fm1_ticks();
    kdiag.prev_stage = kdiag.stage;             /* where the last boot stopped (a WDT reset leaves its last stage) */
    kdiag.prev_reset = kdiag.reset;
    kdiag.prev_why = kdiag.why;
    kdiag.why = 0;
    kdiag.reset = (uint32_t)fm1_boot.p3_rst | ((fm1_boot.rst_src >> 5) & 1u) << 8 | (uint32_t)fm1_boot.wdt_con << 16;
    if (!kdiag.safe) {
        uint32_t q;
        for (q = 0; q < 12u; q++)
            kdiag.t_us[q] = 0;
        kdiag.frame_max_us = 0;
        kdiag.frames = 0;
        kdiag.frame_last_us = kdiag.frame_avg_us = 0;
        kdiag.stage = KS_CSTART;
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)    /* the flash driver, run from RAM */
        *d = *s;
    fm1_mailbox_clear();                        /* boot info and mailbox (before the guard protects the top) */
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;)
        ;
}

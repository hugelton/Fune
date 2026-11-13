/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM-1's outside plugins: images (src/kreisel_plugin_image.h) in the flash that was Felucca's store, run in place through the
 * XIP window. The skip after a crash and the PC limit's widening are ported from Felucca's plugin_fm1.c and fm1_plugin_hal.h (the
 * author's own code, relicensed MPL-2.0 for Kreisel), rewritten for images placed anywhere in the region.
 *
 * Included by fm1_main.c after fm1_store.c (it uses the flash driver, ota_fread, ks_mute, flash_ok, app, kdiag, guard); the
 * console's plug commands (fm1_console.c) install, list, switch and erase them.
 *
 *   flash 0x097000..0x0DFFFF (292 KiB, 73 sectors)  the images, each on sector boundaries (Felucca's store; never written by an
 *                                                    update, which writes [0x4000, 0x97000) and stages at 0xE0000; nothing here
 *                                                    is near the Bluetooth MAC at 0xE9000 or SAVE's [0xEA000, 0xFC000))
 *   RAM 0x01C08000, K_FM1_PLUG_RAM bytes             the images' .data and .bss (.plug_ram, first in RAM: app.ld keeps it at
 *                                                    that address, which the images are linked for)
 *
 * At power-on (pl_boot, before the saved song is loaded, so a song finds its plugin): nothing is touched unless a sector of the
 * region starts with an image's magic (read over SPI). Then the region is mapped as plain (unencrypted) XIP, the first header is
 * read both ways and must agree, each image is checked (k_image_check: header, CRC, linked here, record and pointers inside it),
 * the PC limit is widened over the region, and k_image_load registers them through k_plugin_register.
 * SAFE start, no plugin at all: HOME held at power-on (as for the song), or the boot after a run with plugins that ended in a fault,
 * a watchdog reset or a boot that never got healthy (a .noinit mark; that boot may be the SAFE screen's: the one after it skips).
 * One image is switched off for good with `plugoff N` (a bit of its header, no erase). */
#include "kreisel_plugin_image.h"
#ifndef PL_LO                                    /* (a board with none: an empty region, boards/smk37/smk37_map.h) */
#define PL_LO 0x00097000u
#define PL_HI 0x000E0000u
#endif
#define PL_LEN (PL_HI - PL_LO)
#define PL_MAX 8                         /* images listed (the registry holds 8 synths and 8 effects beside the built-in ones) */
#ifndef K_FM1_PLUG_RAM
#define K_FM1_PLUG_RAM 2048u
#endif
#define PL_RAM_AT 0x01C08000u            /* app.ld: .plug_ram */
#define PL_OK(off, n) FL_IN(off, n, PL_LO, PL_HI)
_Static_assert(PL_LO >= FL_DATA_LO && PL_HI <= FL_DATA_HI, "the plugins' region is inside the store the driver may write");
_Static_assert(PL_HI <= 0xE0000u && PL_LO >= 0x97000u, "never the app, the update's staging, the BT MAC (0xE9000) or SAVE");

static uint8_t pl_ram[K_FM1_PLUG_RAM] __attribute__((section(".plug_ram"), aligned(8)));
static const KImagePool PL_POOL = {pl_ram, PL_RAM_AT, K_FM1_PLUG_RAM};
static KImageSlot pl_slot[PL_MAX];
static int pl_n;
static uint8_t pl_xip;                   /* the region mapped as plain XIP (and checked) */
static uint8_t pl_code;                  /* the PC limit lets the region run */
static uint8_t pl_boot_res;              /* 0 none found, 1 loaded, 2 skipped: HOME held, 3 skipped: the run before failed,
                                          * 4 no flash, 5 the XIP window reads otherwise than the flash */
static struct { uint32_t magic, state; } pl_mark __attribute__((section(".noinit")));
#define PL_MARK_MAGIC 0x4D474C50u        /* "PLGM" */
enum { PL_IDLE, PL_ACTIVE, PL_SKIP };    /* the plugins are live (or being registered); the next start skips them */

static const uint8_t *pl_xip_at(uint32_t off) { return fm1_xip_ptr(PL_LO + off); }
static int pl_live(void)
{
    int i, n = 0;
    for (i = 0; i < pl_n; i++) n += pl_slot[i].state == K_IMG_LIVE;
    return n;
}

/* app_main's first step, before SAFE mode is chosen: a run with plugins that did not end well (a fault, the watchdog, a boot that
 * never got healthy) marks the next start to skip them, SAFE mode's start included */
static void pl_mark_boot(void)
{
    if (pl_mark.magic != PL_MARK_MAGIC) {
        pl_mark.magic = PL_MARK_MAGIC;
        pl_mark.state = PL_IDLE;
    } else if (pl_mark.state == PL_ACTIVE && (kdiag.prev_why == 1u || (kdiag.reset & 4u) || guard.failed))
        pl_mark.state = PL_SKIP;
}

/* the region as plain XIP (fm1_flash.h: the data area unencrypted from 0x93000 up, as the SDK maps it), checked against SPI */
static int pl_map(void)
{
    uint8_t a[64];
    uint32_t i;
    if (pl_xip)
        return 1;
    fl_plain_window_init();
    for (i = 0; i < PL_LEN; i += K_IMAGE_SECTOR) {   /* the first sector with something: the same both ways */
        if (ota_fread(PL_LO + i, a, sizeof a))
            return 0;
        if (a[0] != 0xFFu || a[1] != 0xFFu || i + K_IMAGE_SECTOR >= PL_LEN) {
            if (memcmp(a, pl_xip_at(i), sizeof a))
                return 0;
            break;
        }
    }
    pl_xip = 1;
    return 1;
}
/* instruction fetch up to the region's end (fm1_guard.h: window 0 is [0x02000120, _etext]); from Felucca's fm1_pc_code_up_to */
static void pl_open_code(void)
{
    if (pl_code)
        return;
    fm1__dbg_unlock();
    FM1_PC_LIMIT0_H = FM1_XIP(PL_HI) - 1u;
    fm1__dbg_lock();
    pl_code = 1;
}
/* the region read again (its states kept by place: an image keeps what this start made of it) */
static void pl_rescan(void)
{
    uint32_t off[PL_MAX];
    int8_t st[PL_MAX], er[PL_MAX];
    int16_t ix[PL_MAX];
    int i, j, n = pl_n;
    for (i = 0; i < n; i++)
        off[i] = pl_slot[i].off, st[i] = pl_slot[i].state, er[i] = pl_slot[i].err, ix[i] = pl_slot[i].index;
    pl_n = pl_xip ? k_image_scan(pl_xip_at(0), PL_LEN, pl_slot, PL_MAX) : 0;
    for (i = 0; i < pl_n; i++)
        for (j = 0; j < n; j++)
            if (pl_slot[i].off == off[j] && pl_slot[i].state != K_IMG_BAD) {
                pl_slot[i].state = st[j];
                pl_slot[i].err = er[j];
                pl_slot[i].index = ix[j];
            }
}

static void pl_boot(int home_held)
{
    uint32_t i, w;
    int any = 0, skip;
    if (!flash_ok) {
        pl_boot_res = 4;
        return;
    }
    for (i = 0; i < PL_LEN && !any; i += K_IMAGE_SECTOR)   /* (over SPI: the XIP window is not touched on an FM-1 with none) */
        any = !ota_fread(PL_LO + i, &w, 4u) && w == K_IMAGE_MAGIC;
    if (!any) {
        pl_mark.state = PL_IDLE;
        return;
    }
    if (!pl_map()) {
        pl_boot_res = 5;
        return;
    }
    pl_n = k_image_scan(pl_xip_at(0), PL_LEN, pl_slot, PL_MAX);
    skip = home_held || pl_mark.state == PL_SKIP;
    pl_boot_res = home_held ? 2 : skip ? 3 : 1;
    if (!skip)
        pl_open_code();
    pl_mark.state = skip ? PL_IDLE : PL_ACTIVE;     /* (a fault while they register: the next start skips them) */
    if (!k_image_load(pl_xip_at(0), FM1_XIP(PL_LO), PL_LEN, pl_slot, pl_n, &PL_POOL, skip) && !skip)
        pl_mark.state = PL_IDLE;
}

/* ---- the flash (the console's install, switch and erase); only inside the region ---- */
static int pl_erase(uint32_t off)              /* a sector of the region (offset in it), the sound muted as SAVE does */
{
    uint32_t took = 0, f;
    int rc;
    if (!PL_OK(PL_LO + off, K_IMAGE_SECTOR) || (off & (K_IMAGE_SECTOR - 1u)))
        return -8;
    fm1_wdt_feed();
    f = irq_save();
    ks_mute();
    fm1__led_lines(0);
    rc = FL_FAR(fl_erase4k_ram)(PL_LO + off, &took);
    irq_restore(f);
    fm1_wdt_feed();
    return rc;
}
static int pl_prog(uint32_t off, const void *src_ram, uint32_t n)
{
    if (!PL_OK(PL_LO + off, n))
        return -8;
    fm1_wdt_feed();
    return fl_write(PL_LO + off, src_ram, n);
}
static int pl_find(int number)                 /* the console's numbers are 1-based: the slot, or -1 */
{
    return number >= 1 && number <= pl_n ? number - 1 : -1;
}
static uint32_t pl_ram_used(void)
{
    uint32_t u = 0;
    int i;
    for (i = 0; i < pl_n; i++)
        if (pl_slot[i].state != K_IMG_BAD && pl_slot[i].h.ram)
            u += (pl_slot[i].h.data_size + pl_slot[i].h.bss_size + 3u) & ~3u;
    return u;
}
static uint32_t pl_free(void)                  /* bytes of free sectors */
{
    uint32_t u = 0;
    int i;
    for (i = 0; i < pl_n; i++) u += pl_slot[i].len;
    return PL_LEN - u;
}
/* switch image i on or off: one more bit of its flags cleared. 0, -1 already so, -2 no switch left (install it again), or the
 * driver's */
static int pl_switch(int i, int on)
{
    uint8_t f = pl_slot[i].h.flags;
    int rc;
    if (pl_slot[i].state == K_IMG_BAD)
        return -3;
    if (k_image_on(&pl_slot[i].h) == on)
        return -1;
    if (!f)
        return -2;
    f &= (uint8_t)(f - 1u);
    if ((rc = pl_prog(pl_slot[i].off + 7u, &f, 1u)) != 0)
        return rc;
    pl_slot[i].h.flags = f;
    if (!on && pl_slot[i].state != K_IMG_LIVE)
        pl_slot[i].state = K_IMG_OFF;
    return 0;
}
/* erase image i (not one live: its code may be playing) */
static int pl_erase_image(int i)
{
    uint32_t s;
    int rc;
    if (pl_slot[i].state == K_IMG_LIVE)
        return -1;
    for (s = 0; s < pl_slot[i].len; s += K_IMAGE_SECTOR)
        if ((rc = pl_erase(pl_slot[i].off + s)) != 0)
            return rc;
    pl_rescan();
    return 0;
}
/* an image just written at off: listed, and live if it can be (its RAM, the registry: an id already live waits for a restart) */
static int pl_take(uint32_t off)
{
    int i;
    pl_rescan();
    for (i = 0; i < pl_n; i++)
        if (pl_slot[i].off == off) {
            pl_slot[i].state = 0;
            pl_open_code();
            if (k_image_load_one(pl_xip_at(0), FM1_XIP(PL_LO), PL_LEN, pl_slot, pl_n, i, &PL_POOL))
                pl_mark.state = PL_ACTIVE;
            return i;
        }
    return -1;
}

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Ported from felucca-2.0-snap/firmware/src/storage.c (the A/B sector pair, the commit record written last, the newest
 * valid sequence wins, the read back after a write, the CRC-32) and storage_hw.c (reads in 256-byte IRQ-off windows, the
 * erase with the audio buffer silenced and the LED scan stopped, writes only inside the store's own sectors),
 * (c) Leo Kuroshita: the author's own code, relicensed MPL-2.0 for Kreisel (2026-11-07). Cut down to what Kreisel keeps: six songs and 24 presets.
 *
 * SAVE's store (src/kreisel_save.h, a KStore) on the FM-1's SPI NOR. Included by fm1_main.c (it uses the flash driver, app,
 * abuf, flash_ok).
 *
 * FLASH MAP (1 MiB part, JEDEC 85 60 14):
 *   0x000000..0x003FFF  bootloader / flash head (SPL, isd_config)            never written by anything here or by the update
 *   0x004000..0x092FFF  the app (XIP)                                        the update loader writes it ([0x4000,0x97000))
 *   0x093000..0x096FFF  FL_XDATA0 (Felucca's plugin slots)                   unused by Kreisel
 *   0x097000..0x0DFFFF  the outside plugins' images (Felucca's store before)  fm1_plugins.c (2026-11-12)
 *   0x0E0000..0x0E4FFF  the update's staged loader (OTA_AREA, ota.c)        the update path: not used here
 *   0x0E5000..0x0E6FFF  Felucca's autosave A/B                               untouched
 *   0x0E7000..0x0E8FFF  Felucca's FM6 voice bank; 0xE8F00 the stock firmware's update record   untouched
 *   0x0E9000            the Bluetooth MAC                                    NEVER touched (outside KS_LO..KS_HI)
 *   0x0EA000..0x0FBFFF  KREISEL: 9 objects, each an A/B pair of 4 KiB sectors (below)
 *   0x0FC000..0x0FFFFF  Felucca's settings / FM6 patches                     untouched
 * [0xEA000, 0xFC000) is the stock firmware's "USR" area, which Felucca 2.0 also takes for its sample pack: the owner chose to
 * give it to Kreisel (2026-11-06). A stock install (its USR data) or a Felucca pack install writes it back; Kreisel's songs and
 * presets are then gone. It sits inside the BSP's FL_XDATA1 (0xE7000..0xFC000), which FL_RANGE_OK lets the driver write.
 *
 *   object 0  song 1          0xF8000 / 0xF9000   (where Kreisel kept its one song before: that song is song 1)
 *   object 1  song 2          0xFA000 / 0xFB000
 *   object 2..5  songs 3..6   0xEA000, 0xEC000, 0xEE000, 0xF0000 (+ 0x1000 for B)
 *   object 6..8  presets 1..8, 9..16, 17..24   0xF2000, 0xF4000, 0xF6000 (+ 0x1000 for B)
 *
 * A sector: a 32-byte commit record at 0 (magic "KRSV", type, object<<1 | copy, sequence, length, CRC-32 of the payload, CRC-32
 * of the record), the payload from 0x100. A song's payload is its KRSG file; a preset bank's is 8 cells of 384 bytes, each a KRSP
 * file or erased (0xFF). The loader clears an update record it finds at bytes 0xF00..0xF07 of a sector: the payloads end before
 * (checked at compile time). A write erases the copy that is not the current one, programs the payload (a bank: the current
 * copy's cells streamed over with the one cell changed), the commit record LAST, and reads it back: a power cut at any point leaves
 * the object as it was. The sequence is global (the highest of all objects + 1), so the song written last is the newest song.
 * Erasing a song erases the older copy first, then the current one: cut between, the song is still there. Whatever a sector held
 * before (the USR data, a sample pack) is erased when its object is first written. */
#include <string.h>
#include "kreisel_demo.h"
#ifndef KS_LO                                  /* (another board's own place: boards/smk37/smk37_map.h) */
#define KS_LO 0x000EA000u
#define KS_HI 0x000FC000u
#define KS_PAIR_AT {0xF8000u, 0xFA000u, 0xEA000u, 0xEC000u, 0xEE000u, 0xF0000u, 0xF2000u, 0xF4000u, 0xF6000u}
#define KS_RANGE_TEXT "0xEA000..0xFBFFF"
#endif
#define KS_SECTOR 0x1000u
#define KS_PAYLOAD_OFF 0x100u
#define KS_OBJS 9
#define KS_BANK0 K_SONG_SLOTS                  /* the first preset bank's object */
#define KS_CELL 384u
#define KS_CELLS 8
#define KS_BANK_LEN (KS_CELL * KS_CELLS)
#define KS_SET_LEN 64u                         /* the settings' record (K_SETTINGS_BYTES), after the first bank's cells */
#define KS_BANK_ALL (KS_BANK_LEN + KS_SET_LEN) /* what a bank is written as (older ones, KS_BANK_LEN long, read as no settings) */
#define KS_MAGIC 0x5653524Bu                   /* "KRSV" */
#define KS_TYPE_SONG 1u
#define KS_TYPE_BANK 2u
_Static_assert(KS_PAYLOAD_OFF + K_SAVE_BUF <= 0xF00u, "a song stays clear of the sector's update-record bytes (0xF00)");
_Static_assert(KS_PAYLOAD_OFF + KS_BANK_ALL <= 0xF00u, "a bank stays clear of the sector's update-record bytes (0xF00)");
_Static_assert(KS_SET_LEN >= K_SETTINGS_BYTES, "the settings fit their place");
_Static_assert(KS_BANK0 + (K_USER_PRESETS + KS_CELLS - 1) / KS_CELLS == KS_OBJS, "six songs and three banks");
static const uint32_t KS_PAIR[KS_OBJS] = KS_PAIR_AT;

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} ks_hdr_t;
_Static_assert(sizeof(ks_hdr_t) == 32u, "commit record layout");

enum { KS_BLANK, KS_VALID, KS_DAMAGED, KS_FOREIGN, KS_READERR };   /* what a sector holds */
/* the store's calls return 0, or: -1 no flash, -2 bad argument / too big, -3 unreadable, -7 read back failed, other negatives the
 * flash driver's (-8 outside the store, -1..-4 from fl_*_ram) */
static struct { int op, rc, obj, copy; uint32_t seq, len; } ks_last;   /* op: 1 write, 2 read, 3 erase, 4 clear */
static uint8_t ks_boot_res;                    /* 0 not run, 1 loaded, 2 none saved, 3 skipped: unclean boot, 4 skipped: HOME held,
                                                * 5 skipped: the last load never finished, 6 refused,
                                                * 7 skipped: RESTORE LAST OFF, 8 COLDWIRE (kreisel_demo.h) */
static struct { int8_t known, copy; uint32_t seq, len; } ks_cur[KS_OBJS];   /* each object's current copy (-1 none), cached */
static uint32_t ks_pend __attribute__((section(".noinit")));   /* "LOAD" while a boot's song load runs: a crash in it skips the next */
#define KS_PEND_MAGIC 0x4C4F4144u   /* "LOAD" */
#define KS_PEND_SCAN 0x5343414Eu    /* "SCAN" */

static uint32_t ks_crc32_add(uint32_t c, const void *p, uint32_t n)     /* zlib CRC-32, 4 bits per step; running: from 0xFFFFFFFF, the CRC is ~c */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t ks_crc32(const void *p, uint32_t n) { return ~ks_crc32_add(0xFFFFFFFFu, p, n); }
static uint32_t ks_base(int obj, int copy) { return KS_PAIR[obj] + (copy ? KS_SECTOR : 0u); }
static uint32_t ks_type(int obj) { return obj < KS_BANK0 ? KS_TYPE_SONG : KS_TYPE_BANK; }
#define KS_OK(off, n) FL_IN(off, n, KS_LO, KS_HI)  /* the only flash this file touches */

static void ks_mute(void)                      /* IRQs are off: the DMA would loop stale audio (a buzz) for the erase */
{
#if KREISEL_FM1_AUDIO
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
#endif
}
static int ks_erase(uint32_t off)
{
    uint32_t took = 0, f;
    int rc;
    if (!KS_OK(off, KS_SECTOR) || (off & 0xFFFu))
        return -8;
    fm1_wdt_feed();
    f = irq_save();
    ks_mute();
    fm1__led_lines(0);                         /* the erase stops the LED scan: dark rows, the tick lights them again (Felucca #236) */
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    fm1_wdt_feed();
    return rc;
}
static int ks_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!KS_OK(off, n))
        return -8;
    fm1_wdt_feed();
    return fl_write(off, src, n);              /* pages of <= 256 bytes with IRQs off, the source in RAM */
}

/* what a sector holds; a valid one gives its record. The payload is checked by streaming its CRC. */
static int ks_state(int obj, int copy, ks_hdr_t *h)
{
    uint8_t b[256];
    ks_hdr_t t;
    uint32_t i, c, off, n, base = ks_base(obj, copy);
    if (ota_fread(base, b, 256u))
        return KS_READERR;
    for (i = 0; i < 256u && b[i] == 0xFFu; i++)
        ;
    if (i == 256u)
        return KS_BLANK;
    memcpy(&t, b, sizeof t);
    if (t.magic != KS_MAGIC)
        return KS_FOREIGN;
    if (t.type != ks_type(obj) || t.slot != (uint16_t)((obj << 1) | copy) || !t.len ||
        t.len > (obj < KS_BANK0 ? (uint32_t)K_SAVE_BUF : KS_BANK_ALL) || t.hcrc != ks_crc32(&t, sizeof t - 4u))
        return KS_DAMAGED;
    c = 0xFFFFFFFFu;
    for (off = 0; off < t.len; off += n) {
        n = t.len - off > sizeof b ? (uint32_t)sizeof b : t.len - off;
        if (ota_fread(base + KS_PAYLOAD_OFF + off, b, n))
            return KS_READERR;
        c = ks_crc32_add(c, b, n);
    }
    if (~c != t.crc)
        return KS_DAMAGED;
    if (h)
        *h = t;
    return KS_VALID;
}
/* an object's current copy: the newer valid sequence (wrap-aware), -1 when neither is valid; cached until it is written */
static int ks_current(int obj)
{
    if (!ks_cur[obj].known) {
        ks_hdr_t a, b;
        int va = ks_state(obj, 0, &a) == KS_VALID, vb = ks_state(obj, 1, &b) == KS_VALID, c = -1;
        if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u)))
            c = 1, a = b;
        else if (va)
            c = 0;
        ks_cur[obj].copy = (int8_t)c;
        ks_cur[obj].seq = c >= 0 ? a.seq : 0u;
        ks_cur[obj].len = c >= 0 ? a.len : 0u;
        ks_cur[obj].known = 1;
    }
    return ks_cur[obj].copy;
}
static uint32_t ks_seq_top(void)               /* the highest sequence of all objects */
{
    uint32_t m = 0;
    int o;
    for (o = 0; o < KS_OBJS; o++)
        if (ks_current(o) >= 0 && ks_cur[o].seq > m)
            m = ks_cur[o].seq;
    return m;
}

/* An object written anew: `len` payload bytes, the current copy's payload with [p_off, p_off + p_n) replaced by `patch` (or by
 * 0xFF when patch is 0), into the copy that is not the current one; the commit record last, then read back. */
static int ks_commit(int obj, uint32_t len, const uint8_t *patch, uint32_t p_off, uint32_t p_n)
{
    uint8_t b[256] __attribute__((aligned(4)));
    ks_hdr_t h, chk;
    uint32_t off, n, c = 0xFFFFFFFFu, base, src = 0, src_len = 0;
    int cur = ks_current(obj), to = cur == 0 ? 1 : 0, rc;
    if (cur >= 0) {
        src = ks_base(obj, cur) + KS_PAYLOAD_OFF;
        src_len = ks_cur[obj].len;
    }
    h.seq = ks_seq_top() + 1u;
    base = ks_base(obj, to);
    ks_cur[obj].known = 0;                      /* (whatever happens next, read it again) */
    if ((rc = ks_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += n) {
        uint32_t i, lo, hi, blank = 1;
        n = len - off > sizeof b ? (uint32_t)sizeof b : len - off;
        memset(b, 0xFF, n);
        if (off < src_len && ota_fread(src + off, b, src_len - off < n ? src_len - off : n))
            return -3;
        lo = p_off > off ? p_off : off;
        hi = p_off + p_n < off + n ? p_off + p_n : off + n;
        for (i = lo; i < hi; i++)
            b[i - off] = patch ? patch[i - p_off] : 0xFFu;
        c = ks_crc32_add(c, b, n);
        for (i = 0; i < n && blank; i++)
            blank = b[i] == 0xFFu;
        if (!blank && (rc = ks_prog(base + KS_PAYLOAD_OFF + off, b, n)) != 0)   /* (erased bytes need no programming) */
            return rc;
    }
    h.magic = KS_MAGIC;
    h.type = (uint16_t)ks_type(obj);
    h.slot = (uint16_t)((obj << 1) | to);
    h.len = len;
    h.crc = ~c;
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = ks_crc32(&h, sizeof h - 4u);
    if ((rc = ks_prog(base, &h, sizeof h)) != 0)        /* the commit record, last */
        return rc;
    if (ks_state(obj, to, &chk) != KS_VALID || memcmp(&chk, &h, sizeof h))   /* a write-protected or failing part must not say saved */
        return -7;
    ks_last.obj = obj;
    ks_last.copy = to;
    ks_last.seq = h.seq;
    ks_last.len = h.len;
    return 0;
}

/* KStore (kreisel_save.h): a song slot is an object, a preset a cell of a bank */
static void ks_where(int kind, int slot, int *obj, uint32_t *cell)
{
    *obj = kind == K_STORE_SONG ? slot : KS_BANK0 + slot / KS_CELLS;
    *cell = kind == K_STORE_SONG ? 0u : (uint32_t)(slot % KS_CELLS) * KS_CELL;
}
static int ks_arg(int kind, int slot) { return kind == K_STORE_SONG ? slot >= 0 && slot < K_SONG_SLOTS : kind == K_STORE_PRESET && slot >= 0 && slot < K_USER_PRESETS; }
static long ks_read(void *ctx, int kind, int slot, uint8_t *buf, size_t cap)
{
    int obj, cur;
    uint32_t cell, n;
    (void)ctx;
    ks_last.op = 2;
    if (!flash_ok)
        return ks_last.rc = -1;
    if (!ks_arg(kind, slot))
        return ks_last.rc = -2;
    ks_where(kind, slot, &obj, &cell);
    if ((cur = ks_current(obj)) < 0)
        return ks_last.rc = 0;                 /* empty */
    if (kind == K_STORE_SONG) {
        n = ks_cur[obj].len;
    } else {                                    /* a cell: "KRSP", version, payload length, ..., CRC-16 */
        uint8_t h8[8];
        if (ota_fread(ks_base(obj, cur) + KS_PAYLOAD_OFF + cell, h8, 8u))
            return ks_last.rc = -3;
        if (memcmp(h8, "KRSP", 4))
            return ks_last.rc = 0;
        n = 10u + (uint32_t)(h8[6] | h8[7] << 8);
        if (n > KS_CELL)
            return ks_last.rc = 0;
    }
    if (n > cap)
        return ks_last.rc = -2;
    if (ota_fread(ks_base(obj, cur) + KS_PAYLOAD_OFF + cell, buf, n))
        return ks_last.rc = -3;
    ks_last.rc = 0;
    return (long)n;
}
static int ks_write(void *ctx, int kind, int slot, const uint8_t *buf, size_t n)
{
    int obj;
    uint32_t cell;
    (void)ctx;
    ks_last.op = 1;
    if (!flash_ok)
        return ks_last.rc = -1;
    if (!ks_arg(kind, slot) || !n || n > (kind == K_STORE_SONG ? (size_t)K_SAVE_BUF : (size_t)KS_CELL))
        return ks_last.rc = -2;
    ks_where(kind, slot, &obj, &cell);
    if (kind == K_STORE_SONG)
        return ks_last.rc = ks_commit(obj, (uint32_t)n, buf, 0u, (uint32_t)n);
    return ks_last.rc = ks_commit(obj, KS_BANK_ALL, buf, cell, (uint32_t)n);   /* (a shorter preset leaves the old one's tail in
                                                 * the cell: the read takes the length from the file's own header) */
}
static int ks_erase_slot(void *ctx, int kind, int slot)
{
    int obj, cur, rc;
    uint32_t cell;
    (void)ctx;
    ks_last.op = 3;
    if (!flash_ok)
        return ks_last.rc = -1;
    if (!ks_arg(kind, slot))
        return ks_last.rc = -2;
    ks_where(kind, slot, &obj, &cell);
    if ((cur = ks_current(obj)) < 0)
        return ks_last.rc = 0;
    if (kind == K_STORE_PRESET)
        return ks_last.rc = ks_commit(obj, KS_BANK_ALL, 0, cell, KS_CELL);   /* the bank anew, that cell erased */
    ks_cur[obj].known = 0;
    if ((rc = ks_erase(ks_base(obj, !cur))) != 0 || (rc = ks_erase(ks_base(obj, cur))) != 0)   /* the older copy first */
        return ks_last.rc = rc;
    return ks_last.rc = 0;
}
static int ks_latest(void *ctx)                /* the song with the highest sequence */
{
    uint32_t m = 0;
    int o, at = -1;
    (void)ctx;
    if (!flash_ok)
        return -1;
    for (o = 0; o < K_SONG_SLOTS; o++)
        if (ks_current(o) >= 0 && (at < 0 || ks_cur[o].seq > m)) {
            m = ks_cur[o].seq;
            at = o;
        }
    return at;
}
/* the settings: a record in the first bank, after its eight cells (the bank is written whole: presets and settings keep each other) */
static long ks_read_settings(void *ctx, uint8_t *buf, size_t cap)
{
    uint8_t b[KS_SET_LEN];
    int cur;
    uint32_t n;
    (void)ctx;
    if (!flash_ok || (cur = ks_current(KS_BANK0)) < 0 || ks_cur[KS_BANK0].len < KS_BANK_ALL)
        return 0;
    if (ota_fread(ks_base(KS_BANK0, cur) + KS_PAYLOAD_OFF + KS_BANK_LEN, b, KS_SET_LEN))
        return -3;
    if (memcmp(b, "KRST", 4) || (n = 6u + b[5]) > KS_SET_LEN || n > cap)
        return 0;
    memcpy(buf, b, n);
    return (long)n;
}
static int ks_write_settings(void *ctx, const uint8_t *buf, size_t n)
{
    (void)ctx;
    ks_last.op = 1;
    if (!flash_ok)
        return ks_last.rc = -1;
    if (!n || n > KS_SET_LEN)
        return ks_last.rc = -2;
    return ks_last.rc = ks_commit(KS_BANK0, KS_BANK_ALL, buf, KS_BANK_LEN, (uint32_t)n);
}
static const KStore KS_STORE = {ks_read, ks_write, ks_erase_slot, ks_latest, 0, ks_read_settings, ks_write_settings};

static int ks_clear(void)                       /* every Kreisel sector erased, whatever it holds (console `songclear yes`) */
{
    uint32_t off;
    int rc, o;
    ks_last.op = 4;
    if (!flash_ok)
        return ks_last.rc = -1;
    for (o = 0; o < KS_OBJS; o++)
        ks_cur[o].known = 0;
    for (off = KS_LO; off < KS_HI; off += KS_SECTOR)
        if ((rc = ks_erase(off)) != 0)
            return ks_last.rc = rc;
    return ks_last.rc = 0;
}

/* power-on, after k_init and before the audio starts: the store set (the slots' names read), then the song saved last, unless this
 * boot follows a crash, a watchdog reset or a boot that never got healthy (as Felucca skips its autosave restore), or HOME is held.
 * A .noinit marker says how far the boot before got: "SCAN" (it died reading the slots: no store this time, SAVE fails until the
 * next boot) or "LOAD" (it died loading the song: the store, but no song). (A power cut in the middle leaves the marker too: the
 * FM-1's RAM keeps it through a short one, as when the host's USB power drops while it sleeps.) */
static uint32_t ks_boot_ms;
static uint8_t ks_late;
static void ks_boot(int clean, int home_held)
{
    uint32_t t0 = fm1_ticks(), was = ks_pend;
    if (!flash_ok)
        return;
    ks_pend = 0;
    if (was == KS_PEND_SCAN) {
        ks_boot_res = 5;
        ks_late = 1;                           /* (the store once this boot is healthy: ks_boot_late) */
        return;
    }
    ks_pend = KS_PEND_SCAN;
    k_store_set(&KS_STORE);
    ks_pend = 0;
    (void)k_settings_load(&app);               /* the settings first: RESTORE LAST says whether the song comes back */
    if (was == KS_PEND_MAGIC)
        ks_boot_res = 5;
    else if (!clean)
        ks_boot_res = 3;
    else if (home_held)
        ks_boot_res = 4;
    else if (KREISEL_DEMO_SONG == 1)
        ks_boot_res = k_demo_load(&app) == 0 ? 8 : 6;   /* (8: COLDWIRE, while developing: kreisel_demo.h; the slots stay as they were;
                                                         * before RESTORE LAST, which is about the saved song) */
    else if (app.set_v[K_SET_RESTORE])
        ks_boot_res = 7;                       /* (7: skipped, RESTORE LAST OFF) */
    else if (ks_latest(0) < 0)
        ks_boot_res = KREISEL_DEMO_SONG == 2 && k_demo_load(&app) == 0 ? 8 : 2;   /* (nothing saved yet: the demo, a first start) */
    else {
        ks_pend = KS_PEND_MAGIC;
        ks_boot_res = k_save_boot(&app) == 0 ? 1 : 6;
        ks_pend = 0;
    }
    ks_boot_ms = (fm1_ticks() - t0) / FM1_TICKS_PER_US / 1000u;
}
/* the boot after one that died in the scan went without the store (SAVE failed until the next boot: twice on the owner's FM-1,
 * 2026-11-10, a USB power dip while the host slept, it seems). Once this boot is healthy the scan is tried again, its marker set as
 * at boot: the store and the settings back, no song loaded (the one in hand stays) */
static void ks_boot_late(void)
{
    if (!ks_late || !flash_ok)
        return;
    ks_late = 0;
    ks_pend = KS_PEND_SCAN;
    k_store_set(&KS_STORE);
    ks_pend = 0;
    (void)k_settings_load(&app);
}

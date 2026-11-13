/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Where Fune was started from, before it writes any flash (only on M-VAVE's own bootloader, as it was
 * made and tested; other loaders such as fm1-dock put their own data where Fune keeps songs and plugins).
 *
 * Two things, both read-only:
 *  - the boot area: the flash header and its first record (each ENC 0xFFFF, CRC16), the boot container at 0xA0 (its header
 *    and payload each ciphered from their own start, as ota.c's ota_boot_valid reads a package's), the payload's CRC16
 *    computed over all of it. The container must be sound and not m-bootloader's ("AC79BLID" at payload +0x40); whether it
 *    is one of the stock ones Fune knows (BOOT_STOCK) is reported, not required. The flash header's app_dir_head must be
 *    at 0x4000.
 *  - the mapping: Fune's XIP window must start at flash 0x4000 (FM1_XIP, which the store and the plugins assume). The
 *    plain (unencrypted) window is compared with the flash read directly at a few places that a unit has filled (the
 *    Bluetooth MAC, song 1, the key and MAC sector); a loader that maps Fune from elsewhere shows other bytes there.
 * Included by fm1_main.c after its ota_fread (the flash read with interrupts held off), and by the update loader
 * (BSP loader/loader.c with LDR_BOOT_CHECK: build.sh), which refuses to write an app on any other boot area. */

typedef struct {
    uint8_t hdr_ok;      /* the flash header and its boot record read right (CRCs) */
    uint8_t cont_ok;     /* the boot container's header and payload CRCs right */
    uint8_t other_bl;    /* a bootloader that names itself (m-bootloader: "AC79BLID") */
    uint8_t stock;       /* the container is one of BOOT_STOCK */
    uint8_t mapped;      /* 1: XIP starts at 0x4000; 0: it does not; 2: could not tell (the sampled bytes all 0xFF) */
    uint8_t verdict;     /* BOOT_OK, or why not */
    uint16_t pcrc;       /* the payload's CRC16, as computed */
    uint32_t blen;       /* the boot container's length */
    uint32_t appdir;     /* app_dir_head's flash address (0: not found) */
} BootInfo;
enum { BOOT_OK, BOOT_NOT_STOCK, BOOT_LAYOUT, BOOT_MAPPED, BOOT_UNREAD };

/* M-VAVE's own boot containers (length, payload CRC16), as read from units (`boot` on the console) */
static const struct { uint32_t len; uint16_t pcrc; } BOOT_STOCK[] = {
    { 0x3830u, 0x81A2u },   /* the owner's FM-1 (2026-11-13): a 0x3820-byte payload, as m-bootloader's SPEC gives the stock one */
};

#ifndef BOOT_SAMPLE_AT                         /* (another board's places: boards/smk37/smk37_map.h) */
#define BOOT_SAMPLE_AT { 0xE9000u, 0xF8000u, 0xF9000u, 0xFF000u, 0xFFF00u, 0xFFF40u }
#endif
static BootInfo boot_info;
static uint8_t boot_locked;   /* 1: not M-VAVE's bootloader or not mapped at 0x4000: Fune writes no flash (fm1_main.c) */

static int boot_rec_name(const uint8_t *r, const char *name)   /* a decrypted 32-byte directory record's name (at +16) */
{
    uint32_t i;
    for (i = 0; name[i]; i++)
        if (r[16 + i] != (uint8_t)name[i])
            return 0;
    return r[16 + i] == 0u;
}

static void boot_inspect(BootInfo *bi)
{
    static uint8_t b[512];
    uint32_t i, n, k = 0xFFFFu, len, pwant = 0, pcrc = 0;
    for (i = 0; i < sizeof *bi; i++)
        ((uint8_t *)bi)[i] = 0;
    bi->verdict = BOOT_UNREAD;
    if (ota_fread(0, b, 64))
        return;
    ota_jl_enc(b, 32);
    ota_jl_enc(b + 32, 32);
    bi->hdr_ok = ota_crc16(b + 2, 30, 0) == ota_rd16(b) && ota_crc16(b + 34, 30, 0) == ota_rd16(b + 32) && ota_rd32(b + 36) == 0xA0u;
    len = ota_rd32(b + 40);
    bi->blen = len;
    if (bi->hdr_ok && len >= 16u + 0x50u && len <= 0x4000u - 0xA0u) {
        int good = 1;
        for (i = 0; i < len; i += n) {
            uint32_t h = 0;
            n = len - i > sizeof b ? sizeof b : len - i;
            if (ota_fread(0xA0u + i, b, n))
                return;
            if (!i) {
                ota_jl_enc(b, 16);
                if (ota_crc16(b, 14, 0) != ota_rd16(b + 14) || ota_rd16(b + 2) + 16u != len || ota_rd32(b + 4) != 0x01C02000u)
                    good = 0;
                pwant = ota_rd16(b + 12);
                h = 16;
            }
            k = ota_jl_enc_k(b + h, n - h, k);
            pcrc = ota_crc16(b + h, n - h, pcrc);
            if (!i && ota_memeq(b + 16 + 0x40, (const uint8_t *)"AC79BLID", 8))
                bi->other_bl = 1;
        }
        bi->pcrc = (uint16_t)pcrc;
        bi->cont_ok = (uint8_t)(good && pcrc == pwant);
    }
    for (i = 0; i < sizeof BOOT_STOCK / sizeof BOOT_STOCK[0]; i++)
        if (BOOT_STOCK[i].len && BOOT_STOCK[i].len == bi->blen && BOOT_STOCK[i].pcrc == bi->pcrc)
            bi->stock = 1;
    for (i = 0; i < 8u; i++) {   /* the directory after the header: app_dir_head's address */
        if (ota_fread(0x20u + i * 32u, b, 32))
            return;
        ota_jl_enc(b, 32);
        if (ota_crc16(b + 2, 30, 0) != ota_rd16(b))
            break;
        if (boot_rec_name(b, "app_dir_head")) { bi->appdir = ota_rd32(b + 4); break; }
    }
#ifdef FELUCCA_LOADER
    bi->mapped = 1;   /* (the update loader runs from RAM: nothing of it is mapped) */
#else
    {   /* the mapping: the key and MAC sector read directly and through the plain XIP window */
        static const uint32_t AT[6] = BOOT_SAMPLE_AT;   /* (the Bluetooth MAC, song 1, the key and MAC sector: one at least is
                                                 * filled on any unit) */
        int same = 1, blank = 1;
        fl_plain_window_init();
        for (i = 0; i < 6u; i++) {
            const volatile uint8_t *x = (const volatile uint8_t *)FM1_XIP(AT[i]);
            uint32_t j;
            if (ota_fread(AT[i], b, 64))
                return;
            for (j = 0; j < 64u; j++) {
                if (b[j] != x[j]) same = 0;
                if (b[j] != 0xFFu) blank = 0;
            }
        }
        bi->mapped = (uint8_t)(!same ? 0 : blank ? 2 : 1);
    }
#endif
    /* refused: a boot area that is not a sound JieLi boot file, or one that names itself as another loader's. One that is
     * sound but not in BOOT_STOCK is let through (stock=0 on the console): newer units, with the UC25HQ80 flash, may carry
     * a newer M-VAVE bootloader. A loader that keeps the stock boot file but starts Fune elsewhere (fm1-dock) is caught
     * by the layout and the mapping below. */
    bi->verdict = !bi->hdr_ok || !bi->cont_ok || bi->other_bl ? BOOT_NOT_STOCK :
                  bi->appdir != 0x4000u ? BOOT_LAYOUT : bi->mapped == 0 ? BOOT_MAPPED : BOOT_OK;
}

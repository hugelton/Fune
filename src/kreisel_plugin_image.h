/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A plugin as an image in a device's flash (the FM-1: docs/PLUGIN_SDK.md, "On the FM-1"), run in place: no loader of shared
 * libraries, no RAM copy of its code. The portable part, tested on the host (tests/plugin_image_test.c); the FM-1's flash, console
 * and boot are platforms/fm1hw/fm1_plugins.c.
 *
 * An image is a header (KImageHeader, 64 bytes), then the plugin's code and constants, then its .data's first values:
 *
 *   0        KImageHeader       magic, the interface's version, the plugin's id, version and kind, where it is linked, sizes, CRCs
 *   64       code, constants    linked to run at base + 64 (the KPlugin record among them, at `record`)
 *   data_off .data's values     copied to `ram` at start; .bss (bss_size bytes, zeroed) follows it in RAM
 *
 * A file as built (tools/kpl_build.py, `.kpl`) is linked for address 0 (code) and 0 (RAM) and carries its relocations after the
 * image: nreloc little-endian words, each the offset of a 32-bit field (bits 0..29, any alignment) and its kind (bit 31: 1 the RAM
 * address is added, 0 the image's base). Linking (k_image_link, or the sender: platforms/fm1hw/fm1_plugin.py) adds the base and
 * the RAM address the device names, drops the relocations and seals the header (nreloc 0, CRCs). The device keeps only linked
 * images and checks them at every start (k_image_check): the magic, the header's CRC, the interface's major version, that it is
 * linked where it lies, the CRC of the rest, the record inside it and its pointers inside it.
 *
 * Why not position-independent code: the FM-1 toolchain's -fPIC is FDPIC (a GOT base in r15 set by the caller, function
 * descriptors), which the host's plain function pointers (engine_t.render ...) cannot call. A relocated image needs nothing of the
 * host at run time. Integers are little-endian on every device Kreisel builds for. */
#ifndef KREISEL_PLUGIN_IMAGE_H
#define KREISEL_PLUGIN_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#include "kreisel_plugin.h"

#define K_IMAGE_MAGIC 0x4D49504Bu    /* "KPIM" */
#define K_IMAGE_FORMAT 1u            /* this header's layout */
#define K_IMAGE_HDR 64u
#define K_IMAGE_SECTOR 4096u         /* an image starts on a sector and takes whole ones */
#define K_IMAGE_RELOC_RAM 0x80000000u
#define K_IMAGE_RELOC_OFF 0x3FFFFFFFu

typedef struct {
    uint32_t magic;       /* K_IMAGE_MAGIC: written last on a device, so an image cut short has none */
    uint16_t hdr_size;    /* K_IMAGE_HDR */
    uint8_t format;       /* K_IMAGE_FORMAT */
    uint8_t flags;        /* on / off without an erase: off while an odd number of its bits are 0 (each switch clears one more);
                           * outside the header's CRC. 0xFF: on */
    uint16_t api;         /* K_ENGINE_API_VERSION it was built against: the major (high byte) must be the host's */
    uint8_t version;      /* the plugin's own version (KPlugin.version: 1 .. 255, one byte as in a song) */
    uint8_t kind;         /* KPluginKind */
    uint32_t uid;         /* its id (KEngine.uid) */
    uint32_t base;        /* the address byte 0 runs at; 0: a file not yet linked */
    uint32_t size;        /* the whole image: header, code and constants, .data's values (a multiple of 4) */
    uint32_t record;      /* the KPlugin record: its offset */
    uint32_t data_off;    /* .data's first values: their offset (data_size of them) */
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t ram;         /* where .data is linked in RAM (.bss after it); 0 with none, or in a file not yet linked */
    uint32_t nreloc;      /* a file: the relocations after the image; 0 once linked */
    uint32_t crc;         /* zlib CRC-32 of bytes [hdr_size, size) */
    uint32_t reserved[2]; /* 0 */
    uint32_t hcrc;        /* zlib CRC-32 of bytes 4 .. 59 with flags taken as 0xFF */
} KImageHeader;

/* what the checks can say: 0, or */
enum {
    K_IMG_E_MAGIC = -1,     /* no image (an erased sector, an image cut short, something else) */
    K_IMG_E_HEADER = -2,    /* another header layout, or its CRC does not match */
    K_IMG_E_API = -3,       /* built against another major version of the interface (K_ENGINE_API_MAJOR) */
    K_IMG_E_SIZE = -4,      /* sizes that do not fit the image or the room it is in */
    K_IMG_E_BASE = -5,      /* not linked for where it lies (or not linked at all) */
    K_IMG_E_CRC = -6,       /* the code or constants are damaged */
    K_IMG_E_RECORD = -7,    /* the record is not inside it, or says another id, version or kind than the header */
    K_IMG_E_POINTER = -8,   /* the record, its descriptor or its DSP points outside the image */
    K_IMG_E_RAM = -9,       /* its RAM is not inside the device's pool, or overlaps another plugin's */
    K_IMG_E_RELOC = -10     /* (linking) a relocation outside the image, or the file is not a file */
};

uint32_t k_image_crc32(uint32_t crc, const void *p, size_t n);   /* zlib's: k_image_crc32(0, p, n); running: pass the last result */
uint32_t k_image_hcrc(const KImageHeader *h);                    /* the header's own CRC, as hcrc should be */
int k_image_on(const KImageHeader *h);                           /* 1 on, 0 switched off (flags) */
/* the header at p (avail bytes readable): 0 when it is one this host takes (magic, layout, CRC, interface, sizes); *h gets it */
int k_image_header(const uint8_t *p, size_t avail, KImageHeader *h);
/* a whole linked image at p, the address `at` as the plugin sees it (on a device the same as p): k_image_header, linked at `at`,
 * the CRC, the record and its pointers inside the image. 0 or a K_IMG_E_* */
int k_image_check(const uint8_t *p, size_t avail, uint32_t at, KImageHeader *h);
/* its record (after k_image_check) */
const KPlugin *k_image_record(const uint8_t *p, const KImageHeader *h);
/* a file (img, len bytes: header, image, relocations) linked in place for `base` and `ram`: the header sealed; returns the image's
 * length (the relocations dropped), or a K_IMG_E_* */
long k_image_link(uint8_t *img, size_t len, uint32_t base, uint32_t ram);

/* ---- a region of flash holding images, one after another on sector boundaries (the FM-1: 0x097000..0x0DFFFF) ---- */
enum {   /* what became of an image at start (KImageSlot.state) */
    K_IMG_LIVE = 1,         /* registered: its engine is there to play */
    K_IMG_OFF,              /* switched off (flags) */
    K_IMG_SKIPPED,          /* a SAFE start: no plugin this time */
    K_IMG_BAD,              /* refused by k_image_check (err) */
    K_IMG_OLDER,            /* another image of the same id has a newer version (or the same one, earlier in the region) */
    K_IMG_REFUSED           /* refused by k_plugin_register (err: a K_PLUG_E_*), or its RAM (err K_IMG_E_RAM) */
};
typedef struct {
    uint32_t off;           /* its offset in the region */
    uint32_t len;           /* bytes it takes there: whole sectors */
    KImageHeader h;         /* (as read; zero when the header was not one) */
    int8_t state;           /* K_IMG_*, 0 before k_image_load */
    int8_t err;             /* why it was refused */
    int16_t index;          /* its index in the registry (k_plugin_at) when live, else -1 */
} KImageSlot;

/* the images in a region (p, len bytes): each sector that starts with a header this host takes (k_image_header) begins one, which
 * takes the sectors of its size; a sector with the magic but a header it does not take is listed too (K_IMG_BAD, one sector, so it
 * can be erased); the others are free. Up to max into out; returns how many */
int k_image_scan(const uint8_t *p, uint32_t len, KImageSlot *out, int max);
/* where `size` bytes would go: the first run of free sectors long enough, as an offset; -1 when there is none */
long k_image_place(const KImageSlot *s, int n, uint32_t region_len, uint32_t size);
/* a RAM block of `need` bytes for a new image in the pool (pool_at, pool_len: its address and size), clear of the images' RAM:
 * the address (4-aligned), or 0 when there is no room. need 0: 0 */
uint32_t k_image_ram_place(const KImageSlot *s, int n, uint32_t pool_at, uint32_t pool_len, uint32_t need);

/* The pool a device gives images for their .data and .bss: its memory and the address the plugins see it at (the same on a device) */
typedef struct {
    uint8_t *mem;
    uint32_t at, len;
} KImagePool;
/* Register the region's images (p, len bytes, at `at` as the plugins see it; after k_image_scan): `skip` 1 is a SAFE start (all
 * K_IMG_SKIPPED). Each image k_image_check takes, switched on, the newest of its id, whose RAM is inside the pool and clear of the
 * live ones' gets its .data and .bss there and goes through k_plugin_register as any plugin does (k_plugin_check, the device's room
 * set by k_plugin_set_caps). Returns how many are live. */
int k_image_load(const uint8_t *p, uint32_t at, uint32_t len, KImageSlot *s, int n, const KImagePool *pool, int skip);
/* s[i] alone (an image just installed; the others as k_image_load left them): checked, its RAM, registered. 1 live, else 0 */
int k_image_load_one(const uint8_t *p, uint32_t at, uint32_t len, KImageSlot *s, int n, int i, const KImagePool *pool);
#endif

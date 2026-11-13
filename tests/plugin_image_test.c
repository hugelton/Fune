/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Plugin images (src/kreisel_plugin_image.h), what the FM-1 takes from its flash, on a fake region in memory: the CRC, the header,
 * the refusals (another interface, a damaged image, one linked elsewhere, pointers out of it), linking a file, a region's images
 * found, placed, switched off, the newest of an id, a SAFE start, the RAM pool, and registration through k_plugin_register.
 * The code in these images is not code (this host cannot run the FM-1's): registration reads only the record and descriptor. */
#include "kreisel.h"
#include "kreisel_dsp.h"
#include "kreisel_engine_api.h"
#include "kreisel_engine_types.h"
#include "kreisel_plugin_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define REGION (16u * K_IMAGE_SECTOR)
static uint8_t region[REGION] __attribute__((aligned(16)));
#define AT 0x02093000u                    /* the region's address as the plugins see it (the FM-1's: XIP of flash 0x097000) */
static uint8_t pool_mem[256] __attribute__((aligned(16)));
static const KImagePool POOL = {pool_mem, 0x01C08000u, sizeof pool_mem};

static const KEngine DESC = {
    .name = "TONE",
    .uid = K_UID('t', 'o', 'n', 'e'),
    .page_title = {"TONE", "", ""},
    .edit = {{"LVL", KF_PCT, 0, 127, 100, 0, 0}, {"DTN", KF_INT, -50, 50, 0, 0, "ct"}},
    .macro = {0, 1, 0, 1},
    .nparams = 2,
    .page2 = 1,
};

/* one image at region + off: a header, the record, its descriptor, its DSP, 32 bytes of "code", data_size bytes of .data's
 * values; linked for AT + off and `ram`. Returns its size */
typedef struct { uint32_t uid; uint8_t version, kind; uint32_t data, bss, ram; } Spec;
static uint32_t pos8(uint32_t n) { return (n + 7u) & ~7u; }
static uint32_t make(uint32_t off, Spec sp)
{
    uint8_t *img = region + off;
    const uint32_t o_rec = K_IMAGE_HDR, o_desc = pos8(o_rec + sizeof(KPlugin)), o_dsp = pos8(o_desc + sizeof(KEngine));
    const uint32_t o_code = pos8(o_dsp + sizeof(engine_t)), o_data = o_code + 32u, size = (o_data + sp.data + 3u) & ~3u;
    KImageHeader h = {0};
    KPlugin rec = {sizeof(KPlugin), K_PLUGIN_ABI, sp.version, sp.kind, (const KEngine *)(void *)(img + o_desc),
                   img + o_dsp, 0, 0, 0, 0, 0, 0};
    KEngine d = DESC;
    engine_t e = {0};
    uint32_t i;
    d.uid = sp.uid;
    e.name = "TONE";
    e.render = (void (*)(struct track *, voice_t *, int32_t *, uint32_t, const vmod_t *))(uintptr_t)(img + o_code);
    e.note_on = (void (*)(struct track *, voice_t *))(uintptr_t)(img + o_code + 8);
    memset(img, 0xA5, size);
    memcpy(img + o_rec, &rec, sizeof rec);
    memcpy(img + o_desc, &d, sizeof d);
    memcpy(img + o_dsp, &e, sizeof e);
    for (i = 0; i < sp.data; i++) img[o_data + i] = (uint8_t)(0x10 + i);
    h.magic = K_IMAGE_MAGIC;
    h.hdr_size = K_IMAGE_HDR;
    h.format = K_IMAGE_FORMAT;
    h.flags = 0xFF;
    h.api = K_ENGINE_API_VERSION;
    h.version = sp.version;
    h.kind = sp.kind;
    h.uid = sp.uid;
    h.base = AT + off;
    h.size = size;
    h.record = o_rec;
    h.data_off = o_data;
    h.data_size = sp.data;
    h.bss_size = sp.bss;
    h.ram = sp.data + sp.bss ? sp.ram : 0;
    h.crc = k_image_crc32(0, img + K_IMAGE_HDR, size - K_IMAGE_HDR);
    h.hcrc = k_image_hcrc(&h);
    memcpy(img, &h, sizeof h);
    return size;
}
static KImageHeader *hdr(uint32_t off) { return (KImageHeader *)(void *)(region + off); }
static void reseal(uint32_t off) { hdr(off)->hcrc = k_image_hcrc(hdr(off)); }
static int check(uint32_t off) { return k_image_check(region + off, REGION - off, AT + off, 0); }
static const Spec TONE1 = {K_UID('t', 'o', 'n', 'e'), 1, K_PLUG_SYNTH, 0, 0, 0};

static void test_crc_and_header(void)
{
    KImageHeader h;
    uint32_t size;
    assert(k_image_crc32(0, "123456789", 9) == 0xCBF43926u);   /* zlib's check value */
    assert(k_image_crc32(k_image_crc32(0, "1234", 4), "56789", 5) == 0xCBF43926u);   /* running */
    memset(region, 0xFF, sizeof region);
    assert(k_image_header(region, REGION, &h) == K_IMG_E_MAGIC);   /* erased */
    size = make(0, TONE1);
    assert(k_image_header(region, REGION, &h) == 0 && h.size == size && h.uid == TONE1.uid);
    assert(check(0) == 0);
    assert(k_image_record(region, &h)->desc->uid == TONE1.uid);
    assert(k_image_header(region, 63, &h) == K_IMG_E_MAGIC);                      /* not even a header's room */
    assert(k_image_check(region, size - 4, AT, 0) == K_IMG_E_SIZE);                /* cut short */
    /* on / off: each switch clears one more bit, outside the header's CRC */
    assert(k_image_on(hdr(0)));
    hdr(0)->flags = 0xFE;
    assert(!k_image_on(hdr(0)) && check(0) == 0);
    hdr(0)->flags = 0xFC;
    assert(k_image_on(hdr(0)) && check(0) == 0);
    hdr(0)->flags = 0x00;
    assert(k_image_on(hdr(0)));
    hdr(0)->flags = 0xFF;
}

static void test_refusals(void)
{
    uint32_t size;
    memset(region, 0xFF, sizeof region);
    size = make(0, TONE1);
    /* another major version of the interface: refused; a newer minor: taken (it only appends) */
    hdr(0)->api = (uint16_t)((K_ENGINE_API_MAJOR + 1) << 8);
    reseal(0);
    assert(check(0) == K_IMG_E_API);
    hdr(0)->api = (uint16_t)(K_ENGINE_API_MAJOR << 8 | 0xFF);
    reseal(0);
    assert(check(0) == 0);
    /* a header changed without its CRC */
    hdr(0)->size += 4;
    assert(check(0) == K_IMG_E_HEADER);
    hdr(0)->size -= 4;
    assert(check(0) == 0);
    hdr(0)->format = 2;
    reseal(0);
    assert(check(0) == K_IMG_E_HEADER);
    make(0, TONE1);
    /* a damaged byte of code or constants */
    region[size - 40] ^= 1;
    assert(check(0) == K_IMG_E_CRC);
    region[size - 40] ^= 1;
    assert(check(0) == 0);
    /* linked for elsewhere, or not linked */
    assert(k_image_check(region, REGION, AT + K_IMAGE_SECTOR, 0) == K_IMG_E_BASE);
    hdr(0)->nreloc = 3;
    reseal(0);
    assert(check(0) == K_IMG_E_BASE);
    make(0, TONE1);
    /* the record says another id than the header; a version of 0 */
    hdr(0)->uid = K_UID('t', 'o', 'n', 'x');
    reseal(0);
    assert(check(0) == K_IMG_E_RECORD);
    make(0, TONE1);
    hdr(0)->version = 0;
    reseal(0);
    assert(check(0) == K_IMG_E_RECORD);
    /* pointers out of the image: the DSP's render, the descriptor */
    {
        KPlugin r;
        engine_t e;
        const uint32_t o_dsp = (uint32_t)((const uint8_t *)k_image_record(region, hdr(0))->dsp - region);
        make(0, TONE1);
        memcpy(&e, region + o_dsp, sizeof e);
        e.render = (void (*)(struct track *, voice_t *, int32_t *, uint32_t, const vmod_t *))(uintptr_t)(region + size + 64);
        memcpy(region + o_dsp, &e, sizeof e);
        hdr(0)->crc = k_image_crc32(0, region + K_IMAGE_HDR, size - K_IMAGE_HDR);
        reseal(0);
        assert(check(0) == K_IMG_E_POINTER);
        make(0, TONE1);
        memcpy(&r, region + K_IMAGE_HDR, sizeof r);
        r.desc = &DESC;   /* (the host's own: outside) */
        memcpy(region + K_IMAGE_HDR, &r, sizeof r);
        hdr(0)->crc = k_image_crc32(0, region + K_IMAGE_HDR, size - K_IMAGE_HDR);
        reseal(0);
        assert(check(0) == K_IMG_E_POINTER);
        make(0, TONE1);
        memcpy(&r, region + K_IMAGE_HDR, sizeof r);
        r.size = 8;       /* a record shorter than the first interface's */
        memcpy(region + K_IMAGE_HDR, &r, sizeof r);
        hdr(0)->crc = k_image_crc32(0, region + K_IMAGE_HDR, size - K_IMAGE_HDR);
        reseal(0);
        assert(check(0) == K_IMG_E_RECORD);
    }
    /* RAM named without any .data or .bss; .data with no RAM */
    make(0, TONE1);
    hdr(0)->ram = POOL.at;
    reseal(0);
    assert(check(0) == K_IMG_E_BASE);
    make(0, (Spec){TONE1.uid, 1, K_PLUG_SYNTH, 8, 8, 0});
    assert(check(0) == K_IMG_E_BASE);
}

/* a file as kpl_build.py makes it: linked at 0, relocations after the image */
static void test_link(void)
{
    static uint8_t f[K_IMAGE_HDR + 64 + 3 * 4];
    KImageHeader h = {0};
    uint32_t i;
    long n;
    for (i = 0; i < 64; i++) f[K_IMAGE_HDR + i] = (uint8_t)i;
    f[K_IMAGE_HDR + 5] = 0x40; f[K_IMAGE_HDR + 6] = 0; f[K_IMAGE_HDR + 7] = 0; f[K_IMAGE_HDR + 8] = 0;   /* 0x40 at 5: code at 0x40 */
    memset(f + K_IMAGE_HDR + 20, 0, 4);                                                                      /* 0 at 20: .data at 0 */
    h.magic = K_IMAGE_MAGIC;
    h.hdr_size = K_IMAGE_HDR;
    h.format = K_IMAGE_FORMAT;
    h.flags = 0xFF;
    h.api = K_ENGINE_API_VERSION;
    h.version = 3;
    h.kind = K_PLUG_SYNTH;
    h.uid = K_UID('l', 'i', 'n', 'k');
    h.size = K_IMAGE_HDR + 64;
    h.record = K_IMAGE_HDR + 32;
    h.data_off = K_IMAGE_HDR + 56;
    h.data_size = 8;
    h.bss_size = 4;
    h.nreloc = 3;
    h.crc = k_image_crc32(0, f + K_IMAGE_HDR, 64);
    h.hcrc = k_image_hcrc(&h);
    memcpy(f, &h, sizeof h);
    {
        const uint32_t rel[3] = {K_IMAGE_HDR + 5, (K_IMAGE_HDR + 20) | K_IMAGE_RELOC_RAM, K_IMAGE_HDR + 60};
        memcpy(f + K_IMAGE_HDR + 64, rel, sizeof rel);
    }
    {   /* refused, the buffer untouched: a relocation past the image, one of RAM in a plugin that has none, the wrong length */
        static uint8_t g[sizeof f];
        uint32_t bad = K_IMAGE_HDR + 62;
        memcpy(g, f, sizeof f);
        memcpy(g + K_IMAGE_HDR + 64 + 8, &bad, 4);
        assert(k_image_link(g, sizeof g, AT, POOL.at) == K_IMG_E_RELOC);
        assert(!memcmp(g + K_IMAGE_HDR, f + K_IMAGE_HDR, 64));
        assert(k_image_link(f, sizeof f - 4, AT, POOL.at) == K_IMG_E_RELOC);
        assert(k_image_link(f, sizeof f, AT, 0) == K_IMG_E_RELOC);       /* it has .data: RAM must be named */
        assert(k_image_link(f, sizeof f, AT + 2, POOL.at) == K_IMG_E_RELOC);
    }
    n = k_image_link(f, sizeof f, AT, POOL.at);
    assert(n == K_IMAGE_HDR + 64);
    assert(k_image_header(f, (size_t)n, &h) == 0 && h.base == AT && h.ram == POOL.at && h.nreloc == 0);
    assert(h.crc == k_image_crc32(0, f + K_IMAGE_HDR, 64));
    assert(f[K_IMAGE_HDR + 5] == (uint8_t)(AT + 0x40) && f[K_IMAGE_HDR + 8] == (uint8_t)((AT + 0x40) >> 24));   /* unaligned */
    assert(f[K_IMAGE_HDR + 20] == (uint8_t)POOL.at && f[K_IMAGE_HDR + 23] == (uint8_t)(POOL.at >> 24));
    assert(f[K_IMAGE_HDR + 60] == (uint8_t)(60 + (AT & 255u)));
    assert(k_image_link(f, sizeof f, AT, POOL.at) == K_IMG_E_RELOC);   /* linked already */
}

static void test_region(void)
{
    KImageSlot s[8];
    int n, live, i;
    const Spec TONE2 = {TONE1.uid, 2, K_PLUG_SYNTH, 6, 10, POOL.at + 64u};
    const Spec HUM = {K_UID('h', 'u', 'm', '1'), 1, K_PLUG_SYNTH, 0, 0, 0};
    const Spec BUZZ = {K_UID('b', 'u', 'z', 'z'), 4, K_PLUG_SYNTH, 4, 0, POOL.at + 72u};   /* its RAM over TONE 2's */
    const Spec FAR = {K_UID('f', 'a', 'r', '1'), 1, K_PLUG_SYNTH, 0, 4, POOL.at + 4096u};   /* outside the pool */
    k_plugin_reset();
    memset(region, 0xFF, sizeof region);
    make(0, TONE1);                                     /* sector 0: TONE 1 */
    memset(region + 1 * K_IMAGE_SECTOR, 0x5A, 300);     /* sector 1: something else (Felucca's store): free */
    make(2 * K_IMAGE_SECTOR, TONE2);                    /* sector 2: TONE 2, newer, with RAM */
    make(4 * K_IMAGE_SECTOR, HUM);                      /* sector 4 */
    hdr(4 * K_IMAGE_SECTOR)->flags = 0xFE;              /*   switched off */
    make(5 * K_IMAGE_SECTOR, BUZZ);                     /* sector 5 */
    make(6 * K_IMAGE_SECTOR, FAR);                      /* sector 6 */
    make(7 * K_IMAGE_SECTOR, HUM);                      /* sector 7: a damaged header (its magic kept) */
    hdr(7 * K_IMAGE_SECTOR)->hdr_size = 32;
    n = k_image_scan(region, REGION, s, 8);
    assert(n == 6);
    assert(s[0].off == 0 && s[1].off == 2 * K_IMAGE_SECTOR && s[2].off == 4 * K_IMAGE_SECTOR && s[5].off == 7 * K_IMAGE_SECTOR);
    assert(s[5].state == K_IMG_BAD && s[5].err == K_IMG_E_HEADER && s[5].len == K_IMAGE_SECTOR);
    /* room: the free sectors 1 and 3, then 8 .. 15 */
    assert(k_image_place(s, n, REGION, 100) == K_IMAGE_SECTOR);
    assert(k_image_place(s, n, REGION, K_IMAGE_SECTOR + 1) == 8 * K_IMAGE_SECTOR);
    assert(k_image_place(s, n, REGION, 8 * K_IMAGE_SECTOR) == 8 * K_IMAGE_SECTOR);
    assert(k_image_place(s, n, REGION, 8 * K_IMAGE_SECTOR + 1) == -1);
    assert(k_image_place(s, n, REGION, 0) == -1);
    /* RAM: TONE 2 has 64 .. 80, BUZZ 72 .. 76, FAR names 4096: the first free block of 64 from the pool's start, then after 80 */
    assert(k_image_ram_place(s, n, POOL.at, POOL.len, 64) == POOL.at);
    assert(k_image_ram_place(s, n, POOL.at, POOL.len, 65) == POOL.at + 80u);
    assert(k_image_ram_place(s, n, POOL.at, POOL.len, 256) == 0);
    assert(k_image_ram_place(s, n, POOL.at, POOL.len, 0) == 0);

    /* a SAFE start: nothing registered */
    live = k_image_load(region, AT, REGION, s, n, &POOL, 1);
    assert(live == 0 && k_plugin_count() == K_ENGINE_COUNT);
    assert(s[0].state == K_IMG_SKIPPED && s[2].state == K_IMG_OFF && s[5].state == K_IMG_BAD);

    /* a start: TONE 2 (the newer of tone), BUZZ refused for its RAM, FAR for a pool it is outside of */
    memset(pool_mem, 0xEE, sizeof pool_mem);
    live = k_image_load(region, AT, REGION, s, n, &POOL, 0);
    assert(live == 1);
    assert(s[0].state == K_IMG_OLDER && s[1].state == K_IMG_LIVE && s[2].state == K_IMG_OFF);
    assert(s[3].state == K_IMG_REFUSED && s[3].err == K_IMG_E_RAM);
    assert(s[4].state == K_IMG_REFUSED && s[4].err == K_IMG_E_RAM);
    assert(s[1].index == K_ENGINE_COUNT && k_engine_find(TONE1.uid) == K_ENGINE_COUNT);
    assert(k_plugin_at(K_ENGINE_COUNT)->version == 2 && !strcmp(k_engine_at(K_ENGINE_COUNT)->name, "TONE"));
    assert(k_plugin_check(k_plugin_at(K_ENGINE_COUNT), 0) == 0);
    for (i = 0; i < 6; i++) assert(pool_mem[64 + i] == 0x10 + i);   /* .data's values */
    for (i = 6; i < 16; i++) assert(pool_mem[64 + i] == 0);         /* .bss */
    assert(pool_mem[63] == 0xEE && pool_mem[80] == 0xEE);          /* nothing else touched */

    /* an image installed afterwards: HUM, linked into the free sector 1 (the same id as the one switched off) */
    {
        KImageSlot t[8];
        int m;
        make(1 * K_IMAGE_SECTOR, HUM);
        m = k_image_scan(region, REGION, t, 8);
        assert(m == 7 && t[1].off == K_IMAGE_SECTOR);
        for (i = 0; i < 6; i++) t[i + (i >= 1)] = s[i];   /* the others as the start left them */
        t[1].state = 0;
        assert(k_image_load_one(region, AT, REGION, t, m, 1, &POOL) == 1);
        assert(t[1].state == K_IMG_LIVE && k_engine_find(HUM.uid) == K_ENGINE_COUNT + 1);
        /* the same id again (TONE 1 while TONE 2 plays): refused by the registry, until a restart chooses */
        t[0].state = 0;
        assert(k_image_load_one(region, AT, REGION, t, m, 0, &POOL) == 0);
        assert(t[0].state == K_IMG_REFUSED && t[0].err == K_PLUG_E_TAKEN);
    }
    k_plugin_reset();
}

/* the device's room applies to an image as to any plugin: one that wants the FPU is refused on the FM-1 */
static void test_room(void)
{
    static const KDeviceCaps fm1 = {44100u, 40960u, 120u, 6u, 0u};
    KImageSlot s[2];
    KPlugin r;
    uint32_t size;
    k_plugin_reset();
    k_plugin_set_caps(&fm1);
    memset(region, 0xFF, sizeof region);
    size = make(0, TONE1);
    memcpy(&r, region + K_IMAGE_HDR, sizeof r);
    r.needs = K_NEED_FLOAT;
    memcpy(region + K_IMAGE_HDR, &r, sizeof r);
    hdr(0)->crc = k_image_crc32(0, region + K_IMAGE_HDR, size - K_IMAGE_HDR);
    reseal(0);
    assert(k_image_scan(region, REGION, s, 2) == 1);
    assert(k_image_load(region, AT, REGION, s, 1, &POOL, 0) == 0);
    assert(s[0].state == K_IMG_REFUSED && s[0].err == K_PLUG_E_NEED);
    k_plugin_set_caps(0);
    k_plugin_reset();
}

int main(void)
{
    test_crc_and_header();
    test_refusals();
    test_link();
    test_region();
    test_room();
    puts("plugin_image_test: ok");
    return 0;
}

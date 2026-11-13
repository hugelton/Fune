/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Plugin images in flash (kreisel_plugin_image.h): the header, the checks, linking, a region's images and their registration.
 * No device here: the FM-1 reads its region through the XIP window and passes the pointer (platforms/fm1hw/fm1_plugins.c); the
 * tests pass a buffer (tests/plugin_image_test.c). */
#include "kreisel_plugin_image.h"
#include <string.h>
#include "kreisel.h"
#include "kreisel_dsp.h"
#include "kreisel_effect.h"
#include "kreisel_engine_api.h"
#include "kreisel_engine_types.h"
#include "kreisel_plugin_ui.h"

_Static_assert(sizeof(KImageHeader) == K_IMAGE_HDR, "the image header is 64 bytes");
_Static_assert(offsetof(KImageHeader, flags) == 7 && offsetof(KImageHeader, hcrc) == 60, "the image header's layout");

uint32_t k_image_crc32(uint32_t crc, const void *p, size_t n)   /* zlib's CRC-32, four bits a step (a 64-byte table) */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = ~crc;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}
uint32_t k_image_hcrc(const KImageHeader *h)
{
    KImageHeader t = *h;
    t.flags = 0xFFu;
    return k_image_crc32(0, (const uint8_t *)&t + 4, offsetof(KImageHeader, hcrc) - 4u);
}
int k_image_on(const KImageHeader *h)
{
    unsigned z = 0, f = h->flags;
    for (int k = 0; k < 8; k++) z += !(f >> k & 1u);
    return !(z & 1u);
}

int k_image_header(const uint8_t *p, size_t avail, KImageHeader *h)
{
    KImageHeader t;
    if (!p || avail < K_IMAGE_HDR) return K_IMG_E_MAGIC;
    memcpy(&t, p, sizeof t);
    if (h) memset(h, 0, sizeof *h);
    if (t.magic != K_IMAGE_MAGIC) return K_IMG_E_MAGIC;
    if (t.hdr_size != K_IMAGE_HDR || t.format != K_IMAGE_FORMAT || t.hcrc != k_image_hcrc(&t)) return K_IMG_E_HEADER;
    if (h) *h = t;   /* (from here on the caller may list it: its sizes may still be wrong) */
    if ((t.api >> 8) != K_ENGINE_API_MAJOR) return K_IMG_E_API;
    if (t.version < 1 || t.kind > K_PLUG_EFFECT) return K_IMG_E_RECORD;
    if (t.size < K_IMAGE_HDR || (t.size & 3u) || t.size > avail || t.record < K_IMAGE_HDR || (t.record & 3u) ||
        t.record > t.size || t.size - t.record < K_PLUGIN_SIZE_1 || t.data_off < K_IMAGE_HDR || t.data_off > t.size ||
        t.data_size > t.size - t.data_off || t.bss_size > 0x00100000u)
        return K_IMG_E_SIZE;
    return 0;
}

/* [q, q + n) inside [lo, hi) */
static int inside(uintptr_t lo, uintptr_t hi, uintptr_t q, size_t n) { return q >= lo && q <= hi && n <= hi - q; }
#define FN_OK(f) (!(f) || inside(code_lo, code_hi, (uintptr_t)(f), 2u))   /* a function: 0, or in the code */
#define DATA_OK(q, n) inside(lo, hi, (uintptr_t)(q), (n))

const KPlugin *k_image_record(const uint8_t *p, const KImageHeader *h) { return (const KPlugin *)(const void *)(p + h->record); }

static int record_ok(const uint8_t *p, const KImageHeader *h)
{
    const uintptr_t lo = (uintptr_t)p + K_IMAGE_HDR, hi = (uintptr_t)p + h->size;
    const uintptr_t code_lo = lo, code_hi = (uintptr_t)p + (h->data_size ? h->data_off : h->size);
    const KPlugin *rp = k_image_record(p, h);
    KPlugin r = {0};
    if (rp->size < K_PLUGIN_SIZE_1 || rp->size > h->size - h->record) return K_IMG_E_RECORD;
    memcpy(&r, rp, rp->size < sizeof r ? rp->size : sizeof r);   /* what it knows; the rest 0 (as k_plugin_check reads it) */
    if (!r.desc || !DATA_OK(r.desc, sizeof(KEngine))) return K_IMG_E_POINTER;
    if (r.desc->uid != h->uid || r.version != h->version || r.kind != h->kind) return K_IMG_E_RECORD;
    if (r.lane_notes && !DATA_OK(r.lane_notes, K_LANES)) return K_IMG_E_POINTER;
    if (r.ext) {
        const KPluginExt *x = r.ext;
        if (!DATA_OK(x, sizeof x->size) || !DATA_OK(x, x->size < sizeof *x ? x->size : sizeof *x)) return K_IMG_E_POINTER;
        if (x->size >= offsetof(KPluginExt, layouts) && !FN_OK(x->attach)) return K_IMG_E_POINTER;
        if (x->size >= offsetof(KPluginExt, draw) + sizeof x->draw && !FN_OK(x->draw)) return K_IMG_E_POINTER;
        if (x->size >= offsetof(KPluginExt, nlayouts) && x->layouts && !DATA_OK(x->layouts, sizeof *x->layouts)) return K_IMG_E_POINTER;
    }
    if (!r.dsp) return 0;   /* (k_plugin_register says whether it may have none) */
    if (r.kind == K_PLUG_EFFECT) {
        const KEffectDSP *e = r.dsp;
        if (!DATA_OK(e, sizeof *e) || !FN_OK(e->mem_size) || !FN_OK(e->init) || !FN_OK(e->process) || !FN_OK(e->reset) ||
            !FN_OK(e->set_param))
            return K_IMG_E_POINTER;
    } else {
        const engine_t *e = r.dsp;
        if (!DATA_OK(e, sizeof *e) || !e->render || !FN_OK(e->note_on) || !FN_OK(e->render) || !FN_OK(e->amp) || !FN_OK(e->desc) ||
            !FN_OK(e->block) || !FN_OK(e->keys) || !FN_OK(e->done) || !FN_OK(e->post))
            return K_IMG_E_POINTER;
    }
    return 0;
}

int k_image_check(const uint8_t *p, size_t avail, uint32_t at, KImageHeader *h)
{
    KImageHeader t;
    int rc = k_image_header(p, avail, &t);
    if (h) *h = t;
    if (rc) return rc;
    if (t.base != at || t.nreloc || (t.data_size + t.bss_size ? !t.ram || (t.ram & 3u) : t.ram != 0)) return K_IMG_E_BASE;
    if (k_image_crc32(0, p + K_IMAGE_HDR, t.size - K_IMAGE_HDR) != t.crc) return K_IMG_E_CRC;
    return record_ok(p, &t);
}

static uint32_t rd32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void wr32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }

long k_image_link(uint8_t *img, size_t len, uint32_t base, uint32_t ram)
{
    KImageHeader h;
    int rc = k_image_header(img, len, &h);
    uint32_t i;
    if (rc) return rc;
    if (h.base || h.ram || (base & 3u) || (ram & 3u)) return K_IMG_E_RELOC;
    if (h.nreloc > (len - h.size) / 4u || (size_t)h.size + 4u * (size_t)h.nreloc != len) return K_IMG_E_RELOC;
    if (h.data_size + h.bss_size && !ram) return K_IMG_E_RELOC;
    for (i = 0; i < h.nreloc; i++) {   /* every one checked before any is applied: a bad file leaves the buffer as it was */
        const uint32_t r = rd32(img + h.size + 4u * i), off = r & K_IMAGE_RELOC_OFF;
        if ((r & ~(K_IMAGE_RELOC_OFF | K_IMAGE_RELOC_RAM)) || off < K_IMAGE_HDR || off > h.size - 4u ||
            ((r & K_IMAGE_RELOC_RAM) && !(h.data_size + h.bss_size)))
            return K_IMG_E_RELOC;
    }
    for (i = 0; i < h.nreloc; i++) {
        const uint32_t r = rd32(img + h.size + 4u * i), off = r & K_IMAGE_RELOC_OFF;
        wr32(img + off, rd32(img + off) + (r & K_IMAGE_RELOC_RAM ? ram : base));
    }
    h.base = base;
    h.ram = h.data_size + h.bss_size ? ram : 0u;
    h.nreloc = 0;
    h.crc = k_image_crc32(0, img + K_IMAGE_HDR, h.size - K_IMAGE_HDR);
    h.hcrc = k_image_hcrc(&h);
    memcpy(img, &h, sizeof h);
    return (long)h.size;
}

/* ---- a region ---- */
static uint32_t sectors(uint32_t n) { return (n + K_IMAGE_SECTOR - 1u) & ~(K_IMAGE_SECTOR - 1u); }

int k_image_scan(const uint8_t *p, uint32_t len, KImageSlot *out, int max)
{
    int n = 0;
    uint32_t off = 0;
    while (off + K_IMAGE_HDR <= len && n < max) {
        KImageSlot *s = &out[n];
        const int rc = k_image_header(p + off, len - off, &s->h);
        if (rc == K_IMG_E_MAGIC) {
            off += K_IMAGE_SECTOR;
            continue;
        }
        s->off = off;
        s->state = rc ? K_IMG_BAD : 0;
        s->err = (int8_t)rc;
        s->index = -1;
        if (rc == K_IMG_E_HEADER) {   /* nothing of it is trusted but its place: one sector, to be erased */
            memset(&s->h, 0, sizeof s->h);
            s->h.magic = K_IMAGE_MAGIC;
        }
        /* (a sound header of an image this host cannot take, another interface: it keeps its sectors until it is erased) */
        s->len = s->h.size >= K_IMAGE_HDR && s->h.size <= len - off ? sectors(s->h.size) : K_IMAGE_SECTOR;
        off += s->len;
        n++;
    }
    return n;
}

long k_image_place(const KImageSlot *s, int n, uint32_t region_len, uint32_t size)
{
    const uint32_t need = sectors(size);
    uint32_t at = 0;
    int i, moved = 1;
    if (!size || need > region_len) return -1;
    while (moved) {
        moved = 0;
        for (i = 0; i < n; i++)
            if (s[i].off < at + need && at < s[i].off + s[i].len) {
                at = s[i].off + s[i].len;
                moved = 1;
            }
        if (at > region_len - need) return -1;
    }
    return (long)at;
}

static uint32_t ram_need(const KImageHeader *h) { return (h->data_size + h->bss_size + 3u) & ~3u; }

uint32_t k_image_ram_place(const KImageSlot *s, int n, uint32_t pool_at, uint32_t pool_len, uint32_t need)
{
    uint32_t at = pool_at;
    int i, moved = 1;
    need = (need + 3u) & ~3u;
    if (!need || need > pool_len) return 0;
    while (moved) {
        moved = 0;
        for (i = 0; i < n; i++) {
            const uint32_t r = s[i].h.ram, m = ram_need(&s[i].h);
            if (s[i].state == K_IMG_BAD || !m || !r) continue;   /* (every image that names its RAM keeps it: one switched off
                                                                  * may come back) */
            if (r < at + need && at < r + m) {
                at = (r + m + 3u) & ~3u;
                moved = 1;
            }
        }
        if (at - pool_at > pool_len - need) return 0;
    }
    return at;
}

static int ram_ok(const KImageSlot *s, int n, int i, const KImagePool *pool)
{
    const uint32_t r = s[i].h.ram, m = ram_need(&s[i].h);
    int j;
    if (!m) return 1;
    if (!pool || r < pool->at || r - pool->at > pool->len || m > pool->len - (r - pool->at)) return 0;
    for (j = 0; j < n; j++)
        if (j != i && s[j].state == K_IMG_LIVE && ram_need(&s[j].h) && r < s[j].h.ram + ram_need(&s[j].h) && s[j].h.ram < r + m)
            return 0;
    return 1;
}

/* its RAM and its registration (it has passed k_image_check, it is on, the newest of its id) */
static int go_live(const uint8_t *p, KImageSlot *s, int n, int i, const KImagePool *pool)
{
    KImageSlot *e = &s[i];
    const uint8_t *img = p + e->off;
    int idx;
    if (!ram_ok(s, n, i, pool)) {
        e->state = K_IMG_REFUSED;
        e->err = K_IMG_E_RAM;
        return 0;
    }
    if (ram_need(&e->h)) {
        uint8_t *m = pool->mem + (e->h.ram - pool->at);
        memcpy(m, img + e->h.data_off, e->h.data_size);
        memset(m + e->h.data_size, 0, ram_need(&e->h) - e->h.data_size);
    }
    idx = k_plugin_register(k_image_record(img, &e->h));
    if (idx < 0) {
        e->state = K_IMG_REFUSED;
        e->err = (int8_t)idx;
        return 0;
    }
    e->state = K_IMG_LIVE;
    e->index = (int16_t)idx;
    return 1;
}

int k_image_load(const uint8_t *p, uint32_t at, uint32_t len, KImageSlot *s, int n, const KImagePool *pool, int skip)
{
    int i, j, live = 0;
    for (i = 0; i < n; i++) {   /* 1: each checked on its own */
        s[i].index = -1;
        if (s[i].state == K_IMG_BAD) continue;   /* (k_image_scan: not a header this host takes) */
        s[i].err = (int8_t)k_image_check(p + s[i].off, len - s[i].off, at + s[i].off, &s[i].h);
        s[i].state = s[i].err ? K_IMG_BAD : !k_image_on(&s[i].h) ? K_IMG_OFF : skip ? K_IMG_SKIPPED : 0;
    }
    for (i = 0; i < n; i++)     /* 2: of one id, the newest (the first in the region of equals) */
        for (j = 0; j < n && !s[i].state; j++)
            if (j != i && (!s[j].state || s[j].state == K_IMG_OLDER) && s[j].h.uid == s[i].h.uid &&
                (s[j].h.version > s[i].h.version || (s[j].h.version == s[i].h.version && j < i)))
                s[i].state = K_IMG_OLDER;
    for (i = 0; i < n; i++)     /* 3: in the region's order */
        if (!s[i].state) live += go_live(p, s, n, i, pool);
    return live;
}

int k_image_load_one(const uint8_t *p, uint32_t at, uint32_t len, KImageSlot *s, int n, int i, const KImagePool *pool)
{
    if (i < 0 || i >= n) return 0;
    s[i].index = -1;
    s[i].err = (int8_t)k_image_check(p + s[i].off, len - s[i].off, at + s[i].off, &s[i].h);
    s[i].state = s[i].err ? K_IMG_BAD : !k_image_on(&s[i].h) ? K_IMG_OFF : 0;
    return s[i].state ? 0 : go_live(p, s, n, i, pool);
}

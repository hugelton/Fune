/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The few C library hooks the Kreisel core and UI need on the FM-1, where there is no operating system.
 *
 * The UI allocates once, at start (the canvas), and never frees: a bump allocator over a fixed block is enough, and it
 * keeps newlib's malloc (and its sbrk) out of the image. snprintf and the math functions come from the toolchain's
 * newlib (libc.a, libm.a); what they call back for is stubbed here (no files, no stdout). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <reent.h>

#ifndef KREISEL_HEAP_BYTES
#ifdef KREISEL_PX16
#define KREISEL_HEAP_BYTES (118u * 1024u)    /* the 240x240 RGB565 canvas is 112.5 KiB */
#else
#define KREISEL_HEAP_BYTES (226u * 1024u)    /* the 240x240 ARGB canvas is 225 KiB */
#endif
#endif
static uint8_t heap[KREISEL_HEAP_BYTES] __attribute__((aligned(8)));
static size_t heap_used;

void *malloc(size_t n)
{
    void *p;
    n = (n + 7u) & ~(size_t)7u;
    if (n > sizeof heap - heap_used)
        return 0;
    p = heap + heap_used;
    heap_used += n;
    return p;
}
void *calloc(size_t a, size_t b)
{
    size_t n = a * b;
    void *p;
    if (a && n / a != b)
        return 0;
    p = malloc(n);
    if (p)
        memset(p, 0, n);              /* (.bss was zeroed at boot; this stays right if a block is ever reused) */
    return p;
}
void free(void *p) { (void)p; }
void *realloc(void *p, size_t n)
{
    void *q = malloc(n);
    (void)p;                          /* nothing in Kreisel reallocates; a copy would need the old size */
    return q;
}

/* newlib's own allocator hooks (snprintf asks for them), answered by the same bump allocator */
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; (void)p; }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void)r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }

/* newlib's syscall layer */
int _write(int fd, const void *buf, size_t n) { (void)fd; (void)buf; return (int)n; }
int _read(int fd, void *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
long _lseek(int fd, long off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int code) { (void)code; for (;;) ; }
void *_sbrk(int incr) { (void)incr; errno = ENOMEM; return (void *)-1; }

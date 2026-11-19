/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "fbdev.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>

static int fb_fd = -1, fb_bpp, fb_w, fb_h, fb_line;
static uint8_t *fb_mem, *fb_shadow;   /* the mapped screen; what was last written to it (a row compared before it is written) */
static size_t fb_size;

int fbdev_open(const char *path)
{
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    fb_fd = open(path, O_RDWR | O_CLOEXEC);
    if (fb_fd < 0) { perror(path); return -1; }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) != 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &f) != 0) { perror("fb info"); goto fail; }
    fb_bpp = (int)v.bits_per_pixel; fb_w = (int)v.xres; fb_h = (int)v.yres; fb_line = (int)f.line_length;
    if (fb_bpp != 16 && fb_bpp != 32) { fprintf(stderr, "%s: %d bits a pixel (16 or 32 wanted)\n", path, fb_bpp); goto fail; }
    fb_size = (size_t)f.smem_len ? (size_t)f.smem_len : (size_t)fb_line * (size_t)fb_h;
    fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) { fb_mem = NULL; perror("fb mmap"); goto fail; }
    fb_shadow = calloc((size_t)fb_line, (size_t)fb_h);
    if (!fb_shadow) goto fail;
    memset(fb_shadow, 0xA5, (size_t)fb_line * (size_t)fb_h);   /* (nothing matches it: the first frame is written whole) */
    fprintf(stderr, "fb: %s %dx%d, %d bits, %d bytes a line\n", path, fb_w, fb_h, fb_bpp, fb_line);
    return 0;
fail:
    fbdev_close();
    return -1;
}

void fbdev_show(const GCanvas *c)
{
    static uint8_t row[4 * 4096];
    const int pw = c->s > 1 ? c->pw : c->w, ph = c->s > 1 ? c->ph : c->h;
    const int w = pw < fb_w ? pw : fb_w, h = ph < fb_h ? ph : fb_h, bytes = w * fb_bpp / 8;
    int x, y;
    if (!fb_mem || w * 4 > (int)sizeof row) return;
    for (y = 0; y < h; y++) {
        const gpix_t *s = c->px + (size_t)y * (size_t)pw;
        if (fb_bpp == 16) {
            uint16_t *d = (uint16_t *)row;
            for (x = 0; x < w; x++) {
                const uint32_t p = GPIX_RGB(s[x]);
                d[x] = (uint16_t)(((p >> 8) & 0xF800u) | ((p >> 5) & 0x07E0u) | ((p >> 3) & 0x001Fu));
            }
        } else {
            uint32_t *d = (uint32_t *)row;
            for (x = 0; x < w; x++) d[x] = 0xFF000000u | GPIX_RGB(s[x]);
        }
        uint8_t *sh = fb_shadow + (size_t)y * (size_t)fb_line;
        if (memcmp(sh, row, (size_t)bytes) == 0) continue;   /* (unchanged: the panel's deferred I/O sends only what is written) */
        memcpy(sh, row, (size_t)bytes);
        memcpy(fb_mem + (size_t)y * (size_t)fb_line, row, (size_t)bytes);
    }
}

void fbdev_close(void)
{
    if (fb_mem) munmap(fb_mem, fb_size);
    if (fb_fd >= 0) close(fb_fd);
    free(fb_shadow);
    fb_mem = NULL; fb_shadow = NULL; fb_fd = -1;
}

/* ---- the keyboard: evdev keys as SDL key events ---- */
static int key_fd = -1;
static volatile int key_run;
static SDL_Thread *key_thread;

static SDL_Keycode sdl_key(int code)
{
    static const char letters[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";   /* KEY_Q (16) .. KEY_M (50) */
    if (code >= KEY_1 && code <= KEY_9) return SDLK_1 + (code - KEY_1);
    if (code >= KEY_Q && code <= KEY_M && letters[code - KEY_Q]) return (SDL_Keycode)letters[code - KEY_Q];
    if (code >= KEY_F1 && code <= KEY_F10) return SDLK_F1 + (code - KEY_F1);
    switch (code) {
    case KEY_0: return SDLK_0;
    case KEY_ENTER: case KEY_KPENTER: return SDLK_RETURN;
    case KEY_ESC: return SDLK_ESCAPE;
    case KEY_BACKSPACE: return SDLK_BACKSPACE;
    case KEY_DELETE: return SDLK_DELETE;
    case KEY_TAB: return SDLK_TAB;
    case KEY_SPACE: return SDLK_SPACE;
    case KEY_MINUS: return SDLK_MINUS;
    case KEY_EQUAL: return SDLK_EQUALS;
    case KEY_LEFTBRACE: return SDLK_LEFTBRACKET;
    case KEY_RIGHTBRACE: return SDLK_RIGHTBRACKET;
    case KEY_SEMICOLON: return SDLK_SEMICOLON;
    case KEY_APOSTROPHE: return SDLK_QUOTE;
    case KEY_GRAVE: return SDLK_BACKQUOTE;
    case KEY_BACKSLASH: return SDLK_BACKSLASH;
    case KEY_COMMA: return SDLK_COMMA;
    case KEY_DOT: return SDLK_PERIOD;
    case KEY_SLASH: return SDLK_SLASH;
    case KEY_UP: return SDLK_UP;
    case KEY_DOWN: return SDLK_DOWN;
    case KEY_LEFT: return SDLK_LEFT;
    case KEY_RIGHT: return SDLK_RIGHT;
    case KEY_HOME: return SDLK_HOME;
    case KEY_END: return SDLK_END;
    case KEY_PAGEUP: return SDLK_PAGEUP;
    case KEY_PAGEDOWN: return SDLK_PAGEDOWN;
    case KEY_INSERT: return SDLK_INSERT;
    case KEY_F11: return SDLK_F11;
    case KEY_F12: return SDLK_F12;
    default: return SDLK_UNKNOWN;
    }
}

#define QUIT_HOLD_MS 1000u   /* Esc or Home held this long: out, back to the launcher */
static int key_loop(void *unused)
{
    Uint16 mod = 0;
    uint32_t back_down = 0;   /* when Esc / Home went down (0: not held) */
    (void)unused;
    while (key_run) {
        struct pollfd p = { key_fd, POLLIN, 0 };
        struct input_event ev[32];
        int n, i;
        if (back_down && SDL_GetTicks() - back_down >= QUIT_HOLD_MS) {
            SDL_Event q; memset(&q, 0, sizeof q); q.type = SDL_QUIT; SDL_PushEvent(&q);
            back_down = 0;
        }
        if (poll(&p, 1, 50) <= 0) continue;
        n = (int)read(key_fd, ev, sizeof ev);
        if (n <= 0) { if (n < 0) SDL_Delay(50); continue; }
        for (i = 0; i < n / (int)sizeof ev[0]; i++) {
            const struct input_event *e = &ev[i];
            SDL_Keycode k;
            SDL_Event s;
            if (e->type != EV_KEY) continue;
            if (e->code == KEY_LEFTSHIFT || e->code == KEY_RIGHTSHIFT) { mod = e->value ? (mod | KMOD_LSHIFT) : (mod & ~KMOD_SHIFT); continue; }
            if (e->code == KEY_LEFTCTRL || e->code == KEY_RIGHTCTRL) { mod = e->value ? (mod | KMOD_LCTRL) : (mod & ~KMOD_CTRL); continue; }
            if (e->code == KEY_LEFTALT || e->code == KEY_RIGHTALT) { mod = e->value ? (mod | KMOD_LALT) : (mod & ~KMOD_ALT); continue; }
            if (e->code == KEY_ESC || e->code == KEY_HOME) {
                if (e->value == 1) back_down = SDL_GetTicks() | 1u;
                else if (e->value == 0) back_down = 0;
            }
            k = sdl_key(e->code);
            if (k == SDLK_UNKNOWN) continue;
            memset(&s, 0, sizeof s);
            s.type = e->value ? SDL_KEYDOWN : SDL_KEYUP;
            s.key.state = e->value ? SDL_PRESSED : SDL_RELEASED;
            s.key.repeat = e->value == 2;
            s.key.keysym.sym = k;
            s.key.keysym.scancode = SDL_GetScancodeFromKey(k);
            s.key.keysym.mod = mod;
            s.key.timestamp = SDL_GetTicks();
            SDL_PushEvent(&s);
        }
    }
    return 0;
}

static int is_keyboard(int fd)
{
    unsigned long bits[(KEY_MAX + 1) / (8 * sizeof(unsigned long)) + 1];
    memset(bits, 0, sizeof bits);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) return 0;
#define HAS(k) ((bits[(k) / (8 * sizeof(unsigned long))] >> ((k) % (8 * sizeof(unsigned long)))) & 1ul)
    return HAS(KEY_A) && HAS(KEY_Z) && HAS(KEY_SPACE);
#undef HAS
}

int fbdev_keys_open(const char *path)
{
    char name[64];
    int i;
    if (path && path[0]) key_fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    for (i = 0; key_fd < 0 && !(path && path[0]) && i < 32; i++) {
        snprintf(name, sizeof name, "/dev/input/event%d", i);
        key_fd = open(name, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (key_fd >= 0 && !is_keyboard(key_fd)) { close(key_fd); key_fd = -1; }
        else if (key_fd >= 0) path = name;
    }
    if (key_fd < 0) { fprintf(stderr, "keys: no keyboard (%s)\n", path && path[0] ? path : "/dev/input/event*"); return -1; }
    fprintf(stderr, "keys: %s (shared, not grabbed)\n", path);   /* (the launcher reads it too, paused while we run) */
    key_run = 1;
    key_thread = SDL_CreateThread(key_loop, "keys", NULL);
    return key_thread ? 0 : -1;
}

void fbdev_keys_close(void)
{
    key_run = 0;
    if (key_thread) SDL_WaitThread(key_thread, NULL);
    if (key_fd >= 0) close(key_fd);
    key_thread = NULL; key_fd = -1;
}
#else
int fbdev_open(const char *path) { (void)path; return -1; }
void fbdev_show(const GCanvas *c) { (void)c; }
void fbdev_close(void) {}
int fbdev_keys_open(const char *path) { (void)path; return -1; }
void fbdev_keys_close(void) {}
#endif

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_FBDEV_H
#define KREISEL_FBDEV_H
#include "gfx.h"

/* A Linux framebuffer and an evdev keyboard in place of SDL's window (the Cardputer Zero: its LCD is /dev/fb0, RGB565, which
 * its launcher draws to itself; SDL's own drivers there want EGL, which the device has not). SDL stays for audio, the timer
 * and the event queue: the keyboard's keys come in as SDL key events. Linux only; elsewhere every call fails. */
int fbdev_open(const char *path);          /* 0: open, mapped (16 or 32 bits a pixel) */
void fbdev_show(const GCanvas *canvas);    /* the canvas at (0, 0), only the rows that changed written */
void fbdev_close(void);                    /* (the screen left as it was drawn: the launcher draws its own next) */
int fbdev_keys_open(const char *path);     /* path NULL or "": the first keyboard among /dev/input/event*. 0: reading */
void fbdev_keys_close(void);
#endif

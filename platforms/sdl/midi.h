/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_SDL_MIDI_H
#define KREISEL_SDL_MIDI_H
#include <stdint.h>
#include "kreisel.h"
/* MIDI in and out for the SDL builds: a carrier only. The bytes go to the input layer
 * (kreisel_io.c, k_io_midi) from the main loop, whatever thread the system delivers them on.
 *   macOS   CoreMIDI: every source, and those plugged in later (a Mac's Network MIDI and Bluetooth MIDI sessions are sources too)
 *   Linux   the ALSA sequencer (KREISEL_ALSA): a port of its own, every readable port subscribed, and those that appear later (a
 *           USB-host device, BLE MIDI through BlueZ); RTP-MIDI (AppleMIDI on 5004, announced by mDNS) for a Mac or iOS over Wi-Fi
 * Their port: 1 (the clock's USB source: CLK on USB follows it).
 *   sdl_midi_open(name)        0 when something listens
 *   sdl_midi_poll(app, ms)     each frame: what came in, to the app
 *   sdl_midi_close()
 *   sdl_midi_sources()         how many sources it listens to (a status line)
 * MIDI out (kreisel_io.c's sender, k_io_send_to): channel messages and the realtime bytes (F8 FA FB FC), no SysEx.
 *   macOS   a virtual source named after the app ("Fune"): any DAW or app takes it as an input
 *   Linux   an output port on the same ALSA client (KREISEL_ALSA): its subscribers get it; else none
 *   sdl_midi_outputs()         1 when there is somewhere to send
 *   sdl_midi_send(msg, n)      one message, now (main loop only) */
int sdl_midi_open(const char *name);
void sdl_midi_poll(KApp *app, uint32_t ms);
void sdl_midi_close(void);
int sdl_midi_sources(void);
int sdl_midi_outputs(void);
void sdl_midi_send(const uint8_t *msg, int n);
#endif

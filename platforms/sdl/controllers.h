/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_SDL_CONTROLLERS_H
#define KREISEL_SDL_CONTROLLERS_H
/* Controller profiles for the SDL builds (src/kreisel_ctl.h, docs/CONTROLLERS.md): the built-in ones, then the user's files in
 * <data>/controllers/NAME.conf (one a profile; a user's of the same name takes a built-in's place). The first KIO_DEVS have a device of
 * the input layer each (k_io_dev_map); a MIDI source whose name holds a profile's match comes in as that device.
 *   sdl_ctl_load(dir)        read them (the folder made, with a commented example, the first time); how many
 *   sdl_ctl_dev(port_name)   the device for a source: 0 none (the platform's table only)
 *   sdl_ctl_find(name)       a profile by its name (io.conf [keys] map = its name: its key rows): 0 none
 *   sdl_ctl_learn(port, row) MIDI learn's row (k_learn_row) into the port's profile: its file of the user's; a built-in's text into a
 *                            new file; none, a new file named after the port (name = match = the port's name). Read again in its place
 *                            at once (the sources then reconnected by midi.c: a port with no profile before gets its device). 0 written */
#include "kreisel_ctl.h"
int sdl_ctl_load(const char *dir);
int sdl_ctl_dev(const char *port_name);
const KCtlProfile *sdl_ctl_find(const char *name);
int sdl_ctl_learn(const char *port_name, const char *row);
#endif

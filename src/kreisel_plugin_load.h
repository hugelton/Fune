/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_PLUGIN_LOAD_H
#define KREISEL_PLUGIN_LOAD_H
/* Outside plugins as shared libraries (a desktop: SDL): every .dylib / .so / .dll in a folder that exports
 * kreisel_plugin() (sdk/kreisel_sdk.h) is opened and its record registered (k_plugin_register: the device's room is checked).
 * Returns how many were registered; a library that is not a plugin, or is refused, is closed and skipped (and said on stderr). */
int k_plugin_load_dir(const char *dir);
int k_plugin_load_file(const char *path);   /* one library: its index, or < 0 */
#endif

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The loader of outside plugins on a desktop (kreisel_plugin_load.h). The FM-1 has none: its plugins are to come from its flash. */
#include "kreisel_plugin_load.h"
#include "kreisel_plugin.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

typedef const KPlugin *(*entry_fn)(void);

int k_plugin_load_file(const char *path)
{
    entry_fn entry;
    const KPlugin *p;
    int r;
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) return -100;
    entry = (entry_fn)(void (*)(void))GetProcAddress(h, "kreisel_plugin");
    if (!entry) { FreeLibrary(h); return -101; }
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "plugin: %s: %s\n", path, dlerror()); return -100; }
    *(void **)&entry = dlsym(h, "kreisel_plugin");
    if (!entry) { dlclose(h); return -101; }
#endif
    p = entry();
    r = p ? k_plugin_register(p) : K_PLUG_E_NULL;
    if (r < 0) {
        fprintf(stderr, "plugin: %s refused (%d)\n", path, r);
#ifdef _WIN32
        FreeLibrary(h);
#else
        dlclose(h);
#endif
    }
    return r;   /* (a registered one stays open: its descriptor and DSP live in it) */
}

static int is_lib(const char *n)
{
    const char *e = strrchr(n, '.');
    return e && (!strcmp(e, ".dylib") || !strcmp(e, ".so") || !strcmp(e, ".dll"));
}
int k_plugin_load_dir(const char *dir)
{
    int n = 0;
    char path[1024];
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE f;
    snprintf(path, sizeof path, "%s\\*.dll", dir);
    if ((f = FindFirstFileA(path, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
        if (k_plugin_load_file(path) >= 0) n++;
    } while (FindNextFileA(f, &fd));
    FindClose(f);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d)))
        if (is_lib(e->d_name) && snprintf(path, sizeof path, "%s/%s", dir, e->d_name) < (int)sizeof path && k_plugin_load_file(path) >= 0) n++;
    closedir(d);
#endif
    return n;
}

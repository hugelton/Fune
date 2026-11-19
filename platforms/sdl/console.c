/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * The SDL builds' console (console.h): the FM-1's commands for driving the app, over a TCP port on 127.0.0.1. */
#include "console.h"
#include "kreisel_save.h"
#include "kreisel_demo.h"
#include "kreisel_version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
int sdl_console_open(int port) { (void)port; return -1; }
void sdl_console_poll(KApp *app, const KAudio *audio, uint32_t ms) { (void)app; (void)audio; (void)ms; }
void sdl_console_close(void) {}
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

static int lfd = -1, cfd = -1;
static char line[128];
static size_t len;

int sdl_console_open(int port)
{
    struct sockaddr_in a;
    int one = 1;
    if (port <= 0 || port > 65535) return -1;
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return -1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* (this machine only: another reaches it through ssh -L) */
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) != 0 || listen(lfd, 1) != 0) { close(lfd); lfd = -1; return -1; }
    fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL) | O_NONBLOCK);
    return 0;
}

void sdl_console_close(void)
{
    if (cfd >= 0) close(cfd);
    if (lfd >= 0) close(lfd);
    cfd = lfd = -1;
}

static void reply(const char *s)
{
    char b[600];
    const int n = snprintf(b, sizeof b, "%s\r\n", s);
    if (cfd >= 0 && n > 0 && send(cfd, b, (size_t)n, 0) < 0) { close(cfd); cfd = -1; }
}

static const char *word(const char **p)   /* the next word, its end; NULL at the end of the line */
{
    static char w[40];
    size_t n = 0;
    while (**p == ' ') (*p)++;
    if (!**p) return NULL;
    while (**p && **p != ' ' && n + 1 < sizeof w) w[n++] = (char)tolower((unsigned char)*(*p)++);
    w[n] = 0;
    return w;
}

static int name_is(const char *w, const char *name)   /* (no case, no spaces: "env 1" is env1) */
{
    for (; *name; name++) {
        if (*name == ' ') continue;
        if (tolower((unsigned char)*name) != *w++) return 0;
    }
    return !*w;
}

static int num(const char **p, int *v)
{
    const char *w = word(p);
    char *end;
    long x;
    if (!w) return 0;
    x = strtol(w, &end, 10);
    if (*end) return 0;
    *v = (int)x;
    return 1;
}

static void act(KApp *a, KActionType t, int x, int y, uint32_t ms) { k_dispatch(a, (KAction){t, x, y, ms}); }

static void run(KApp *a, const KAudio *au, const char *p, uint32_t ms)
{
    static const char *const BTN[14] = {"octdn", "octup", "fx", "sel", "env", "lfo", "edit", "glo", "home", "save", "arp", "seq", "play", "rec"};
    static const int8_t GROUP[14] = {-1, -1, K_GROUP_FX, K_GROUP_SOUND, K_GROUP_ENV, K_GROUP_LFO, K_GROUP_EDIT, K_GROUP_GLO,
                                     -1, -1, K_GROUP_ARP, K_GROUP_PATTERN, -1, -1};   /* (fm1_main.c's BTN_GROUP) */
    static const char *const ENC[7] = {"sel", "algo", "k1", "k2", "k3", "k4", "presets"};
    char out[600];
    const char *w = word(&p);
    int x, y, i;
    if (!w) return;
    if (!strcmp(w, "stat")) {
        snprintf(out, sizeof out, "page=%d:%s track=%d playing=%d step=%d scene=%d bpm=%d", (int)a->page, k_page_name(a->page),
                 a->track + 1, a->playing, a->step, au && au->song_on ? au->scene + 1 : 0, a->bpm);
        reply(out);
    } else if (!strcmp(w, "ver")) {
        reply("ver " KREISEL_VERSION);
    } else if (!strcmp(w, "slots")) {
        size_t n = (size_t)snprintf(out, sizeof out, "ok");
        for (i = 0; i < K_SONG_SLOTS && n < sizeof out - 40; i++) {
            const char *q = k_save_name(K_STORE_SONG, i);
            n += (size_t)snprintf(out + n, sizeof out - n, " s%d=", i + 1);
            if (!k_save_used(K_STORE_SONG, i)) n += (size_t)snprintf(out + n, sizeof out - n, "-");
            else if (!*q) n += (size_t)snprintf(out + n, sizeof out - n, "*");
            else for (; *q && n < sizeof out - 2; q++) out[n++] = *q == ' ' ? '_' : *q, out[n] = 0;
        }
        reply(out);
    } else if (!strcmp(w, "load")) {
        if (!num(&p, &x) || x < 1 || x > K_SONG_SLOTS) { reply("err load 1..6"); return; }
        if (k_save_load(a, x - 1)) { reply("err load"); return; }
        snprintf(out, sizeof out, "ok slot=%d", x);
        reply(out);
    } else if (!strcmp(w, "demo")) {
        if (k_demo_load(a)) { reply("err demo"); return; }
        snprintf(out, sizeof out, "ok demo=%s", k_demo_name);
        reply(out);
    } else if (!strcmp(w, "page")) {
        const char *q = p;
        int n = -1;
        if (!num(&q, &n)) {
            while (*p == ' ') p++;
            {
                char nm[40];
                size_t k = 0;
                for (; *p && k + 1 < sizeof nm; p++) if (*p != ' ') nm[k++] = (char)tolower((unsigned char)*p);
                nm[k] = 0;
                for (i = 0; i < (int)K_PAGE_COUNT && n < 0; i++) if (name_is(nm, k_page_name((KPage)i))) n = i;
            }
        }
        if (n < 0 || n >= (int)K_PAGE_COUNT) { reply("err page N|NAME"); return; }
        k_open_page(a, (KPage)n);
        reply("ok");
    } else if (!strcmp(w, "track")) {
        if (!num(&p, &x) || x < 1 || x > K_TRACKS) { reply("err track N"); return; }
        act(a, K_ACT_TRACK_DELTA, x - 1 - a->track, 0, ms);
        reply("ok");
    } else if (!strcmp(w, "play")) {
        act(a, K_ACT_PLAY, 0, 0, ms);
        reply("ok");
    } else if (!strcmp(w, "btn")) {
        const char *b = word(&p);
        for (i = 0; b && i < 14; i++) if (!strcmp(b, BTN[i])) break;
        if (!b || i >= 14) { reply("err btn octdn octup fx sel env lfo edit glo home save arp seq play rec"); return; }
        if (GROUP[i] >= 0) { act(a, K_ACT_PAGE_GROUP_DOWN, GROUP[i], 0, ms); act(a, K_ACT_PAGE_GROUP_UP, GROUP[i], 0, ms); }
        else if (i == 8) { act(a, K_ACT_HOME_DOWN, 0, 0, ms); act(a, K_ACT_HOME_UP, 0, 0, ms); }
        else if (i == 9) act(a, K_ACT_SAVE, 0, 0, ms);
        else if (i == 12) act(a, K_ACT_PLAY, 0, 0, ms);
        else if (i == 13) act(a, K_ACT_REC, 0, 0, ms);
        else act(a, K_ACT_OCTAVE_DELTA, i == 0 ? -1 : 1, 0, ms);
        reply("ok");
    } else if (!strcmp(w, "knob")) {
        const char *k = word(&p);
        int e = -1;
        for (i = 0; k && i < 7; i++) if (!strcmp(k, ENC[i])) e = i;
        if (k && e < 0 && k[0] >= '0' && k[0] <= '6' && !k[1]) e = k[0] - '0';
        if (e < 0 || !num(&p, &x)) { reply("err knob sel|algo|k1..k4|presets DELTA"); return; }
        if (e == 0) act(a, K_ACT_SELECT_DELTA, x, 0, ms);
        else if (e == 1) act(a, K_ACT_TYPE_DELTA, x, 0, ms);
        else if (e == 6) act(a, K_ACT_SOUND_DELTA, x, 0, ms);
        else act(a, K_ACT_PARAM_DELTA, e - 2, x * 2, ms);   /* (a detent as the FM-1's: two steps) */
        reply("ok");
    } else if (!strcmp(w, "note")) {
        if (!num(&p, &x) || !num(&p, &y) || x < 0 || x > 127 || y < 0 || y > 127) { reply("err note N VEL"); return; }
        k_note_in(a, x, y);
        reply("ok");
    } else if (!strcmp(w, "key")) {
        const char *d;
        if (!num(&p, &x) || x < 0 || x > 26 || !(d = word(&p)) || (strcmp(d, "down") && strcmp(d, "up"))) { reply("err key ID down|up"); return; }
        act(a, K_ACT_KEY, x, !strcmp(d, "down"), ms);
        reply("ok");
    } else {
        reply("err stat ver slots load demo page track play btn knob note key");
    }
}

void sdl_console_poll(KApp *app, const KAudio *audio, uint32_t ms)
{
    char b[256];
    ssize_t n, i;
    if (lfd < 0) return;
    if (cfd < 0) {
        cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) return;
        fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL) | O_NONBLOCK);
        len = 0;
    }
    for (;;) {
        n = recv(cfd, b, sizeof b, 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { close(cfd); cfd = -1; return; }
        if (n < 0) return;
        for (i = 0; i < n; i++) {
            if (b[i] == '\n' || b[i] == '\r') {
                if (len) { line[len] = 0; run(app, audio, line, ms); len = 0; if (cfd < 0) return; }
            } else if (len + 1 < sizeof line) line[len++] = b[i];
        }
    }
}
#endif

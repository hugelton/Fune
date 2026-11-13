/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* RTP-MIDI (AppleMIDI) as a session partner that listens (kr_rtpmidi.h). Written from the protocol's description (RFC 6295 for
 * the RTP payload; Apple's session exchange: IN / OK / NO / BY / CK / RS on a control port and the data port after it).
 *
 *   an AppleMIDI command   0xFF 0xFF, two letters, then:
 *     IN, OK, NO, BY       version (2), the initiator's token, the sender's SSRC (32 bits each, big-endian), a name (IN, OK)
 *     CK                   SSRC, count (0, 1, 2), three pad bytes, three 64-bit times in 100 us: the initiator sends 0 with its
 *                          time, the partner answers 1 with its own, the initiator closes with 2
 *     RS                   receiver feedback (the sequence number the other side has): nothing to do for a listener
 *   an RTP packet          12 bytes of RTP (version 2, payload type 97), then the MIDI command section: a byte B J Z P LEN
 *                          (B: a 12-bit length over two bytes), the commands, a delta time (1..4 bytes, 7 bits each) before
 *                          each but the first (the first too with Z), running status; J: a recovery journal after them, skipped */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* (clock_gettime, the sockets' names under strict C11: Linux, the SDL build) */
#endif
#include "kr_rtpmidi.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int sk[2] = {-1, -1};            /* the control port, the data port */
static uint32_t my_ssrc;
static char my_name[48];
static struct {
    int on;                              /* invited on the control port (1), on both (2) */
    uint32_t ssrc, token;
    struct sockaddr_in addr[2];
    char name[48];
} peer;
static uint8_t run_status;               /* running status, across packets */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }
static uint64_t ticks100us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 10000u + (uint64_t)ts.tv_nsec / 100000u;
}
static void say(kr_log_fn lg, void *u, const char *s) { if (lg) lg(u, s); }

int kr_rtpmidi_open(unsigned port, const char *name)
{
    int i;
    kr_rtpmidi_close();
    snprintf(my_name, sizeof my_name, "%s", name ? name : "Kreisel");
    my_ssrc = (uint32_t)ticks100us() * 2654435761u ^ 0x4B524549u;   /* ("KREI"; any number, not the same each start) */
    for (i = 0; i < 2; i++) {
        struct sockaddr_in a;
        int one = 1;
        sk[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (sk[i] < 0) { kr_rtpmidi_close(); return -1; }
        setsockopt(sk[i], SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)(port + (unsigned)i));
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sk[i], (struct sockaddr *)&a, sizeof a) < 0) { kr_rtpmidi_close(); return -2; }
        fcntl(sk[i], F_SETFL, fcntl(sk[i], F_GETFL, 0) | O_NONBLOCK);
    }
    memset(&peer, 0, sizeof peer);
    run_status = 0;
    return 0;
}
const char *kr_rtpmidi_peer(void) { return peer.on ? peer.name : ""; }

static void send_cmd(int i, const struct sockaddr_in *to, const char *cmd, uint32_t token, int with_name)
{
    uint8_t b[64];
    int n = 16;
    b[0] = b[1] = 0xFF; b[2] = (uint8_t)cmd[0]; b[3] = (uint8_t)cmd[1];
    put32(b + 4, 2); put32(b + 8, token); put32(b + 12, my_ssrc);
    if (with_name) { const size_t l = strlen(my_name); memcpy(b + 16, my_name, l + 1); n += (int)l + 1; }
    sendto(sk[i], (const char *)b, (size_t)n, 0, (const struct sockaddr *)to, sizeof *to);
}
void kr_rtpmidi_close(void)
{
    int i;
    if (peer.on && sk[0] >= 0) send_cmd(0, &peer.addr[0], "BY", peer.token, 0);
    for (i = 0; i < 2; i++) if (sk[i] >= 0) { close(sk[i]); sk[i] = -1; }
    peer.on = 0;
}

/* the bytes a command takes after its status (-1: SysEx, to its end) */
static int data_len(uint8_t st)
{
    if (st < 0xF0) return (st & 0xE0) == 0xC0 ? 1 : 2;   /* (C0 program, D0 channel pressure: one) */
    if (st == 0xF0) return -1;
    if (st == 0xF1 || st == 0xF3) return 1;
    if (st == 0xF2) return 2;
    return 0;
}
int kr_rtpmidi_parse(const uint8_t *p, int n, kr_midi_fn on_midi, void *user)
{
    int i, len, end, first = 1, found = 0, z;
    if (n < 13 || (p[0] & 0xC0) != 0x80) return 0;   /* (RTP version 2) */
    i = 12 + (p[0] & 0x0F) * 4;                        /* (CSRCs: none from Apple, but allowed) */
    if (p[0] & 0x10) return 0;                         /* (an RTP extension: not sent for MIDI) */
    if (i >= n) return 0;
    z = (p[i] & 0x20) != 0;                            /* Z: the first command has its delta time too */
    if (p[i] & 0x80) { if (i + 1 >= n) return 0; len = (p[i] & 0x0F) << 8 | p[i + 1]; i += 2; }
    else { len = p[i] & 0x0F; i += 1; }
    end = i + len;
    if (end > n) end = n;
    while (i < end) {
        uint8_t msg[3], st;
        int k, dl;
        if (!first || z) {   /* the delta time: up to four bytes, the high bit says one more follows */
            for (k = 0; k < 4 && i < end && (p[i] & 0x80); k++) i++;
            i++;
            if (i >= end) break;
        }
        first = 0;
        if (p[i] & 0x80) st = p[i++];
        else if (run_status) st = run_status;   /* running status */
        else break;
        dl = data_len(st);
        if (dl < 0) {   /* SysEx (or a segment of one): to its end, skipped */
            while (i < end && p[i] != 0xF7 && p[i] != 0xF4 && p[i] != 0xF0) i++;
            if (i < end) i++;
            continue;
        }
        if (st < 0xF0) run_status = st;
        else if (st < 0xF8) run_status = 0;   /* (system common cancels it; realtime does not) */
        msg[0] = st;
        for (k = 0; k < dl; k++) {
            if (i >= end || (p[i] & 0x80)) { dl = -1; break; }
            msg[1 + k] = p[i++];
        }
        if (dl < 0) break;
        if (on_midi) on_midi(user, msg, 1 + dl);
        found++;
    }
    return found;
}

static void control(int i, const uint8_t *b, int n, const struct sockaddr_in *from, kr_log_fn lg, void *u)
{
    char line[96];
    if (n < 4) return;
    if (b[2] == 'I' && b[3] == 'N' && n >= 16) {   /* an invitation: accepted (a new one takes over) */
        const uint32_t token = be32(b + 8), ssrc = be32(b + 12);
        if (i == 0) {
            int k;
            peer.on = 1; peer.ssrc = ssrc; peer.token = token; peer.addr[0] = *from;
            for (k = 0; k < (int)sizeof peer.name - 1 && 16 + k < n && b[16 + k]; k++) peer.name[k] = (char)b[16 + k];
            peer.name[k] = 0;
            run_status = 0;
        } else if (peer.on && ssrc == peer.ssrc) {
            peer.on = 2; peer.addr[1] = *from;
            snprintf(line, sizeof line, "rtp-midi: %s connected (%s)", peer.name, inet_ntoa(from->sin_addr));
            say(lg, u, line);
        }
        send_cmd(i, from, "OK", token, 1);
    } else if (b[2] == 'B' && b[3] == 'Y' && n >= 16) {
        if (peer.on && be32(b + 12) == peer.ssrc) {
            snprintf(line, sizeof line, "rtp-midi: %s left", peer.name);
            say(lg, u, line);
            peer.on = 0;
        }
    } else if (b[2] == 'C' && b[3] == 'K' && n >= 36 && b[8] == 0) {   /* the clock: answered with ours (count 1) */
        uint8_t r[36];
        memcpy(r, b, 36);
        put32(r + 4, my_ssrc);
        r[8] = 1;
        put64(r + 20, ticks100us());
        sendto(sk[i], (const char *)r, sizeof r, 0, (const struct sockaddr *)from, sizeof *from);
    }
    /* (CK 1 and 2, RS: nothing to do) */
}

void kr_rtpmidi_poll(kr_midi_fn on_midi, kr_log_fn on_log, void *user)
{
    struct pollfd pf[2];
    int i, guard;
    if (sk[0] < 0 || sk[1] < 0) return;
    pf[0].fd = sk[0]; pf[1].fd = sk[1]; pf[0].events = pf[1].events = POLLIN; pf[0].revents = pf[1].revents = 0;
    if (poll(pf, 2, 0) <= 0) return;   /* (one call a frame when nothing came: each socket call is a round trip on the Switch) */
    for (i = 0; i < 2; i++) {
        if (!(pf[i].revents & POLLIN)) continue;
        for (guard = 0; guard < 64; guard++) {   /* (at most 64 packets a frame and a port) */
            uint8_t b[1500];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            const int n = (int)recvfrom(sk[i], (char *)b, sizeof b, 0, (struct sockaddr *)&from, &fl);
            if (n <= 0) break;
            if (n >= 4 && b[0] == 0xFF && b[1] == 0xFF) control(i, b, n, &from, on_log, user);
            else if (i == 1 && peer.on == 2 && n >= 12 && be32(b + 8) == peer.ssrc) kr_rtpmidi_parse(b, n, on_midi, user);
        }
    }
}

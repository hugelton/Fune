/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The RTP-MIDI listener (platforms/net/kr_rtpmidi.c) against an initiator played here, as the Mac's Network MIDI does it,
 * over the loopback: the invitation on both ports, the clock exchange, MIDI packets (running status, delta times, SysEx and a
 * journal skipped, a stranger's packet ignored), goodbye. Then the payload parser alone on the corner cases. */
#include "kr_rtpmidi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 25004
static uint8_t got[64][3];
static int got_n, got_len[64], logs;
static void on_midi(void *u, const uint8_t *m, int n) { (void)u; if (got_n < 64) { memcpy(got[got_n], m, (size_t)n); got_len[got_n++] = n; } }
static void on_log(void *u, const char *s) { (void)u; printf("  %s\n", s); logs++; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int sock_to(int port, struct sockaddr_in *a)
{
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv = {1, 0};
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET; a->sin_port = htons((uint16_t)port); a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return s;
}
static int answer(int s, uint8_t *b, int cap) { int i, n = -1; for (i = 0; i < 50 && n <= 0; i++) { kr_rtpmidi_poll(on_midi, on_log, 0); n = (int)recv(s, b, (size_t)cap, MSG_DONTWAIT); if (n <= 0) usleep(2000); } return n; }
static void cmd(uint8_t *b, const char *c, uint32_t token, uint32_t ssrc, const char *name, int *n)
{
    b[0] = b[1] = 0xFF; b[2] = (uint8_t)c[0]; b[3] = (uint8_t)c[1];
    put32(b + 4, 2); put32(b + 8, token); put32(b + 12, ssrc); *n = 16;
    if (name) { strcpy((char *)b + 16, name); *n += (int)strlen(name) + 1; }
}
static int rtp(uint8_t *b, uint32_t ssrc, uint16_t seq, const uint8_t *cmds, int len, int journal)
{
    int n = 12;
    b[0] = 0x80; b[1] = 0x61; b[2] = (uint8_t)(seq >> 8); b[3] = (uint8_t)seq; put32(b + 4, seq * 10u); put32(b + 8, ssrc);
    if (len > 15) { b[n++] = (uint8_t)(0x80 | (journal ? 0x40 : 0) | (len >> 8)); b[n++] = (uint8_t)len; }
    else b[n++] = (uint8_t)((journal ? 0x40 : 0) | len);
    memcpy(b + n, cmds, (size_t)len); n += len;
    if (journal) { b[n++] = 0x20; b[n++] = 0x01; b[n++] = 0x02; }   /* (a journal's bytes: skipped) */
    return n;
}

int main(void)
{
    struct sockaddr_in ca, da;
    const int cs = sock_to(PORT, &ca), ds = sock_to(PORT + 1, &da);
    const uint32_t ssrc = 0x12345678u, token = 0xCAFEF00Du;
    uint8_t b[256], r[256];
    int n;
    assert(kr_rtpmidi_open(PORT, "Kreisel") == 0);
    assert(!kr_rtpmidi_peer()[0]);

    /* the invitation: the control port, then the data port; OK with our name each time */
    cmd(b, "IN", token, ssrc, "Mac", &n); sendto(cs, b, (size_t)n, 0, (struct sockaddr *)&ca, sizeof ca);
    n = answer(cs, r, sizeof r); assert(n >= 17 && r[2] == 'O' && r[3] == 'K' && !memcmp(r + 8, b + 8, 4) && !strcmp((char *)r + 16, "Kreisel"));
    cmd(b, "IN", token, ssrc, "Mac", &n); sendto(ds, b, (size_t)n, 0, (struct sockaddr *)&da, sizeof da);
    n = answer(ds, r, sizeof r); assert(n >= 17 && r[2] == 'O' && r[3] == 'K');
    assert(!strcmp(kr_rtpmidi_peer(), "Mac") && logs == 1);

    /* the clock: CK 0 answered with CK 1, ts1 kept, ts2 ours */
    memset(b, 0, 36); b[0] = b[1] = 0xFF; b[2] = 'C'; b[3] = 'K'; put32(b + 4, ssrc); b[8] = 0; put32(b + 16, 777);
    sendto(ds, b, 36, 0, (struct sockaddr *)&da, sizeof da);
    n = answer(ds, r, sizeof r); assert(n == 36 && r[2] == 'C' && r[8] == 1 && !memcmp(r + 12, b + 12, 8));

    {   /* MIDI: a note on, then (a delta time of two bytes) running status's second note; a SysEx; a note off with velocity */
        static const uint8_t c1[] = {0x90, 60, 100, 0x81, 0x10, 64, 90, 0x00, 0xF0, 0x7E, 0x01, 0xF7, 0x00, 0x80, 60, 0};
        n = rtp(b, ssrc, 1, c1, sizeof c1, 1); sendto(ds, b, (size_t)n, 0, (struct sockaddr *)&da, sizeof da);
        n = rtp(b, 0x0BADu, 2, c1, sizeof c1, 0); sendto(ds, b, (size_t)n, 0, (struct sockaddr *)&da, sizeof da);   /* a stranger's: no */
        for (n = 0; n < 20; n++) { kr_rtpmidi_poll(on_midi, on_log, 0); usleep(2000); }
        assert(got_n == 3);
        assert(got_len[0] == 3 && got[0][0] == 0x90 && got[0][1] == 60 && got[0][2] == 100);
        assert(got_len[1] == 3 && got[1][0] == 0x90 && got[1][1] == 64 && got[1][2] == 90);
        assert(got_len[2] == 3 && got[2][0] == 0x80 && got[2][1] == 60);
    }
    {   /* running status carried into the next packet; a program change of one byte; a clock (realtime) */
        static const uint8_t c2[] = {67, 80, 0x00, 0xC1, 5, 0x00, 0xF8};
        got_n = 0;
        n = rtp(b, ssrc, 3, c2, sizeof c2, 0); sendto(ds, b, (size_t)n, 0, (struct sockaddr *)&da, sizeof da);
        for (n = 0; n < 20; n++) { kr_rtpmidi_poll(on_midi, on_log, 0); usleep(2000); }
        assert(got_n == 3 && got[0][0] == 0x80 && got[0][1] == 67 && got_len[1] == 2 && got[1][0] == 0xC1 && got_len[2] == 1 && got[2][0] == 0xF8);
    }

    /* goodbye */
    cmd(b, "BY", token, ssrc, 0, &n); sendto(cs, b, (size_t)n, 0, (struct sockaddr *)&ca, sizeof ca);
    for (n = 0; n < 20; n++) { kr_rtpmidi_poll(on_midi, on_log, 0); usleep(2000); }
    assert(!kr_rtpmidi_peer()[0] && logs == 2);

    {   /* the parser alone: Z (a delta time before the first), a long (B) section, a truncated one, not RTP */
        static const uint8_t z[] = {0x00, 0x90, 48, 1};
        uint8_t big[300];
        int i;
        got_n = 0;
        n = rtp(b, ssrc, 4, z, sizeof z, 0); b[12] |= 0x20;
        assert(kr_rtpmidi_parse(b, n, on_midi, 0) == 1 && got[0][1] == 48);
        for (i = 0; i < 40; i++) { big[i * 4] = 0x00; big[i * 4 + 1] = 0x90; big[i * 4 + 2] = (uint8_t)i; big[i * 4 + 3] = 1; }
        n = rtp(r, ssrc, 5, big + 1, 40 * 4 - 1, 0);
        assert(r[12] & 0x80);
        got_n = 0; assert(kr_rtpmidi_parse(r, n, on_midi, 0) == 40);
        assert(kr_rtpmidi_parse(r, 20, on_midi, 0) >= 0);   /* (cut short: no read past it) */
        r[0] = 0x40; assert(kr_rtpmidi_parse(r, n, on_midi, 0) == 0);
    }
    kr_rtpmidi_close();
    close(cs); close(ds);
    printf("rtp-midi test: OK\n");
    return 0;
}

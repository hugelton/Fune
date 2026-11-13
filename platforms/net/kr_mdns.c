/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A small multicast DNS responder for one service (kr_mdns.h), after RFC 6762 / 6763: answers sent to the group, records
 * uncompressed, the unique ones (SRV, TXT, A) with the cache-flush bit. Its own address: the one a socket "connected" to the
 * group gets (no packet is sent for that), found at the start (and again only while unknown). */
#define _DEFAULT_SOURCE 1   /* (the multicast names, ip_mreq and IP_ADD_MEMBERSHIP: BSD ones, hidden under a strict -std=c11) */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* (clock_gettime, the sockets' names under strict C11: Linux, the SDL build) */
#endif
#include "kr_mdns.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MDNS_GROUP 0xE00000FBu   /* 224.0.0.251 */
#define MDNS_PORT 5353
static int sk = -1;
static char inst[64], host[64];
static unsigned srv_port;
static uint32_t next_ms, told, last_poll_ms, ip_cached;
static int started;

static int put_name(uint8_t *o, int at, int cap, const char *dotted)   /* "a.b.local" as labels; the end, or -1 */
{
    const char *s = dotted;
    while (*s) {
        const char *e = strchr(s, '.');
        const int l = e ? (int)(e - s) : (int)strlen(s);
        if (l > 63 || at + 1 + l >= cap) return -1;
        o[at++] = (uint8_t)l; memcpy(o + at, s, (size_t)l); at += l;
        s += l; if (*s == '.') s++;
    }
    if (at >= cap) return -1;
    o[at++] = 0;
    return at;
}
static int put16(uint8_t *o, int at, int cap, unsigned v) { if (at + 2 > cap) return -1; o[at] = (uint8_t)(v >> 8); o[at + 1] = (uint8_t)v; return at + 2; }
static int put32(uint8_t *o, int at, int cap, uint32_t v) { at = put16(o, at, cap, v >> 16); return at < 0 ? -1 : put16(o, at, cap, v & 0xFFFFu); }
/* a record: name, type, class (flush: the cache-flush bit), ttl, then its data from fill; the end or -1 */
static int record(uint8_t *o, int at, int cap, const char *name, unsigned type, int flush, uint32_t ttl, int (*fill)(uint8_t *, int, int, uint32_t), uint32_t arg)
{
    int len_at, end;
    if ((at = put_name(o, at, cap, name)) < 0 || (at = put16(o, at, cap, type)) < 0 || (at = put16(o, at, cap, flush ? 0x8001u : 1u)) < 0 ||
        (at = put32(o, at, cap, ttl)) < 0 || (len_at = at, at = put16(o, at, cap, 0)) < 0 || (end = fill(o, at, cap, arg)) < 0) return -1;
    o[len_at] = (uint8_t)((end - at) >> 8); o[len_at + 1] = (uint8_t)(end - at);
    return end;
}
static char full_inst[160], full_host[96];
static int fill_ptr(uint8_t *o, int at, int cap, uint32_t a) { (void)a; return put_name(o, at, cap, full_inst); }
static int fill_srv(uint8_t *o, int at, int cap, uint32_t a)
{
    (void)a;
    if ((at = put16(o, at, cap, 0)) < 0 || (at = put16(o, at, cap, 0)) < 0 || (at = put16(o, at, cap, srv_port)) < 0) return -1;
    return put_name(o, at, cap, full_host);
}
static int fill_txt(uint8_t *o, int at, int cap, uint32_t a) { (void)a; if (at + 1 > cap) return -1; o[at] = 0; return at + 1; }   /* (no keys: one empty string) */
static int fill_a(uint8_t *o, int at, int cap, uint32_t ip) { return put32(o, at, cap, ip); }

int kr_mdns_build(uint8_t *o, int cap, uint32_t ip, uint32_t ttl)
{
    int at = 12;
    if (cap < 12) return -1;
    memset(o, 0, 12);
    o[2] = 0x84;                           /* a response, authoritative */
    o[7] = ip ? 4 : 3;                     /* answers: PTR, SRV, TXT (A when the address is known) */
    if ((at = record(o, at, cap, "_apple-midi._udp.local", 12, 0, ttl ? 4500u : 0u, fill_ptr, 0)) < 0) return -1;
    if ((at = record(o, at, cap, full_inst, 33, 1, ttl, fill_srv, 0)) < 0) return -1;
    if ((at = record(o, at, cap, full_inst, 16, 1, ttl ? 4500u : 0u, fill_txt, 0)) < 0) return -1;
    if (ip && (at = record(o, at, cap, full_host, 1, 1, ttl, fill_a, ip)) < 0) return -1;
    return at;
}

/* a name in a packet (labels, or a pointer back to them), dotted into out; the place after it, or -1 */
static int get_name(const uint8_t *p, int n, int at, char *out, int cap)
{
    int o = 0, jumps = 0, after = -1;
    while (at < n) {
        const int l = p[at];
        if (l == 0) { if (after < 0) after = at + 1; if (o > 0) o--; out[o] = 0; return after; }   /* (the last dot off) */
        if ((l & 0xC0) == 0xC0) {
            if (at + 1 >= n || ++jumps > 16) return -1;
            if (after < 0) after = at + 2;
            at = (l & 0x3F) << 8 | p[at + 1];
            continue;
        }
        if (l > 63 || at + 1 + l > n || o + l + 1 >= cap) return -1;
        memcpy(out + o, p + at + 1, (size_t)l); o += l; out[o++] = '.';
        at += 1 + l;
    }
    return -1;
}
static int same(const char *a, const char *b) { while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++; return !*a && !*b; }
int kr_mdns_wants(const uint8_t *q, int n)
{
    int i, at = 12, qd;
    if (n < 12 || (q[2] & 0x80)) return 0;   /* (a response, not a query) */
    qd = q[4] << 8 | q[5];
    for (i = 0; i < qd && i < 16; i++) {
        char name[256];
        unsigned type;
        if ((at = get_name(q, n, at, name, sizeof name)) < 0 || at + 4 > n) return 0;
        type = (unsigned)(q[at] << 8 | q[at + 1]);
        at += 4;
        if ((same(name, "_apple-midi._udp.local") && (type == 12 || type == 255)) ||
            (same(name, "_services._dns-sd._udp.local") && type == 12) ||
            same(name, full_inst) || same(name, full_host)) return 1;
    }
    return 0;
}

static uint32_t my_ip(void)   /* the address the group is reached from (host order); 0 none */
{
    struct sockaddr_in a, me;
    socklen_t l = sizeof me;
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    uint32_t ip = 0;
    if (s < 0) return 0;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(MDNS_PORT); a.sin_addr.s_addr = htonl(MDNS_GROUP);
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0 && getsockname(s, (struct sockaddr *)&me, &l) == 0) ip = ntohl(me.sin_addr.s_addr);
    close(s);
    return ip == 0x7F000001u ? 0 : ip;
}
static void send_all(uint32_t ttl)
{
    uint8_t b[512];
    struct sockaddr_in g;
    int n;
    if (!ip_cached) ip_cached = my_ip();   /* (once: finding it is a few socket calls, ~50 ms on the Switch) */
    n = kr_mdns_build(b, sizeof b, ip_cached, ttl);
    if (sk < 0 || n <= 0) return;
    memset(&g, 0, sizeof g);
    g.sin_family = AF_INET; g.sin_port = htons(MDNS_PORT); g.sin_addr.s_addr = htonl(MDNS_GROUP);
    sendto(sk, (const char *)b, (size_t)n, 0, (struct sockaddr *)&g, sizeof g);
}

int kr_mdns_open(const char *instance, const char *hostname, unsigned port)
{
    struct sockaddr_in a;
    struct ip_mreq mr;
    int one = 1;
    unsigned char ttl = 255;
    kr_mdns_close();
    snprintf(inst, sizeof inst, "%s", instance); snprintf(host, sizeof host, "%s", hostname);
    snprintf(full_inst, sizeof full_inst, "%s._apple-midi._udp.local", inst);
    snprintf(full_host, sizeof full_host, "%s.local", host);
    srv_port = port;
    if ((sk = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return -1;
    setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(sk, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);   /* (beside the system's own responder, where there is one) */
#endif
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(MDNS_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sk, (struct sockaddr *)&a, sizeof a) < 0) { close(sk); sk = -1; return -2; }
    memset(&mr, 0, sizeof mr);
    mr.imr_multiaddr.s_addr = htonl(MDNS_GROUP); mr.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sk, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr) < 0) { close(sk); sk = -1; return -3; }
    setsockopt(sk, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    fcntl(sk, F_SETFL, fcntl(sk, F_GETFL, 0) | O_NONBLOCK);
    started = 0; told = 0; ip_cached = my_ip();
    return 0;
}
void kr_mdns_poll(uint32_t now_ms)
{
    int guard;
    if (sk < 0) return;
    if (!started) { started = 1; next_ms = now_ms; }
    if ((int32_t)(now_ms - next_ms) >= 0) {   /* announced: three a second apart, then every 100 s */
        send_all(120);
        told++;
        next_ms = now_ms + (told < 3 ? 1000u : 100000u);
    }
    if (now_ms - last_poll_ms < 100u) return;   /* queries: looked at ten times a second (an answer within 100 ms is soon) */
    last_poll_ms = now_ms;
    {   struct pollfd pf;
        pf.fd = sk; pf.events = POLLIN; pf.revents = 0;
        if (poll(&pf, 1, 0) <= 0) return; }
    for (guard = 0; guard < 32; guard++) {
        uint8_t b[1500];
        const int n = (int)recv(sk, (char *)b, sizeof b, 0);
        if (n <= 0) break;
        if (kr_mdns_wants(b, n)) send_all(120);
    }
}
void kr_mdns_close(void)
{
    if (sk < 0) return;
    send_all(0);   /* goodbye: the records gone from the caches at once */
    close(sk);
    sk = -1;
}

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Bonjour responder's packets (platforms/net/kr_mdns.c): the answer holds the service's four records, a goodbye has TTL 0;
 * a query for the service (plain or with a pointer back), for the instance or the host is wanted, anything else and a response
 * are not. (The socket side, on the network: dns-sd -B _apple-midi._udp on the Mac.) */
#include "kr_mdns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int has(const uint8_t *b, int n, const char *s) { int i, l = (int)strlen(s); for (i = 0; i + l <= n; i++) if (!memcmp(b + i, s, (size_t)l)) return 1; return 0; }
static int q_name(uint8_t *b, int at, const char *const *labels)
{
    for (; *labels; labels++) { b[at++] = (uint8_t)strlen(*labels); memcpy(b + at, *labels, strlen(*labels)); at += (int)strlen(*labels); }
    b[at++] = 0;
    return at;
}
int main(void)
{
    uint8_t b[512], q[256];
    int n, at;
    /* (kr_mdns_open sets the names; a socket on 5353 may be refused here, the names are set before that) */
    kr_mdns_open("Kreisel", "kreisel-switch", 5004);
    n = kr_mdns_build(b, sizeof b, 0xC0A80353u, 120);
    assert(n > 12 && b[2] == 0x84 && b[7] == 4);
    assert(has(b, n, "\x07Kreisel\x0b_apple-midi\x04_udp\x05local") && has(b, n, "\x0ekreisel-switch\x05local"));
    assert(has(b, n, "\xC0\xA8\x03\x53") && has(b, n, "\x13\x8C"));   /* the address, the port 5004 */
    assert(kr_mdns_build(b, 40, 0xC0A80353u, 120) < 0);              /* (too small: refused, not overrun) */
    n = kr_mdns_build(b, sizeof b, 0, 0); assert(n > 12 && b[7] == 3); /* a goodbye, no address known */
    {   static const char *const svc[] = {"_apple-midi", "_udp", "local", 0}, *const other[] = {"_airplay", "_udp", "local", 0};
        memset(q, 0, 12); q[5] = 1; at = q_name(q, 12, svc); q[at++] = 0; q[at++] = 12; q[at++] = 0; q[at++] = 1;
        assert(kr_mdns_wants(q, at) == 1);
        q[2] = 0x84; assert(kr_mdns_wants(q, at) == 0); q[2] = 0;          /* (a response: no) */
        at = q_name(q, 12, other); q[at++] = 0; q[at++] = 12; q[at++] = 0; q[at++] = 1;
        assert(kr_mdns_wants(q, at) == 0);
        /* two questions, the second the service through a pointer to "_udp.local" in the first ("_airplay._udp.local") */
        q[5] = 2; q[at++] = 11; memcpy(q + at, "_apple-midi", 11); at += 11; q[at++] = 0xC0; q[at++] = 12 + 9; q[at++] = 0; q[at++] = 255; q[at++] = 0; q[at++] = 1;
        assert(kr_mdns_wants(q, at) == 1);
        q[at - 6] = 0xC0; q[at - 5] = (uint8_t)(at - 6); assert(kr_mdns_wants(q, at) == 0);   /* (a pointer to itself: no loop) */
    }
    {   static const char *const h[] = {"KREISEL-SWITCH", "local", 0};   /* (names compare without case) */
        memset(q, 0, 12); q[5] = 1; at = q_name(q, 12, h); q[at++] = 0; q[at++] = 1; q[at++] = 0; q[at++] = 1;
        assert(kr_mdns_wants(q, at) == 1);
    }
    kr_mdns_close();
    printf("mdns test: OK\n");
    return 0;
}

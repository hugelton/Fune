/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KR_MDNS_H
#define KR_MDNS_H
#include <stdint.h>
/* Bonjour for the RTP-MIDI session (kr_rtpmidi.h): the session announced on the network (multicast DNS, 224.0.0.251:5353) as
 * an _apple-midi._udp service, so the Mac's Audio MIDI Setup lists it under Directory by itself (no address to type). Announced
 * at the start (three times, a second apart) and every 100 s after; a query that asks for the service, the instance or the host
 * is answered with them all (PTR, SRV, TXT, A). Polled from the frame; nothing is threaded.
 *   kr_mdns_open("Kreisel", "kreisel-switch", 5004)    0 on success
 *   kr_mdns_poll(ms)                                    the queries waiting; an announcement when due
 *   kr_mdns_close()                                     a goodbye (the records with TTL 0), the socket closed */
int kr_mdns_open(const char *instance, const char *host, unsigned port);
void kr_mdns_poll(uint32_t now_ms);
void kr_mdns_close(void);
/* (tests) the answer, for the address ip (host order), ttl 0 a goodbye; a query's bytes: 1 when it asks for one of ours */
int kr_mdns_build(uint8_t *out, int cap, uint32_t ip, uint32_t ttl);
int kr_mdns_wants(const uint8_t *q, int n);
#endif

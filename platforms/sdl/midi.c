/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI in and out for the SDL builds (midi.h). */
#include "midi.h"
#include "kreisel_io.h"
#include "controllers.h"
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 1   /* (the clock's USB source) */

/* the messages come in on the system's thread (CoreMIDI's) and go out on the main loop's: one writer, one reader, no lock */
#define QN 1024u
static struct { uint8_t b[3], n, dev, src; } q[QN];   /* dev: the source's controller profile (controllers.h), 0 none; src: its name (src_name) */
static atomic_uint q_w, q_r;
static void push(const uint8_t *b, int n, int dev, int src)
{
    const unsigned w = atomic_load_explicit(&q_w, memory_order_relaxed);
    if (n < 1 || n > 3 || w - atomic_load_explicit(&q_r, memory_order_acquire) >= QN) return;   /* (full: dropped) */
    memcpy(q[w % QN].b, b, (size_t)n); q[w % QN].n = (uint8_t)n; q[w % QN].dev = (uint8_t)dev; q[w % QN].src = (uint8_t)src;
    atomic_store_explicit(&q_w, w + 1, memory_order_release);
}
/* the sources' names, a number each (1 ..; 0 none), for MIDI learn's profile (only ever added to: a number keeps its name) */
#define NSRC 48
static char src_names[NSRC][64];
static atomic_int nsrc;
static int src_id(const char *name)
{
    int i;
    const int n = atomic_load(&nsrc);
    if (!name || !name[0]) return 0;
    for (i = 0; i < n; i++) if (!strcmp(src_names[i], name)) return i + 1;
    if (n >= NSRC) return 0;
    snprintf(src_names[n], sizeof src_names[n], "%s", name);
    atomic_store(&nsrc, n + 1);
    return n + 1;
}
static const char *src_name(int id) { return id >= 1 && id <= atomic_load(&nsrc) ? src_names[id - 1] : ""; }
static void reconnect(void);
static int refresh;   /* (MIDI learn wrote a profile: each source's device again) */
static int learned;   /* (how many profiles it wrote) */
static void drain(KApp *app, uint32_t ms)
{
    unsigned r = atomic_load_explicit(&q_r, memory_order_relaxed);
    const unsigned w = atomic_load_explicit(&q_w, memory_order_acquire);
    static int log = -1;
    if (log < 0) log = getenv("FUNE_MIDI_LOG") != 0;   /* (a line a message, for checking a route) */
    for (; r != w; r++) {
        if (log) fprintf(stderr, "midi: in %02X %02X %02X (%d)\n", q[r % QN].b[0], q[r % QN].n > 1 ? q[r % QN].b[1] : 0, q[r % QN].n > 2 ? q[r % QN].b[2] : 0, q[r % QN].n);
        k_io_midi_dev(app, PORT, q[r % QN].dev, q[r % QN].b, q[r % QN].n, ms);
#if K_WITH_CONNECT
        if (k_learn_row(app)) {   /* MIDI learn bound a control: the row into this source's profile, in use at once */
            const int ok = sdl_ctl_learn(src_name(q[r % QN].src), k_learn_row(app)) == 0;
            k_learn_done(app, ok);
            if (ok) { refresh = 1; learned++; }
        }
#endif
    }
    atomic_store_explicit(&q_r, r, memory_order_release);
    if (refresh) { reconnect(); refresh = 0; }
}
static int sources, outs;
int sdl_midi_sources(void) { return sources; }
int sdl_midi_outputs(void) { return outs; }
static int msg_ok(const uint8_t *m, int n)   /* (what goes out: channel messages and the realtime bytes; no SysEx) */
{
    const int k = m[0] & 0xF0;
    if (n < 1 || m[0] < 0x80 || (m[0] >= 0xF0 && m[0] < 0xF8)) return 0;
    return m[0] >= 0xF8 ? n >= 1 : k == 0xC0 || k == 0xD0 ? n >= 2 : n >= 3;
}
/* which sources (the Connect tab's MIDI in, io.conf's in =): 0 all, 1 none, 2 those whose name holds sel_name (any case) */
static int sel_mode, sel_now = -1;
static char sel_name[32];
static int wanted(const char *name)
{
    size_t i, j, n = strlen(sel_name);
    if (sel_mode == 1) return 0;
    if (sel_mode != 2 || !n) return 1;
    for (i = 0; name && name[i]; i++) {
        for (j = 0; j < n && name[i + j]; j++) { char x = name[i + j], y = sel_name[j]; if ((x | 32) != (y | 32)) break; }
        if (j == n) return 1;
    }
    return 0;
}
static void reconnect(void);
static void follow_choice(const KApp *app)   /* (the choice changed: the sources again) */
{
#if K_WITH_CONNECT
    const int m = app->set_v[K_SET_MIDI_IN];
    if (m == sel_now && (m != 2 || !strcmp(sel_name, k_io_in_name()))) return;
    sel_mode = m; sel_now = m; snprintf(sel_name, sizeof sel_name, "%s", k_io_in_name());
    reconnect();
#else
    (void)app;
#endif
}

#if defined(__APPLE__)
/* ---- CoreMIDI: MIDI 1.0 as Universal MIDI Packets; each source connected, again when the setup changes ---- */
#include <CoreMIDI/CoreMIDI.h>
static MIDIClientRef client;
static MIDIPortRef in_port;
static MIDIEndpointRef out_src;   /* MIDI out: a virtual source named after the app (any DAW can take it) */
static void connect_all(void)
{
    ItemCount i, n = MIDIGetNumberOfSources();
    sources = 0;
    for (i = 0; i < n; i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        CFStringRef nm = NULL;
        char name[128] = "";
        if (!src || src == out_src) continue;   /* (not our own out: it would come straight back in) */
        if (MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &nm) == noErr && nm) { CFStringGetCString(nm, name, sizeof name, kCFStringEncodingUTF8); CFRelease(nm); }
        if (!wanted(name)) { MIDIPortDisconnectSource(in_port, src); continue; }
        if (refresh) MIDIPortDisconnectSource(in_port, src);   /* (a profile written: its refCon anew) */
        if (MIDIPortConnectSource(in_port, src, (void *)(intptr_t)(sdl_ctl_dev(name) | src_id(name) << 8)) == noErr) sources++;   /* (its profile's
                                                 * device and its name's number ride as the connection's refCon; connecting one twice: harmless) */
    }
}
static void reconnect(void) { if (in_port) { connect_all(); fprintf(stderr, "midi: %d source%s\n", sources, sources == 1 ? "" : "s"); } }
static void ump_words(const UInt32 *w, UInt32 n, int dev)
{
    UInt32 i = 0;
    while (i < n) {
        const UInt32 x = w[i], mt = x >> 28;
        const UInt32 len = mt <= 2 ? 1 : mt <= 4 ? 2 : mt == 5 ? 4 : mt <= 0xC ? 1 : 4;   /* (a UMP's size by its type) */
        if (mt == 1 || mt == 2) {   /* system (clock, start, stop) / MIDI 1.0 channel voice */
            const uint8_t st = (uint8_t)(x >> 16), b[3] = {st, (uint8_t)(x >> 8 & 0x7F), (uint8_t)(x & 0x7F)};
            const int k = st & 0xF0;
            push(b, st >= 0xF8 ? 1 : st >= 0xF0 ? (st == 0xF2 ? 3 : st == 0xF1 || st == 0xF3 ? 2 : 1) : k == 0xC0 || k == 0xD0 ? 2 : 3, dev & 0xFF, dev >> 8);
        }
        i += len;
    }
}
int sdl_midi_open(const char *name)
{
    CFStringRef nm = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
    OSStatus e = MIDIClientCreateWithBlock(nm, &client, ^(const MIDINotification *msg) {
        if (msg->messageID == kMIDIMsgSetupChanged) connect_all();   /* (a device plugged in or out) */
    });
    if (e == noErr)
        e = MIDIInputPortCreateWithProtocol(client, nm, kMIDIProtocol_1_0, &in_port, ^(const MIDIEventList *list, void *src) {
            const MIDIEventPacket *p = &list->packet[0];
            UInt32 i;
            for (i = 0; i < list->numPackets; i++) { ump_words(p->words, p->wordCount, (int)(intptr_t)src); p = MIDIEventPacketNext(p); }
        });
    if (e == noErr && MIDISourceCreateWithProtocol(client, nm, kMIDIProtocol_1_0, &out_src) == noErr) outs = 1;
    CFRelease(nm);
    if (e != noErr) { fprintf(stderr, "midi: CoreMIDI unavailable (%d)\n", (int)e); return -1; }
    connect_all();
    fprintf(stderr, "midi: CoreMIDI, %d source%s, out %s\n", sources, sources == 1 ? "" : "s", outs ? name : "none");
    return 0;
}
void sdl_midi_send(const uint8_t *m, int n)   /* one message as a MIDI 1.0 UMP (group 0), now (timestamp 0) */
{
    MIDIEventList list;
    MIDIEventPacket *p;
    UInt32 w;
    if (!out_src || !msg_ok(m, n)) return;
    w = m[0] >= 0xF8 ? 0x10000000u | (UInt32)m[0] << 16
                     : 0x20000000u | (UInt32)m[0] << 16 | (UInt32)(m[1] & 0x7F) << 8 | (UInt32)(n > 2 ? m[2] & 0x7F : 0);
    p = MIDIEventListInit(&list, kMIDIProtocol_1_0);
    if (MIDIEventListAdd(&list, sizeof list, p, 0, 1, &w)) MIDIReceivedEventList(out_src, &list);
}
void sdl_midi_poll(KApp *app, uint32_t ms) { follow_choice(app); drain(app, ms); }
void sdl_midi_close(void)
{
    if (out_src) MIDIEndpointDispose(out_src);
    if (in_port) MIDIPortDispose(in_port);
    if (client) MIDIClientDispose(client);
    out_src = 0; in_port = 0; client = 0; outs = 0;
}

#elif defined(__linux__)
/* ---- Linux: the ALSA sequencer (KREISEL_ALSA) and RTP-MIDI, both polled from the main loop (no thread) ---- */
#include "kr_rtpmidi.h"
#include "kr_mdns.h"
static int rtp_on;
static void rtp_msg(void *user, const uint8_t *msg, int len)   /* (its sessions as one source named "RTP-MIDI": a profile may match it) */
{
    static int dev, seen = -1;
    if (seen != learned) { dev = sdl_ctl_dev("RTP-MIDI"); seen = learned; }
    (void)user; push(msg, len, dev, src_id("RTP-MIDI"));
}
static void rtp_log(void *user, const char *line) { (void)user; fprintf(stderr, "midi: %s\n", line); }
#if KREISEL_ALSA
#include <alsa/asoundlib.h>
static snd_seq_t *seq;
static int my_port = -1, out_port = -1;   /* in; out (a readable port: whoever subscribes gets it) */
static snd_midi_event_t *dec, *enc;
static struct { int c, p, dev, src; } subs[64];
static int nsubs;
static void subscribe(int client_id, int port_id)
{
    snd_seq_port_info_t *pi;
    if (client_id == snd_seq_client_id(seq) || client_id == SND_SEQ_CLIENT_SYSTEM) return;
    snd_seq_port_info_alloca(&pi);
    if (snd_seq_get_any_port_info(seq, client_id, port_id, pi) < 0 || !wanted(snd_seq_port_info_get_name(pi))) return;
    if (nsubs < 64 && snd_seq_connect_from(seq, my_port, client_id, port_id) == 0) { subs[nsubs].c = client_id; subs[nsubs].p = port_id; subs[nsubs].dev = sdl_ctl_dev(snd_seq_port_info_get_name(pi)); subs[nsubs].src = src_id(snd_seq_port_info_get_name(pi)); nsubs++; sources++; }
}
static void subscribe_all(void);
static void reconnect(void)
{
    int i;
    if (!seq) return;
    for (i = 0; i < nsubs; i++) snd_seq_disconnect_from(seq, my_port, subs[i].c, subs[i].p);
    nsubs = 0; sources = 0;
    subscribe_all();
    fprintf(stderr, "midi: %d source%s\n", sources, sources == 1 ? "" : "s");
}
static void subscribe_all(void)
{
    snd_seq_client_info_t *ci; snd_seq_port_info_t *pi;
    snd_seq_client_info_alloca(&ci); snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        snd_seq_port_info_set_client(pi, snd_seq_client_info_get_client(ci));
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned cap = snd_seq_port_info_get_capability(pi);
            if ((cap & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) == (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                subscribe(snd_seq_port_info_get_client(pi), snd_seq_port_info_get_port(pi));
        }
    }
}
#else
static void reconnect(void) {}
#endif
int sdl_midi_open(const char *name)
{
    int ok = 0;
#if KREISEL_ALSA
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) == 0) {
        snd_seq_set_client_name(seq, name);
        my_port = snd_seq_create_simple_port(seq, name, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        snd_seq_connect_from(seq, my_port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);   /* (to hear of ports appearing) */
        snd_midi_event_new(16, &dec);
        snd_midi_event_no_status(dec, 1);
        out_port = snd_seq_create_simple_port(seq, name, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (out_port >= 0 && snd_midi_event_new(16, &enc) == 0) outs = 1;
        subscribe_all();
        fprintf(stderr, "midi: ALSA, %d source%s, out %s\n", sources, sources == 1 ? "" : "s", outs ? name : "none");
        ok = 1;
    } else fprintf(stderr, "midi: ALSA sequencer unavailable\n");
#endif
    if (kr_rtpmidi_open(5004, name) == 0) {   /* RTP-MIDI: a Mac or an iPhone invites it (Audio MIDI Setup, Network) */
        rtp_on = 1; ok = 1;
        if (kr_mdns_open(name, "fune", 5004) != 0) fprintf(stderr, "midi: mDNS unavailable (add the address by hand)\n");
    }
    return ok ? 0 : -1;
}
void sdl_midi_send(const uint8_t *m, int n)
{
#if KREISEL_ALSA
    snd_seq_event_t ev;
    if (!seq || !outs || !msg_ok(m, n)) return;
    snd_seq_ev_clear(&ev);
    snd_midi_event_reset_encode(enc);   /* (a whole message each time: no running status) */
    if (snd_midi_event_encode(enc, m, n, &ev) < 1 || ev.type == SND_SEQ_EVENT_NONE) return;
    snd_seq_ev_set_source(&ev, out_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output_direct(seq, &ev);
#else
    (void)m; (void)n;
#endif
}
void sdl_midi_poll(KApp *app, uint32_t ms)
{
    follow_choice(app);
#if KREISEL_ALSA
    if (seq) {
        snd_seq_event_t *ev;
        while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
            if (ev->type == SND_SEQ_EVENT_PORT_START) subscribe(ev->data.addr.client, ev->data.addr.port);   /* (a device plugged in) */
            else if (ev->type != SND_SEQ_EVENT_PORT_EXIT && ev->type != SND_SEQ_EVENT_PORT_SUBSCRIBED && ev->type != SND_SEQ_EVENT_PORT_UNSUBSCRIBED) {
                uint8_t b[16];
                const long n = snd_midi_event_decode(dec, b, sizeof b, ev);
                int k, dev = 0, src = 0;
                for (k = 0; k < nsubs; k++) if (subs[k].c == ev->source.client && subs[k].p == ev->source.port) { dev = subs[k].dev; src = subs[k].src; }   /* (its profile's) */
                if (n > 0 && n <= 3) push(b, (int)n, dev, src);
            }
        }
    }
#endif
    if (rtp_on) { kr_rtpmidi_poll(rtp_msg, rtp_log, 0); kr_mdns_poll(ms); }
    drain(app, ms);
}
void sdl_midi_close(void)
{
    if (rtp_on) { kr_mdns_close(); kr_rtpmidi_close(); rtp_on = 0; }
#if KREISEL_ALSA
    if (dec) snd_midi_event_free(dec);
    if (enc) snd_midi_event_free(enc);
    if (seq) snd_seq_close(seq);
    dec = 0; enc = 0; seq = 0; outs = 0;
#endif
}

#else
static void reconnect(void) {}
int sdl_midi_open(const char *name) { (void)name; return -1; }
void sdl_midi_send(const uint8_t *m, int n) { (void)m; (void)n; }
void sdl_midi_poll(KApp *app, uint32_t ms) { (void)app; (void)ms; }
void sdl_midi_close(void) {}
#endif

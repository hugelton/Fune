/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KR_RTPMIDI_H
#define KR_RTPMIDI_H
#include <stdint.h>
/* MIDI over Wi-Fi: an RTP-MIDI (AppleMIDI) session partner, as macOS's Network MIDI (Audio MIDI Setup, MIDI Studio, Network)
 * and iOS speak it. Kreisel only listens: an initiator (the Mac) invites it on a control port and the next one (data), keeps the
 * clocks in step (CK), and sends MIDI in RTP packets; each command reaches the callback as its bytes (running status resolved,
 * SysEx and the recovery journal skipped). One partner at a time; a new invitation takes over. Plain BSD sockets, non-blocking,
 * polled from the frame (libnx's, POSIX's): nothing is threaded.
 *   kr_rtpmidi_open(5004, "Kreisel")     the ports 5004 and 5005; 0 on success
 *   kr_rtpmidi_poll(on_midi, user, log)  every frame: every packet waiting, answered or passed on
 *   kr_rtpmidi_close()                   says goodbye (BY) to the partner, closes the ports */
typedef void (*kr_midi_fn)(void *user, const uint8_t *msg, int len);
typedef void (*kr_log_fn)(void *user, const char *line);
int kr_rtpmidi_open(unsigned port, const char *name);
void kr_rtpmidi_poll(kr_midi_fn on_midi, kr_log_fn on_log, void *user);
void kr_rtpmidi_close(void);
const char *kr_rtpmidi_peer(void);   /* the partner's name, "" when none */
/* (tests) one RTP-MIDI payload (the RTP header on) parsed into on_midi; the commands found */
int kr_rtpmidi_parse(const uint8_t *p, int n, kr_midi_fn on_midi, void *user);
#endif

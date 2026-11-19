/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * Ableton Link for the SDL builds (link.h). This file is MPL-2.0; the Link library it is built with (third_party/link) is
 * GPL-2.0-or-later, so a program built with it is distributed under the GPL as a whole (LICENSING.md). */
#include "link.h"

#include <ableton/Link.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#include <atomic>
#include <chrono>
#include <cmath>

namespace {
constexpr double kQuantum = 4.0;   /* a bar of four beats: the sequencer's 16 steps */
ableton::Link *g_link;
ableton::link::HostTimeFilter<ableton::Link::Clock> g_filter;   /* (the audio thread's) */
double g_sample;                                                 /* the frames rendered (the audio thread's) */
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_rate{0}, g_latency{0};
/* the main loop's: what each side was at the last poll (-1: Link just turned on) */
int g_app_bpm = -1, g_app_playing = -1;
double g_link_tempo;
bool g_link_playing;

std::chrono::microseconds latency_us()
{
    const uint32_t rate = g_rate.load(std::memory_order_relaxed);
    return std::chrono::microseconds(rate ? (int64_t)g_latency.load(std::memory_order_relaxed) * 1000000 / rate : 0);
}
}

extern "C" {
void sdl_link_init(KApp *a)
{
    if (g_link) return;
    g_link = new ableton::Link(a->bpm > 0 ? a->bpm : 120);
    g_link->enableStartStopSync(true);
    a->link_peers = 0;   /* (the row shown) */
}

void sdl_link_poll(KApp *a)
{
    if (!g_link) return;
    const bool want = a->set_v[K_SET_LINK] && a->clock_src <= 0;
    if (want != g_link->isEnabled()) {
        g_link->enable(want);
        g_on.store(want, std::memory_order_relaxed);
        g_app_bpm = g_app_playing = -1;
    }
    if (!want) { a->link_peers = 0; return; }
    {const size_t n = g_link->numPeers(); a->link_peers = (int8_t)(n > 99 ? 99 : n);}
    auto st = g_link->captureAppSessionState();
    const auto now = g_link->clock().micros();
    bool commit = false;
    const int ap = a->playing ? 1 : 0;
    if (g_app_bpm < 0 || a->bpm != g_app_bpm) {   /* turned on (a session joined later takes its own tempo), or the BPM turned here */
        st.setTempo(a->bpm, now);
        g_link_tempo = a->bpm;
        commit = true;
    } else if (std::fabs(st.tempo() - g_link_tempo) > 1e-6) {   /* another peer's: as a whole BPM (the steps follow the exact one) */
        const long b = std::lround(st.tempo());
        g_link_tempo = st.tempo();
        k_ref_set(a, k_param_ref(a, K_PAGE_CLOCK, 0), (int)(b < 40 ? 40 : b > 240 ? 240 : b));
    }
    if (g_app_playing < 0) g_link_playing = st.isPlaying();   /* (turned on: neither side's state pushed to the other) */
    else if (ap != g_app_playing) {   /* PLAY or STOP here: the session's beat 0 as our first step sounds (alone: now; else the next bar) */
        if (ap) st.setIsPlayingAndRequestBeatAtTime(true, now + latency_us(), 0.0, kQuantum);
        else st.setIsPlaying(false, now);
        g_link_playing = ap != 0;
        commit = true;
    } else if (st.isPlaying() != g_link_playing) {   /* another peer's PLAY or STOP */
        g_link_playing = st.isPlaying();
        if (g_link_playing != (ap != 0)) {KAction e = {K_ACT_PLAY, 0, 0, a->clock_ms}; k_dispatch(a, e);}
    }
    g_app_bpm = a->bpm;
    g_app_playing = a->playing ? 1 : 0;
    if (commit) g_link->commitAppSessionState(st);
}

void sdl_link_format(uint32_t rate, uint32_t latency_frames)
{
    g_rate.store(rate, std::memory_order_relaxed);
    g_latency.store(latency_frames, std::memory_order_relaxed);
}

void sdl_link_audio(KAudio *audio, uint32_t frames)
{
    const uint32_t rate = g_rate.load(std::memory_order_relaxed);
    const auto at = g_link && rate ? g_filter.sampleTimeToHostTime(g_sample) : std::chrono::microseconds(0);
    g_sample += frames;
    if (!g_link || !rate || !g_on.load(std::memory_order_relaxed)) return;
    const auto st = g_link->captureAudioSessionState();
    const double tempo = st.tempo();
    if (!(tempo > 0.0)) return;
    const double phase = st.phaseAtTime(at + latency_us(), kQuantum);   /* the session's place as this block is heard */
    k_audio_phase_lock(audio, (int64_t)std::llround(phase * 4.0 * 65536.0),
                       (int64_t)std::llround((double)rate * 15.0 / tempo * 65536.0));
}

void sdl_link_close(void)
{
    if (!g_link) return;
    g_on.store(false);
    g_link->enable(false);
    delete g_link;
    g_link = nullptr;
}
}

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * Ableton Link against a peer of its own (not a ctest: it needs the network's multicast, and takes some seconds): Fune's side
 * (platforms/sdl/link.cpp) and a second Link in the same program find each other. Alone, PLAY starts at once; the peer's tempo
 * comes to the BPM, a BPM turned here goes to the peer, the peer's PLAY starts the sequencer, whose bars then begin where the
 * peer's do (the error printed, in ms), and STOP here stops the peer. The audio is rendered in time, a block every block's
 * length from start to end, as a callback would (the main loop's poll between blocks).
 *   build/k8/kreisel_link_check        exit 0: all of it held */
#include "link.h"
#include <ableton/Link.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace std::chrono;

static const uint32_t kRate = 48000, kBlock = 512;
static KApp app;
static KAudio audio;
static ableton::Link *peer;        /* (made after the alone part) */
static ableton::Link::Clock host;
static microseconds t0;
static uint64_t blocks;
static float buf[2 * kBlock];

static microseconds block_us(uint64_t n) { return microseconds((int64_t)(n * kBlock * 1000000 / kRate)); }

/* the next block, when it is due: the main loop's part, then the callback's. Returns when it was due: a sound card plays the
 * blocks on that even clock, however late the thread woke (Link's filter in link.cpp follows the even one too) */
static microseconds block_now()
{
    const auto due = t0 + block_us(blocks++);
    while (host.micros() < due) std::this_thread::sleep_for(microseconds(200));
    sdl_link_poll(&app);
    k_audio_sync(&audio, &app);
    sdl_link_audio(&audio, kBlock);
    k_audio_render(&audio, buf, kBlock);
    return due;
}

static bool wait_for(const char *what, bool (*done)(), int ms)
{
    const uint64_t first = blocks, limit = (uint64_t)ms * kRate / 1000 / kBlock;
    while (blocks - first < limit) {
        block_now();
        if (done()) {
            std::printf("ok   %s (%d ms)\n", what, (int)(block_us(blocks - first).count() / 1000));
            return true;
        }
    }
    std::printf("FAIL %s\n", what);
    return false;
}

static void play_here() { KAction e = {K_ACT_PLAY, 0, 0, 0}; k_dispatch(&app, e); }

int main()
{
    int fails = 0;
    k_init(&app);
    sdl_link_init(&app);
    app.set_v[K_SET_LINK] = 1;
    k_audio_init(&audio, kRate);
    sdl_link_format(kRate, kBlock);
    t0 = host.micros();
    for (int i = 0; i < 100; i++) block_now();   /* (a second of audio first: the sample clock's filter settles) */

    {   /* alone in the session: PLAY here starts at once (a block or two), not at some next bar */
        const uint64_t first = blocks;
        play_here();
        while (blocks - first < 200 && audio.current_step < 0) block_now();
        const int n = (int)(blocks - first);
        const bool ok = n <= 2;
        std::printf("%s PLAY alone: the first step in block %d\n", ok ? "ok  " : "FAIL", n);
        fails += !ok;
        play_here();
        block_now();
    }

    peer = new ableton::Link(100.0);
    peer->enableStartStopSync(true);
    peer->enable(true);
    fails += !wait_for("the peer found", [] { return app.link_peers == 1; }, 8000);
    {   /* the peer's tempo, as a whole BPM */
        auto st = peer->captureAppSessionState();
        st.setTempo(133.4, peer->clock().micros());
        peer->commitAppSessionState(st);
    }
    fails += !wait_for("the peer's 133.4 BPM here as 133", [] { return app.bpm == 133; }, 3000);
    k_ref_set(&app, k_param_ref(&app, K_PAGE_CLOCK, 0), 128);   /* a BPM turned here */
    fails += !wait_for("128 BPM here at the peer", [] {
        return std::fabs(peer->captureAppSessionState().tempo() - 128.0) < 1e-6; }, 3000);
    {   /* the peer's PLAY, at its next bar */
        auto st = peer->captureAppSessionState();
        st.setIsPlayingAndRequestBeatAtTime(true, peer->clock().micros(), 0.0, 4.0);
        peer->commitAppSessionState(st);
    }
    fails += !wait_for("the peer's PLAY here", [] { return app.playing != 0; }, 3000);

    {   /* each step 0 where the peer's bar begins: the frame it fired at, as heard (a block's latency on) */
        int bars = 0, last = audio.current_step;
        double worst = 0.0;
        const uint64_t first = blocks;
        while (bars < 8 && blocks - first < 20u * kRate / kBlock) {
            const uint64_t frame0 = audio.frame;
            const auto at = block_now();
            if (audio.current_step == 0 && last != 0 && audio.last_trigger_frame[0] >= frame0) {
                const auto heard = at + block_us(1) + microseconds((int64_t)((audio.last_trigger_frame[0] - frame0) * 1000000 / kRate));
                double ph = peer->captureAppSessionState().phaseAtTime(heard, 4.0);   /* 0 (or just under 4): on the bar */
                if (ph > 2.0) ph -= 4.0;
                const double ms = ph * 60000.0 / 128.0;
                std::printf("     bar %d: %+.2f ms from the peer's\n", bars, ms);
                if (std::fabs(ms) > worst) worst = std::fabs(ms);
                bars++;
            }
            last = audio.current_step;
        }
        const bool ok = bars == 8 && worst < 2.0;
        std::printf("%s the bars on the peer's (worst %.2f ms)\n", ok ? "ok  " : "FAIL", worst);
        fails += !ok;
    }

    play_here();   /* STOP here */
    fails += !wait_for("STOP here at the peer", [] { return !peer->captureAppSessionState().isPlaying(); }, 3000);

    peer->enable(false);
    delete peer;
    sdl_link_close();
    std::printf("%s\n", fails ? "link check: FAILED" : "link check: all held");
    return fails ? 1 : 0;
}

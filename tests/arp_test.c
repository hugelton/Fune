/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel_audio.h"
#include "kreisel_arp.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void render(KAudio *a, size_t frames)
{
    float buf[2 * 251];
    while (frames) {
        size_t n = frames < 251 ? frames : 251;
        k_audio_render(a, buf, n);
        for (size_t i = 0; i < n * 2; i++)
            assert(isfinite(buf[i]) && fabsf(buf[i]) <= 1.f);
        frames -= n;
    }
}
static void setup(KApp *p, KAudio *a, int mode)
{
    k_init(p);
    k_roll_fill(p, 0, 0, 0, 60);       /* isolate the live keyboard from the roll */
    p->params[0][K_PAGE_ARP][0] = mode;
    p->params[0][K_PAGE_ARP][1] = 2; /* 1/16 */
    p->params[0][K_PAGE_ARP][2] = 1;
    p->params[0][K_PAGE_ARP][3] = 64;
    k_audio_init(a, 44100);
    k_audio_sync(a, p);
}
int main(void)
{
    static const int updn[6] = {0,1,2,1,0,1};
    static const int dnup[6] = {2,1,0,1,2,1};
    KAudio a, b;
    KApp p;
    uint32_t rng, step;
    unsigned walk, plus;
    int i;

    assert(K_ARP_CLOCKS[2] == 6 && K_ARP_CLOCKS[5] == 4 && K_ARP_CLOCKS[9] == 384);
    for (i = 0; i < 6; i++) {
        rng = 1; walk = plus = 0;
        assert(k_arp_pick(3, (unsigned)i, 3, &rng, &walk, &plus) == updn[i]);
        assert(k_arp_pick(7, (unsigned)i, 3, &rng, &walk, &plus) == dnup[i]);
    }
    for (i = 1; i <= 14; i++) {
        rng = 123; walk = 0;
        for (step = 0; step < 99; step++) {
            int k = k_arp_pick((unsigned)i, step, 5, &rng, &walk, &plus);
            assert(k >= 0 && k < 5 && plus <= 1);
            if (i == 8) assert(plus == (step & 1u));
        }
    }
    rng = 1; walk = plus = 0;
    assert(k_arp_pick(0, 0, 3, &rng, &walk, &plus) == -1);
    assert(k_arp_pick(1, 0, 0, &rng, &walk, &plus) == -1);
    assert(k_arp_pick(6, 0, 3, &rng, &walk, &plus) == 0);
    assert(k_arp_pick(6, 1, 3, &rng, &walk, &plus) == 0);
    assert(k_arp_pick(6, 2, 3, &rng, &walk, &plus) == 1);

    /* OFF is still a normal held note and does not generate sequencer triggers. */
    setup(&p, &a, 0);
    k_audio_note(&a, 0, 60, 100, 1);
    render(&a, 1024);
    assert(a.triggers[0] == 0 && a.voices[0] == 1);
    k_audio_note(&a, 0, 60, 0, 0);

    /* UP plays immediately, then follows a sixteenth-note clock independent
     * of the audio callback's chunking. Releasing all keys ends the phrase. */
    setup(&p, &a, 1);
    k_audio_note(&a, 0, 60, 100, 1);
    k_audio_note(&a, 0, 64, 112, 1);
    k_audio_note(&a, 0, 67, 120, 1);
    render(&a, 1);
    assert(a.triggers[0] == 1);
    assert(a.last_trigger_frame[0] == 0);
    assert(a.arp_live[0][0].vel != 0);
    render(&a, 18000);
    assert(a.triggers[0] == 4);   /* frames 0, ~5513, ~11025, ~16538 */
    assert(a.last_trigger_frame[0] >= 16537 && a.last_trigger_frame[0] <= 16539);
    k_audio_note(&a, 0, 60, 0, 0);
    k_audio_note(&a, 0, 64, 0, 0);
    k_audio_note(&a, 0, 67, 0, 0);
    i = (int)a.triggers[0];
    render(&a, 30000);
    assert((int)a.triggers[0] == i);

    /* Changing MODE to OFF with a key held restores the usual sustained key. */
    setup(&p, &a, 1);
    k_audio_note(&a, 0, 60, 100, 1);
    render(&a, 200);
    p.params[0][K_PAGE_ARP][0] = 0;
    k_audio_sync(&a, &p);
    render(&a, 4096);
    assert(a.voices[0] > 0);
    k_audio_note(&a, 0, 60, 0, 0);

    /* Sequencer steps feed the same ARP, including the step's duration.
     * Step 0 lasts two sixteenths; no other steps are enabled. */
    setup(&p, &a, 1);
    p.playing = 1;
    k_roll_fill(&p, 0, 0, 0, 60); k_roll_add(&p, 0, 0, 0, 60, 2, 100);
    k_audio_sync(&a, &p);
    render(&a, 1);
    assert(a.current_step == 0 && a.triggers[0] == 1);
    render(&a, 8000);
    assert(a.triggers[0] >= 2);
    i = (int)a.triggers[0];
    render(&a, 20000);
    assert((int)a.triggers[0] == i); /* expired step doesn't stick */

    /* External MIDI clock: 6 pulses for 1/16, 3 for 1/32,
     * with no internal free-run when the input clock is selected. */
    setup(&p, &a, 1);
    p.clock_src = 1;
    p.playing = 1;
    k_audio_sync(&a, &p);
    k_audio_note(&a, 0, 60, 100, 1);
    render(&a, 20000);
    assert(a.triggers[0] == 0);
    k_audio_clock_tick(&a, 5);
    render(&a, 32);
    assert(a.triggers[0] == 0);
    k_audio_clock_tick(&a, 1);
    render(&a, 32);
    assert(a.triggers[0] == 1);
    p.params[0][K_PAGE_ARP][1] = 3;
    k_audio_sync(&a, &p);
    k_audio_clock_tick(&a, 3);
    render(&a, 32);
    assert(a.triggers[0] == 2);

    /* Callback partitioning cannot change clock/trigger scheduling. */
    setup(&p, &a, 1);
    k_audio_note(&a, 0, 60, 100, 1);
    k_audio_render(&a, (float[2 * 128]){0}, 128);
    render(&a, 22000);
    uint64_t one_count = a.triggers[0], one_at = a.last_trigger_frame[0];
    setup(&p, &b, 1);
    k_audio_note(&b, 0, 60, 100, 1);
    render(&b, 22128);
    assert(b.triggers[0] == one_count && b.last_trigger_frame[0] == one_at);

    puts("arp: patterns, live, sequencer, clocks and buffer partition OK");
    return 0;
}

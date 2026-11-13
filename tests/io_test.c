/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* the input layer (kreisel_io.c): MIDI bytes read whole, through a table; keys through a table */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kreisel.h"
#include "kreisel_io.h"

static KApp a;
static uint8_t sent[8][3];
static int nsent;
static int nclock, nstart, nstop, first_rt;   /* the realtime bytes out: clocks, Starts, Stops; the first one seen */
static void cap(const uint8_t *m, int n)
{
    if (n == 1) {   /* (the clock's: counted, not kept) */
        if (!first_rt) first_rt = m[0];
        nclock += m[0] == 0xF8; nstart += m[0] == 0xFA; nstop += m[0] == 0xFC;
        return;
    }
    if (nsent < 8 && n == 3) { sent[nsent][0] = m[0]; sent[nsent][1] = m[1]; sent[nsent][2] = m[2]; }
    nsent++;
}
static void midi(int port, const uint8_t *b, int n) { k_io_midi(&a, port, b, n, 1000); }

int main(void)
{
    k_init(&a);
    k_io_midi_map(k_io_map_fm1, k_io_map_fm1_n);
    {   /* a note on, then one more by running status, then off as a note on of velocity 0 */
        const uint8_t m[] = {0x90, 60, 100, 64, 90, 60, 0};
        midi(1, m, (int)sizeof m);
        assert(a.midi_on[64] && !a.midi_on[60]);
    }
    {   /* a realtime byte in the middle of a message does not break it; a note off */
        const uint8_t m[] = {0x90, 67, 0xF8, 80, 0x80, 67, 0};
        midi(1, m, (int)sizeof m);
        assert(!a.midi_on[67]);
        const uint8_t on[] = {0x90, 67, 0xF8, 80};
        midi(1, on, (int)sizeof on); assert(a.midi_on[67]);
    }
    {   /* SysEx skipped whole: its data no note */
        const uint8_t m[] = {0xF0, 0x7D, 0x90, 0xF7};   /* (0x90 here ends the SysEx as a status; its data: none) */
        const uint8_t s[] = {0xF0, 0x7D, 0x41, 0x45, 0x42, 0xF7, 0x48, 0x46};   /* (the data after F7: no status, skipped) */
        midi(2, m, (int)sizeof m); midi(2, s, (int)sizeof s);
        assert(!a.midi_on[0x41] && !a.midi_on[0x45] && !a.midi_on[0x48]);
    }
    {   /* a message split across two calls, each port its own place */
        const uint8_t p1[] = {0x90, 50}, p2[] = {0x90, 52, 70}, p1b[] = {90};
        midi(1, p1, 2); midi(2, p2, 3); midi(1, p1b, 1);
        assert(a.midi_on[50] && a.midi_on[52]);
    }
    {   /* CC 102 play, CC 100 a page (the FM-1's table) */
        const uint8_t play[] = {0xB0, 102, 127}, page[] = {0xB3, 100, K_PAGE_FX};
        assert(!a.playing); midi(1, play, 3); assert(a.playing);
        midi(1, page, 3); assert(a.page == K_PAGE_FX);
    }
    {   /* no table for keys: none taken; the Zero's: F up, K the value, 2 SEQ, 1 HOME, 0 every page */
        assert(!k_io_key(&a, 'f', 1, 0, 0));
        k_io_key_map(k_io_map_zero, k_io_map_zero_n);
        k_open_page(&a, K_PAGE_MIXER);
        {   int lv = a.params[0][K_PAGE_MIXER][0];
            assert(k_io_key(&a, 'k', 1, 1, 0) && a.params[0][K_PAGE_MIXER][0] == lv + 1);   /* (held: repeats) */
            assert(k_io_key(&a, KIO_K_PGDN, 1, 0, 0) && a.params[0][K_PAGE_MIXER][0] == lv); }
        assert(k_io_key(&a, '2', 1, 0, 10) && (a.page == K_PAGE_ROLL || a.page == K_PAGE_DRUM));
        assert(k_io_key(&a, '2', 0, 0, 20));   /* (its release: the table's, nothing) */
        assert(k_io_key(&a, '1', 1, 0, 30) && a.page == K_PAGE_MIXER);
        assert(k_io_key(&a, '0', 1, 0, 40) && a.menu_open);
        assert(k_io_key(&a, '0', 1, 1, 50) && a.menu_open);   /* (a held 0 does not repeat) */
        assert(!k_io_key(&a, 'q', 1, 0, 60));   /* (not in the table: the platform's) */
        a.menu_open = 0; a.track = 0; k_open_page(&a, K_PAGE_ROLL); a.pattern[0] = 0;   /* , . the pattern on SEQ */
        assert(k_io_key(&a, '.', 1, 0, 70) && a.pattern[0] == 1);
        assert(k_io_key(&a, ',', 1, 0, 80) && a.pattern[0] == 0);
        {   /* r REC, u undo, [ ] the octave, g the settings, s SAVE (the keyboard alone: the audit's A-2) */
            const int rec = a.recording, oct = a.octave;
            a.menu_open = 0; k_open_page(&a, K_PAGE_ROLL);
            assert(k_io_key(&a, 'r', 1, 0, 100) && a.recording == !rec); k_io_key(&a, 'r', 1, 0, 101);
            assert(k_io_key(&a, ']', 1, 0, 102) && a.octave == oct + 1); k_io_key(&a, '[', 1, 0, 103); assert(a.octave == oct);
            assert(k_io_key(&a, 'u', 1, 0, 104));
            assert(k_io_key(&a, 'g', 1, 0, 105) && a.settings_open); k_io_key(&a, 'g', 1, 0, 106); assert(!a.settings_open);
            assert(k_io_key(&a, 's', 1, 0, 107) && a.save_open); k_dispatch(&a, (KAction){K_ACT_BACK, 0, 0, 108}); assert(!a.save_open);
        }
        {   /* K on HOME's tile of track 3: its level (the track in hand kept) */
            int f, lv;
            k_open_page(&a, K_PAGE_HOME); a.track = 0; lv = a.params[2][K_PAGE_MIXER][0];
            for (f = 0; f < a.focus_count; f++) if (a.focus[f].kind == K_NODE_TRACK && a.focus[f].index == 2) a.focused = f;
            assert(a.focus[a.focused].kind == K_NODE_TRACK);
            assert(k_io_key(&a, 'l', 1, 0, 90) && a.params[2][K_PAGE_MIXER][0] == lv - 1 && a.track == 0);
        }
    }
    {   /* a computer's table (the SDL builds): notes, every CC to the core's (CC 7 the level of the track in hand) */
        const uint8_t cc[] = {0xB0, 7, 0}, on[] = {0x91, 70, 90};
        k_init(&a); k_io_midi_map(k_io_map_desk, k_io_map_desk_n);
        midi(1, cc, 3); assert(a.params[a.track][K_PAGE_MIXER][0] == 0);
        midi(1, on, 3); assert(a.midi_on[70]);
        { const uint8_t up[] = {0xE0, 0x7F, 0x7F}, mid[] = {0xE0, 0, 0x40}; midi(1, up, 3); assert(a.bend[a.track] == 8191); midi(1, mid, 3); assert(a.bend[a.track] == 0); }   /* (pitch bend: the track in hand) */
        { const uint8_t page[] = {0xB0, 100, K_PAGE_FX}; const int pg = a.page; midi(1, page, 3); assert((int)a.page == pg); }   /* (CC 100: the FM-1's alone) */
    }
#if K_WITH_CONNECT
    {   /* the Connect tab's channel: the others not heard; realtime always */
        const uint8_t c1[] = {0x90, 40, 90}, c2[] = {0x91, 41, 90};
        k_init(&a); k_io_midi_map(k_io_map_desk, k_io_map_desk_n); a.set_v[K_SET_MIDI_CH] = 2;
        midi(1, c1, 3); midi(1, c2, 3);
        assert(!a.midi_on[40] && a.midi_on[41]);
        assert(!strcmp(k_setting_value(&a, K_SET_MIDI_CH), "2") && !strcmp(k_setting_tab_name(k_setting_tab(K_SET_MIDI_IN)), "Connect"));
        a.clock_src = 0; assert(!strcmp(k_setting_value(&a, K_SET_MIDI_CLOCK), "Internal"));
    }
#endif
    {   /* MIDI out: nothing without a sender; a note on, its off on the track's channel */
        k_io_note_out(0, 60, 100); assert(nsent == 0);
        k_io_send_to(cap);
        k_io_note_out(2, 60, 100); k_io_note_out(2, 60, 0); k_io_note_out(0, 200, 100);
        assert(nsent == 2 && sent[0][0] == 0x92 && sent[0][1] == 60 && sent[0][2] == 100 && sent[1][0] == 0x82 && sent[1][2] == 0);
        k_io_send_to(0);
    }
    {   /* the sequencer's notes out: a note of two steps on a synth track, its off two steps on; stopped, nothing sounding */
        static KAudio au;
        static float buf[2 * 512];
        int t = 0, i, k, on = -1, off = -1, frames = 0;
        k_init(&a);
        while (t < K_TRACKS && k_is_drum(&a, t)) t++;
        assert(t < K_TRACKS && k_roll_add(&a, t, 0, 0, 61, 2, 90) >= 0);
        k_audio_init(&au, 44100);
        k_io_send_to(cap); nsent = 0;
        k_io_send_audio(&au);   /* (a sender: the audio keeps its notes) */
        a.playing = 1; k_audio_sync(&au, &a);
        for (i = 0; i < 40; i++) {   /* ~0.46 s: 120 bpm, a step 125 ms */
            k_audio_render(&au, buf, 512); frames += 512;
            k_io_send_audio(&au);
            for (k = 0; k < nsent && k < 8; k++) if (sent[k][1] == 61) {
                if (on < 0 && sent[k][0] == (0x90 | t) && sent[k][2] == 90) on = frames;
                if (off < 0 && sent[k][0] == (0x80 | t)) off = frames;
            }
            nsent = 0;
        }
        assert(on > 0 && off - on > 8000 && off - on < 14000);   /* (two steps: ~11000 frames) */
        a.playing = 0; k_audio_sync(&au, &a);
        for (i = 0; i < 4; i++) { k_audio_render(&au, buf, 512); k_io_send_audio(&au); }
        for (k = 0; k < 16; k++) assert(!au.mo_hold[k].left);   /* (stopped: every note sent, off) */
        k_io_send_to(0);
    }
    {   /* an arp's track: the notes the arp plays go out (not the written one), each off as its voice lets go; all off at Stop */
        static KAudio au;
        static float buf[2 * 512];
        int i, k, t = 0, ons = 0, offs = 0;
        k_init(&a);
        while (t < K_TRACKS && k_is_drum(&a, t)) t++;
        assert(t < K_TRACKS && k_roll_add(&a, t, 0, 0, 60, 16, 100) >= 0 && k_roll_add(&a, t, 0, 0, 64, 16, 100) >= 0);
        a.params[t][K_PAGE_ARP][0] = 1;   /* (a mode: UP) */
        k_audio_init(&au, 44100);
        k_io_send_to(cap); nsent = 0;
        k_io_send_audio(&au);
        a.playing = 1; k_audio_sync(&au, &a);
        for (i = 0; i < 90; i++) {   /* ~1 s */
            k_audio_render(&au, buf, 512);
            k_io_send_audio(&au);
            for (k = 0; k < nsent && k < 8; k++) {
                if (sent[k][0] == (0x90 | t)) ons++;
                if (sent[k][0] == (0x80 | t)) offs++;
            }
            nsent = 0;
        }
        a.playing = 0; k_audio_sync(&au, &a);
        for (i = 0; i < 4; i++) {
            k_audio_render(&au, buf, 512); k_io_send_audio(&au);
            for (k = 0; k < nsent && k < 8; k++) if (sent[k][0] == (0x80 | t)) offs++;
            nsent = 0;
        }
        {   /* a key of the bed on an arp's track: its own note does not go out (the arp's do, from the audio) */
            int before = nsent;
            k_init(&a); a.track = t; a.params[t][K_PAGE_ARP][0] = 1; a.page = K_PAGE_SOUND;
            k_dispatch(&a, (KAction){K_ACT_KEY, 0, 1, 0});
            assert(nsent == before);
            k_dispatch(&a, (KAction){K_ACT_KEY, 0, 0, 1});
            a.params[t][K_PAGE_ARP][0] = 0;
            k_dispatch(&a, (KAction){K_ACT_KEY, 0, 1, 2});
            assert(nsent == before + 2 && sent[before + 1][0] == (0x90 | t));   /* (no arp: the key's note; the off before it) */
            k_dispatch(&a, (KAction){K_ACT_KEY, 0, 0, 3});
            nsent = 0;
        }
        printf("io_test: arp out %d on, %d off\n", ons, offs);
        assert(ons >= 4 && offs == ons);   /* (the written chord alone would be two) */
        for (k = 0; k < K_AUDIO_VOICES; k++) assert(!au.mo_arp[t][k]);
        k_io_send_to(0);
    }
    {   /* the clock out: Start, 24 a quarter from the first step (swing or not), Stop; none on an external clock */
        static KAudio au;
        static float buf[2 * 441];
        int i;
        k_init(&a); a.swing = 60;
        k_audio_init(&au, 44100);
        k_io_send_to(cap); nsent = nclock = nstart = nstop = first_rt = 0;
        k_io_send_audio(&au);
        a.playing = 1; k_audio_sync(&au, &a);
        for (i = 0; i < 100; i++) { k_audio_render(&au, buf, 441); k_io_send_audio(&au); }   /* 1 s at 120 bpm: 2 quarters, 48 clocks */
        assert(first_rt == 0xFA && nstart == 1 && nstop == 0 && nclock >= 48 && nclock <= 49);
        a.bpm = 60; k_audio_sync(&au, &a); nclock = 0;
        for (i = 0; i < 100; i++) { k_audio_render(&au, buf, 441); k_io_send_audio(&au); }   /* 1 s at 60 bpm: 24 */
        assert(nclock >= 23 && nclock <= 25);
        a.playing = 0; k_audio_sync(&au, &a); nclock = 0;
        for (i = 0; i < 10; i++) { k_audio_render(&au, buf, 441); k_io_send_audio(&au); }
        assert(nstop == 1 && nclock == 0);
        k_init(&a); a.clock_src = 1;   /* following an external clock: nothing of its own goes out */
        k_audio_init(&au, 44100); nclock = nstart = nstop = 0;
        k_io_send_audio(&au);
        a.playing = 1; k_audio_sync(&au, &a);
        for (i = 0; i < 20; i++) { k_audio_render(&au, buf, 441); k_io_send_audio(&au); }
        assert(nstart == 0 && nclock == 0);
        k_io_send_to(0);
    }
    printf("io_test: ok\n");
    return 0;
}

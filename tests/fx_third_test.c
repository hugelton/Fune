/* SPDX-License-Identifier: MPL-2.0
 * The effects' third values (2026-11-12): each changes the sound, a switch switches (off at its middle and below), SYNC follows the
 * song's tempo, GAIN's PAN balances the track, the values survive a song, the FM-1's knobs are the three values and MIX. */
#include "kreisel_audio.h"
#include "kreisel_fx.h"
#include "kreisel_song.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK 32u
enum { CHO = 2, DLY = 3, REV = 4, GAIN = 5, FUZZ = 7, WAH = 8, CRUSH = 9, PHASER = 10, TREMOLO = 11, WOBBLE = 12, NOISE = 13, PITCH = 14 };

static uint8_t mem[1 << 18] __attribute__((aligned(8)));
static uint8_t state[1024] __attribute__((aligned(8)));

/* the effect alone (its backend, as the host runs it): a test signal, its third value set to `third` (< 0: left at its start) */
static uint64_t render(int type, int32_t third, int bpm, unsigned blocks)
{
    const KEffectDSP *d = k_effect_at(type)->dsp;
    const size_t m = d->mem_size ? d->mem_size(K_AUDIO_FS) : 0;
    uint64_t h = 1469598103934665603ull;
    uint32_t rng = 99u, i = 0;
    int32_t buf[BLOCK];
    memset(state, 0, sizeof state); memset(mem, 0, sizeof mem);
    d->init(state, m ? mem : 0, K_AUDIO_FS);
    if (bpm) d->set_param(state, 0xF0u, bpm);   /* PD_HOST_TEMPO */
    if (type == GAIN) d->set_param(state, 0, 30000);   /* (loud: the clip has something to do) */
    if (third >= 0) d->set_param(state, 2, third);
    for (unsigned b = 0; b < blocks; b++) {
        for (unsigned j = 0; j < BLOCK; j++, i++) {
            rng = rng * 1664525u + 1013904223u;
            buf[j] = (i / 3000u) & 1u ? (int32_t)(rng >> 17) - 16384 : (int32_t)((i * 517u) & 0xFFFFu) - 32768;
        }
        d->process(state, buf, buf, BLOCK);
        for (unsigned j = 0; j < BLOCK; j++) { h ^= (uint32_t)buf[j]; h *= 1099511628211ull; }
    }
    return h;
}
static int is_switch(int type) { return type == DLY || type == GAIN || type == WAH || type == TREMOLO || type == WOBBLE; }

/* the first block index (of 32 frames) an impulse comes back at, through the host's insert (a synced delay) */
static int echo_at(KApp *app, KAudio *audio)
{
    int32_t b[BLOCK];
    k_fx_forget(); k_audio_init(audio, K_AUDIO_FS); k_audio_sync(audio, app);
    for (int blk = 0; blk < 3000; blk++) {
        memset(b, 0, sizeof b);
        if (!blk) b[0] = 40000;
        k_fx_process_track(0, b, BLOCK);
        for (int j = 0; j < (int)BLOCK; j++) if (blk && b[j]) return blk * (int)BLOCK + j;
    }
    return -1;
}

int main(void)
{
    static KApp app, loaded;
    static KAudio audio;
    const int third[] = {CHO, DLY, REV, GAIN, FUZZ, WAH, CRUSH, PHASER, TREMOLO, WOBBLE, NOISE, PITCH};
    static const char *const label[] = {"DPTH", "SYNC", "TONE", "PAN", "TONE", "MODE", "TONE", "FDBK", "WAVE", "SYNC", "TONE", "FINE"};
    k_plugin_set_caps(NULL);

    /* the catalogue: three values each (GAIN's PAN its second), their labels */
    for (unsigned k = 0; k < sizeof third / sizeof third[0]; k++) {
        const KEngine *e = k_effect_at(third[k])->desc;
        assert(e->nparams == 3);
        assert(!strcmp(e->edit[third[k] == GAIN ? 1 : 2].label, label[k]));
        assert(e->edit[2].def == 50 || e->edit[2].fmt == KF_ONOFF);
    }
    assert(!strcmp(k_effect_at(GAIN)->desc->edit[2].label, "SAT") && k_effect_at(GAIN)->desc->edit[2].fmt == KF_ONOFF);
    assert(k_effect_at(GAIN)->desc->edit[1].fmt == KF_CENTER && k_effect_at(PITCH)->desc->edit[2].fmt == KF_CENTER);

    /* each third value changes the sound (GAIN's PAN is the host's: below); at its middle as it was started */
    for (unsigned k = 0; k < sizeof third / sizeof third[0]; k++) {
        const int t = third[k];
        const uint64_t start = render(t, -1, 0, 300), mid = render(t, 16384, 0, 300), top = render(t, 32767, 0, 300);
        assert(start == mid);
        if (t == DLY || t == WOBBLE) continue;   /* (synced: at another tempo than the start's, below) */
        if (t != GAIN) assert(top != mid);
        if (!is_switch(t)) assert(render(t, 0, 0, 300) != mid);   /* both ways from the middle */
        else assert(render(t, 0, 0, 300) == mid && render(t, 16385, 0, 300) == top);   /* a switch: off to the middle, on above */
    }
    assert(render(GAIN, 32767, 0, 300) != render(GAIN, 0, 0, 300));   /* GAIN's SAT */
    /* SYNC: off, the tempo is nothing to it; on, it follows it */
    assert(render(DLY, 0, 90, 2000) == render(DLY, 0, 150, 2000) && render(WOBBLE, 0, 90, 300) == render(WOBBLE, 0, 150, 300));
    assert(render(DLY, 32767, 90, 2000) != render(DLY, 32767, 150, 2000));
    assert(render(WOBBLE, 32767, 90, 300) != render(WOBBLE, 32767, 150, 300));
    assert(render(DLY, 32767, 120, 2000) != render(DLY, 0, 120, 2000));

    /* a synced delay through the host: TIME 1/4 (index 9: 59 .. 62 of 100) comes back a beat later, at the song's tempo */
    k_init(&app);
    app.params[0][K_PAGE_FX][0] = DLY;
    app.params[0][K_PAGE_FX][3] = 100;
    app.params[0][K_PAGE_FX][1] = 60;
    assert(k_fx_div_index(60) == 9);
    app.params[0][K_PAGE_FX][2] = 0;   /* (no feedback) */
    app.bpm = 120;
    {
        const int free_at = echo_at(&app, &audio);
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] = 100;   /* SYNC on */
        assert(echo_at(&app, &audio) == 22050);                    /* a beat at 120 */
        app.bpm = 100;
        assert(echo_at(&app, &audio) == 26460);                    /* at 100 */
        app.bpm = 60;                                              /* a beat at 60 is past the line (1 s): halved, an eighth */
        assert(echo_at(&app, &audio) == 22050);
        app.params[0][K_PAGE_FX][1] = 100;                         /* 1/1 at 60: 4 s, halved to 0.5 s */
        assert(echo_at(&app, &audio) == 22050);
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] = 50;     /* off (the default): free again */
        app.params[0][K_PAGE_FX][1] = 60;
        assert(echo_at(&app, &audio) == free_at);
    }
    {   /* the tempo changing while it plays reaches the insert */
        int32_t b[BLOCK];
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] = 100;
        app.bpm = 120;
        k_fx_forget(); k_audio_init(&audio, K_AUDIO_FS); k_audio_sync(&audio, &app);
        app.bpm = 150;
        k_audio_sync(&audio, &app);
        for (int blk = 0, at = -1; blk < 1000 && at < 0; blk++) {
            memset(b, 0, sizeof b);
            if (!blk) b[0] = 40000;
            k_fx_process_track(0, b, BLOCK);
            for (int j = 0; j < (int)BLOCK && at < 0; j++) if (blk && b[j]) at = blk * (int)BLOCK + j;
            if (at >= 0) assert(at == 17640);   /* a beat at 150 */
        }
    }

    /* the page: SYNC on shows TIME as a division; a switch's detent goes to on or off; the FM-1's K1..K3 are the values, K4 MIX */
    {
        KParamProps p;
        KParamRef r;
        int slot, param;
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = DLY;
        k_set_layout(&app, K_LAY_FX_ALGO);
        k_open_page(&app, K_PAGE_FX);
        for (int i = 0; i < 3; i++) { k_fx_item(&app, i, &slot, &param); assert(slot == 0 && param == i); }
        k_fx_item(&app, 3, &slot, &param); assert(param == -2);
        assert(k_page_count(&app, K_PAGE_FX) == 8);   /* (no rows past the first: three values fit) */
        r = k_param_ref(&app, K_PAGE_FX, 0);
        assert(k_param_props(&app, r, &p) && p.d.fmt == KF_PCT);
        r = k_param_ref(&app, K_PAGE_FX, 2);
        assert(k_param_props(&app, r, &p) && p.d.fmt == KF_ONOFF && !k_switch_on(&p.d, k_param_get(&app, K_PAGE_FX, 2)));
        k_ref_turn(&app, r, 1);
        assert(app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] == 100 && k_switch_on(&p.d, 100));
        r = k_param_ref(&app, K_PAGE_FX, 0);
        assert(k_param_props(&app, r, &p) && p.d.fmt == KF_FXDIV);
        k_ref_turn(&app, k_param_ref(&app, K_PAGE_FX, 2), -1);
        assert(app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] == 0);
        assert(k_param_props(&app, r, &p) && p.d.fmt == KF_PCT);
        k_ref_reset(&app, k_param_ref(&app, K_PAGE_FX, 2));
        assert(app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] == 50);   /* reset: off */
        app.params[0][K_PAGE_FX][0] = GAIN;
        for (int i = 0; i < 3; i++) { k_fx_item(&app, i, &slot, &param); assert(param == i); }   /* GAIN PAN SAT */
        {
            const KParamDesc *d = &k_effect_at(WAH)->desc->edit[2];
            assert(!strcmp(k_switch_name(d, 50), "Env") && !strcmp(k_switch_name(d, 51), "LFO"));
            d = &k_effect_at(GAIN)->desc->edit[2];
            assert(!strcmp(k_switch_name(d, 0), "Off") && !strcmp(k_switch_name(d, 100), "On"));
        }
    }

    /* GAIN's PAN: a balance after the inserts, by MIX; centred, none */
    {
        int32_t l, r;
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = GAIN;
        app.params[0][K_PAGE_FX][3] = 100;
        k_fx_forget(); k_audio_init(&audio, K_AUDIO_FS); k_audio_sync(&audio, &app);
        assert(!k_fx_track_balance(0, &l, &r));
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 1)] = 100;   /* right */
        k_audio_sync(&audio, &app);
        assert(k_fx_track_balance(0, &l, &r) && l == 0 && r == 4096);
        app.params[0][K_PAGE_FX][3] = 50;                          /* half mixed: half the way */
        k_audio_sync(&audio, &app);
        assert(k_fx_track_balance(0, &l, &r) && l == 2048 && r == 4096);
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 1)] = 0;      /* left */
        k_audio_sync(&audio, &app);
        assert(k_fx_track_balance(0, &l, &r) && l == 4096 && r == 2048);
        assert(!k_fx_track_balance(1, &l, &r));
        {   /* heard: a note on track 1 panned hard right by GAIN's PAN has no left */
            static int32_t out[2 * 256];
            long left = 0, right = 0;
            app.params[0][K_PAGE_FX][3] = 100;
            app.params[0][K_PAGE_FX][k_fx_store_param(0, 1)] = 100;
            k_audio_sync(&audio, &app);
            k_audio_note(&audio, 0, 60, 100, 1);
            for (int n = 0; n < 40; n++) {
                k_audio_render_s32(&audio, out, 256);
                for (int j = 0; j < 256; j++) { left += out[2 * j] < 0 ? -out[2 * j] : out[2 * j]; right += out[2 * j + 1] < 0 ? -out[2 * j + 1] : out[2 * j + 1]; }
            }
            assert(right > 1000 && left == 0);
        }
    }

    /* a song keeps the third values; one from before them (no such values) opens them at their middle */
    {
        uint8_t bytes[16384];
        size_t size;
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = DLY;
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] = 100;   /* SYNC on */
        app.params[0][K_PAGE_FX][4] = GAIN;
        app.params[0][K_PAGE_FX][k_fx_store_param(1, 1)] = 20;    /* PAN */
        app.params[0][K_PAGE_FX][k_fx_store_param(1, 2)] = 100;   /* SAT */
        app.params[1][K_PAGE_FX][0] = PITCH;
        app.params[1][K_PAGE_FX][k_fx_store_param(0, 2)] = 63;    /* FINE +13 ct */
        size = k_song_save(&app, bytes, sizeof bytes);
        assert(size > 0);
        k_init(&loaded);
        assert(k_song_load(&loaded, bytes, size) == K_SONG_OK);
        assert(loaded.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] == 100);
        assert(loaded.params[0][K_PAGE_FX][k_fx_store_param(1, 1)] == 20 && loaded.params[0][K_PAGE_FX][k_fx_store_param(1, 2)] == 100);
        assert(loaded.params[1][K_PAGE_FX][k_fx_store_param(0, 2)] == 63);
        app.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] = 50;   /* as a song from before SYNC: nothing stored for it */
        app.params[0][K_PAGE_FX][k_fx_store_param(1, 1)] = 50;
        app.params[0][K_PAGE_FX][k_fx_store_param(1, 2)] = 50;
        app.params[1][K_PAGE_FX][k_fx_store_param(0, 2)] = 50;
        size = k_song_save(&app, bytes, sizeof bytes);
        k_init(&loaded);
        loaded.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] = 77;   /* (what was there before the load goes) */
        assert(k_song_load(&loaded, bytes, size) == K_SONG_OK);
        assert(loaded.params[0][K_PAGE_FX][k_fx_store_param(0, 2)] == 50 && loaded.params[1][K_PAGE_FX][k_fx_store_param(0, 2)] == 50);
    }
    puts("the effects' third values: each heard, switches switch, SYNC on the tempo, GAIN's PAN, songs keep them");
    return 0;
}

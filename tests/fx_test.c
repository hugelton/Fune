/* SPDX-License-Identifier: MPL-2.0
 * All built-in Pendel effects: metadata, full parameter UI, real-time bounds,
 * silence recovery and bit-exact bypass. */
#include "kreisel_audio.h"
#include "kreisel_fx.h"
#include "kreisel_song.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N 32

static void bounds(int type, const int32_t *buf)
{
    for (int i = 0; i < N; i++) {
        if (buf[i] < -262140 || buf[i] > 262140) {   /* (the inserts run 12 dB down: their rail +/-65535 comes back x 4) */
            fprintf(stderr, "FX %d overflow at %d: %d\n", type, i, (int)buf[i]);
            assert(0);
        }
    }
}
static int32_t magnitude(int32_t v) { return v < 0 ? -v : v; }

int main(void)
{
    KApp app;
    KAudio audio;
    k_plugin_set_caps(NULL);
    assert(k_effect_count() == K_FX_TYPES - 1);
    assert(k_effect_at(0) == NULL && k_effect_at(-1) == NULL);
    for (int type = 1; type <= k_effect_count(); type++) {
        const KPlugin *p = k_effect_at(type);
        int32_t b[N], original[N];
        int n;
        assert(p && p->kind == K_PLUG_EFFECT && p->dsp);
        assert(p->desc && p->desc->uid && p->desc->nparams > 0);
        assert(p->desc->nparams <= 2 + K_FX_EXTRA_PER_SLOT);
        assert(p->fs == K_AUDIO_FS && p->ram > 0 && p->cost > 0);
        assert(k_effect_find(p->desc->uid) == type);
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = type;
        app.params[0][K_PAGE_FX][3] = 100;
        /* Explicitly remove hiss, feedback and saturation for the silence test. */
        for (int k = 0; k < p->desc->nparams; k++)
            app.params[0][K_PAGE_FX][k_fx_store_param(0, k)] = 0;
        n = p->desc->nparams > 2 ? (p->desc->nparams - 2 + 3) / 4 : 0;
        assert(k_page_count(&app, K_PAGE_FX) == 8 + n * 4);
        for (int item = 0; item < k_page_count(&app, K_PAGE_FX); item++) {
            int slot, param, idx = k_fx_item(&app, item, &slot, &param);
            assert(idx >= 0 && idx < K_FX_ITEMS);
            if (param >= 0) {
                const KPlugin *q = k_effect_at(app.params[0][K_PAGE_FX][slot * 4]);
                assert(q && param < q->desc->nparams);
                assert(k_param(&app, K_PAGE_FX, item) == &app.params[0][K_PAGE_FX][idx]);
            }
        }
        k_audio_init(&audio, K_AUDIO_FS);
        k_audio_sync(&audio, &app);
        for (int j = 0; j < N; j++) b[j] = 0;
        k_fx_process_track(0, b, N);
        for (int j = 0; j < N; j++) {
            if (b[j] != 0) fprintf(stderr, "FX %d not silent at startup: %d\n", type, (int)b[j]);
            assert(b[j] == 0);
        }
        /* An impulse followed by more than four seconds of silence must settle.
         * This also exercises delay/reverb tails and fixed-size delay memories. */
        memset(b, 0, sizeof b);
        b[0] = 12000;
        k_fx_process_track(0, b, N);
        bounds(type, b);
        for (int block = 0; block < 6000; block++) {
            memset(b, 0, sizeof b);
            k_fx_process_track(0, b, N);
            bounds(type, b);
        }
        for (int j = 0; j < N; j++) {
            if (magnitude(b[j]) > 8)
                fprintf(stderr, "FX %d did not decay to silence: %d\n", type, (int)b[j]);
            assert(magnitude(b[j]) <= 8);   /* (2 at the inserts' own level) */
        }
        /* Exercise every parameter at its upper bound, including the extended
         * bank (AMP has seven controls). The host must bound the output. */
        for (int k = 0; k < p->desc->nparams; k++)
            app.params[0][K_PAGE_FX][k_fx_store_param(0, k)] = 100;
        k_audio_sync(&audio, &app);
        for (int block = 0; block < 40; block++) {
            for (int j = 0; j < N; j++)
                b[j] = ((j + block) & 1) ? 30000 : -30000;
            k_fx_process_track(0, b, N);
            bounds(type, b);
        }
        /* Dry path must be exactly preserved at zero mix. */
        app.params[0][K_PAGE_FX][3] = 0;
        k_audio_sync(&audio, &app);
        for (int j = 0; j < N; j++) b[j] = original[j] = (j - 16) * 1200;
        k_fx_process_track(0, b, N);
        assert(memcmp(b, original, sizeof b) == 0);
    }
    /* A declared device with too little RAM never starts an FX instance. */
    {
        const KDeviceCaps tiny = {K_AUDIO_FS, 1, 1, K_AUDIO_VOICES, 0};
        k_plugin_set_caps(&tiny);
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = 1;
        app.params[0][K_PAGE_FX][3] = 100;
        k_audio_init(&audio, K_AUDIO_FS);
        k_audio_sync(&audio, &app);
        int32_t b[N], original[N];
        for (int j = 0; j < N; j++) b[j] = original[j] = (j - 16) * 1000;
        k_fx_process_track(0, b, N);
        assert(memcmp(b, original, sizeof b) == 0);
        k_plugin_set_caps(NULL);
    }
    /* Both insert slots can expose independent extended parameter rows.
     * Those values survive a song round-trip at fixed legacy-compatible indexes. */
    {
        uint8_t bytes[16384];
        KApp loaded;
        size_t size;
        int slot, param;
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = 1;  /* DRIVE has three parameters */
        app.params[0][K_PAGE_FX][4] = 15; /* AMP has seven parameters */
        app.params[0][K_PAGE_FX][16] = 42; /* AMP's third parameter */
        k_open_page(&app, K_PAGE_FX);
        assert(k_page_count(&app, K_PAGE_FX) == 20);
        assert(k_fx_item(&app, 12, &slot, &param) == 16);
        assert(slot == 1 && param == 2);
        for (int k = 0; k < 3; k++)
            k_dispatch(&app, (KAction){K_ACT_FOCUS_NEXT, 0, 0, (uint32_t)k + 1});
        assert(app.win == 12);
        k_dispatch(&app, (KAction){K_ACT_PARAM_DELTA, 0, 10, 5});
        assert(app.params[0][K_PAGE_FX][16] == 50);   /* (AMP's BASS: - 0 +, it stops at its 0 on the way across) */
        k_dispatch(&app, (KAction){K_ACT_PARAM_DELTA, 0, 2, 6});
        assert(app.params[0][K_PAGE_FX][16] == 52);
        size = k_song_save(&app, bytes, sizeof bytes);
        assert(size > 0);
        k_init(&loaded);
        assert(k_song_load(&loaded, bytes, size) == K_SONG_OK);
        assert(loaded.params[0][K_PAGE_FX][0] == 1 &&
               loaded.params[0][K_PAGE_FX][4] == 15 &&
               loaded.params[0][K_PAGE_FX][16] == 52);
    }
    {   /* another track's insert changed: this track's delay goes on (only the changed slot is made anew) */
        int32_t b[N];
        long after = 0;
        k_plugin_set_caps(NULL);
        k_init(&app);
        app.params[0][K_PAGE_FX][0] = 3;   /* DLY */
        app.params[0][K_PAGE_FX][3] = 100;
        k_audio_init(&audio, K_AUDIO_FS);
        k_audio_sync(&audio, &app);
        memset(b, 0, sizeof b); b[0] = 40000;
        k_fx_process_track(0, b, N);
        for (int blk = 0; blk < 200; blk++) { memset(b, 0, sizeof b); k_fx_process_track(0, b, N); }
        app.params[3][K_PAGE_FX][0] = 4;   /* REV on track 4 */
        app.params[3][K_PAGE_FX][3] = 100;
        k_audio_sync(&audio, &app);
        for (int blk = 0; blk < 2000; blk++) {
            memset(b, 0, sizeof b); k_fx_process_track(0, b, N);
            for (int j = 0; j < N; j++) after += magnitude(b[j]);
        }
        assert(after > 0);   /* the echoes still come */
    }
    {   /* the effect picked: one command for ALGORITHM, < > and a list; stops or goes round; the preset marked only by a change */
        static KApp f;
        const int top = k_effect_count();
        k_init(&f);
        assert(!k_fx_type_step(&f, 0, -1, 0) && f.params[0][K_PAGE_FX][0] == 0 && !f.preset_dirty[0]);   /* NONE: no lower */
        assert(k_fx_type_step(&f, 0, -1, 1) && f.params[0][K_PAGE_FX][0] == top && f.preset_dirty[0]);    /* round: the last */
        f.preset_dirty[0] = 0;
        assert(!k_fx_type_set(&f, 0, top) && !f.preset_dirty[0]);              /* the same again: nothing changed */
        assert(k_fx_type_step(&f, 0, 1, 1) && f.params[0][K_PAGE_FX][0] == 0);  /* round: NONE after the last */
        assert(k_fx_type_set(&f, 1, 999) && f.params[0][K_PAGE_FX][4] == top);  /* kept within the effects */
        assert(!k_fx_type_set(&f, 2, 1) && !k_fx_type_step(&f, -1, 1, 0));      /* no such slot */
        f.params[0][K_PAGE_FX][4] = 0;
        k_set_layout(&f, K_LAY_FX_ALGO); k_open_page(&f, K_PAGE_FX); f.win = 4;   /* ALGORITHM on slot 2's row */
        k_dispatch(&f, (KAction){K_ACT_TYPE_DELTA, 1, 0, 10});
        assert(f.params[0][K_PAGE_FX][4] == 1 && f.params[0][K_PAGE_FX][0] == 0);
    }
    puts("15 effects: bounded signal, silent start/end, full parameter banks, safe caps, bit-exact bypass");
    return 0;
}

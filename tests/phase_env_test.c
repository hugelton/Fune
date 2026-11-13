/* SPDX-License-Identifier: MPL-2.0
 * PHASE, as the CZ: three envelopes (DCO pitch, DCW wave, DCA level), each line taking its own amount of each.
 * Tests the page (three rows), each envelope's audible effect, the line amounts, glide, and song persistence. */
#include "kreisel_audio.h"
#include "kreisel_song.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define SAMPLES 2048
static float dry[2*SAMPLES], shaped[2*SAMPLES], held[2*SAMPLES];
/* PHASE's slots (params[SOUND][4 + k]) */
enum { PITCH = 4, GLIDE, DTN, LINE, WAVE1, DCO1, DCW1, DCA1, WAVE2, DCO2, DCW2, DCA2 };
static void setup(KApp *p, int phase)
{
    k_init(p);
    p->sound[0] = phase;
    for (int i = 0; i < k_engine_at(phase)->nparams; i++)
        p->params[0][K_PAGE_SOUND][4 + i] = k_engine_at(phase)->edit[i].def;
    p->params[0][K_PAGE_SOUND][DCW1] = 60;
    p->params[0][K_PAGE_SOUND][DCW2] = 60;
    p->params[0][K_PAGE_ENV1][0] = 0;   /* DCW A */
    p->params[0][K_PAGE_ENV1][1] = 20;  /* DCW D */
    p->params[0][K_PAGE_ENV1][2] = 100; /* DCW S */
    p->params[0][K_PAGE_ENV1][3] = 30;  /* DCW R */
    p->params[0][K_PAGE_ENV2][0] = 0;   /* DCA A */
    p->params[0][K_PAGE_ENV2][1] = 20;  /* DCA D */
    p->params[0][K_PAGE_ENV2][2] = 100; /* DCA S */
    p->params[0][K_PAGE_ENV2][3] = 30;  /* DCA R */
    p->params[0][K_PAGE_ENV2][4] = 0;   /* DCO A */
    p->params[0][K_PAGE_ENV2][5] = 20;  /* DCO D */
    p->params[0][K_PAGE_ENV2][6] = 100; /* DCO S */
    p->params[0][K_PAGE_ENV2][7] = 30;  /* DCO R */
}
static double render(const KApp *p, float *dst, int skip, int note)
{
    KAudio audio;
    float tmp[2*512];
    double energy = 0;
    k_audio_init(&audio, K_AUDIO_FS);
    k_audio_sync(&audio, p);
    k_audio_note(&audio, 0, note, 127, 1);
    while(skip > 0) {
        int n = skip > 512 ? 512 : skip;
        k_audio_render(&audio, tmp, n);
        skip -= n;
    }
    k_audio_render(&audio, dst, SAMPLES);
    for (int i = 0; i < 2*SAMPLES; i++) {
        assert(isfinite(dst[i]) && fabsf(dst[i]) <= 1.0f);
        energy += (double)dst[i] * dst[i];
    }
    return energy;
}
static double changed(const float *a, const float *b)
{
    double difference = 0;
    for (int i = 0; i < 2*SAMPLES; i++) {
        double d = a[i]-b[i];
        difference += d*d;
    }
    return difference;
}
static int crossings(const float *a)   /* the left channel's rising zero crossings: its pitch */
{
    int n = 0;
    for (int i = 1; i < SAMPLES; i++) if (a[2 * (i - 1)] < 0.f && a[2 * i] >= 0.f) n++;
    return n;
}
int main(void)
{
    int phase = k_engine_find(K_UID('K','P','H','S'));
    KApp p, loaded;
    uint8_t buf[16384];
    size_t n;
    assert(phase >= 0);
    setup(&p, phase);
    /* one ENV page, three rows: DCO (ENV 2's 4 .. 7), DCW (ENV 1's A D S R), DCA (ENV 2's); ENV 2 opens its DCA row */
    k_open_page(&p, K_PAGE_ENV1);
    assert(k_page_count(&p, K_PAGE_ENV1) == 12);
    for (int k = 0; k < 4; k++) {
        assert(k_param(&p, K_PAGE_ENV1, k) == &p.params[0][K_PAGE_ENV2][4 + k]);
        assert(k_param(&p, K_PAGE_ENV1, 4 + k) == &p.params[0][K_PAGE_ENV1][k]);
        assert(k_param(&p, K_PAGE_ENV1, 8 + k) == &p.params[0][K_PAGE_ENV2][k]);
    }
    k_open_page(&p, K_PAGE_ENV2); assert(p.page == K_PAGE_ENV1 && p.win == 8);
    k_set_layout(&p, K_LAY_ENV_PAIR);
    assert(k_page_count(&p, K_PAGE_ENV1) == 12 && k_param(&p, K_PAGE_ENV1, 0) == &p.params[0][K_PAGE_ENV2][4]);   /* (wide: the same) */
    k_set_layout(&p, 0);

    /* DCW: a line's amount of the DCW envelope changes its timbre */
    assert(render(&p, shaped, 4096, 60) > 0.02);
    p.params[0][K_PAGE_SOUND][DCW1] = 0; p.params[0][K_PAGE_SOUND][DCW2] = 0;
    assert(render(&p, dry, 4096, 60) > 0.02);
    assert(changed(dry, shaped) > 0.01);
    p.params[0][K_PAGE_SOUND][DCW1] = 60; p.params[0][K_PAGE_SOUND][DCW2] = 60;

    /* the DCW ADSR alone: a slow attack, a different wave early on */
    p.params[0][K_PAGE_ENV1][0] = 100;
    assert(render(&p, held, 128, 60) > 0.001);
    assert(render(&p, shaped, 128, 60) > 0.001); /* deterministic restart */
    assert(changed(held, shaped) < 1e-6);
    p.params[0][K_PAGE_ENV1][0] = 0;
    assert(render(&p, dry, 128, 60) > 0.001);
    assert(changed(dry, shaped) > 0.01);

    /* DCA: its attack and sustain set the loudness; a line's DCA its level */
    double quick = render(&p, shaped, 0, 60);
    p.params[0][K_PAGE_ENV2][0] = 100;
    double slow = render(&p, dry, 0, 60);
    assert(quick > slow * 2.0 + 0.0001);
    p.params[0][K_PAGE_ENV2][0] = 0;
    p.params[0][K_PAGE_ENV2][2] = 0;
    double muted = render(&p, dry, 44100, 60);
    p.params[0][K_PAGE_ENV2][2] = 100;
    double sustained = render(&p, shaped, 44100, 60);
    assert(sustained > muted * 10.0 + 0.001);
    p.params[0][K_PAGE_SOUND][DCA1] = 0; p.params[0][K_PAGE_SOUND][DCA2] = 0;
    assert(render(&p, dry, 4096, 60) < 1e-6);   /* both lines' levels at 0: silence */
    p.params[0][K_PAGE_SOUND][DCA1] = 127; p.params[0][K_PAGE_SOUND][DCA2] = 0;

    /* DCO: a line's amount of the DCO envelope raises its pitch (an octave up at the top, held by the sustain) */
    p.params[0][K_PAGE_SOUND][DCW1] = 0;   /* (a plain wave: its crossings are its cycles) */
    render(&p, dry, 4096, 48);
    p.params[0][K_PAGE_SOUND][DCO1] = 12;
    render(&p, shaped, 4096, 48);
    printf("dco: %d crossings, %d with DCO1 +12\n", crossings(dry), crossings(shaped));
    assert(crossings(shaped) > crossings(dry) * 3 / 2);
    p.params[0][K_PAGE_SOUND][DCO1] = 0;

    /* GLIDE: a second note starts near the first's pitch */
    {
        KAudio au; float g1[2 * SAMPLES];
        p.params[0][K_PAGE_SOUND][GLIDE] = 100;
        k_audio_init(&au, K_AUDIO_FS); k_audio_sync(&au, &p);
        k_audio_note(&au, 0, 36, 127, 1); k_audio_render(&au, g1, SAMPLES); k_audio_note(&au, 0, 36, 0, 0);
        k_audio_note(&au, 0, 72, 127, 1); k_audio_render(&au, g1, SAMPLES);
        p.params[0][K_PAGE_SOUND][GLIDE] = 0;
        render(&p, dry, 0, 72);
        printf("glide: %d crossings gliding, %d not\n", crossings(g1), crossings(dry));
        assert(crossings(g1) < crossings(dry) * 3 / 4);
    }

    /* the three envelopes and the line amounts are saved with the song */
    setup(&p, phase);
    p.params[0][K_PAGE_ENV1][1] = 77;
    p.params[0][K_PAGE_ENV2][3] = 62;
    p.params[0][K_PAGE_ENV2][6] = 21;   /* DCO S */
    p.params[0][K_PAGE_SOUND][DCO2] = -7;
    n = k_song_save(&p, buf, sizeof buf);
    assert(n);
    k_init(&loaded);
    assert(k_song_load(&loaded, buf, n) == K_SONG_OK);
    assert(loaded.sound[0] == phase && loaded.params[0][K_PAGE_ENV1][1] == 77 && loaded.params[0][K_PAGE_ENV2][3] == 62 &&
           loaded.params[0][K_PAGE_ENV2][6] == 21 && loaded.params[0][K_PAGE_SOUND][DCO2] == -7);
    /* ANALOG still uses the generic ENV1 amp + ENV2 auxiliary scheme. */
    k_init(&p);
    assert(k_page_count(&p, K_PAGE_ENV1) == 8 && k_page_count(&p, K_PAGE_ENV2) == 8);
    assert(k_param(&p, K_PAGE_ENV1, 5) == &p.params[0][K_PAGE_ENV_DEST][1]);
    puts("PHASE DCO/DCW/DCA: three envelopes, line amounts, glide, page and songs OK");
    return 0;
}

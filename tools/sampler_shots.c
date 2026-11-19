/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler's screens as PNGs (K_WITH_SAMPLES builds), for looking at them: not the documented set (docs/SCREENS.md, the FM-1's
 * and the Zero's), which stays as it is.
 *   kreisel_sampler_shots [outdir]    (default build/sampler_shots: zero_*.png, h700_*.png, mac_*.png)
 * A sound is made here (a sung "ah": harmonics under two formants, a vibrato, a swell and a fall), written into a folder of its
 * own and given to track 1; the shots: nothing loaded, the sound with its first row in hand, the second row with a loop and two
 * voices playing, recording. */
#define main ui_shots_main   /* (its PNG writer; its own main unused) */
#include "ui_shots.c"
#undef main
#include "kreisel_audio.h"
#include "kreisel_plugin.h"
#include "sample/k_rec.h"
#include "sample/k_smp.h"
#include "sample/k_browse.h"
#include "sample/k_wav.h"
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#define SR 44100
static void make_voice(const char *path)
{
    const int n = SR * 3 / 2;
    int16_t *f = malloc((size_t)n * sizeof *f);
    double ph = 0;
    int i, h;
    for (i = 0; i < n; i++) {
        const double t = (double)i / SR, f0 = 196 * (1 + 0.006 * sin(2 * M_PI * 5.5 * t) * (t > 0.3)), env = (t < 0.08 ? t / 0.08 : 1) * (t > 1.1 ? exp(-(t - 1.1) * 9) : 1) * (0.75 + 0.25 * sin(2 * M_PI * 0.9 * t));
        double s = 0;
        ph += 2 * M_PI * f0 / SR;
        for (h = 1; h < 24; h++) {   /* "ah": formants near 750 and 1200 Hz */
            const double fh = f0 * h, a = exp(-pow((fh - 750) / 260, 2)) + 0.6 * exp(-pow((fh - 1200) / 300, 2)) + 0.25 / h;
            s += a * sin(ph * h);
        }
        f[i] = (int16_t)(9000 * env * s);
    }
    k_wav_write(path, f, (uint32_t)n, 1, SR);
    free(f);
}

static void shot(const char *dir, const char *prof, const char *name, KApp *a, int heads)
{
    const KProfile *p = k_profile_find(prof);
    KCursorVisual c = {0};
    char path[512];
    static KAudio au;
    if (heads) {   /* voices in the sample, for the playheads */
        float buf[2 * 441];
        int k;
        k_audio_init(&au, SR);
        k_audio_sync(&au, a);
        k_audio_note(&au, a->track, 60, 110, 1);
        for (k = 0; k < 40; k++) k_audio_render(&au, buf, 441);
        k_audio_note(&au, a->track, 67, 100, 1);
        for (k = 0; k < 25; k++) k_audio_render(&au, buf, 441);
    }
    k_set_shown(a, p->shown); k_set_layout(a, k_profile_layout(p));
    if (!ui_draw_init(p, K_THEME_DARK)) exit(1);
    ui_draw(a, &c, 4600); ui_draw(a, &c, 4800); ui_draw(a, &c, 5000);
    snprintf(path, sizeof path, "%s/%s_%s.png", dir, prof, name);
    if (!write_png(path, ui_draw_canvas())) exit(1);
    ui_draw_shutdown();
    printf("%s\n", path);
}

int main(int argc, char **argv)
{
    static const char *const PROFS[3] = {"zero", "h700", "mac"};
    const char *dir = argc > 1 ? argv[1] : "build/sampler_shots";
    char sdir[600], wav[700];
    int pi, idx, k;
    crc_init();
    mkdir(dir, 0755);
    snprintf(sdir, sizeof sdir, "%s/samples", dir);
    mkdir(sdir, 0755);
    k_smp_set_dir(sdir);
    snprintf(wav, sizeof wav, "%s/ah-196.wav", sdir);
    make_voice(wav);
    {   /* more files for the browser: a decaying hit, a long pad, a chopped loop (their shapes differ in the preview) */
        static int16_t q[SR * 4];
        static const struct { const char *name; int len; int kind; } F[] = {
            {"kick-808.wav", SR / 2, 0}, {"pad-glass.wav", SR * 4, 1}, {"break-94bpm.wav", SR * 5 / 2, 2}, {"rhodes-c3.wav", SR * 2, 0},
            {"snare-tight.wav", SR / 4, 0}, {"vox-chop-01.wav", SR, 2}, {"field-rain.wav", SR * 3, 1}};
        size_t j;
        for (j = 0; j < sizeof F / sizeof F[0]; j++) {
            for (k = 0; k < F[j].len; k++) {
                const double t = (double)k / SR, e = F[j].kind == 0 ? exp(-t * 7) : F[j].kind == 1 ? sin(M_PI * t / ((double)F[j].len / SR)) : (fmod(t, 0.32) < 0.2 ? exp(-fmod(t, 0.32) * 12) : 0);
                q[k] = (int16_t)(18000 * e * sin(2 * M_PI * (F[j].kind == 0 ? 60 + 200 * exp(-t * 30) : 220) * t + 2 * sin(2 * M_PI * 3 * t)));
            }
            snprintf(wav, sizeof wav, "%s/%s", sdir, F[j].name);
            k_wav_write(wav, q, (uint32_t)F[j].len, 1, SR);
        }
        snprintf(wav, sizeof wav, "%s/ah-196.wav", sdir);
    }
    idx = k_plugin_register_host(&K_SAMPLER_PLUGIN);
    if (idx < 0) return 1;
    for (pi = 0; pi < 3; pi++) {
        static KApp a;
        k_init(&a);
        k_set_shown(&a, k_profile_find(PROFS[pi])->shown); k_set_layout(&a, k_profile_layout(k_profile_find(PROFS[pi])));
        a.playing = 0;
        a.sound[0] = idx;
        for (k = 0; k < k_engine_at(idx)->nparams; k++) a.params[0][K_PAGE_SOUND][4 + k] = k_engine_at(idx)->edit[k].def;
        k_open_page(&a, K_PAGE_SOUND);
        k_smp_clear_all();
        shot(dir, PROFS[pi], "sampler_empty", &a, 0);
        k_smp_assign(0, "ah-196.wav");
        a.params[0][K_PAGE_SOUND][4 + 0] = 6; a.params[0][K_PAGE_SOUND][4 + 1] = 110;
        shot(dir, PROFS[pi], "sampler", &a, 0);
        a.params[0][K_PAGE_SOUND][4 + 2] = 1; a.params[0][K_PAGE_SOUND][4 + 3] = 40; a.params[0][K_PAGE_SOUND][4 + 4] = 55;
        a.win = 4;
        shot(dir, PROFS[pi], "sampler_loop", &a, 1);
        a.win = 0;
        k_rec_init(SR, SR * 10);
        k_rec_set_live(1);
        k_rec_start(1);
        {   int16_t q[256]; for (k = 0; k < 256; k++) q[k] = (int16_t)(k & 8 ? 60 : -60); k_rec_input16(q, 256, 1); }   /* a quiet room */
        {   KWav w; if (k_wav_load(wav, &w, 0) == K_WAV_OK) { k_rec_input16(w.frames, w.nframes * 2 / 3, 1); k_wav_free(&w); } }
        shot(dir, PROFS[pi], "sampler_rec", &a, 0);
        k_rec_stop(); k_rec_set_live(0);
        k_browse_open(0);   /* the files, one down from the track's */
        k_browse_move(2);
        shot(dir, PROFS[pi], "sampler_files", &a, 0);
        k_browse_close(0);
    }
    return 0;
}

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host tool: renders every page at the FM-1 (240x240) and the Cardputer Zero (320x170) profile into PNG files.
 *   kreisel_shots [outdir]     (default build/shots; files fm1_home.png, zero_mixer.png ...)
 * The PNG is written with stored (uncompressed) deflate blocks: no zlib needed. */
#include "draw.h"
#include "kreisel_engine.h"
#include "kreisel_save.h"
#include "kreisel_song.h"
#include "kreisel_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_RECORD   /* kreisel_sketch: the same screens as their shapes, a JSON line each (gfx_rec.h; tools/ui_sketch.py makes the .sketch) */
#include "gfx_rec.h"
FILE *g_rec_out;
int g_rec_depth;
static void rec_str(const char *s)
{
    fputc('"', g_rec_out);
    for (; s && *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', g_rec_out); if ((unsigned char)*s >= 32) fputc(*s, g_rec_out); }
    fputc('"', g_rec_out);
}
void g_rec(const char *kind, const int *iv, int ni, const uint32_t *cv, int nc, const char *s)
{
    int i;
    fprintf(g_rec_out, "{\"k\":\"%s\",\"i\":[", kind);
    for (i = 0; i < ni; i++) fprintf(g_rec_out, "%s%d", i ? "," : "", iv[i]);
    fprintf(g_rec_out, "],\"c\":[");
    for (i = 0; i < nc; i++) fprintf(g_rec_out, "%s%u", i ? "," : "", cv[i]);
    fprintf(g_rec_out, "]");
    if (s) { fprintf(g_rec_out, ",\"s\":"); rec_str(s); }
    fprintf(g_rec_out, "}\n");
}
void g_rec_curve16(const int *p16, int n, int th16, uint32_t col, int base)   /* (points in 1/16 px, th16 = thick / 2 + 1/2; 0: a fill) */
{
    int i;
    fprintf(g_rec_out, "{\"k\":\"curve\",\"t\":%.2f,\"b\":%d,\"c\":[%u],\"p\":[", th16 > 8 ? (th16 - 8) / 8.0 : 0.0, base, col);
    for (i = 0; i < n; i++) fprintf(g_rec_out, "%s%.2f,%.2f", i ? "," : "", p16[2 * i] / 16.0, p16[2 * i + 1] / 16.0);
    fprintf(g_rec_out, "]}\n");
}
#endif

static uint32_t crc_tab[256];
static void crc_init(void)
{
    uint32_t n, k, c;
    for (n = 0; n < 256; n++) { c = n; for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_tab[n] = c; }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *p, size_t n) { while (n--) c = crc_tab[(c ^ *p++) & 255u] ^ (c >> 8); return c; }
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, size_t n)
{
    uint8_t h[8], t[4]; uint32_t c;
    be32(h, (uint32_t)n); memcpy(h + 4, type, 4); memcpy(t, type, 4);
    fwrite(h, 1, 8, f);
    c = crc_upd(0xFFFFFFFFu, t, 4);
    if (n) { fwrite(d, 1, n, f); c = crc_upd(c, d, n); }
    be32(h, ~c); fwrite(h, 1, 4, f);
}
static int write_png(const char *path, const GCanvas *cv)
{
    size_t rowb = 1 + (size_t)cv->w * 3, raw = rowb * (size_t)cv->h, nblk = (raw + 65534) / 65535, zn = 2 + raw + 5 * nblk + 4, i, o = 0, left;
    uint8_t *img = malloc(raw), *z = malloc(zn), ihdr[13];
    uint32_t a = 1, b = 0;
    FILE *f;
    int x, y;
    if (!img || !z) return 0;
    for (y = 0; y < cv->h; y++) {
        uint8_t *r = img + rowb * (size_t)y;
        r[0] = 0;
        for (x = 0; x < cv->w; x++) { uint32_t p = GPIX_RGB(cv->px[y * cv->w + x]); r[1 + 3 * x] = (uint8_t)(p >> 16); r[2 + 3 * x] = (uint8_t)(p >> 8); r[3 + 3 * x] = (uint8_t)p; }
    }
    for (i = 0; i < raw; i++) { a = (a + img[i]) % 65521u; b = (b + a) % 65521u; }
    z[o++] = 0x78; z[o++] = 0x01;
    for (i = 0, left = raw; left;) {
        size_t n = left > 65535 ? 65535 : left;
        z[o++] = left == n; z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8); z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, img + i, n); o += n; i += n; left -= n;
    }
    be32(z + o, (b << 16) | a); o += 4;
    be32(ihdr, (uint32_t)cv->w); be32(ihdr + 4, (uint32_t)cv->h); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    f = fopen(path, "wb");
    if (!f) { free(img); free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    chunk(f, "IHDR", ihdr, 13); chunk(f, "IDAT", z, o); chunk(f, "IEND", NULL, 0);
    fclose(f); free(img); free(z);
    return 1;
}

/* a store in memory with a few things in it, for the SAVE shots */
static uint8_t smem[2][K_USER_PRESETS][K_SAVE_BUF];
static long slen[2][K_USER_PRESETS];
static long s_read(void *c, int k, int s, uint8_t *b, size_t cap) { (void)c; if (slen[k][s] > (long)cap) return -1; memcpy(b, smem[k][s], (size_t)slen[k][s]); return slen[k][s]; }
static int s_write(void *c, int k, int s, const uint8_t *b, size_t n) { (void)c; memcpy(smem[k][s], b, n); slen[k][s] = (long)n; return 0; }
static int s_erase(void *c, int k, int s) { (void)c; slen[k][s] = 0; return 0; }
static const KStore SSTORE = {s_read, s_write, s_erase, 0, 0};
static void fill_store(void)
{
    static const char *const SONGS[3] = {"JAZZ NIGHT", "SUB DEMO", "TEST 3"};
    static const int AT[3] = {0, 1, 3};
    KApp a;
    int i;
    for (i = 0; i < 3; i++) { k_init(&a); a.bpm = 100 + i * 10; snprintf(a.song_name, sizeof a.song_name, "%s", SONGS[i]); slen[0][AT[i]] = (long)k_song_save(&a, smem[0][AT[i]], K_SAVE_BUF); }
    for (i = 0; i < 6; i++) { char nm[16]; k_init(&a); a.sound[0] = i % (K_ENGINE_COUNT - 1); snprintf(nm, sizeof nm, "%s %d", i % 2 ? "PAD" : "BASS", i + 1); slen[1][i] = (long)k_preset_save(&a, 0, nm, smem[1][i], K_SAVE_BUF); }
    k_store_set(&SSTORE);
}
static void lower(char *d, const char *s) { for (; *s; s++, d++) *d = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s == ' ' ? '_' : *s); *d = 0; }

static void eng_defaults(KApp *a, uint32_t uid)   /* track 1 on that engine, at its defaults */
{
    int e = k_engine_find(uid), i;
    if (e < 0) return;   /* (an engine not built in) */
    a->sound[0] = e;
    for (i = 0; i < K_ENGINE[e].nparams; i++) a->params[0][K_PAGE_SOUND][4 + i] = K_ENGINE[e].edit[i].def;
    if (K_ENGINE[e].gate) { a->params[0][K_PAGE_ENV1][0] = 0; a->params[0][K_PAGE_ENV1][2] = 100; a->params[0][K_PAGE_ENV1][3] = 10; }   /* (as engine_defaults) */
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/shots";
    fill_store();
    static const char *PROF[2] = {"fm1", "zero"};
    int pi, pg, t, i, n = 0, np = 2;
    if (argc > 2) { PROF[0] = argv[2]; np = 1; }   /* kreisel_shots dir switch: one other profile (not the documented set) */
    crc_init();
    for (pi = 0; pi < np; pi++) {
        const KProfile *p = k_profile_find(PROF[pi]);
        for (pg = 0; pg <= K_PAGE_COUNT + 52; pg++) {   /* past the pages: the page menu, the settings, SYNTH with its hint, each engine's SOUND and EDIT */
            KApp a; KCursorVisual c = {0}; char name[48], path[256]; uint32_t now = 5000;
            k_init(&a); k_set_shown(&a, p->shown); k_set_layout(&a, k_profile_layout(p));
            a.sound[1] = K_ENGINE_COUNT - 1;   /* track 2 is the drum track (DRUM page) */
            for (i = 0; i < K_ENGINE[K_ENGINE_COUNT - 1].nparams; i++) a.params[1][K_PAGE_SOUND][4 + i] = K_ENGINE[K_ENGINE_COUNT - 1].edit[i].def;   /* at its defaults */
            for (t = 0; t < K_TRACKS; t++) {
                a.hit_ms[t] = now - 60u * (unsigned)(t + 1); a.hit_v[t] = (uint8_t)(95 - 20 * t);
                for (i = 0; i < K_STEPS; i += 2 + t % 2) { k_roll_add(&a, t, a.pattern[t], i, 48 + (i * 5 + t * 3) % 17, 1 + i % 3, 60 + (i * 11) % 60); }
            }
            for (i = 0; i < K_LANES; i++) { int q; a.lane_on[1][i] = (uint16_t)(0x1111u << (i % 4)) | (uint16_t)(i * 0x0505u); for (q = 0; q < 4; q++) a.lane_pat[1][q][i] = a.lane_on[1][i]; }   /* (every pattern the same beat) */
            a.playing = 1; a.step = 5; a.beat = 1; a.master = 80; a.battery = 3; a.out_lv[0] = 78; a.out_lv[1] = 93;   /* (the meter: left green, right into the red) */
            a.track = pg == K_PAGE_DRUM ? 1 : 0;
            for (i = 0; i < 8; i++) for (t = 0; t < K_TRACKS; t++) a.song[i][t] = (uint8_t)((i / 2 + t) % 4);   /* a song to show, eight scenes (the app starts empty) */
            if (pg == K_PAGE_SONG) { a.song_play = 1; a.song_scene = 2; }   /* SONG: scene 3 playing, framed with its playhead */
            if (pg == K_PAGE_ARP) { static const uint8_t H[8] = {60, 64, 67, 72, 67, 64, 60, 55}; for (i = 0; i < 8; i++) a.arp_hist[0][i] = H[i]; a.arp_n[0] = 8; a.arp_ms[0] = now - 60; a.clock_ms = now; }   /* (the arp playing) */
            if (pg == K_PAGE_COUNT + 1 || pg == K_PAGE_COUNT + 28 || pg == K_PAGE_ENV_DEST || pg == K_PAGE_CLOCK) continue;   /* (ENV DEST, CLOCK: their values only, on ENV 1 and ENV 2's rows, on SEQ's and SONG's cards; no page; were the settings: none yet; DRUM's LFO wave: DRUM has no LFO) */
            if (pg < K_PAGE_COUNT && k_page_folded(&a, (KPage)pg)) continue;   /* (a big screen: ENV, LFO, EDIT are on SOUND, no page of their own to shoot) */
            if (!K_WITH_VECTOR && (pg == K_PAGE_COUNT + 21 || pg == K_PAGE_COUNT + 22)) continue;   /* (VECTOR: not in the MVP) */
            if (!K_WITH_ENSEMBLE && (pg == K_PAGE_COUNT + 19 || pg == K_PAGE_COUNT + 20)) continue;   /* (ENSEMBLE: not in the MVP) */
            if (pg == K_PAGE_COUNT) { k_open_page(&a, K_PAGE_HOME); a.menu_open = 1; a.menu_group = K_GROUP_PATTERN; a.menu_selected = 1; snprintf(name, sizeof name, "menu"); }
            else if (pg == K_PAGE_COUNT + 3) { eng_defaults(&a, K_UID('K','L','O','F')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "lofi_sel"); }
            else if (pg == K_PAGE_COUNT + 4) { eng_defaults(&a, K_UID('K','L','O','F')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "lofi_edit"); }
            else if (pg == K_PAGE_COUNT + 5) { eng_defaults(&a, K_UID('K','L','O','F')); k_open_page(&a, K_PAGE_ENV1); snprintf(name, sizeof name, "lofi_env1"); }
            else if (pg == K_PAGE_COUNT + 6) { eng_defaults(&a, K_UID('K','P','H','S')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "phase_sel"); }
            else if (pg == K_PAGE_COUNT + 7) { eng_defaults(&a, K_UID('K','P','H','S')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "phase_edit"); }
            else if (pg == K_PAGE_COUNT + 8) { eng_defaults(&a, K_UID('K','T','R','I')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "trio_sel"); }
            else if (pg == K_PAGE_COUNT + 9) { eng_defaults(&a, K_UID('K','T','R','I')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "trio_edit"); }
            else if (pg == K_PAGE_COUNT + 10) { eng_defaults(&a, K_UID('K','O','G','N')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "organ_sel"); }
            else if (pg == K_PAGE_COUNT + 11) { eng_defaults(&a, K_UID('K','O','G','N')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "organ_edit"); }
            else if (pg == K_PAGE_COUNT + 12) { eng_defaults(&a, K_UID('K','O','G','N')); k_open_page(&a, K_PAGE_ENV1); a.win = 4; snprintf(name, sizeof name, "organ_env1"); }   /* (its second row in hand: FLT SHP dim) */
            else if (pg == K_PAGE_COUNT + 13) { eng_defaults(&a, K_UID('K','O','G','N')); k_open_page(&a, K_PAGE_LFO); a.win = 4; snprintf(name, sizeof name, "organ_lfo"); }
            else if (pg == K_PAGE_COUNT + 14) { a.missing[0].uid = K_UID('f','u','z','z'); a.missing[0].version = 1; k_open_page(&a, K_PAGE_ENV1); snprintf(name, sizeof name, "missing_env1"); }   /* a plugin missing: the plain skin */
            else if (pg == K_PAGE_COUNT + 15) { a.track = 1; k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "drum_sel"); }
            else if (pg == K_PAGE_COUNT + 16) { a.track = 1; k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "drum_edit"); }
            else if (pg == K_PAGE_COUNT + 17) { a.track = 1; a.lane_p[1][0][K_LANE_ENV + 1] = 80; a.lane_p[1][0][K_LANE_ENV + 3] = 40; k_open_page(&a, K_PAGE_ENV1); snprintf(name, sizeof name, "drum_env1"); }
            else if (pg == K_PAGE_COUNT + 18) { a.track = 1; k_open_page(&a, K_PAGE_LFO); a.win = 8; snprintf(name, sizeof name, "drum_lfo"); }
            else if (pg == K_PAGE_COUNT + 19) { eng_defaults(&a, K_UID('K','E','N','S')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "ensemble_sel"); }
            else if (pg == K_PAGE_COUNT + 20) { eng_defaults(&a, K_UID('K','E','N','S')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "ensemble_edit"); }
            else if (pg == K_PAGE_COUNT + 21) { eng_defaults(&a, K_UID('K','V','E','C')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "vector_sel"); }
            else if (pg == K_PAGE_COUNT + 22) { eng_defaults(&a, K_UID('K','V','E','C')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "vector_edit"); }
            else if (pg == K_PAGE_COUNT + 23) { eng_defaults(&a, K_UID('K','P','H','S')); k_open_page(&a, K_PAGE_ENV1); snprintf(name, sizeof name, "phase_dcw"); }
            else if (pg == K_PAGE_COUNT + 24) { eng_defaults(&a, K_UID('K','P','H','S')); k_open_page(&a, K_PAGE_ENV2); snprintf(name, sizeof name, "phase_dca"); }
            else if (pg == K_PAGE_COUNT + 25) {
                eng_defaults(&a, K_UID('K','P','H','S'));
                k_open_page(&a, K_PAGE_ENV1);
                a.menu_open = 1; a.menu_group = K_GROUP_ENV; a.menu_selected = 1;
                snprintf(name, sizeof name, "phase_env_menu");
            }
            else if (pg == K_PAGE_COUNT + 26) {
                k_open_page(&a, K_PAGE_FX);
                a.params[0][K_PAGE_FX][0] = 1; /* DIST */
                a.params[0][K_PAGE_FX][1] = 76; a.params[0][K_PAGE_FX][2] = 35; a.params[0][K_PAGE_FX][3] = 60;
                a.params[0][K_PAGE_FX][4] = 2; /* CHO */
                a.params[0][K_PAGE_FX][5] = 28; a.params[0][K_PAGE_FX][6] = 67; a.params[0][K_PAGE_FX][7] = 40;
                snprintf(name, sizeof name, "fx_loaded");
            }
            else if (pg == K_PAGE_COUNT + 27) {
                k_open_page(&a, K_PAGE_FX);
                a.params[0][K_PAGE_FX][0] = 15; /* AMP: has more than two parameters */
                for (i = 8; i < 12; i++) *k_param(&a, K_PAGE_FX, i) = 20 + (i - 8) * 22;
                a.win = 8; /* the first extra row, not just the initial two FX parameters */
                k_set_shown(&a, p->shown);
                snprintf(name, sizeof name, "fx_extended");
            }
            else if (pg == K_PAGE_COUNT + 29) { k_open_page(&a, K_PAGE_HOME); a.playing = 0; a.song_from = 2; snprintf(a.song_name, sizeof a.song_name, "SUB DEMO"); k_dispatch(&a, (KAction){K_ACT_SAVE, 0, 0, now}); a.save_act = K_SAVE_LOAD; snprintf(name, sizeof name, "save_song"); }
            else if (pg == K_PAGE_COUNT + 30) { k_open_page(&a, K_PAGE_SOUND); a.playing = 0; k_dispatch(&a, (KAction){K_ACT_SAVE, 0, 0, now}); a.save_open = 2; a.save_act = K_SAVE_NONE; a.save_sel[1] = 2; snprintf(name, sizeof name, "save_sound"); }
            else if (pg == K_PAGE_COUNT + 31) { k_open_page(&a, K_PAGE_HOME); a.playing = 0; k_dispatch(&a, (KAction){K_ACT_SAVE, 0, 0, now}); a.save_sel[0] = 1; a.save_act = K_SAVE_NAME; k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, now}); snprintf(name, sizeof name, "save_name");   /* the menu's Name, OCT+ */ }
            else if (pg == K_PAGE_COUNT + 32) { eng_defaults(&a, K_UID('K','F','M','2')); k_open_page(&a, K_PAGE_SOUND); snprintf(name, sizeof name, "fm2_sel"); }
            else if (pg == K_PAGE_COUNT + 33) { eng_defaults(&a, K_UID('K','F','M','2')); k_open_page(&a, K_PAGE_EDIT); snprintf(name, sizeof name, "fm2_edit"); }
            else if (pg == K_PAGE_COUNT + 34) { a.track = 1; k_open_page(&a, K_PAGE_SOUND); k_balloon_show(&a.balloon, K_BALLOON_HINT, now - 500); snprintf(name, sizeof name, "drum_hint"); }
            else if (pg == K_PAGE_COUNT + 35) { k_open_page(&a, K_PAGE_SOUND); a.preset_slot[0] = 3; snprintf(a.preset_name[0], sizeof a.preset_name[0], "MY BASS"); a.save_ask = 1; snprintf(name, sizeof name, "save_ask"); }
            else if (pg == K_PAGE_COUNT + 36) { k_open_page(&a, K_PAGE_HOME); snprintf(a.preset_name[0], sizeof a.preset_name[0], "Fat Bass"); k_balloon_show(&a.balloon, K_BALLOON_PRESET, now - 500); snprintf(name, sizeof name, "preset_balloon"); }
            else if (pg == K_PAGE_COUNT + 37) { k_open_page(&a, K_PAGE_HOME); snprintf(a.preset_name[0], sizeof a.preset_name[0], "Fat Bass"); a.factory[0] = 0; a.preset_dirty[0] = 1; k_balloon_show(&a.balloon, K_BALLOON_DIRTY, now - 500); snprintf(name, sizeof name, "preset_dirty"); }
            else if (pg == K_PAGE_COUNT + 38) { k_open_page(&a, K_PAGE_HOME); a.settings_open = 1; a.settings_sel = K_SET_ANIM; a.set_v[K_SET_LEDS] = 1; snprintf(name, sizeof name, "settings"); }
            else if (pg == K_PAGE_COUNT + 39) { k_open_page(&a, K_PAGE_FX); a.settings_open = 1; a.settings_sel = K_SET_CLEAR; a.set_ask = 1; snprintf(name, sizeof name, "settings_clear"); }
            else if (pg == K_PAGE_COUNT + 40) {   /* the tools' list on ROLL: EUCLID tried on the pattern (its values red) */
                k_open_page(&a, K_PAGE_ROLL); a.playing = 0;
                k_dispatch(&a, (KAction){K_ACT_TOOLS, 0, 0, now});
                k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now});
                k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 0, 2, now});
                snprintf(name, sizeof name, "roll_tools");
            }
            else if (pg == K_PAGE_COUNT + 41) {   /* on DRUM: the lane in hand, RANDOM in hand */
                a.track = 1; k_open_page(&a, K_PAGE_DRUM); a.playing = 0; a.cur_lane[1] = 2;
                k_dispatch(&a, (KAction){K_ACT_TOOLS, 0, 0, now});
                k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now}); k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now});
                snprintf(name, sizeof name, "drum_tools");
            }
            else if (pg >= K_PAGE_COUNT + 42 && pg <= K_PAGE_COUNT + 45) {   /* the other pages' tools: SONG (DUPLICATE in hand), SYNTH (RANDOM
                                                * tried), MIXER, FX */
                static const KPage TP[4] = {K_PAGE_SONG, K_PAGE_SOUND, K_PAGE_MIXER, K_PAGE_FX};
                static const char *const TN[4] = {"song_tools", "synth_tools", "mixer_tools", "fx_tools"};
                const int w = pg - K_PAGE_COUNT - 42;
                k_open_page(&a, TP[w]); a.playing = 0;
                k_dispatch(&a, (KAction){K_ACT_TOOLS, 0, 0, now});
                if (w == 0) k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now});
                if (w == 1) k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 0, 2, now});
                if (w == 3) k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now});
                snprintf(name, sizeof name, "%s", TN[w]);
            }
            else if (pg == K_PAGE_COUNT + 46) {   /* the tracker view (SEQ's tools, Tracker): the steps 4 on, the playhead at 5 */
                k_open_page(&a, K_PAGE_ROLL); a.rows_view = 1;
                for (i = 0; i < 4; i++) k_dispatch(&a, (KAction){K_ACT_DOWN, 0, 0, now});
                snprintf(name, sizeof name, "tracker");
            }
            else if (pg == K_PAGE_COUNT + 47) {   /* the steps' chances (2026-11-12): on DRUM, the odd steps at 50 %, the sixth at 25 % (fainter);
                                                * the tools at Chance, 75 % tried on the step in hand */
                a.track = 1; k_open_page(&a, K_PAGE_DRUM); a.playing = 0; a.cur_lane[1] = 2;
                for (i = 1; i < K_STEPS; i += 2) k_chance_set(&a, 1, a.pattern[1], i, 2);
                k_chance_set(&a, 1, a.pattern[1], 6, 3);
                k_lane_step_toggle(&a, 1, 2, 4); k_lane_step_toggle(&a, 1, 2, 4);   /* (the step in hand: the fifth) */
                k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 3, 2, now});   /* K4: the step in hand's chance (its card, in use) */
                snprintf(name, sizeof name, "drum_chance");
            }
            else if (pg == K_PAGE_COUNT + 48) { k_open_page(&a, K_PAGE_HOME); a.settings_open = 1; a.settings_sel = K_SET_METRO; a.set_v[K_SET_METRO] = 1; a.set_v[K_SET_COUNT] = 1; snprintf(name, sizeof name, "settings_metro"); }   /* the metronome's tab */
            else if (pg == K_PAGE_COUNT + 49) {   /* a tool's values (the action sheets): a cursor's second box, Euclid opened and turned; a knobs
                                                 * device's values over its knobs */
                k_open_page(&a, K_PAGE_ROLL); a.playing = 0;
                k_dispatch(&a, (KAction){K_ACT_TOOLS, 0, 0, now});
                if (a.lay & K_LAY_SHEET_CURSOR) { k_dispatch(&a, (KAction){K_ACT_DOWN, 0, 0, now}); k_dispatch(&a, (KAction){K_ACT_ACCEPT, 0, 0, now}); k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, now}); }
                else { k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now}); k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, now}); k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, 4, now}); }   /* (Random, K2 its range) */
                snprintf(name, sizeof name, "tools_values");
            }
            else if (pg == K_PAGE_COUNT + 50 || pg == K_PAGE_COUNT + 51) {   /* MIDI learn (a device with the Connect tab): its Learn row; then
                                                 * knob 1 pointed at and CC 74 moved: the balloon says what was bound */
                if (!K_WITH_CONNECT || !(a.lay & K_LAY_CONNECT)) continue;
#if K_WITH_CONNECT
                a.playing = 0;
                if (pg == K_PAGE_COUNT + 50) { k_open_page(&a, K_PAGE_HOME); a.settings_open = 1; a.settings_sel = K_SET_MIDI_LEARN; snprintf(name, sizeof name, "settings_connect"); }
                else {
                    k_open_page(&a, K_PAGE_SOUND); a.clock_ms = now - 900;
                    k_learn_start(&a);
                    k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 0, 1, now - 800});
                    k_learn_midi(&a, 0xB0, 1, 74, 10, now - 500);
                    snprintf(name, sizeof name, "midi_learn");
                }
#endif
            }
            else if (pg == K_PAGE_COUNT + 52) {   /* the effects' switches standing up: TREMOLO's WAVE on (Sqr), GAIN's SAT off */
                int it, sl, pa;
                k_open_page(&a, K_PAGE_FX);
                k_fx_type_set(&a, 0, 11); k_fx_type_set(&a, 1, 5);
                for (it = 0; it < k_page_count(&a, K_PAGE_FX); it++) { sl = -1; pa = -3; k_fx_item(&a, it, &sl, &pa); if (sl == 0 && pa == 2) k_param_set(&a, K_PAGE_FX, it, 100); }
                snprintf(name, sizeof name, "fx_switches");
            }
            else if (pg == K_PAGE_COUNT + 2) { k_open_page(&a, K_PAGE_SOUND); k_balloon_show(&a.balloon, K_BALLOON_HINT, now - 500); snprintf(name, sizeof name, "synth_hint"); }
            else { k_open_page(&a, (KPage)pg); lower(name, k_page_name((KPage)pg)); }
            if (pg == K_PAGE_MIXER || pg == K_PAGE_FX) { a.touched = 1u << 1; a.touch_ms[1] = now - 100; }   /* a card in use */
            /* The scripted a.win assignments (ORGAN and DRUM LFO rows) must
             * also rebuild focus nodes. Otherwise the image emphasizes row 2
             * or 3 while the interactive cursor still points at row 1. */
            k_set_shown(&a, p->shown);
            /* Screenshot fixtures must be legal UI states, never an invalid
             * menu index silently producing a blank highlight. */
            if (a.menu_open && (a.menu_selected < 0 || a.menu_selected >= k_menu_count(&a))) {
                fprintf(stderr, "invalid menu selection in %s: %d\n", name, a.menu_selected);
                return 1;
            }
            if (pg == K_PAGE_ENV2 && (a.lay & K_LAY_ENV_PAIR) && a.win != 8) {
                fprintf(stderr, "ENV2 screenshot failed to focus the right-hand half\n");
                return 1;
            }
            if ((!strcmp(name, "synth_hint") || !strcmp(name, "drum_hint")) && !(p->input & (K_IN_KNOBS | K_IN_KEYS))) continue;   /* (the hint names SELECT: a device that has one) */
            if (!ui_draw_init(p, K_THEME_DARK)) return 1;
            ui_draw(&a, &c, now - 400); ui_draw(&a, &c, now - 200);
#ifdef G_RECORD
            {   /* only the screens asked for (KREISEL_SKETCH="organ_sel organ_edit ..."; all when unset) */
                const char *want = getenv("KREISEL_SKETCH");
                char key[64], all[512];
                snprintf(key, sizeof key, " %s ", name);
                snprintf(all, sizeof all, " %s ", want ? want : "");
                if (!want || strstr(all, key)) {
                    snprintf(path, sizeof path, "%s/%s_%s.jsonl", dir, PROF[pi], name);
                    g_rec_out = fopen(path, "w");
                }
            }
#endif
            ui_draw(&a, &c, now);
#ifdef G_RECORD
            if (g_rec_out) { fclose(g_rec_out); g_rec_out = 0; }
            ui_draw_shutdown(); n++;
            continue;
#endif
            snprintf(path, sizeof path, "%s/%s_%s.png", dir, PROF[pi], name);
            if (!write_png(path, ui_draw_canvas())) { fprintf(stderr, "cannot write %s (make the directory first)\n", path); ui_draw_shutdown(); return 1; }
            ui_draw_shutdown(); n++;
        }
    }
    printf("%d screenshots in %s\n", n, dir);
    return 0;
}

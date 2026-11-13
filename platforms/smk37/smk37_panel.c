/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Fune on the M-VAVE SMK-37 Pro (Alpha): the controls the FM-1 has not. Included by platforms/fm1hw/fm1_main.c (FM1_BOARD_EXTRAS,
 * from third_party/felucca-bsp/boards/smk37/smk37_map.h) after its own input code; board_poll runs each round of its main loop.
 *
 * The FM-1's own controls reach the core as they do on an FM-1: the board layer (boards/smk37/fm1_input.h) hands fm1_main.c the
 * 14 FM-1 buttons, the 27 middle keys F3..G5 and the seven FM-1 encoders under the FM-1's ids; K1 is the MASTER pot. What is
 * left is mapped here, by table and by the core's own input calls (no other way into the core's values):
 *
 *   the 16 pads         the FM-1's 16 white keys (K_ACT_KEY 0..15): the steps on STEP and DRUM, the lane in hand's steps on a
 *                       drum track, notes on a synth track's other pages; lit as the FM-1 lights those keys
 *   the 4 faders        the four tracks' levels (k_level_in), each taken over once it is moved
 *   the pitch wheel     pitch bend of the track in hand (k_bend_in), centred where it rests at power-on
 *   the mod wheel       macro 1 of the track in hand (k_cc_in 16), as the touch surface's mod strip
 *   the 10 outer keys   C3..E3 and G#5..C6: notes on the track in hand (k_note_in), the octave as the middle keys'
 *   <  >                SELECT one step back / on (the cursor)
 *   KNOB BANK           the page's tools (K_ACT_TOOLS)
 *   FADER BANK          nothing yet
 * Velocity: measured for every key and pad (smk_in.vel, pad_vel), sent with the outer keys' notes only; K_ACT_KEY has none, and
 * the core is not changed for the Alpha (the FM-1's image stays as it is). */
#include "theme.h"

#define SMK_FADER_DEAD 2               /* of 127: a move under it is noise */
#define SMK_WHEEL_DEAD 12              /* of 1023: the pitch wheel's rest */
static const uint8_t SMK_FADER_AT[4] = SMK_FADER_ADDR;
/* hardware pad p -> the white key it is (reading order: the top row 1..8, the bottom row 9..16) */
static const uint8_t SMK_PAD_KEY[SMK_NPAD] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
/* white key w -> the FM-1's note key (0 = F3 .. 26 = G5), for its LED level */
static const uint8_t SMK_WHITE_NOTE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
/* the pad LEDs' chain: hardware pad p is LED SMK_PAD_SLOT[p] */
static const uint8_t SMK_PAD_SLOT[SMK_NPAD] = {0, 15, 14, 13, 12, 11, 10, 9, 1, 2, 3, 4, 5, 6, 7, 8};
#define SMK_PAD_RESET 250u             /* zero bytes before a frame: the latch (newer LED parts want it long) */
static uint8_t smk_pad_frame[SMK_PAD_RESET + SMK_NPAD * 24u] __attribute__((aligned(4)));

static struct {
    uint8_t init;
    uint64_t outer_down;               /* the outer keys sounding (bit k) */
    uint8_t outer_note[SMK_NKEY];
    uint32_t pads_down;
    uint8_t fader_sent[4], fader_live[4];
    int16_t fader_acc[4];              /* x 16 */
    int16_t pitch_c, pitch_lo, pitch_hi, bend_sent;
    int16_t mod_rest, mod_top, mod_sent;
    uint8_t pad_lv[SMK_NPAD];
    uint32_t pad_rgb, last_ms;
    uint32_t trim_ok;
} smk;

static int smk_is_outer(uint32_t k) { return k < SMK_KEY0 || k >= SMK_KEY0 + 27u; }

static void smk_pads_show(void)
{
    uint32_t p, i, changed = 0, rgb = smk.pad_rgb;
    for (p = 0; p < SMK_NPAD; p++) {
        uint32_t q = fm1_led_lv[14u + SMK_WHITE_NOTE[SMK_PAD_KEY[p]]] & 0x7Fu;
        if (q != smk.pad_lv[p]) { smk.pad_lv[p] = (uint8_t)q; changed = 1; }
    }
    if (!changed)
        return;
    for (p = 0; p < SMK_NPAD; p++) {
        uint8_t *o = smk_pad_frame + SMK_PAD_RESET + SMK_PAD_SLOT[p] * 24u;
        uint32_t q = smk.pad_lv[p], r = ((rgb >> 16) & 255u) * q / 192u, g = ((rgb >> 8) & 255u) * q / 192u, b = (rgb & 255u) * q / 192u;
        uint32_t grb = g << 16 | r << 8 | b;   /* (a third of full at most: the LEDs are bright) */
        for (i = 0; i < 24u; i++)
            o[i] = (grb >> (23u - i)) & 1u ? 0xF0u : 0xC0u;
    }
    smk_pads_send(smk_pad_frame, sizeof smk_pad_frame);
}

static void smk_init(void)
{
    KTheme th;
    uint32_t i;
    smk.init = 1;
    fl_plain_window_init();                    /* the calibration record as plain XIP: read only */
    smk.trim_ok = (uint32_t)smk_trim_use((const uint8_t *)FM1_XIP(0xC1000u));
    k_theme(&th, K_THEME_DARK);
    smk.pad_rgb = th.theme;
    for (i = 0; i < SMK_PAD_RESET; i++)
        smk_pad_frame[i] = 0;
    for (i = 0; i < SMK_NPAD; i++)
        smk.pad_lv[i] = 0xFF;                  /* (sent once, dark) */
    for (i = 0; i < 4u; i++)
        smk.fader_acc[i] = -1;
    smk.pitch_c = -1;
    smk.mod_rest = -1;
}

/* a fader 0..127 from its reading, smoothed; sent once it has moved from where it was first read (the level is not taken from
 * where the fader happens to stand at power-on) */
static void smk_faders(uint32_t now)
{
    uint32_t f;
    if (smk_an.sweeps < 2u)
        return;
    for (f = 0; f < 4u; f++) {
        int32_t raw = smk_an.c[SMK_FADER_AT[f]], a = smk.fader_acc[f], v, s;
        a = a < 0 ? raw * 16 : a + ((raw * 16 - a) >> 2);
        smk.fader_acc[f] = (int16_t)a;
        v = (a / 16) * 127 / 1023;
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        if (!smk.fader_sent[f]) {
            smk.fader_sent[f] = (uint8_t)(v + 1);   /* (+ 1: 0 is "not read yet") */
            continue;
        }
        s = smk.fader_sent[f] - 1;
        if (v - s >= SMK_FADER_DEAD || s - v >= SMK_FADER_DEAD || (smk.fader_live[f] && v != s && (v == 0 || v == 127))) {
            smk.fader_live[f] = 1;
            smk.fader_sent[f] = (uint8_t)(v + 1);
            k_level_in(&app, (int)f, (int)v);
            input_seen = 1;
            last_input_ms = now;
        }
    }
}

/* the pitch wheel (centred at rest, its travel learnt) as a bend; the mod wheel (up reads lower) as macro 1 */
static void smk_wheels(uint32_t now)
{
    int32_t p = smk_an.c[SMK_C_PITCH], m = smk_an.c[SMK_C_MOD], b, v;
    if (smk_an.sweeps < 4u)
        return;
    if (smk.pitch_c < 0) {
        smk.pitch_c = (int16_t)p;
        smk.pitch_lo = (int16_t)(p - 90);
        smk.pitch_hi = (int16_t)(p + 90);
        smk.bend_sent = 8192;
        smk.mod_rest = (int16_t)m;
        smk.mod_top = (int16_t)(m - 150);
        smk.mod_sent = -1;
        return;
    }
    if (p < smk.pitch_lo) smk.pitch_lo = (int16_t)p;
    if (p > smk.pitch_hi) smk.pitch_hi = (int16_t)p;
    if (p > smk.pitch_c + SMK_WHEEL_DEAD)
        b = 8192 + (p - smk.pitch_c - SMK_WHEEL_DEAD) * 8191 / (smk.pitch_hi - smk.pitch_c - SMK_WHEEL_DEAD);
    else if (p < smk.pitch_c - SMK_WHEEL_DEAD)
        b = 8192 - (smk.pitch_c - SMK_WHEEL_DEAD - p) * 8192 / (smk.pitch_c - SMK_WHEEL_DEAD - smk.pitch_lo);
    else
        b = 8192;
    b = b < 0 ? 0 : b > 16383 ? 16383 : b;
    if (b - smk.bend_sent > 64 || smk.bend_sent - b > 64 || (b == 8192 && smk.bend_sent != 8192)) {
        smk.bend_sent = (int16_t)b;
        k_bend_in(&app, (int)b);
        input_seen = 1;
        last_input_ms = now;
    }
    if (m > smk.mod_rest) smk.mod_rest = (int16_t)m;
    if (m < smk.mod_top) smk.mod_top = (int16_t)m;
    v = (smk.mod_rest - m) * 127 / (smk.mod_rest - smk.mod_top);
    v = v < 4 ? 0 : v > 127 ? 127 : v;          /* (a few counts off the rest: none) */
    if (smk.mod_sent < 0 ? v > 0 : (v - smk.mod_sent >= 2 || smk.mod_sent - v >= 2 || (v == 0 && smk.mod_sent))) {
        smk.mod_sent = (int16_t)v;
        k_cc_in(&app, 16, (int)v);
        input_seen = 1;
        last_input_ms = now;
    }
}

static void board_poll(uint32_t now)
{
    uint64_t kp, keys;
    uint32_t xp, xr, pp, k, pads;
    if (!smk.init)
        smk_init();
    smk_input_edges(&kp, &xp, &xr, &pp);
    keys = smk_in.keys;
    for (k = 0; k < SMK_NKEY; k++) {           /* the outer keys: notes */
        const uint64_t m = (uint64_t)1 << k;
        if (!smk_is_outer(k))
            continue;
        if ((kp & m) && !(smk.outer_down & m)) {
            int n = 48 + (int)k + 12 * app.octave;   /* (C3 = 48 at octave 0: the middle keys' F3 is 53) */
            n = n < 0 ? 0 : n > 127 ? 127 : n;
            smk.outer_note[k] = (uint8_t)n;
            smk.outer_down |= m;
            k_note_in(&app, n, smk_in.vel[k]);
            input_seen = 1;
            last_input_ms = now;
        }
        if ((smk.outer_down & m) && !(keys & m)) {
            smk.outer_down &= ~m;
            k_note_in(&app, smk.outer_note[k], 0);
            input_seen = 1;
        }
    }
    pads = smk_in.pads;
    for (k = 0; k < SMK_NPAD; k++) {           /* the pads: the white keys */
        const uint32_t m = 1u << k;
        if ((pp & m) && !(smk.pads_down & m)) {
            smk.pads_down |= m;
            act(K_ACT_KEY, SMK_PAD_KEY[k], 1, now);
        }
        if ((smk.pads_down & m) && !(pads & m)) {
            smk.pads_down &= ~m;
            act(K_ACT_KEY, SMK_PAD_KEY[k], 0, now);
        }
    }
    if (xp & (1u << SMK_B_LEFT)) act(K_ACT_SELECT_DELTA, -1, 0, now);
    if (xp & (1u << SMK_B_RIGHT)) act(K_ACT_SELECT_DELTA, 1, 0, now);
    if (xp & (1u << SMK_B_KNOB_BANK)) act(K_ACT_TOOLS, 0, 0, now);
    (void)xr;
    if (now - smk.last_ms >= 20u) {
        smk.last_ms = now;
        smk_faders(now);
        smk_wheels(now);
        smk_led_x[SMK_B_KNOB_BANK] = (uint8_t)(app.tools_open ? FM1_ANIM_FULL : 0u);
        smk_led_x[SMK_B_LEFT] = (uint8_t)(smk_in.xbtn & (1u << SMK_B_LEFT) ? FM1_ANIM_FULL : 0u);
        smk_led_x[SMK_B_RIGHT] = (uint8_t)(smk_in.xbtn & (1u << SMK_B_RIGHT) ? FM1_ANIM_FULL : 0u);
        if (!lcd_pending)
            smk_pads_show();
    }
}

/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Ported from felucca-2.0-snap/firmware/src/console.c, (c) Leo Kuroshita: the author's own code, relicensed MPL-2.0 for Kreisel (2026-11-07).
 *
 * USB serial console (CDC-ACM; the BSP's chip/usb.c with FELUCCA_CDC=1, EP2/EP3) so a script can drive the FM-1 and read
 * measurements without touching it (platforms/fm1hw/fm1_con.py). Included by fm1_main.c; runs in the main loop
 * (cdc_task), usb_poll moves the bytes. The baud rate is ignored.
 *
 * Line based, no echo, no prompt, each reply one line ending in CR LF:
 *   stat                     key=value pairs: stage, uptime, frames, frame ms (last / mean / max), audio, page, track, playing
 *   page N|NAME              open page N (0..16, the KPage numbers) or by name (home, mixer, roll, "env 1", ...)
 *   track N                  select track N (1..4)
 *   play                     toggle play
 *   key ID down|up           K_ACT_KEY, ID 0..26 (the key bed: 0..15 white keys, 16..26 black ones)
 *   knob K DELTA             turn encoder K by DELTA detents as the panel does: 0 select, 1 algo, 2..5 K1..K4, 6 presets
 *                            (names too: sel algo k1 k2 k3 k4 presets)
 *   btn NAME                 press + release a panel button (octdn octup fx sel env lfo edit glo home save arp seq play rec)
 *   note N VEL               a live note in (k_note_in), VEL 0 = off
 *   inp                      the key / LED scan since the last `inp`: tick gaps, each column's longest and total on-time
 *   prof                     reset the timing window (frame max / mean, audio worst); `stat` then reports that window
 *   save [1..6]              the song into a slot (default: the slot it came from, else 1), as the SAVE page's SAVE does
 *   load [1..6]              a song slot into the app (default: the one saved last, as the boot does); untouched when it fails
 *   songput N / songdata HEX / songload   a song file sent from the computer (fm1_put_song.py): N bytes announced, then the
 *                            bytes as hex, ~24 a line, then loaded into the app (k_song_load; refused, the app as it was). `save N`
 *                            then keeps it in slot N
 *   slots                    the slots: s1..s6 the songs, p1..p24 the presets, each NAME ('_' for a space), '*' unnamed, '-' empty
 *   slotget song|preset N    a slot's bytes as kept (k_save_get) into the transfer buffer: ok bytes=N crc=0x... (zlib CRC-32)
 *   slotdata OFF             64 of them from OFF as hex: ok OFF HEX (fm1_slots.py pull)
 *   slotput song|preset N CRC   the bytes sent (songput, songdata) into slot N, when their CRC-32 is CRC and the load would take
 *                            them (k_save_put: k_song_check / k_preset_check); refused while playing (fm1_slots.py push)
 *   songinfo                 what the boot did (boot=loaded|none|unclean|home|unfinished|refused), the last store call, then a line
 *                            per object: both sectors (blank / valid / damaged / foreign, sequence, bytes), the current one, the
 *                            song's name or the bank's used presets (#)
 *   songclear yes            erases every Kreisel sector, 0xEA000..0xFBFFF (all six songs and 24 presets)
 *   plugs                    the outside plugins (fm1_plugins.c): a line of the region (images, free bytes, RAM pool, what the start
 *                            did), then a line an image: its number, place, size, id, version, state (live off skipped bad older
 *                            refused), why it was refused
 *   plugput SIZE RAM         an image of SIZE bytes, its .data + .bss RAM bytes, is to come: the FM-1 picks its place and RAM, erases
 *                            the sectors, says ok base=0x.. ram=0x.. (the sender links the .kpl for them: fm1_plugin.py)
 *   plugdata HEX             its next bytes (~27 a line), written a page at a time; the header's page waits for plugend
 *   plugend CRC              all sent and its CRC-32 is CRC: the header checked (linked for that place), read back through XIP, the
 *                            magic written last; then live at once unless an image of its id is live (then from the next start)
 *   plugoff N / plugon N     switch image N off / on from the next start (a bit of its header; on also takes it now if it can)
 *   plugerase N              erase image N (not one that is live: plugoff N, restart, then erase)
 *                            The plug commands that write flash are refused while playing (the sound stops for each erase).
 *   usb, crash, memr ADDR [LEN], uboot yes   (read-only diagnostics, and the SysEx soft key)
 * Every write command sets input_seen so the screen redraws. Nothing here writes memory; save and songclear write flash, only
 * Kreisel's own sectors (fm1_store.c). */
#define CON_LINE 64u

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t stalled;             /* the host stopped reading: drop the rest of this reply */
} con;

static void con_putc(char c)
{
    uint32_t t0 = ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}
static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}
static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}
static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}
static void con_kv(const char *k, int32_t v)            /* " key=value" */
{
    con_putc(' ');
    con_puts(k);
    con_putc('=');
    con_dec(v);
}
static void con_ms3(const char *k, uint32_t us)         /* " key=12.345" (a time in us as ms) */
{
    con_putc(' ');
    con_puts(k);
    con_putc('=');
    con_dec((int32_t)(us / 1000u));
    con_putc('.');
    con_putc((char)('0' + us / 100u % 10u));
    con_putc((char)('0' + us / 10u % 10u));
    con_putc((char)('0' + us % 10u));
}
static void con_end(void) { con_puts("\r\n"); }
static void con_ok(void) { con_puts("ok\r\n"); }
static void con_err(const char *why) { con_puts("err "); con_puts(why); con_end(); }

static const char *con_skip(const char *s)
{
    while (*s == ' ')
        s++;
    return s;
}
static int con_num(const char **p, int32_t *out)        /* decimal (signed) or 0x hex */
{
    const char *s = con_skip(*p);
    uint32_t v = 0, base = 10, d, n = 0;
    int neg = 0;
    if (*s == '-')
        neg = 1, s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    if (!n || (*s && *s != ' '))
        return 0;
    *p = s;
    *out = neg ? -(int32_t)v : (int32_t)v;
    return 1;
}
static struct { uint8_t b[K_SAVE_BUF]; uint32_t len, n; uint8_t out; } up;   /* a file on its way in (songput, songdata, songload,
                                                 * slotput) or out (slotget, slotdata: out 1) */
static int con_hexdig(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = con_skip(*p);
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}
/* names compare without case and spaces: "env 1" == "ENV1" == "env1" */
static char con_fold(char c) { return (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); }
static int con_name_is(const char *arg, const char *name)
{
    for (;;) {
        while (*arg == ' ')
            arg++;
        while (*name == ' ')
            name++;
        if (!*arg || !*name)
            return !*arg && !*name;
        if (con_fold(*arg++) != con_fold(*name++))
            return 0;
    }
}
static int con_pick(const char *arg, const char *const *names, uint32_t n)   /* index of a name, or -1 */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (names[i] && con_name_is(arg, names[i]))
            return (int)i;
    return -1;
}

static const char *const CON_BTN[14] = {"octdn", "octup", "fx", "sel", "env", "lfo", "edit", "glo",
                                        "home", "save", "arp", "seq", "play", "rec"};
static const char *const CON_ENC[7] = {"sel", "algo", "k1", "k2", "k3", "k4", "presets"};

/* ---- the outside plugins (fm1_plugins.c) ---- */
/* an install on its way: the header's page kept in up.b[0 .. 255] until plugend, the page being filled in up.b[256 .. 511] */
static struct { uint32_t off, size, ram, n, crc; uint8_t on; } pin;
_Static_assert(sizeof up.b >= 512u, "the console's buffer holds an image's first page and the one being filled");
static void con_uid(uint32_t u)
{
    int k;
    for (k = 24; k >= 0; k -= 8) {
        const char c = (char)(u >> k);
        con_putc(c > ' ' && c < 0x7F ? c : '?');
    }
}
static void con_plug_rc(const char *what, int rc)
{
    con_puts("err ");
    con_puts(what);
    con_putc(' ');
    con_dec(rc);
    con_end();
}
static void con_plugs(void)
{
    static const char *const ST[7] = {"new", "live", "off", "skipped", "bad", "older", "refused"};
    static const char *const BO[6] = {"none", "loaded", "home", "failed", "noflash", "xipbad"};   /* (pl_boot_res) */
    int i;
    con_puts("plugs");
    con_kv("n", pl_n);
    con_kv("live", pl_live());
    con_kv("free", (int32_t)pl_free());
    con_kv("ram", (int32_t)pl_ram_used());
    con_kv("pool", (int32_t)K_FM1_PLUG_RAM);
    con_puts(" boot=");
    con_puts(BO[pl_boot_res]);
    con_kv("xip", pl_xip);
    con_end();
    for (i = 0; i < pl_n; i++) {
        const KImageSlot *s = &pl_slot[i];
        con_puts("plug ");
        con_dec(i + 1);
        con_puts(" at=0x");
        con_hex(PL_LO + s->off, 6);
        con_kv("bytes", (int32_t)s->h.size);
        con_puts(" uid=");
        con_uid(s->h.uid);
        con_kv("v", s->h.version);
        con_kv("kind", s->h.kind);
        con_kv("ram", (int32_t)(s->h.data_size + s->h.bss_size));
        con_puts(" state=");
        con_puts(ST[s->state >= 0 && s->state <= K_IMG_REFUSED ? s->state : 0]);
        con_kv("on", s->state == K_IMG_BAD ? 0 : k_image_on(&s->h));
        if (s->err)
            con_kv("err", s->err);
        if (s->state == K_IMG_LIVE) {
            con_kv("engine", s->index);
            con_puts(" name=");
            con_puts(s->h.kind == K_PLUG_EFFECT ? k_effect_at(s->index)->desc->name : k_engine_at(s->index)->name);
        }
        con_end();
    }
}
static void con_plugput(const char *p)
{
    int32_t a, b;
    long off;
    uint32_t ram = 0, s;
    int rc;
    pin.on = 0;
    up.len = up.n = 0;
    up.out = 0;
    if (!con_num(&p, &a) || !con_num(&p, &b) || a < (int32_t)K_IMAGE_HDR || (uint32_t)a > PL_LEN || (a & 3) || b < 0 ||
        (uint32_t)b > K_FM1_PLUG_RAM) {
        con_err("plugput SIZE RAM (the image's bytes, its .data + .bss bytes)");
        return;
    }
    if (!flash_ok) { con_err("plugput no flash"); return; }
    if (app.playing) { con_err("plugput playing (stop first)"); return; }
    if (!pl_map()) { con_err("plugput xip (the XIP window reads otherwise than the flash)"); return; }
    pl_rescan();
    if (pl_n >= PL_MAX) { con_err("plugput full (plugerase one)"); return; }
    if ((off = k_image_place(pl_slot, pl_n, PL_LEN, (uint32_t)a)) < 0) { con_err("plugput no room"); return; }
    if (b && !(ram = k_image_ram_place(pl_slot, pl_n, PL_RAM_AT, K_FM1_PLUG_RAM, (uint32_t)b))) { con_err("plugput no RAM"); return; }
    input_seen = 1;
    for (s = 0; s < (uint32_t)a; s += K_IMAGE_SECTOR)
        if ((rc = pl_erase((uint32_t)off + s)) != 0) { con_plug_rc("plugput erase", rc); return; }
    pin.off = (uint32_t)off;
    pin.size = (uint32_t)a;
    pin.ram = ram;
    pin.n = pin.crc = 0;
    pin.on = 1;
    con_puts("ok base=0x");
    con_hex(FM1_XIP(PL_LO + pin.off), 8);
    con_puts(" ram=0x");
    con_hex(ram, 8);
    con_end();
}
static void con_plugdata(const char *p)
{
    uint32_t got = 0;
    int rc;
    p = con_skip(p);
    while (pin.on && p[0] && p[1] && pin.n < pin.size) {
        const int hi = con_hexdig(p[0]), lo = con_hexdig(p[1]);
        const uint32_t at = pin.n;
        uint8_t v;
        if (hi < 0 || lo < 0) break;
        v = (uint8_t)(hi << 4 | lo);
        up.b[at < 256u ? at : 256u + (at & 255u)] = v;
        pin.crc = k_image_crc32(pin.crc, &v, 1u);
        pin.n++;
        p += 2;
        got++;
        if (at >= 256u && (at & 255u) == 255u && (rc = pl_prog(pin.off + (at & ~255u), up.b + 256, 256u)) != 0) {
            pin.on = 0;
            con_plug_rc("plugdata write", rc);
            return;
        }
    }
    if (!got) { con_err("plugdata HEX (after plugput)"); return; }
    con_puts("ok n=");
    con_dec((int32_t)pin.n);
    con_end();
}
static void con_plugend(const char *p)
{
    int32_t c;
    KImageHeader h;
    const uint8_t *x;
    const uint32_t first = pin.size < 256u ? pin.size : 256u;
    int rc, i;
    if (!pin.on || pin.n != pin.size || !con_num(&p, &c)) { con_err("plugend CRC (after plugput and all its plugdata)"); return; }
    pin.on = 0;
    if (pin.crc != (uint32_t)c) { con_err("plugend crc"); return; }
    if ((rc = k_image_header(up.b, pin.size, &h)) != 0) { con_plug_rc("plugend header", rc); return; }
    if (h.size != pin.size || h.base != FM1_XIP(PL_LO + pin.off) || h.ram != pin.ram || h.nreloc) {
        con_err("plugend not linked for the place plugput gave");
        return;
    }
    if (pin.size > 256u && (pin.size & 255u) && (rc = pl_prog(pin.off + (pin.size & ~255u), up.b + 256, pin.size & 255u)) != 0) {
        con_plug_rc("plugend write", rc);
        return;
    }
    if ((rc = pl_prog(pin.off + 4u, up.b + 4, first - 4u)) != 0) { con_plug_rc("plugend write", rc); return; }
    x = pl_xip_at(pin.off);                     /* read back as it will run, before the magic makes it an image */
    if (memcmp(x + 4, up.b + 4, K_IMAGE_HDR - 4u) || k_image_crc32(0, x + K_IMAGE_HDR, pin.size - K_IMAGE_HDR) != h.crc) {
        con_err("plugend read back (the sectors stay free)");
        return;
    }
    if ((rc = pl_prog(pin.off, up.b, 4u)) != 0) { con_plug_rc("plugend magic", rc); return; }
    input_seen = 1;
    if ((i = pl_take(pin.off)) < 0) { con_err("plugend not found after writing"); return; }
    con_puts("ok n=");
    con_dec(i + 1);
    con_puts(" uid=");
    con_uid(pl_slot[i].h.uid);
    con_kv("live", pl_slot[i].state == K_IMG_LIVE);
    if (pl_slot[i].err)
        con_kv("err", pl_slot[i].err);   /* (-4 K_PLUG_E_TAKEN: its id is live: from the next start, the newest of them) */
    con_end();
}
static void con_plug_n(const char *p, int on, int erase)   /* plugoff N, plugon N, plugerase N */
{
    int32_t a;
    int i, rc;
    if (!con_num(&p, &a) || (i = pl_find((int)a)) < 0) { con_err("plug number (plugs)"); return; }
    if (app.playing) { con_err("playing (stop first)"); return; }
    input_seen = 1;
    if (erase) {
        if ((rc = pl_erase_image(i)) != 0) {
            con_plug_rc(rc == -1 ? "plugerase live (plugoff N, restart, then plugerase N)" : "plugerase", rc);
            return;
        }
        con_ok();
        return;
    }
    if ((rc = pl_switch(i, on)) != 0 && rc != -1) {
        con_plug_rc(rc == -2 ? "plug no switch left (install it again)" : "plug switch", rc);
        return;
    }
    if (on && pl_slot[i].state != K_IMG_LIVE) {
        pl_open_code();
        if (k_image_load_one(pl_xip_at(0), FM1_XIP(PL_LO), PL_LEN, pl_slot, pl_n, i, &PL_POOL))
            pl_mark.state = PL_ACTIVE;
    }
    con_puts("ok n=");
    con_dec(i + 1);
    con_kv("on", k_image_on(&pl_slot[i].h));
    con_kv("live", pl_slot[i].state == K_IMG_LIVE);   /* (switched off while live: it plays until the next start) */
    con_end();
}

static void con_stat(void)
{
    uint32_t n = prof_n;
    con_puts("stat");
    con_kv("stage", (int32_t)kdiag.stage);
    con_kv("up_ms", (int32_t)ms);
    con_kv("frames", (int32_t)kdiag.frames);
    con_ms3("frame_last_ms", kdiag.frame_last_us);
    {   /* the last frame's parts (fm1_main.c fm1_mark): ground, header, page, cards, cursor+hint, LCD */
        extern uint32_t fm1_part_us(int k);
        static const char *const N[6] = {"t_ground_us", "t_header_us", "t_page_us", "t_cards_us", "t_rest_us", "t_lcd_us"};
        int k;
        for (k = 0; k < 6; k++) con_kv(N[k], (int32_t)fm1_part_us(k));
        con_kv("t_v_frame_us", (int32_t)fm1_part_us(10)); con_kv("t_v_screen_us", (int32_t)fm1_part_us(11));
        con_kv("t_v_rows_us", (int32_t)fm1_part_us(12)); con_kv("t_v_line_us", (int32_t)fm1_part_us(13));   /* (inside a view, marks 11 ..) */
        con_kv("c_label_us", (int32_t)fm1_part_us(20)); con_kv("c_knob_us", (int32_t)fm1_part_us(21));
        con_kv("c_places_us", (int32_t)fm1_part_us(22)); con_kv("c_value_us", (int32_t)fm1_part_us(23));   /* (sk_control, all of a frame's) */
        con_kv("k_disc_us", (int32_t)fm1_part_us(24)); con_kv("k_ring_us", (int32_t)fm1_part_us(25));
        { extern uint32_t ui_knob_stat[3]; con_kv("knobs", (int32_t)ui_knob_stat[0]); con_kv("flat", (int32_t)ui_knob_stat[1]); con_kv("copied", (int32_t)ui_knob_stat[2]); }
    }
    con_ms3("frame_mean_ms", n ? prof_sum / n : kdiag.frame_avg_us);
    con_ms3("frame_max_ms", kdiag.frame_max_us);
    con_kv("prof_frames", (int32_t)n);
    con_kv("audio_max_us", (int32_t)diag_audio(0));
#if KREISEL_FM1_AUDIO
    con_kv("audio_gap_max_us", (int32_t)audio_gap_max_us);
    con_kv("sync_max_us", (int32_t)sync_max_us);
    con_kv("audio_same_half", (int32_t)audio_same_half);
    con_kv("vcap", k_audio_voice_cap_now());                /* the voices a track may take now (the load guard thins them) */
    con_kv("audio_offs", (int32_t)audio_offs);             /* the times the sound had to pause */
#endif
    con_kv("audio_halves", (int32_t)diag_audio(1));
    con_kv("audio_over", (int32_t)diag_audio(2));
    con_kv("audio_on", (int32_t)(diag_audio(3) & 1u));
    con_kv("audio_peak", (int32_t)(diag_audio(3) >> 1));
    con_putc(' ');
    con_puts("page=");
    con_dec((int32_t)app.page);
    con_putc(':');
    con_puts(k_page_name(app.page));
    con_kv("track", app.track + 1);
    con_kv("playing", app.playing);
    con_kv("bpm", app.bpm);
    con_kv("clk", app.clock_src);
    con_kv("master", app.master);
    con_kv("batt_raw", batt_raw);
    con_kv("batt", app.battery);
    con_kv("usb_power", app.charging);
    con_end();
}

static void con_page(const char *p)
{
    int32_t n;
    const char *a = con_skip(p);
    if (!con_num(&p, &n)) {
        const char *names[K_PAGE_COUNT];
        uint32_t i;
        for (i = 0; i < K_PAGE_COUNT; i++)
            names[i] = k_page_name((KPage)i);
        n = con_pick(a, names, K_PAGE_COUNT);
    }
    if (n < 0 || n >= (int32_t)K_PAGE_COUNT) {
        con_err("page 0..18 or a name (home step drum roll song synth env1 env2 envdest fx mixer bus1 bus2 lfo arp scl clock tracks edit)");
        return;
    }
    k_open_page(&app, (KPage)n);
    input_seen = 1;
    con_ok();
}

static void con_exec(const char *p)
{
    int32_t a, b;
    int put;
    p = con_skip(p);
    if (boot_locked) {   /* (fm1_bootcheck.c: not M-VAVE's bootloader) only what reads: nothing touches the flash or the app */
        const char *q = p;
        if (!con_word(&q, "boot") && !con_word(&q, "ver") && !con_word(&q, "usb") && !con_word(&q, "memr") &&
            !con_word(&q, "crash") && !con_word(&q, "uboot")) {
            con_err("locked: not M-VAVE's bootloader (boot)");
            return;
        }
    }
    if (con_word(&p, "stat"))
        con_stat();
    else if (con_word(&p, "page"))
        con_page(p);
    else if (con_word(&p, "track")) {
        if (!con_num(&p, &a) || a < 1 || a > K_TRACKS) {
            con_err("track 1..4");
            return;
        }
        act(K_ACT_TRACK_DELTA, (int)a - 1 - app.track, 0, ms);
        con_ok();
    } else if (con_word(&p, "play")) {
        act(K_ACT_PLAY, 0, 0, ms);
        con_ok();
    } else if (con_word(&p, "key")) {
        if (!con_num(&p, &a) || a < 0 || a > 26) {
            con_err("key 0..26 down|up");
            return;
        }
        if (con_word(&p, "down"))
            act(K_ACT_KEY, (int)a, 1, ms);
        else if (con_word(&p, "up"))
            act(K_ACT_KEY, (int)a, 0, ms);
        else {
            con_err("key ID down|up");
            return;
        }
        con_ok();
    } else if (con_word(&p, "knob")) {
        int k = -1;
        if (con_num(&p, &a))
            k = (int)a;
        else {
            uint32_t i;
            for (i = 0; i < 7u && k < 0; i++) {
                const char *q = p;
                if (con_word(&q, CON_ENC[i]))
                    k = (int)i, p = q;
            }
        }
        if (k < 0 || k > 6) {
            con_err("knob 0..6|sel|algo|k1..k4|presets DELTA");
            return;
        }
        if (!con_num(&p, &b)) {
            con_err("knob K DELTA");
            return;
        }
        enc_event((uint32_t)k, b, ms);
        con_ok();
    } else if (con_word(&p, "btn")) {
        int i = con_pick(con_skip(p), CON_BTN, 14);
        if (i < 0) {
            con_err("btn octdn octup fx sel env lfo edit glo home save arp seq play rec");
            return;
        }
        btn_event((uint32_t)i, 1, ms);
        btn_event((uint32_t)i, 0, ms);
        con_ok();
    } else if (con_word(&p, "ver")) {                   /* ver: Kreisel's version (kreisel_version.h) */
        con_puts("ver " KREISEL_VERSION);
        con_end();
    } else if (con_word(&p, "note")) {
        if (!con_num(&p, &a) || !con_num(&p, &b) || a < 0 || a > 127 || b < 0 || b > 127) {
            con_err("note N VEL (0..127)");
            return;
        }
        k_note_in(&app, (int)a, (int)b);
        input_seen = 1;
        con_ok();
    } else if (con_word(&p, "save")) {                  /* save [1..6]: the song into a slot (the one it came from, else 1) */
        int slot = app.song_from ? app.song_from - 1 : 0;
        if (con_num(&p, &a)) {
            if (a < 1 || a > K_SONG_SLOTS) {
                con_err("save [1..6]");
                return;
            }
            slot = (int)a - 1;
        }
        input_seen = 1;
        if (k_save_song(&app, slot)) {
            con_puts("err save ");
            con_dec(ks_last.rc);
            con_end();
        } else {
            con_puts("ok slot=");
            con_dec(slot + 1);
            con_kv("bytes", (int32_t)ks_last.len);
            con_kv("sector", (int32_t)ks_base(ks_last.obj, ks_last.copy));
            con_kv("seq", (int32_t)ks_last.seq);
            con_end();
        }
    } else if (con_word(&p, "load")) {                  /* load [1..6]: a song slot (default: the one saved last) */
        int slot = ks_latest(0);
        if (con_num(&p, &a))
            slot = (int)a - 1;
        input_seen = 1;
        if (slot < 0 || k_save_load(&app, slot)) {
            con_puts("err load ");
            con_dec(ks_last.rc);
            con_end();
        } else {
            con_puts("ok slot=");
            con_dec(slot + 1);
            con_end();
        }
    } else if (con_word(&p, "songput")) {               /* songput N: a song file of N bytes follows (songdata, songload) */
        if (!con_num(&p, &a) || a < 16 || a > (int32_t)sizeof up.b) { con_puts("err songput size"); con_end(); }
        else { up.len = (uint32_t)a; up.n = up.out = 0; pin.on = 0; con_puts("ok"); con_end(); }   /* (an install's pages: gone) */
    } else if (con_word(&p, "songdata")) {              /* songdata HEX: the next bytes */
        uint32_t got = 0;
        while (*p == ' ') p++;
        while (p[0] && p[1] && up.n < up.len && !up.out) {
            const int hi = con_hexdig(p[0]), lo = con_hexdig(p[1]);
            if (hi < 0 || lo < 0) break;
            up.b[up.n++] = (uint8_t)(hi << 4 | lo); p += 2; got++;
        }
        con_puts(got ? "ok n=" : "err songdata"); if (got) con_dec((int32_t)up.n); con_end();
    } else if (con_word(&p, "songload")) {              /* songload: the bytes sent, into the app */
        int rc = up.len && up.n == up.len && !up.out ? k_song_load(&app, up.b, up.n) : -100;
        input_seen = 1;
        if (rc) { con_puts("err songload "); con_dec(rc); con_end(); }
        else { con_puts("ok name="); con_puts(app.song_name); con_kv("bytes", (int32_t)up.n); con_end(); }
        up.len = up.n = 0;
    } else if (con_word(&p, "slots")) {                 /* every slot's name: s1..s6, p1..p24 */
        int k, i;
        const char *q;
        con_puts("ok");
        for (k = 0; k < 2; k++)
            for (i = 0; i < (k ? K_USER_PRESETS : K_SONG_SLOTS); i++) {
                con_putc(' ');
                con_putc(k ? 'p' : 's');
                con_dec(i + 1);
                con_putc('=');
                q = k_save_name(k ? K_STORE_PRESET : K_STORE_SONG, i);
                if (!k_save_used(k ? K_STORE_PRESET : K_STORE_SONG, i))
                    con_putc('-');
                else if (!*q)
                    con_putc('*');
                else
                    for (; *q; q++)
                        con_putc(*q == ' ' ? '_' : *q);
            }
        con_end();
    } else if ((put = con_word(&p, "slotput")) != 0 || con_word(&p, "slotget")) {   /* a slot in (from the buffer) or out (into it) */
        const int kind = con_word(&p, "song") ? K_STORE_SONG : con_word(&p, "preset") ? K_STORE_PRESET : -1;
        if (kind < 0 || !con_num(&p, &a) || a < 1 || a > (kind == K_STORE_SONG ? K_SONG_SLOTS : K_USER_PRESETS) || (put && !con_num(&p, &b))) {
            con_err(put ? "slotput song 1..6|preset 1..24 CRC" : "slotget song 1..6|preset 1..24");
            return;
        }
        if (!put) {
            const long n = (pin.on = 0, k_save_get(kind, (int)a - 1, up.b, sizeof up.b));   /* (an install's pages: gone) */
            up.len = up.n = 0;
            up.out = 0;
            if (n <= 0) { con_err(n ? "slotget read" : "slotget empty"); return; }
            up.len = up.n = (uint32_t)n;
            up.out = 1;
            con_puts("ok");
            con_kv("bytes", (int32_t)n);
            con_puts(" crc=0x");
            con_hex(ks_crc32(up.b, up.n), 8);
            con_end();
        } else {
            int rc;
            if (up.out || !up.len || up.n != up.len) { con_err("slotput: nothing sent (songput, songdata)"); return; }
            if (ks_crc32(up.b, up.n) != (uint32_t)b) { con_err("slotput crc"); return; }
            input_seen = 1;
            rc = k_save_put(&app, kind, (int)a - 1, up.b, up.n);
            up.len = up.n = 0;
            if (rc == K_SAVE_ERR_PLAYING) { con_err("slotput playing (stop first)"); return; }
            if (rc) { con_puts("err slotput "); con_dec(rc); con_kv("store", ks_last.rc); con_end(); return; }
            con_puts("ok slot=");
            con_dec((int32_t)a);
            con_kv("bytes", (int32_t)(10u + (up.b[6] | (uint32_t)up.b[7] << 8)));   /* (the file's own length: what was kept) */
            con_puts(" name=");
            con_puts(k_save_name(kind, (int)a - 1));
            con_end();
        }
    } else if (con_word(&p, "slotdata")) {              /* slotdata OFF: 64 bytes of what slotget read, as hex */
        uint32_t k, e;
        if (!up.out || !con_num(&p, &a) || a < 0 || (uint32_t)a >= up.len) { con_err("slotdata OFF (after slotget)"); return; }
        e = (uint32_t)a + 64u < up.len ? (uint32_t)a + 64u : up.len;
        con_puts("ok ");
        con_dec(a);
        con_putc(' ');
        for (k = (uint32_t)a; k < e; k++)
            con_hex(up.b[k], 2);
        con_end();
    } else if (con_word(&p, "songinfo")) {              /* each object's two sectors, the slots' names, what the boot did */
        static const char *const ST[5] = {"blank", "valid", "damaged", "foreign", "readerr"};
        static const char *const BO[9] = {"notrun", "loaded", "none", "unclean", "home", "unfinished", "refused", "restoreoff", "demo"};   /* (ks_boot_res; 7, 8 were past the list) */
        ks_hdr_t h;
        int o, c, k;
        con_puts("songinfo flash_ok=");
        con_dec(flash_ok);
        con_puts(" flash_id=0x");
        con_hex(flash_id, 6);
        con_puts(" boot=");
        con_puts(BO[ks_boot_res]);
        con_kv("boot_ms", (int32_t)ks_boot_ms);
        con_kv("latest", ks_latest(0) + 1);
        con_kv("last_op", ks_last.op);
        con_kv("last_rc", ks_last.rc);
        con_end();
        for (o = 0; o < KS_OBJS; o++) {
            con_puts(o < KS_BANK0 ? "song " : "bank ");
            con_dec(o < KS_BANK0 ? o + 1 : o - KS_BANK0 + 1);
            for (c = 0; c < 2; c++) {
                int st = ks_state(o, c, &h);
                con_puts(c ? " B=" : " A=");
                con_puts(ST[st]);
                con_puts("@0x");
                con_hex(ks_base(o, c), 6);
                if (st == KS_VALID) {
                    con_kv("seq", (int32_t)h.seq);
                    con_kv("bytes", (int32_t)h.len);
                }
            }
            con_puts(" current=");
            k = ks_current(o);
            con_puts(k < 0 ? "none" : k ? "B" : "A");
            if (o < KS_BANK0) {
                con_puts(" name=");
                con_puts(k_save_name(K_STORE_SONG, o));
            } else
                for (k = 0; k < KS_CELLS; k++)
                    con_putc(k_save_used(K_STORE_PRESET, (o - KS_BANK0) * KS_CELLS + k) ? '#' : '.');
            con_end();
        }
    } else if (con_word(&p, "flr")) {                   /* flr OFF [LEN<=32]: read the SPI flash (read only) */
        uint8_t fb[32];
        uint32_t k;
        if (!con_num(&p, &a) || a < 0 || a >= 0x100000) {
            con_err("flr OFF [LEN<=32]");
            return;
        }
        if (!con_num(&p, &b) || b <= 0 || b > 32)
            b = 32;
        if (ota_fread((uint32_t)a, fb, (uint32_t)b)) {
            con_err("flr read");
            return;
        }
        con_hex((uint32_t)a, 6);
        con_putc(':');
        for (k = 0; k < (uint32_t)b; k++) {
            con_putc(' ');
            con_hex(fb[k], 2);
        }
        con_end();
    } else if (con_word(&p, "songclear")) {
        if (con_word(&p, "yes")) {
            int rc = ks_clear();
            k_save_scan();
            if (rc) {
                con_puts("err songclear ");
                con_dec(rc);
                con_end();
            } else
                con_ok();
        } else {
            con_err("type 'songclear yes' (erases every song and preset: " KS_RANGE_TEXT ")");
        }
    } else if (con_word(&p, "gamma")) {                 /* gamma N: the colour mode (fm1_main.c lcd_gamma) */
        if (!con_num(&p, &a) || a < 0 || a > 4) {
            con_err("gamma 0..4");
            return;
        }
        lcd_gamma = (uint32_t)a;
        lcd_lut_init();
        lcd_init();                                     /* (a reset of the panel: its own gamma back to the default, or the table) */
        band_valid = 0;
        input_seen = 1;
        con_ok();
    } else if (con_word(&p, "sat")) {                   /* sat N: saturation in percent (100 = as drawn) */
        if (!con_num(&p, &a) || a < 0 || a > 300) {
            con_err("sat 0..300 (percent)");
            return;
        }
        lcd_sat = (int32_t)(a * 256 / 100);
        band_valid = 0;
        input_seen = 1;
        con_ok();
    } else if (con_word(&p, "tone")) {                  /* tone N: a square, period 2N frames (0 off); level -18 dBFS */
        if (!con_num(&p, &a) || a < 0 || a > 4000) {
            con_err("tone N (half period in frames, 0 off)");
            return;
        }
#if KREISEL_FM1_AUDIO
        tone_half = (uint32_t)a;
#endif
        con_ok();
    } else if (con_word(&p, "inp")) {                   /* the key / LED scan since the last `inp`: tick gaps (a column's on-time) */
        uint32_t i, f = irq_save(), hist[FM1_GAP_BINS], onm[FM1_NCOL], on[FM1_NCOL], gmax = fm1_in_stat.gap_max, dn = fm1_in_stat.dim_n, ds = fm1_in_stat.dim_sum;
        for (i = 0; i < FM1_GAP_BINS; i++) { hist[i] = fm1_in_stat.gap_hist[i]; fm1_in_stat.gap_hist[i] = 0; }
        for (i = 0; i < FM1_NCOL; i++) { onm[i] = fm1_in_stat.on_max[i]; on[i] = fm1_in_stat.on[i]; fm1_in_stat.on_max[i] = fm1_in_stat.on[i] = 0; }
        fm1_in_stat.gap_max = fm1_in_stat.dim_n = fm1_in_stat.dim_sum = 0;
        irq_restore(f);
        con_puts("inp");
        con_kv("gap_max_us", (int32_t)(gmax / FM1_TICKS_PER_US));
        con_puts(" hist(<.15,.3,.6,1.2,2.4,4.8,9.6,more)=");
        for (i = 0; i < FM1_GAP_BINS; i++) { if (i) con_putc(','); con_dec((int32_t)hist[i]); }
        con_puts(" on_max_us=");
        for (i = 0; i < FM1_NCOL; i++) { if (i) con_putc(','); con_dec((int32_t)(onm[i] / FM1_TICKS_PER_US)); }
        con_puts(" on_ms=");
        for (i = 0; i < FM1_NCOL; i++) { if (i) con_putc(','); con_dec((int32_t)(on[i] / FM1_TICKS_PER_US / 1000u)); }
        con_kv("dim_n", (int32_t)dn);
        con_kv("dim_avg_ns", dn ? (int32_t)((uint64_t)ds * 1000u / FM1_TICKS_PER_US / dn) : 0);
        con_end();
    } else if (con_word(&p, "prof")) {
        kdiag.frame_max_us = 0;
        prof_n = prof_sum = 0;
        diag_audio_reset();
        con_ok();
    } else if (con_word(&p, "usb")) {
        con_puts("usb");
        con_kv("resets", (int32_t)usb.resets);
        con_kv("sof", (int32_t)usb.sof_seen);
        con_kv("suspends", (int32_t)usb.suspends);
        con_kv("retries", (int32_t)usb.retries);
        con_kv("midi_rx", (int32_t)usb.rx_pkts);
        con_kv("midi_tx", (int32_t)usb.tx_pkts);
        con_kv("cdc_rx", (int32_t)cdc.rx_pkts);
        con_kv("cdc_tx", (int32_t)cdc.tx_pkts);
        con_end();
#if FELUCCA_UAC
    } else if (con_word(&p, "uac")) {                   /* uac: the USB audio input's state (the computer records the FM-1) */
        con_puts("uac");
        con_kv("alt", uac.alt); con_kv("r48", uac.r48); con_kv("go", uac.go); con_kv("flowing", uac.flowing); con_kv("feed", uac.feed);
        con_kv("starts", (int32_t)uac.starts); con_kv("pkts", (int32_t)uac.pkts); con_kv("frames", (int32_t)uac.frames);
        con_kv("underruns", (int32_t)uac.underruns); con_kv("overruns", (int32_t)uac.overruns); con_kv("missed", (int32_t)uac.missed);
        con_kv("stalls", (int32_t)uac.stalls); con_kv("rate_sets", (int32_t)uac.rate_sets);
        con_end();
#endif
    } else if (con_word(&p, "crash")) {
        const uint32_t *w = (const uint32_t *)&kdiag.rec;
        uint32_t i;
        if (kdiag.rec.magic != FM1_CRASH_MAGIC) {
            con_puts("no crash record\r\n");
            return;
        }
        con_puts("crash");
        for (i = 0; i < sizeof kdiag.rec / 4u; i++) {
            con_puts(" 0x");
            con_hex(w[i], 8);
        }
        con_end();
    } else if (con_word(&p, "boot")) {   /* the boot area and the mapping, read again now (fm1_bootcheck.c) */
        BootInfo bi;
        boot_inspect(&bi);
        con_puts("boot hdr="); con_hex(bi.hdr_ok, 1);
        con_puts(" cont="); con_hex(bi.cont_ok, 1);
        con_puts(" len=0x"); con_hex(bi.blen, 4);
        con_puts(" pcrc=0x"); con_hex(bi.pcrc, 4);
        con_puts(" other="); con_hex(bi.other_bl, 1);
        con_puts(" stock="); con_hex(bi.stock, 1);
        con_puts(" appdir=0x"); con_hex(bi.appdir, 5);
        con_puts(" mapped="); con_hex(bi.mapped, 1);
        con_puts(" verdict="); con_hex(bi.verdict, 1);
        con_end();
    } else if (con_word(&p, "memr")) {
        uint32_t i, len = 64u;
        if (!con_num(&p, &a)) {
            con_err("memr ADDR [LEN<=64]");
            return;
        }
        if (con_num(&p, &b) && b > 0)
            len = (uint32_t)b > 64u ? 64u : (uint32_t)b;
        if (!fm1_mem_readable((uint32_t)a, len)) {
            con_err("only RAM 01C00000-01C80000 and XIP 02000000-02100000");
            return;
        }
        con_hex((uint32_t)a, 8);
        con_putc(':');
        for (i = 0; i < len; i++) {
            con_putc(' ');
            con_hex(fm1_peek8((uint32_t)a + i), 2);
        }
        con_end();
    } else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_err("type 'uboot yes'");
        }
    } else if (con_word(&p, "plugs"))
        con_plugs();
    else if (con_word(&p, "plugput"))
        con_plugput(p);
    else if (con_word(&p, "plugdata"))
        con_plugdata(p);
    else if (con_word(&p, "plugend"))
        con_plugend(p);
    else if (con_word(&p, "plugoff"))
        con_plug_n(p, 0, 0);
    else if (con_word(&p, "plugon"))
        con_plug_n(p, 1, 0);
    else if (con_word(&p, "plugerase"))
        con_plug_n(p, 0, 1);
    else if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("stat page track play key knob btn note save load slots slotget slotdata slotput songinfo songclear "
                 "plugs plugput plugdata plugend plugoff plugon plugerase prof usb crash memr uboot\r\n");
    else if (*p)
        con_err("? (help)");
}

static void cdc_task(void)                              /* main loop */
{
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
        } else if (c == 0x7F || c == 0x08) {
            if (con.len)
                con.len--;
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
        }
    }
}

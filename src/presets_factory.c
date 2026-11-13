/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The factory presets: a few sounds of each engine, PRESETS steps through them before the user's (kreisel_save.c). Included by
 * kreisel.c (k_factory_*). Each is a line of settings over the engine's defaults, words split by spaces:
 *   LABEL=V      an engine parameter by its label (KEngine.edit), V a number or, for a list, one of its names (WAVE1=SAW)
 *   A= D= S= R=  the amplitude envelope (ENV 1; PHASE's DCA, ENV 2), 0..100
 *   WA= WD= WS= WR=  PHASE's DCW envelope (ENV 1)      OA= OD= OS= OR=  PHASE's DCO envelope (ENV 2's second four)
 *   L.LABEL=V    the LFO page's (RATE WAVE PHS FADE PIT FLT SHP AMP)
 *   LABEL.n=V    DRUM: lane n's (1..8; PIECE LVL PAN TUNE TONE DCAY CLIK ATK PDEP PTIM), a piece by its name with _ for the space
 *                (PIECE.1=T8_BD); every lane starts from a plain piece's settings, the kit's own (KTUN KDCY CRSH HUMN) as LABEL=V
 * Read when one is picked, never in the audio: a few string compares. A label the engine lacks is skipped. */

typedef struct { uint32_t uid; const char *name, *set; } KFactory;
#define KU(a, b, c, d) K_UID(a, b, c, d)
static const KFactory FACTORY[] = {
    /* ANALOG */
    {KU('K','A','N','L'), "Fat Bass",   "WAVE1=SAW WAVE2=SQR PITCH=-12 DTN=6 SUB=60 SUBP=-1 CUT=40 RES=40 ENV=80 DRV=30 A=0 D=35 S=40 R=10"},
    {KU('K','A','N','L'), "Saw Lead",   "WAVE1=SAW WAVE2=SAW DTN=14 CUT=85 RES=25 ENV=65 GLIDE=20 A=0 D=40 S=80 R=20"},
    {KU('K','A','N','L'), "Sync Lead",  "WAVE1=SAW WAVE2=SAW SYNC=1 PITCH=12 CUT=100 RES=20 ENV=60 A=0 D=50 S=70 R=25 L.RATE=55 L.PIT=1 L.FADE=60"},
    {KU('K','A','N','L'), "Soft Pad",   "WAVE1=SAW WAVE2=SAW DTN=16 CUT=55 RES=15 ENV=55 A=55 D=60 S=80 R=60 L.RATE=25 L.FLT=8"},
    {KU('K','A','N','L'), "Pluck",      "WAVE1=P25 WAVE2=SAW LVL2=80 CUT=50 RES=40 ENV=85 A=0 D=35 S=0 R=25"},
    {KU('K','A','N','L'), "Brass",      "WAVE1=SAW WAVE2=SAW DTN=10 CUT=50 RES=10 ENV=75 A=15 D=40 S=70 R=20"},
    {KU('K','A','N','L'), "Poly Keys",  "WAVE1=SAW WAVE2=SQR DTN=8 CUT=60 RES=15 ENV=50 A=0 D=55 S=40 R=30"},
    {KU('K','A','N','L'), "Breath",     "WAVE1=TRI LVL1=70 LVL2=0 NOIS=110 COLOR=40 CUT=70 RES=30 ENV=65 A=40 D=60 S=70 R=50 L.RATE=30 L.FLT=15"},
    {KU('K','A','N','L'), "Rubber Bass", "WAVE1=SQR WAVE2=SAW LVL2=60 PITCH=-12 CUT=35 RES=60 ENV=75 DRV=40 GLIDE=25 A=0 D=30 S=30 R=10"},
    {KU('K','A','N','L'), "Rave Stab",   "WAVE1=SAW WAVE2=SAW DTN=30 CUT=75 RES=20 ENV=55 A=0 D=30 S=35 R=20"},
    {KU('K','A','N','L'), "Deep Sub",    "WAVE1=SIN LVL2=0 SUB=110 SUBP=-2 CUT=30 ENV=30 A=0 D=40 S=70 R=12"},
    {KU('K','A','N','L'), "Noise Sweep", "LVL1=0 LVL2=0 NOIS=110 COLOR=80 CUT=40 RES=85 ENV=70 A=60 D=70 S=50 R=70 L.RATE=20 L.FLT=40"},
    /* PHASE */
    {KU('K','P','H','S'), "Glass",      "WAVE1=DSIN WAVE2=SAW DTN=8 DCW1=90 DCW2=40 DCA2=60 WA=0 WD=45 WS=20 WR=30 A=0 D=60 S=30 R=45"},
    {KU('K','P','H','S'), "Bell",       "WAVE1=DSIN WAVE2=SPLS LINE=RING PITCH=7 DCW1=70 DCW2=60 WA=0 WD=50 WS=0 WR=40 A=0 D=75 S=0 R=60"},
    {KU('K','P','H','S'), "Reso Bass",  "WAVE1=RSAW WAVE2=SAW PITCH=-12 DCW1=100 DCA2=40 WA=0 WD=30 WS=10 WR=10 A=0 D=40 S=60 R=10"},
    {KU('K','P','H','S'), "PD Strings", "WAVE1=SAW WAVE2=SAW DTN=14 DCW1=50 DCW2=50 DCA2=110 WA=40 WD=60 WS=60 WR=50 A=45 D=50 S=85 R=55"},
    {KU('K','P','H','S'), "Sweep",      "WAVE1=RTRP WAVE2=RSAW DCW1=120 DCW2=90 WA=30 WD=70 WS=40 WR=40 A=5 D=40 S=80 R=30"},
    {KU('K','P','H','S'), "Voices",     "WAVE1=RTRI WAVE2=RTRP DTN=10 DCW1=60 DCW2=50 WA=35 WD=50 WS=50 WR=40 A=35 D=50 S=80 R=50 L.RATE=50 L.PIT=1 L.FADE=50"},
    {KU('K','P','H','S'), "Fold Lead",  "WAVE1=SAW DCA2=0 FOLD=60 DCW1=80 GLIDE=15 WA=0 WD=40 WS=60 WR=20 A=0 D=40 S=80 R=20"},
    {KU('K','P','H','S'), "Digi Bass",   "WAVE1=RSAW DCW1=95 DCA1=110 PITCH=-12 WA=0 WD=30 WS=20 WR=10 A=0 D=40 S=60 R=10"},
    {KU('K','P','H','S'), "Crystal",     "WAVE1=DSIN DCW1=60 WAVE2=SPLS DCW2=40 DCA2=70 DTN=6 A=0 D=60 S=40 R=45"},
    {KU('K','P','H','S'), "Digital Pad", "WAVE1=SAW WAVE2=RTRI DCW1=70 DCW2=50 DTN=10 A=50 D=60 S=80 R=60"},
    {KU('K','P','H','S'), "Ring Lead",   "LINE=RING WAVE1=SQR WAVE2=SAW DCO2=7 DCW1=70 DCW2=60 GLIDE=15 A=0 D=30 S=70 R=20"},
    /* TRIO */
    {KU('K','T','R','I'), "Chip Bass",  "W1=PLS W2=SAW W3=TRI INT2=-12 INT3=0 LVL3=0 PW=40 CUT=45 RES=60 ENV=80 A=0 D=35 S=50 R=10"},
    {KU('K','T','R','I'), "Ring Bell",  "MOD=RING W1=TRI W2=TRI INT2=19 LVL3=0 CUT=110 RES=10 A=0 D=70 S=0 R=50"},
    {KU('K','T','R','I'), "Chip Lead",  "W1=PLS W2=PLS PW=30 INT2=12 LVL2=50 LVL3=0 CUT=100 RES=20 A=0 D=30 S=80 R=15 L.RATE=70 L.PIT=2 L.FADE=50"},
    {KU('K','T','R','I'), "Sync Rip",   "MOD=SYNC W1=SAW W2=SAW INT2=19 LVL3=0 CUT=90 RES=30 ENV=70 A=0 D=45 S=60 R=20"},
    {KU('K','T','R','I'), "Major",      "W1=SAW W2=SAW W3=SAW INT2=4 INT3=7 LVL2=110 LVL3=100 CUT=70 RES=20 ENV=60 A=10 D=50 S=70 R=40"},
    {KU('K','T','R','I'), "Minor",      "W1=SAW W2=SAW W3=SAW INT2=3 INT3=7 LVL2=110 LVL3=100 CUT=70 RES=20 ENV=60 A=10 D=50 S=70 R=40"},
    {KU('K','T','R','I'), "Wind",       "W1=NOI LVL2=0 LVL3=0 MODE=BP CUT=60 RES=80 A=60 D=60 S=60 R=60 L.RATE=20 L.FLT=30"},
    {KU('K','T','R','I'), "PWM Pad",    "W1=PLS W2=PLS INT2=0 DTN=12 PW=50 LVL3=0 CUT=60 RES=20 A=50 D=50 S=80 R=60 L.RATE=35 L.SHP=25"},
    {KU('K','T','R','I'), "Chip Brass",  "W1=SAW W2=PLS PW=40 INT2=12 LVL2=80 CUT=80 RES=30 ENV=50 A=10 D=40 S=70 R=20"},
    {KU('K','T','R','I'), "Arp Pluck",   "W1=PLS PW=30 W2=TRI INT2=7 INT3=12 LVL3=60 CUT=60 ENV=80 A=0 D=25 S=0 R=15"},
    {KU('K','T','R','I'), "Ring Chime",  "MOD=RING W1=TRI W2=TRI INT2=19 LVL2=100 CUT=110 A=0 D=70 S=0 R=60"},
    {KU('K','T','R','I'), "Thick Bass",  "W1=SAW W2=SAW INT2=-12 DTN=12 CUT=45 RES=40 ENV=70 DRV=40 A=0 D=35 S=50 R=10"},
    /* LOFI */
    {KU('K','L','O','F'), "Chip Lead",  "WAVE=PLS DUTY=25% VIB=4 A=0 D=30 S=80 R=15"},
    {KU('K','L','O','F'), "Bass Line",  "WAVE=TRI CRSH=8BIT A=0 D=40 S=60 R=10"},
    {KU('K','L','O','F'), "Arp Run",    "WAVE=PLS DUTY=12% ARP=MAJ RATE=2 A=0 D=50 S=70 R=15"},
    {KU('K','L','O','F'), "Laser",      "WAVE=SAW SWP=-50 A=0 D=35 S=0 R=20"},
    {KU('K','L','O','F'), "Power Up",   "WAVE=PLS DUTY=50% SWP=40 ARP=OCT RATE=1 A=0 D=60 S=0 R=20"},
    {KU('K','L','O','F'), "Crush Saw",  "WAVE=SAW CRSH=4BIT SRATE=60 A=0 D=50 S=70 R=20"},
    {KU('K','L','O','F'), "Noise Hit",  "WAVE=PLS NOISE=SHORT NLVL=127 TONE=90 A=0 D=15 S=0 R=5"},
    {KU('K','L','O','F'), "Square Pad", "WAVE=PLS DUTY=50% VIB=2 TONE=80 A=50 D=50 S=80 R=50"},
    {KU('K','L','O','F'), "Noise Hat",   "WAVE=OFF NOISE=WHITE NLVL=120 COLOR=110 A=0 D=10 S=0 R=5"},
    {KU('K','L','O','F'), "Chip Arp",    "WAVE=PLS DUTY=25% ARP=MAJ RATE=2 A=0 D=50 S=60 R=10"},
    {KU('K','L','O','F'), "Zap",         "WAVE=PLS DUTY=12% SWP=-50 A=0 D=30 S=0 R=10"},
    {KU('K','L','O','F'), "Crunch Bass", "WAVE=TRI CRSH=6BIT SRATE=30 A=0 D=40 S=70 R=10"},
    /* ORGAN (a gate: no envelope) */
    {KU('K','O','G','N'), "Jazz",       "16'=8 5.3'=8 8'=8 4'=0 2.7'=0 2'=0 1.6'=0 1.3'=0 1'=0 PERC=3RD ROTR=SLOW VIB=C3"},
    {KU('K','O','G','N'), "Gospel",     "16'=8 5.3'=8 8'=8 4'=8 2.7'=0 2'=0 1.6'=0 1.3'=0 1'=8 PERC=OFF ROTR=FAST VIB=C3"},
    {KU('K','O','G','N'), "Church",     "16'=8 5.3'=4 8'=8 4'=8 2.7'=3 2'=4 1.6'=2 1.3'=2 1'=2 PERC=OFF ROTR=OFF VIB=OFF"},
    {KU('K','O','G','N'), "Drawbar Bass","16'=8 8'=6 5.3'=0 4'=0 2'=0 ROTR=OFF"},
    /* ENSEMBLE */
    {KU('K','E','N','S'), "Strings",    "A=30 D=50 S=90 R=50"},
    {KU('K','E','N','S'), "Cellos",     "BASS=60 CELLO=110 VIOLA=40 VLN=0 TONE=70 A=30 D=50 S=90 R=50"},
    {KU('K','E','N','S'), "Brass Str",  "BRASS=90 VLN=60 TONE=100 A=15 D=50 S=80 R=35"},
    {KU('K','E','N','S'), "Bright",     "BASS=0 CELLO=30 VIOLA=90 VLN=120 TONE=120 HPF=30 A=25 D=50 S=90 R=45"},
    /* FM2 */
    {KU('K','F','M','2'), "E.Piano",    "RATIO=1 INDEX=45 ENV=100 FEED=10 A=0 D=60 S=20 R=40"},
    {KU('K','F','M','2'), "Bell",       "RATIO=4 TUNE=20 INDEX=60 ENV=100 A=0 D=80 S=0 R=60"},
    {KU('K','F','M','2'), "FM Bass",    "RATIO=1 INDEX=70 ENV=100 FEED=30 A=0 D=35 S=40 R=10"},
    {KU('K','F','M','2'), "Metal",      "RATIO=6 TUNE=33 INDEX=90 FEED=60 ENV=60 A=0 D=50 S=30 R=40"},
    {KU('K','F','M','2'), "Marimba",    "RATIO=4 INDEX=50 ENV=127 A=0 D=30 S=0 R=20"},
    {KU('K','F','M','2'), "Brass",      "RATIO=1 INDEX=60 ENV=90 FEED=50 A=20 D=40 S=70 R=20"},
    {KU('K','F','M','2'), "Glass",      "RATIO=3 INDEX=40 ENV=60 MIX=30 A=0 D=70 S=20 R=60"},
    {KU('K','F','M','2'), "Growl",      "RATIO=1/2 INDEX=100 FEED=90 DRIVE=60 ENV=50 A=0 D=40 S=80 R=15 L.RATE=60 L.FLT=20"},
    {KU('K','F','M','2'), "Bright Keys", "RATIO=2 INDEX=60 ENV=80 A=0 D=55 S=30 R=35"},
    {KU('K','F','M','2'), "Wood Bass",   "PITCH=-12 RATIO=1 INDEX=50 ENV=90 A=0 D=30 S=20 R=10"},
    {KU('K','F','M','2'), "Bell Pad",    "RATIO=3 INDEX=45 ENV=40 A=45 D=70 S=60 R=70 L.RATE=30 L.PIT=1"},
    {KU('K','F','M','2'), "Pluck",       "RATIO=4 INDEX=70 ENV=100 FEED=20 A=0 D=20 S=0 R=15"},

    /* DRUM: kits (the lanes BD SD CH OH CP LT HT RS) */
    {KU('D','R','U','M'), "Classic",    "PIECE.1=T8_BD PIECE.2=T8_SD PIECE.3=T8_CH PIECE.4=T8_OH PIECE.5=T8_CP PIECE.6=T8_TOM TUNE.6=40 PIECE.7=T8_TOM TUNE.7=90 PIECE.8=T8_RS"},
    {KU('D','R','U','M'), "Punch",      "PIECE.1=T9_BD DCAY.1=80 PIECE.2=T9_SD PIECE.3=SP_CH PIECE.4=SP_OH PIECE.5=T8_CP PIECE.6=T9_TOM TUNE.6=40 PIECE.7=T9_TOM TUNE.7=90 PIECE.8=T9_RS"},
    {KU('D','R','U','M'), "Light",      "PIECE.1=K5_BD PIECE.2=K7_SD PIECE.3=K5_CH PIECE.4=K5_OH PIECE.5=K5_CL PIECE.6=K7_TOM TUNE.6=40 PIECE.7=K7_TOM TUNE.7=90 PIECE.8=K5_RS"},
    {KU('D','R','U','M'), "Sampled",    "PIECE.1=SP_BD PIECE.2=T9_SD PIECE.3=SP_CH PIECE.4=SP_OH PIECE.5=K7_CP PIECE.6=T8_CNG TUNE.6=50 PIECE.7=T8_CNG TUNE.7=80 PIECE.8=T8_CB"},
    {KU('D','R','U','M'), "Percussion", "PIECE.1=T8_CNG TUNE.1=40 PIECE.2=T8_CNG TUNE.2=70 PIECE.3=V1_SHA PIECE.4=T8_MA PIECE.5=T8_CL PIECE.6=T8_CB PIECE.7=K5_CB PIECE.8=V1_PI PAN.1=35 PAN.2=65 PAN.6=40 PAN.7=60"},
    {KU('D','R','U','M'), "Crushed",    "PIECE.1=T8_BD DCAY.1=90 PIECE.2=K7_SD PIECE.3=K7_CH PIECE.4=K7_OH PIECE.5=K7_CP PIECE.6=T8_TOM TUNE.6=40 PIECE.7=T8_TOM TUNE.7=90 PIECE.8=TP_ZP CRSH=70"},
};
#undef KU
#define K_FACTORY_N ((int)(sizeof FACTORY / sizeof FACTORY[0]))

/* the n-th factory preset of the engine `uid` (-1 none): its place in FACTORY */
static int factory_at(uint32_t uid, int n)
{
    int i;
    for (i = 0; i < K_FACTORY_N; i++)
        if (FACTORY[i].uid == uid && n-- == 0) return i;
    return -1;
}
int k_factory_count(uint32_t uid)
{
    int i, n = 0;
    for (i = 0; i < K_FACTORY_N; i++) n += FACTORY[i].uid == uid;
    return n;
}
const char *k_factory_name(uint32_t uid, int n)
{
    const int i = factory_at(uid, n);
    return i < 0 ? "" : FACTORY[i].name;
}
static int fac_word(const char *w, int n, const char *s) { return (int)strlen(s) == n && !strncmp(w, s, (size_t)n); }
static int fac_bad;   /* words of the last factory_set that set nothing (a label or a name the engine lacks): k_factory_check */
/* a value: one of the descriptor's names, else a number (a list's value that is no name: counted as bad) */
static int fac_value(const KParamDesc *d, const char *v, int n)
{
    int k;
    if (d && d->fmt == KF_ENUM && d->names) {
        for (k = 0; d->names[k]; k++)
            if (fac_word(v, n, d->names[k])) return d->min + k;
        if (!(v[0] >= '0' && v[0] <= '9')) fac_bad++;
    }
    return atoi(v);
}
/* the settings of FACTORY[i] onto track t, whose engine is already that one at its defaults */
static void factory_set(KApp *a, int t, int i)
{
    const KEngine *e = k_engine_at(a->sound[t]);
    const int phase = e->uid == K_UID('K', 'P', 'H', 'S'), drum = e->uid == K_UID('D', 'R', 'U', 'M');
    const char *p = FACTORY[i].set;
    if (drum) {   /* a kit: every lane from a plain piece's settings (as k_init's kit), then the kit's words */
        static const uint8_t PLAIN[10] = {0, 64, 64, 64, 100, 0, 50, 50, 0, 50};
        int l, q;
        for (l = 0; l < K_LANES; l++) for (q = 0; q < 10 && q < K_LANE_VALS; q++) a->lane_p[t][l][q] = PLAIN[q];
    }
    while (*p) {
        const char *w = p, *eq, *v;
        int wn, vn, k;
        while (*p && *p != ' ') p++;
        eq = memchr(w, '=', (size_t)(p - w));
        if (!eq) fac_bad++;
        else {
            wn = (int)(eq - w); v = eq + 1; vn = (int)(p - v);
            if (wn == 1 && strchr("ADSR", w[0])) {   /* the amplitude envelope */
                k = (int)(strchr("ADSR", w[0]) - "ADSR");
                a->params[t][phase ? K_PAGE_ENV2 : K_PAGE_ENV1][k] = clamp(atoi(v), 0, 100);
            } else if (wn == 2 && (w[0] == 'W' || w[0] == 'O') && strchr("ADSR", w[1])) {   /* PHASE's DCW, DCO */
                k = (int)(strchr("ADSR", w[1]) - "ADSR");
                if (w[0] == 'W') a->params[t][K_PAGE_ENV1][k] = clamp(atoi(v), 0, 100);
                else a->params[t][K_PAGE_ENV2][4 + k] = clamp(atoi(v), 0, 100);
            } else if (drum && wn > 2 && w[wn - 2] == '.' && w[wn - 1] >= '1' && w[wn - 1] <= '0' + K_LANES) {   /* DRUM: a lane's */
                static const uint8_t DRUM_LANE[10] = {0, 4, K_LANE_PAN, 1, 2, 3, K_LANE_ENV + 3, K_LANE_ENV, K_LANE_ENV + 1, K_LANE_ENV + 2};   /* (as kreisel.c eng_slot) */
                char nm[16];
                int q;
                for (q = 0; q < vn && q < 15; q++) nm[q] = v[q] == '_' ? ' ' : v[q];   /* (a piece's name: _ for its space) */
                nm[q] = 0;
                for (k = 0; k < 10; k++)
                    if (fac_word(w, wn - 2, e->edit[k].label)) { a->lane_p[t][w[wn - 1] - '1'][DRUM_LANE[k]] = k_param_clamp(&e->edit[k], fac_value(&e->edit[k], nm, q)); break; }
                if (k == 10) fac_bad++;
            } else if (wn > 2 && w[0] == 'L' && w[1] == '.') {   /* the LFO's */
                const KParamDesc *L = k_page_desc(K_PAGE_LFO);
                for (k = 0; k < 8; k++)
                    if (fac_word(w + 2, wn - 2, L[k].label)) { a->params[t][K_PAGE_LFO][k] = k_param_clamp(&L[k], fac_value(&L[k], v, vn)); break; }
                if (k == 8) fac_bad++;
            } else {
                for (k = 0; k < e->nparams; k++)
                    if (fac_word(w, wn, e->edit[k].label)) {
                        const int x = k_param_clamp(&e->edit[k], fac_value(&e->edit[k], v, vn));
                        if (e->edit[k].fmt == KF_LINK_FENV) a->params[t][K_PAGE_ENV_DEST][1] = x;   /* (ENV: the host's ENV 1 -> filter, where the knob is) */
                        else a->params[t][K_PAGE_SOUND][4 + k] = x;
                        break;
                    }
                if (k == e->nparams) fac_bad++;
            }
        }
        while (*p == ' ') p++;
    }
}
#ifndef KREISEL_FM1_AUDIO   /* (the tests' only: its scratch app is ~17 KB of RAM the FM-1 has not) */
/* every factory preset applied to a scratch app: the words that set nothing, in all (0: every line is right); its first in `bad` */
int k_factory_check(char *bad, int len)
{
    static KApp scratch;
    int i, total = 0;
    for (i = 0; i < K_FACTORY_N; i++) {
        const int e = k_engine_find(FACTORY[i].uid);
        if (e < 0) continue;   /* (an engine not built in here: ENSEMBLE out of the MVP; its presets never listed) */
        k_init(&scratch);
        scratch.sound[0] = e;
        fac_bad = 0;
        factory_set(&scratch, 0, i);
        if (fac_bad && !total && bad) snprintf(bad, (size_t)len, "%s", FACTORY[i].name);
        total += fac_bad;
    }
    return total;
}
#endif

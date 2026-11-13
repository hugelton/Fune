/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* What a synth engine's DSP shares with the host: the voices and parts, the parameter slots a part's `p` holds (P_*), and the
 * start of a part (track_t) as an engine sees it. The built-in engines (included by kreisel_audio.c) and the outside plugins
 * (sdk/kreisel_sdk.h) both read it; the host's own track_t begins with the same `p`, and checks that it does. */
#ifndef KREISEL_DSP_H
#define KREISEL_DSP_H
#include <stdint.h>

#define NVOICE 6        /* voices a part (K_AUDIO_VOICES) */
#ifndef K_TRACKS
#define K_TRACKS 4      /* (kreisel.h: a platform's own) */
#endif
#define NPART K_TRACKS  /* parts: one a track */

enum { P_ATK, P_DEC, P_SUS, P_REL, P_FTYPE, P_ED_PIT, P_ED_FLT, P_ED_SHP, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
       P_E8, P_E9, P_E10, P_E11, P_E12, P_E13, P_E14, P_E15, P_E16, P_E17, P_E18, P_E19,   /* the engine's own, edit[0 ..] */
       P_LRATE, P_LWAVE, P_LPHASE, P_LFADE, P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,    /* the LFO page; its four routings */
       P_E2_DLY, P_E2_HOLD, P_E2_CRV, P_E2_AMT, P_ED2_AMP,                              /* ENV 2; ENV DEST's AMP (ENV 2 to the amplitude) */
       P_X_A, P_X_D, P_X_S, P_X_R,   /* an engine's third envelope, A D S R (PHASE's DCO: ENV 2's items 4 .. 7); added at the end */
       P_COUNT };

/* a part as an outside engine sees it: its parameters (the host's track_t has more after them; an engine reads only these) */
#define K_TRACK_PREFIX int16_t p[P_COUNT]
#endif

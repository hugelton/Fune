/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM2: two sine operators in series, OP2 (modulator, with feedback) -> OP1 (carrier).
 * SOUND P_E0 PITCH, 1 TUNE, 2 RATIO, 3 GLIDE | 4 INDEX, 5 ENV, 6 FEED, 7 KTR; EDIT 8 MIX, 9 DRIVE, 10 LEVEL, 11 VEL. The host ADSR controls the carrier
 * amplitude; ENV follows that ADSR for modulation depth. The host's
 * FLT destination (m->cutoff) also changes INDEX, as PHASE uses it for DCW.
 * 32-bit phases and the host's sine LUT; no floating point or allocation. */

static void kfm2_note_on(track_t *t, voice_t *v)
{
    v->ph[1] = v->ph[0];
    if (!v->env) {   /* a silent voice: from rest */
        v->s[0] = 0;                    /* OP2's last two samples (the feedback takes their mean, as the DX: no fizz at high FEED) */
        v->s[2] = 0;
        v->s[1] = -1;                   /* the last block's index (Q8; none yet: this block starts at its own) */
    } else v->s[2] = v->s[0];           /* a retrigger while it sounds: OP2's memory and the index go on from where they were (kept: .keep),
                                         * not from 0 (a step in the timbre, heard as a click, as PHASE's DCW was) */
    v->s[7] = t->glide_last ? t->glide_last : INT32_MIN;   /* glide: from the last note's pitch (1/16 st << 8), or none yet */
}

static void kfm2_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint16_t RATIO_Q8[8] = {128, 256, 512, 768, 1024, 1536, 2048, 4096};
    const int16_t *p = t->p;
    const int32_t semi = clamp(p[P_E0], -24, 24), ratio = clamp(p[P_E2], 0, 7);
    const int32_t depth = clamp(p[P_E4], 0, 127), feedback = clamp(p[P_E6], 0, 127);
    const int32_t env = clamp(p[P_E5], 0, 127), tune = clamp(p[P_E1], -50, 50);
    const int32_t mix = clamp(p[P_E8], 0, 127), level = clamp(p[P_E10], 0, 127) * 258;
    const int32_t vs = clamp(p[P_E11], 0, 127), ktr = clamp(p[P_E7], 0, 127);
    const int32_t drive = clamp(p[P_E9], 0, 127);
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) /
                                            (127 * (int32_t)v->vel)) : 4096;
    int32_t base = m->pitch16;
    uint32_t inc1, inc2, ph1 = v->ph[0], ph2 = v->ph[1], i;
    int32_t last = v->s[0], prev = v->s[2], index, ix, dix;
    uint64_t mi;
    {   /* glide: the pitch moves from the last note's to this one's, slower as GLIDE rises */
        const int32_t g = clamp(p[P_E3], 0, 127), target = m->pitch16 * 256;
        int32_t cur = v->s[7] == INT32_MIN || !g ? target : v->s[7];
        if (g) { const int32_t dl = target - cur; const int32_t st = (int32_t)((k_smul(dl, 65536 / (1 + g * g / 8)) + 32768) >> 16); cur += st ? st : (dl > 0) - (dl < 0); }   /* (a unit at least: an upward glide reaches its note) */
        v->s[7] = cur;
        t->glide_last = cur;
        base = cur >> 8;
    }
    inc1 = semi || base != m->pitch16 ? cents_inc(clamp(base + semi * 16, 0, 2047), 0, 0) : m->inc;

    if ((semi || base != m->pitch16) && m->fine)
        inc1 = (uint32_t)(k_smul((int32_t)inc1, 4096 + m->fine) / 4096);
    /* Signed cents detune, then an operator ratio. Saturate at Nyquist
     * instead of allowing an overflowing phase increment to wrap pitch. */
    mi = k_umul(k_umulsh(inc1, (uint32_t)(32768 + tune * 19), 15), RATIO_Q8[ratio]) >> 8;   /* (>> 23 in two steps: each product 32 x 32) */
    inc2 = (uint32_t)(mi > 0x7FFFFFFFu ? 0x7FFFFFFFu : mi);
    /* ENV=0 holds the index; ENV=127 tracks the voice's ADSR completely. */
    index = (int32_t)(k_smul(depth, (127 - env) * 32767 + env * clamp(m->envq15, 0, 32767)) /
                      (127 * 32767));
    index += m->cutoff / 256;           /* ENV/LFO -> FLT become index modulation */
    index += ((int32_t)v->note - 60) * ktr / 128;
    index = clamp(index, 0, 127);
    /* the index moves across the block from the last block's, not in a step at its start (a step clicked: an ENV or an LFO on the
     * index stepped it every 32 samples) */
    ix = v->s[1] < 0 ? index << 8 : v->s[1];
    dix = n ? ((index << 8) - ix) / (int32_t)n : 0;
    v->s[1] = index << 8;
    for (i = 0; i < n; ++i, ix += dix) {
        /* FEED is phase feedback within OP2; INDEX produces up to ~9.4
         * radians of phase deviation at maximum, with 64-bit intermediates. */
        int32_t op2 = sine_i(ph2 + (uint32_t)k_smul((last + prev) >> 1, feedback * 256));
        int32_t op1 = sine_i(ph1 + (uint32_t)k_smul(op2, ix * 6));   /* (ix Q8: x 1536 / 256) */
        int32_t s = mulq15(op1, 32767 - mix * 128) + mulq15(op2, mix * 128);
        if (drive) s = softclip((int32_t)(k_smul(s, 128 + drive * 3) / 128));
        s = (int32_t)(((int64_t)s * level) >> 14);   /* (x 2: one sine sat ~6 dB under the other engines' two oscillators) */
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += voice_amp(s, m, i);
        prev = last;
        last = op2;
        ph1 += inc1;
        ph2 += inc2;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->s[0] = last;
    v->s[2] = prev;
}

static const engine_t ENG_KFM2 = {
    .name = K_ENGINE_NAME_KFM2,
    .page_title = {"OPERATORS", "OUTPUT"},
    .note_on = kfm2_note_on,
    .render = kfm2_render,
    .knob = {P_E4, P_E2, P_E6, P_E5},
    .keep = 0x83,                /* the glide (s[7]); OP2's last sample (s[0]) and the index (s[1]): a retrigger goes on from there */
};

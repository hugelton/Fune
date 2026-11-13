/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host-side arpeggiator: included by kreisel_audio.c after tone_on.
 * The UI declares MODE/RATE/OCT/GATE; this file owns playback only.
 * No heap allocation, floats, or reads of mutable KApp on the audio thread. */

typedef struct { uint8_t pitch, vel; uint8_t octave; uint32_t order; } arp_item_t;
#define ARP_ITEMS (K_ARP_LIVE_MAX * 4u)

static int arp_has(const KAudio *a, int t)
{
    unsigned i;
    for (i = 0; i < K_ARP_LIVE_MAX; i++) if (a->arp_live[t][i].vel) return 1;
    for (i = 0; i < K_STEPS; i++) if (a->arp_seq[t][i].until > a->frame) return 1;
    return 0;
}

static void arp_release(KAudio *a, int t)
{
    uint8_t mask = a->arp_voice_mask[t];
    unsigned i;
    for (i = 0; i < NVOICE; i++) if (mask & (1u << i)) {
        voice_t *v = &trk[t].v[i];
        if (v->active && v->gate && a->rel_at[t][i] != ~(uint64_t)0) {
            v->gate = 0;
            v->stage = 3;
        }
        mo_arp_off(a, t, (int)i);
    }
    a->arp_voice_mask[t] = 0;
}

/* Move ordinary held keys into/out of the arp on a MODE change.
 * The sequencer's prior notes are allowed to release naturally. */
static void arp_mode_switch(KAudio *a, int t, int old_mode, int new_mode)
{
    unsigned i, k;
    arp_release(a, t);
    if (!old_mode && new_mode) {
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[t].v[i];
            if (v->active && v->gate && a->rel_at[t][i] == ~(uint64_t)0) {
                v->gate = 0; v->stage = 3;
            }
        }
    } else if (old_mode && !new_mode) {
        for (i = 0; i < K_ARP_LIVE_MAX; i++) if (a->arp_live[t][i].vel) {
            k = (unsigned)tone_on(a, t, a->arp_live[t][i].note, a->arp_live[t][i].vel);
            if (k < NVOICE) a->rel_at[t][k] = ~(uint64_t)0;
        }
    }
    memset(a->arp_seq[t], 0, sizeof a->arp_seq[t]);
    a->arp_step[t] = a->arp_walk[t] = a->arp_ext_acc[t] = a->arp_pending[t] = 0;
    a->arp_next_q16[t] = 0;
    a->arp_rng[t] = 0x9E3779B9u ^ ((uint32_t)t * 0x85EBCA6Bu);
}

static void arp_live_note(KAudio *a, int t, int note, int vel)
{
    unsigned i;
    unsigned free_slot = K_ARP_LIVE_MAX;
    unsigned oldest = 0;
    for (i = 0; i < K_ARP_LIVE_MAX; i++) {
        KArpKey *k = &a->arp_live[t][i];
        if (k->vel && k->note == note) {  /* retrigger refreshes velocity, not order */
            k->vel = (uint8_t)vel;
            return;
        }
        if (!k->vel && free_slot == K_ARP_LIVE_MAX) free_slot = i;
        if (k->order < a->arp_live[t][oldest].order) oldest = i;
    }
    if (!vel) return;
    if (free_slot == K_ARP_LIVE_MAX) free_slot = oldest;
    a->arp_live[t][free_slot].note = (uint8_t)note;
    a->arp_live[t][free_slot].vel = (uint8_t)vel;
    a->arp_live[t][free_slot].order = ++a->arp_order;
}
static void arp_live_off(KAudio *a, int t, int note)
{
    unsigned i;
    for (i = 0; i < K_ARP_LIVE_MAX; i++)
        if (a->arp_live[t][i].vel && a->arp_live[t][i].note == note)
            a->arp_live[t][i].vel = 0;
}

/* Sorted pitches for UP/DN and sorted press order for ORD.
 * Duplicates between a held key and a sequenced note are one pitch. */
static void arp_add(arp_item_t *items, unsigned *count, int note, int vel, uint32_t order, unsigned oct)
{
    unsigned i;
    int pitch = note + 12 * (int)oct;
    if (pitch > 127 || pitch < 0) return;
    for (i = 0; i < *count; i++) if (items[i].pitch == pitch) {
        if (vel > items[i].vel) items[i].vel = (uint8_t)vel;
        if (order < items[i].order) { items[i].order = order; items[i].octave = (uint8_t)oct; }
        return;
    }
    if (*count >= ARP_ITEMS) return;
    items[*count].pitch = (uint8_t)pitch;
    items[*count].vel = (uint8_t)vel;
    items[*count].order = order;
    items[*count].octave = (uint8_t)oct;
    ++*count;
}
static unsigned arp_collect(KAudio *a, int t, arp_item_t *items)
{
    const unsigned octs = a->arp_oct[t] ? a->arp_oct[t] : 1u;
    unsigned n = 0, i, o, j;
    for (i = 0; i < K_ARP_LIVE_MAX; i++) {
        const KArpKey *k = &a->arp_live[t][i];
        if (!k->vel) continue;
        for (o = 0; o < octs; o++) arp_add(items, &n, k->note, k->vel, k->order, o);
    }
    for (i = 0; i < K_STEPS; i++) {
        KArpSeqNote *k = &a->arp_seq[t][i];
        if (k->until && k->until <= a->frame) k->until = 0;
        if (!k->until) continue;
        for (o = 0; o < octs; o++) arp_add(items, &n, k->note, k->vel, k->order, o);
    }
    for (i = 1; i < n; i++) {
        arp_item_t tmp = items[i];
        j = i;
        while (j) {
            const arp_item_t *prev = &items[j - 1u];
            int before = a->arp_mode[t] == 5 ?
                (prev->octave > tmp.octave || (prev->octave == tmp.octave && prev->order > tmp.order)) :
                prev->pitch > tmp.pitch;
            if (!before) break;
            items[j] = items[j - 1u];
            --j;
        }
        items[j] = tmp;
    }
    return n;
}
static uint64_t arp_period_q16(const KAudio *a, int t)
{
    unsigned rate = a->arp_rate[t] < 10u ? a->arp_rate[t] : 2u;
    unsigned bpm = (unsigned)(a->bpm > 0 ? a->bpm : 120);
    return k_umul(a->rate * 60u, 65536u * K_ARP_CLOCKS[rate]) / (bpm * 24u);   /* (each factor 32 bits: the CPU erratum, kreisel_mul.h) */
}
static void arp_play(KAudio *a, int t)
{
    arp_item_t items[ARP_ITEMS];
    unsigned n = arp_collect(a, t, items);
    const uint64_t period = arp_period_q16(a, t);
    uint64_t gate_frames = (k_umul((uint32_t)(period >> 16), a->arp_gate[t]) + 126u) / 127u;
    unsigned octave = 0, k, i;
    int index;
    if (!n) { arp_release(a, t); return; }
    if (gate_frames < 1) gate_frames = 1;
    arp_release(a, t);
    if (a->arp_mode[t] == 14) {    /* CHORD retriggers up to the six host voices */
        for (i = 0; i < n && i < NVOICE; i++) {
            k = (a->arp_step[t] + i) % n;
            index = tone_on(a, t, items[k].pitch, items[k].vel);
            mo_arp_on(a, t, index, items[k].pitch, items[k].vel);
            if (i == 0) a->arp_last[t] = (uint8_t)items[k].pitch;   /* (CHORD: its first note stands for it) */
            if (index >= 0) {
                a->arp_voice_mask[t] |= (uint8_t)(1u << index);
                a->rel_at[t][index] = a->frame + gate_frames;
                a->triggers[t]++;
                a->last_trigger_frame[t] = a->frame;
            }
        }
    } else {
        index = k_arp_pick(a->arp_mode[t], a->arp_step[t], n,
                           &a->arp_rng[t], &a->arp_walk[t], &octave);
        if (index >= 0 && (unsigned)index < n) {
            int pitch = items[index].pitch + (int)octave * 12;
            if (pitch > 127) pitch = 127;
            k = (unsigned)tone_on(a, t, pitch, items[index].vel);
            mo_arp_on(a, t, (int)k, pitch, items[index].vel);
            a->arp_last[t] = (uint8_t)pitch;
            if (k < NVOICE) {
                a->arp_voice_mask[t] |= (uint8_t)(1u << k);
                a->rel_at[t][k] = a->frame + gate_frames;
                a->triggers[t]++;
                a->last_trigger_frame[t] = a->frame;
            }
        }
    }
    a->arp_step[t]++;
}
/* a sequenced note into the arp's held notes: a slot free (or the one that ends first; the same note again, its own) */
static void arp_seq_on(KAudio *a, int t, int note, int vel, unsigned len, uint64_t step_q16)
{
    KArpSeqNote *slot = &a->arp_seq[t][0];
    int i;
    for (i = 0; i < K_STEPS; i++) {
        KArpSeqNote *k = &a->arp_seq[t][i];
        if (k->until > a->frame && k->note == note) { slot = k; break; }
        if (k->until <= a->frame) { if (slot->until > a->frame) slot = k; }
        else if (slot->until > a->frame && k->until < slot->until) slot = k;
    }
    slot->note = (uint8_t)note;
    slot->vel = (uint8_t)vel;
    slot->order = ++a->arp_order;
    slot->until = a->frame + (k_umul((uint32_t)(step_q16 >> 8), (uint32_t)len) >> 8);
    if (slot->until <= a->frame) slot->until = a->frame + 1;
    if (!a->ext && !a->arp_next_q16[t]) a->arp_next_q16[t] = a->frame << 16;
}
/* Call after the sequencer's event at this frame, before mixing the next
 * buffer chunk. Every track runs its own clock while keys are held. */
static void arp_process(KAudio *a)
{
    int t;
    for (t = 0; t < K_TRACKS; t++) {
        uint64_t now = a->frame << 16;
        uint64_t period;
        if (!a->arp_mode[t]) continue;
        if (!arp_has(a, t)) {
            arp_release(a, t);
            a->arp_next_q16[t] = 0;
            a->arp_pending[t] = 0;
            continue;
        }
        if (a->ext) {
            if (a->playing && a->arp_pending[t]) {
                /* Drop excessive queued pulses: an interrupted USB stream
                 * must not make a burst of notes at a single sample. */
                a->arp_pending[t] = 0;
                arp_play(a, t);
            }
            continue;
        }
        if (!a->arp_next_q16[t]) a->arp_next_q16[t] = now;
        if (a->arp_next_q16[t] > now) continue;
        arp_play(a, t);
        period = arp_period_q16(a, t);
        a->arp_next_q16[t] += period;
        if (a->arp_next_q16[t] <= now) a->arp_next_q16[t] = now + period;
    }
}
static size_t arp_limit_run(const KAudio *a, size_t run)
{
    int t;
    uint64_t now = a->frame << 16;
    if (a->ext) return run;
    for (t = 0; t < K_TRACKS; t++) {
        uint64_t next = a->arp_next_q16[t], frames;
        if (!a->arp_mode[t] || !next || next <= now || !arp_has(a, t)) continue;
        frames = (next - now + 65535u) >> 16;
        if (frames && frames < run) run = (size_t)frames;
    }
    return run;
}

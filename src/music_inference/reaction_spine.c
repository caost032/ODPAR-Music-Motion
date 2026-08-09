#include "odm_music_reaction.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define REACT_Q31_ONE ((uint32_t)INT32_MAX)
#define REACT_Q31_010 UINT32_C(214748365)
#define REACT_Q31_012 UINT32_C(257698038)
#define REACT_Q31_020 UINT32_C(429496729)
#define REACT_Q31_030 UINT32_C(644245094)
#define REACT_Q31_060 UINT32_C(1288490188)
#define REACT_HIST_BUCKETS UINT32_C(256)
#define REACT_QUANTILE_NUM UINT64_C(99)
#define REACT_QUANTILE_DEN UINT64_C(100)
#define REACT_ATTACK_QUANTILE_NUM UINT64_C(95)
#define REACT_ATTACK_QUANTILE_DEN UINT64_C(100)
#define REACT_BROADBAND_RELEASE_DIVISOR UINT32_C(8)
#define REACT_EVENT_THRESHOLD_MULTIPLIER UINT32_C(2)
#define REACT_EVENT_BASELINE_DIVISOR UINT32_C(32)
#define REACT_EVENT_REFRACTORY_TICKS UINT32_C(14)
#define REACT_EVENT_PULSE_DECAY_DIVISOR UINT32_C(5)
#define REACT_OFFLINE_PEAK_RADIUS_TICKS UINT32_C(2)
#define REACT_OFFLINE_CANDIDATE_QUANTILE_NUM UINT64_C(50)
#define REACT_OFFLINE_CANDIDATE_QUANTILE_DEN UINT64_C(100)
#define REACT_ATTACK_REFERENCE_LEVEL_Q31 UINT32_C(1503238553) /* 0.70 */
#define REACT_SALIENCE_QUANTILE_DEN UINT64_C(100)
#define REACT_SALIENCE_KNOT_COUNT UINT32_C(10)
#define REACT_SALIENCE_FLOOR_Q31 REACT_Q31_010
#define REACT_SUBTICK_MAX_OFFSET_SAMPLES INT32_C(240) /* +/-5 ms at 48 kHz */
#define REACT_SUBTICK_TRUST_MIN_Q31 UINT32_C(268435456) /* 1/8 prominence; reject weak false precision */
#define REACT_EVENT_PRESENTATION_PULSE_SAMPLES UINT32_C(6720) /* 14 canonical ticks = 140 ms */

_Static_assert(sizeof(odm_music_reaction_event) == ODM_MUSIC_REACTION_EVENT_RECORD_BYTES,
               "music reaction event record size drift");

static const uint8_t react_salience_quantile_num[REACT_SALIENCE_KNOT_COUNT] = {
    10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u, 99u
};

/* 23.4375 Hz .. 18 kHz over the canonical 48 kHz / 2048 FFT. The first
 * region intentionally preserves one-bin resolution for bass/mid detail;
 * upper lanes widen monotonically. This table is policy, not runtime math. */
static const uint16_t reaction_lane_edges[ODM_MUSIC_REACTION_LANE_COUNT + 1u] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
    25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,
    47,48,49,50,51,52,53,54,55,56,57,58,59,60,64,68,73,78,84,90,96,
    103,111,119,127,136,146,156,168,180,192,206,221,237,254,272,291,
    312,335,359,384,412,441,473,507,543,582,624,669,717,768
};

/* Frequency families: ~23-94, 94-211, 211-609, 609-2508,
 * 2508-8016 and 8016-18000 Hz. DC and ultrasonic/noise tail are excluded. */
static const uint16_t reaction_family_edges[ODM_MUSIC_REACTION_FAMILY_COUNT + 1u] = {
    1,4,9,26,107,342,768
};

static uint32_t react_clamp_q31(uint32_t value) {
    return value > REACT_Q31_ONE ? REACT_Q31_ONE : value;
}

/* 0.33 Spectral Trajectory Novelty. A narrow spectral component can move
 * between adjacent log-frequency lanes without representing a fresh onset.
 * Compare the current lane against the previous same-lane magnitude plus a
 * bounded (3/4) previous-neighbour reference. This is strictly causal: only
 * the immediately preceding tick participates, and neighbouring evidence can
 * suppress a false onset but can never manufacture positive energy. */
static uint32_t react_neighbor_weight_3_4(uint32_t v) {
    return (uint32_t)(((uint64_t)v * UINT64_C(3) + UINT64_C(2)) / UINT64_C(4));
}

static uint32_t react_trajectory_previous_ticks(const odm_music_reaction_tick *ticks,
                                                uint64_t index, uint32_t lane) {
    uint32_t ref, n;
    if (!ticks || index == 0u) return ticks ? ticks[index].lane_magnitude_q31[lane] : 0u;
    ref = ticks[index - 1u].lane_magnitude_q31[lane];
    if (lane > 0u) {
        n = react_neighbor_weight_3_4(ticks[index - 1u].lane_magnitude_q31[lane - 1u]);
        if (n > ref) ref = n;
    }
    if (lane + 1u < ODM_MUSIC_REACTION_LANE_COUNT) {
        n = react_neighbor_weight_3_4(ticks[index - 1u].lane_magnitude_q31[lane + 1u]);
        if (n > ref) ref = n;
    }
    return ref;
}

static uint32_t react_trajectory_previous_state(const odm_music_reaction_state *state,
                                                uint32_t lane, uint32_t current) {
    uint32_t ref, n;
    if (!state || state->initialized == 0u) return current;
    ref = state->previous_lane_q31[lane];
    if (lane > 0u) {
        n = react_neighbor_weight_3_4(state->previous_lane_q31[lane - 1u]);
        if (n > ref) ref = n;
    }
    if (lane + 1u < ODM_MUSIC_REACTION_LANE_COUNT) {
        n = react_neighbor_weight_3_4(state->previous_lane_q31[lane + 1u]);
        if (n > ref) ref = n;
    }
    return ref;
}

static int react_zero_u32(const uint32_t *v, uint32_t count) {
    uint32_t i;
    if (!v) return 0;
    for (i = 0u; i < count; ++i) if (v[i] != 0u) return 0;
    return 1;
}

static int react_center_canonical(uint64_t tick_index, uint64_t center_sample) {
    if (tick_index > UINT64_MAX / (uint64_t)ODM_MUSIC_TICK_SAMPLES) return 0;
    return center_sample == tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES;
}

static int react_tick_valid(const odm_music_reaction_tick *t) {
    uint32_t i;
    if (!t || t->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        t->flags != 0u || !react_center_canonical(t->tick_index, t->center_sample) ||
        !react_zero_u32(t->reserved, 7u)) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i)
        if (t->lane_magnitude_q31[i] > REACT_Q31_ONE) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i)
        if (t->family_magnitude_q31[i] > REACT_Q31_ONE) return 0;
    return t->broadband_magnitude_q31 <= REACT_Q31_ONE;
}

static int react_profile_valid(const odm_music_reaction_profile *p) {
    uint32_t i;
    if (!p || p->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        p->flags != 0u || p->tick_count == 0u ||
        p->first_tick_index > p->last_tick_index ||
        p->last_tick_index - p->first_tick_index + 1u != p->tick_count ||
        !react_zero_u32(p->reserved, 14u)) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
        if (p->lane_magnitude_ref[i] > REACT_Q31_ONE ||
            p->lane_attack_ref[i] > REACT_Q31_ONE) return 0;
    }
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i) {
        if (p->family_magnitude_ref[i] > REACT_Q31_ONE ||
            p->family_attack_ref[i] > REACT_Q31_ONE) return 0;
    }
    return p->broadband_magnitude_ref <= REACT_Q31_ONE &&
           p->broadband_attack_ref <= REACT_Q31_ONE;
}

static int react_state_valid(const odm_music_reaction_state *s) {
    uint32_t i;
    if (!s || s->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        s->initialized > 1u || s->event_baseline_q31 > REACT_Q31_ONE ||
        s->event_pulse_q31 > REACT_Q31_ONE ||
        s->refractory_ticks > REACT_EVENT_REFRACTORY_TICKS ||
        s->previous_broadband_q31 > REACT_Q31_ONE ||
        s->broadband_envelope_q31 > REACT_Q31_ONE ||
        !react_zero_u32(s->reserved, 13u)) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
        if (s->previous_lane_q31[i] > REACT_Q31_ONE ||
            s->lane_envelope_q31[i] > REACT_Q31_ONE ||
            s->lane_fast_envelope_q31[i] > REACT_Q31_ONE ||
            s->lane_fast_envelope_q31[i] > s->lane_envelope_q31[i]) return 0;
    }
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i) {
        if (s->previous_family_q31[i] > REACT_Q31_ONE ||
            s->family_envelope_q31[i] > REACT_Q31_ONE) return 0;
    }
    return 1;
}

static uint32_t react_average(const uint32_t *values, uint32_t start, uint32_t end) {
    uint64_t sum = 0u;
    uint32_t i;
    uint32_t count;
    if (!values || start >= end) return 0u;
    count = end - start;
    for (i = start; i < end; ++i) sum += react_clamp_q31(values[i]);
    sum = (sum + (uint64_t)(count / 2u)) / (uint64_t)count;
    return react_clamp_q31((uint32_t)sum);
}

static uint64_t react_isqrt_u64(uint64_t value) {
    uint64_t result = 0u;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint32_t react_norm(uint32_t value, uint32_t reference) {
    uint64_t n;
    if (value == 0u || reference == 0u) return 0u;
    if (value >= reference) return REACT_Q31_ONE;
    n = (uint64_t)value * (uint64_t)REACT_Q31_ONE + (uint64_t)reference / 2u;
    return (uint32_t)(n / (uint64_t)reference);
}

/* Attack references are P95, so mapping every value >= P95 directly to one
 * destroys the ordering of the strongest five percent of attacks. Preserve
 * deterministic headroom instead: the reference itself maps to 0.70 and the
 * remaining raw Q1.31 domain maps monotonically to (0.70, 1.0]. Values below
 * the reference remain linear. No floor or synthetic energy is introduced. */
static uint32_t react_attack_norm(uint32_t value, uint32_t reference) {
    uint64_t n;
    if (value == 0u || reference == 0u) return 0u;
    if (reference >= REACT_Q31_ONE) return react_norm(value, reference);
    if (value <= reference) {
        n = (uint64_t)value * (uint64_t)REACT_ATTACK_REFERENCE_LEVEL_Q31 +
            (uint64_t)reference / 2u;
        return react_clamp_q31((uint32_t)(n / (uint64_t)reference));
    }
    {
        uint32_t input_room = REACT_Q31_ONE - reference;
        uint32_t output_room = REACT_Q31_ONE - REACT_ATTACK_REFERENCE_LEVEL_Q31;
        uint64_t delta = (uint64_t)(value - reference) * (uint64_t)output_room +
                         (uint64_t)input_room / 2u;
        uint64_t out = (uint64_t)REACT_ATTACK_REFERENCE_LEVEL_Q31 +
                       delta / (uint64_t)input_room;
        return out > REACT_Q31_ONE ? REACT_Q31_ONE : (uint32_t)out;
    }
}

/* Compression deliberately goes the opposite direction from 0.14's cube:
 * 75% linear + 25% sqrt preserves peak order while keeping quieter real
 * detail visible. No invented floor is added. */
static uint32_t react_shape(uint32_t q31) {
    uint64_t radicand;
    uint64_t root;
    uint64_t mixed;
    q31 = react_clamp_q31(q31);
    if (q31 == 0u) return 0u;
    radicand = (uint64_t)q31 << 31;
    root = react_isqrt_u64(radicand);
    if (root > REACT_Q31_ONE) root = REACT_Q31_ONE;
    mixed = UINT64_C(3) * (uint64_t)q31 + root;
    mixed = (mixed + UINT64_C(2)) / UINT64_C(4);
    return react_clamp_q31((uint32_t)mixed);
}

static uint32_t react_positive_delta(uint32_t current, uint32_t previous) {
    return current > previous ? current - previous : 0u;
}

static uint32_t react_release(uint32_t previous, uint32_t current, uint32_t divisor) {
    uint32_t decay;
    uint32_t held;
    if (current >= previous) return current;
    decay = previous / divisor;
    if (decay == 0u && previous != 0u) decay = 1u;
    held = previous > decay ? previous - decay : 0u;
    return current > held ? current : held;
}

static uint32_t react_max(uint32_t a, uint32_t b) { return a > b ? a : b; }

static uint32_t react_ratio_q31(uint32_t numerator, uint32_t denominator) {
    uint64_t n;
    if (numerator == 0u || denominator == 0u) return 0u;
    if (numerator >= denominator) return REACT_Q31_ONE;
    n = (uint64_t)numerator * (uint64_t)REACT_Q31_ONE + (uint64_t)denominator / 2u;
    return react_clamp_q31((uint32_t)(n / (uint64_t)denominator));
}

/* Frequency-aware temporal release policy. At the canonical 100 Hz tick
 * rate, low-frequency energy is intentionally allowed a longer physical tail,
 * while presence/air recover faster so hats, consonants and short transients do
 * not smear into a generic whole-halo breath. These divisors are policy values:
 * larger divisor = slower release because decay = previous/divisor. */
static const uint8_t react_family_release_divisor[ODM_MUSIC_REACTION_FAMILY_COUNT] = {
    12u, 10u, 8u, 7u, 5u, 4u
};

/* Faster same-lane contour. A smaller divisor releases more quickly while
 * remaining bounded below the slow envelope for identical input. This creates
 * an explicit causal distinction between sustained body and release tail. */
static const uint8_t react_family_fast_release_divisor[ODM_MUSIC_REACTION_FAMILY_COUNT] = {
    4u, 4u, 3u, 3u, 2u, 2u
};

static odm_status react_policy_encode(uint8_t *buffer, uint64_t capacity,
                                      uint64_t *out_required) {
    odm_wire_writer w;
    odm_status st;
    uint64_t written = 0u;
    uint32_t i;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_MUSIC_REACTION_POLICY_BYTES;
    if (!buffer || capacity < ODM_MUSIC_REACTION_POLICY_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_MUSIC_REACTION_POLICY_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_MUSIC_REACTION_POLICY_BYTES);
    if (st != ODM_STATUS_OK) return st;
#define RPW(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    RPW(odm_wire_write_bytes(&w, (const uint8_t *)"ODMRXP11", 8u));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_POLICY_VERSION));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_SCHEMA_VERSION));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_LANE_COUNT));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_FAMILY_COUNT));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_TICK_RATE));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_TICK_SAMPLES));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_WINDOW_SAMPLES));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_QUANTILE_NUM));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_QUANTILE_DEN));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_ATTACK_QUANTILE_NUM));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_ATTACK_QUANTILE_DEN));
    RPW(odm_wire_write_u32(&w, 3u)); /* shaping: 3/4 linear + 1/4 sqrt */
    RPW(odm_wire_write_u32(&w, 4u));
    RPW(odm_wire_write_u32(&w, 1u)); /* magnitude normalization: reference clips at Q1.31 one */
    RPW(odm_wire_write_u32(&w, 2u)); /* attack normalization: percentile reference retains headroom */
    RPW(odm_wire_write_u32(&w, REACT_ATTACK_REFERENCE_LEVEL_Q31));
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i)
        RPW(odm_wire_write_u32(&w, react_family_release_divisor[i]));
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i)
        RPW(odm_wire_write_u32(&w, react_family_fast_release_divisor[i]));
    RPW(odm_wire_write_u32(&w, REACT_BROADBAND_RELEASE_DIVISOR));
    RPW(odm_wire_write_u32(&w, REACT_EVENT_THRESHOLD_MULTIPLIER));
    RPW(odm_wire_write_u32(&w, REACT_Q31_030));
    RPW(odm_wire_write_u32(&w, REACT_EVENT_BASELINE_DIVISOR));
    RPW(odm_wire_write_u32(&w, REACT_EVENT_REFRACTORY_TICKS));
    RPW(odm_wire_write_u32(&w, 0u)); /* event strength is raw coherence evidence; no synthetic floor */
    RPW(odm_wire_write_u32(&w, REACT_EVENT_PULSE_DECAY_DIVISOR));
    RPW(odm_wire_write_u32(&w, REACT_OFFLINE_PEAK_RADIUS_TICKS));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_PROJECTION_SCHEMA_VERSION));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_PROJECTION_MAX_GAP_SAMPLES));
    RPW(odm_wire_write_u32(&w, 1u)); /* projection sustain = latest source tick <= presentation */
    RPW(odm_wire_write_u32(&w, 1u)); /* projection instant interval = (previous,current] */
    RPW(odm_wire_write_u32(&w, 1u)); /* no-future-evidence / non-anticipatory */
    RPW(odm_wire_write_u32(&w, 1u)); /* batch seek first interval = (sample-1,sample] */
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_OFFLINE_CANDIDATE_QUANTILE_NUM));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_OFFLINE_CANDIDATE_QUANTILE_DEN));
    RPW(odm_wire_write_u32(&w, 2u)); /* event detection: max(broadband, second-strongest family) */
    RPW(odm_wire_write_u32(&w, 2u)); /* event strength: mean(broadband, top2 families) */
    RPW(odm_wire_write_u32(&w, 1u)); /* lane-local attacks cannot fire global event alone */
    RPW(odm_wire_write_u32(&w, REACT_SALIENCE_KNOT_COUNT));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_SALIENCE_QUANTILE_DEN));
    for (i = 0u; i < REACT_SALIENCE_KNOT_COUNT; ++i)
        RPW(odm_wire_write_u32(&w, (uint32_t)react_salience_quantile_num[i]));
    RPW(odm_wire_write_u32(&w, REACT_SALIENCE_FLOOR_Q31));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE));
    RPW(odm_wire_write_u32(&w, 2u)); /* salience domain: macro-gated + refractory-published peaks */
    RPW(odm_wire_write_u32(&w, 2u)); /* salience map: P10..P90,P99 -> 0.10..0.90,1.00 */
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_EVENT_SCHEMA_VERSION));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_EVENT_RECORD_BYTES));
    RPW(odm_wire_write_u32(&w, (uint32_t)REACT_SUBTICK_MAX_OFFSET_SAMPLES));
    RPW(odm_wire_write_u32(&w, 1u)); /* event record uses same published peak set as contextual salience */
    RPW(odm_wire_write_u32(&w, 1u)); /* timing refinement = bounded 3-point parabola over event score */
    RPW(odm_wire_write_u32(&w, 1u)); /* record retains raw evidence + derived salience separately */
    RPW(odm_wire_write_u32(&w, 3u)); /* lane temporal authorities: slow, fast, attack */
    RPW(odm_wire_write_u32(&w, 1u)); /* fast envelope is same-lane / no cross-lane diffusion */
    RPW(odm_wire_write_u32(&w, 1u)); /* invariant fast <= slow */
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_EVENT_PROJECTION_SCHEMA_VERSION));
    RPW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_EVENT_PROJECTION_RECORD_BYTES));
    RPW(odm_wire_write_u32(&w, REACT_SUBTICK_TRUST_MIN_Q31));
    RPW(odm_wire_write_u32(&w, REACT_EVENT_PRESENTATION_PULSE_SAMPLES));
    RPW(odm_wire_write_u32(&w, 1u)); /* weak sub-tick evidence falls back to canonical tick center */
    RPW(odm_wire_write_u32(&w, 1u)); /* event interval = (previous presentation, current] */
    RPW(odm_wire_write_u32(&w, 1u)); /* presentation pulse decay is sample-domain linear */
    RPW(odm_wire_write_u32(&w, 1u)); /* pulse winner = max decayed contextual salience */
    RPW(odm_wire_write_u32(&w, 1u)); /* dominant captured event exposes its own sample-decayed pulse */
    for (i=0u;i<ODM_MUSIC_REACTION_LANE_COUNT+1u;++i)
        RPW(odm_wire_write_u16(&w,reaction_lane_edges[i]));
    for (i=0u;i<ODM_MUSIC_REACTION_FAMILY_COUNT+1u;++i)
        RPW(odm_wire_write_u16(&w,reaction_family_edges[i]));
    RPW(odm_wire_writer_finish(&w, &written));
#undef RPW
    if (written > ODM_MUSIC_REACTION_POLICY_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

static uint32_t react_lane_family(uint32_t lane) {
    uint32_t family;
    uint32_t start_bin;
    uint32_t end_bin;
    uint32_t center_bin;
    if (lane >= ODM_MUSIC_REACTION_LANE_COUNT) return ODM_MUSIC_REACTION_FAMILY_AIR;
    start_bin = reaction_lane_edges[lane];
    end_bin = reaction_lane_edges[lane + 1u];
    center_bin = start_bin + (end_bin - start_bin - 1u) / 2u;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        if (center_bin < reaction_family_edges[family + 1u]) return family;
    }
    return ODM_MUSIC_REACTION_FAMILY_AIR;
}

static uint32_t react_event_score_from_frame(const odm_music_reaction_frame *f) {
    uint32_t family;
    uint32_t a = 0u, b = 0u;
    if (!f) return 0u;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        uint32_t v = f->family_attack_q31[family];
        if (v >= a) { b = a; a = v; }
        else if (v > b) b = v;
    }
    (void)a;
    /* Family-only global impact requires at least two coherent frequency
     * families. A single narrow family may reshape its local geometry but may
     * not drag the whole scene. Broadband evidence remains an independent
     * global route. */
    return react_max(b, f->broadband_attack_q31);
}

static uint32_t react_event_strength_basis(const odm_music_reaction_frame *f) {
    uint32_t family;
    uint32_t a = 0u, b = 0u;
    uint64_t sum;
    if (!f) return 0u;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        uint32_t v = f->family_attack_q31[family];
        if (v >= a) { b = a; a = v; }
        else if (v > b) b = v;
    }
    sum = (uint64_t)f->broadband_attack_q31 + (uint64_t)a + (uint64_t)b;
    return react_clamp_q31((uint32_t)((sum + 1u) / 3u));
}

static int react_event_local_peak(const odm_music_reaction_frame *frames,
                                  uint64_t count, uint64_t index) {
    uint32_t score;
    uint64_t begin, end, j;
    if (!frames || index >= count) return 0;
    score = react_event_score_from_frame(&frames[index]);
    if (score <= frames[index].event_threshold_q31) return 0;
    begin = index > REACT_OFFLINE_PEAK_RADIUS_TICKS ?
            index - REACT_OFFLINE_PEAK_RADIUS_TICKS : 0u;
    end = index + REACT_OFFLINE_PEAK_RADIUS_TICKS < count ?
          index + REACT_OFFLINE_PEAK_RADIUS_TICKS : count - 1u;
    for (j = begin; j <= end; ++j) {
        uint32_t other;
        if (j == index) continue;
        other = react_event_score_from_frame(&frames[j]);
        if (other > score || (other == score && j < index)) return 0;
    }
    return 1;
}

/* Robust macro gate over LOCAL onset candidates, not over all 100 Hz ticks.
 * This distinction is critical: silence and ordinary sustain do not get to
 * define the event percentile, and a song with only a few legitimate impacts
 * does not lose all but the global maximum. The exact nearest-rank median is
 * selected by four integer radix passes with no allocation. */
static uint32_t react_select_event_score(const odm_music_reaction_frame *frames,
                                         uint64_t count) {
    uint32_t prefix = 0u;
    uint32_t mask = 0u;
    uint64_t eligible = 0u;
    uint64_t rank;
    uint64_t i;
    int shift;
    if (!frames || count == 0u) return 0u;
    for (i = 0u; i < count; ++i)
        if (react_event_local_peak(frames, count, i) != 0) ++eligible;
    if (eligible == 0u) return 0u;
    rank = (eligible * REACT_OFFLINE_CANDIDATE_QUANTILE_NUM +
            REACT_OFFLINE_CANDIDATE_QUANTILE_DEN - 1u) /
           REACT_OFFLINE_CANDIDATE_QUANTILE_DEN;
    if (rank == 0u) rank = 1u;
    for (shift = 24; shift >= 0; shift -= 8) {
        uint64_t hist[256];
        uint64_t acc = 0u, before = 0u;
        uint32_t bucket, selected = 0u;
        memset(hist, 0, sizeof(hist));
        for (i = 0u; i < count; ++i) {
            uint32_t v;
            if (react_event_local_peak(frames, count, i) == 0) continue;
            v = react_event_score_from_frame(&frames[i]);
            if ((v & mask) == prefix) ++hist[(v >> (unsigned)shift) & 255u];
        }
        for (bucket = 0u; bucket < 256u; ++bucket) {
            acc += hist[bucket];
            if (acc >= rank) { selected = bucket; break; }
            before = acc;
        }
        rank -= before;
        prefix |= selected << (unsigned)shift;
        mask |= UINT32_C(255) << (unsigned)shift;
    }
    return react_clamp_q31(prefix);
}

/* Exact robust order statistic for contextual macro salience. Crucially,
 * the calibration domain is the SAME set of peaks that can actually be
 * published after the macro gate and refractory rule. Rejected near-duplicate
 * candidates therefore cannot distort presentation hierarchy. */
static uint32_t react_select_salience_basis(const odm_music_reaction_frame *frames,
                                            uint64_t count, uint32_t macro_gate,
                                            uint64_t quantile_num,
                                            uint64_t quantile_den) {
    uint32_t prefix = 0u;
    uint32_t mask = 0u;
    uint64_t eligible = 0u;
    uint64_t rank;
    uint64_t i;
    uint64_t next_allowed = 0u;
    int shift;
    if (!frames || count == 0u || quantile_num == 0u || quantile_den == 0u ||
        quantile_num > quantile_den) return 0u;
    for (i = 0u; i < count; ++i) {
        uint32_t score = react_event_score_from_frame(&frames[i]);
        int is_peak = react_event_local_peak(frames, count, i);
        if (score < macro_gate || i < next_allowed) is_peak = 0;
        if (is_peak != 0) {
            ++eligible;
            next_allowed = i > UINT64_MAX - REACT_EVENT_REFRACTORY_TICKS ?
                           UINT64_MAX : i + REACT_EVENT_REFRACTORY_TICKS;
        }
    }
    if (eligible == 0u) return 0u;
    rank = (eligible * quantile_num + quantile_den - 1u) / quantile_den;
    if (rank == 0u) rank = 1u;
    for (shift = 24; shift >= 0; shift -= 8) {
        uint64_t hist[256];
        uint64_t acc = 0u, before = 0u;
        uint32_t bucket, selected = 0u;
        memset(hist, 0, sizeof(hist));
        next_allowed = 0u;
        for (i = 0u; i < count; ++i) {
            uint32_t score = react_event_score_from_frame(&frames[i]);
            uint32_t v;
            int is_peak = react_event_local_peak(frames, count, i);
            if (score < macro_gate || i < next_allowed) is_peak = 0;
            if (is_peak == 0) continue;
            next_allowed = i > UINT64_MAX - REACT_EVENT_REFRACTORY_TICKS ?
                           UINT64_MAX : i + REACT_EVENT_REFRACTORY_TICKS;
            v = react_event_strength_basis(&frames[i]);
            if ((v & mask) == prefix) ++hist[(v >> (unsigned)shift) & 255u];
        }
        for (bucket = 0u; bucket < 256u; ++bucket) {
            acc += hist[bucket];
            if (acc >= rank) { selected = bucket; break; }
            before = acc;
        }
        rank -= before;
        prefix |= selected << (unsigned)shift;
        mask |= UINT32_C(255) << (unsigned)shift;
    }
    return react_clamp_q31(prefix);
}

/* Contextual salience is deliberately NOT raw audio evidence. It is a monotone
 * full-track presentation ranking over legitimate published macro peaks. Ten
 * exact quantile knots preserve event ordering while preventing a narrow raw
 * attack range from making most impacts visually indistinguishable. */
static uint32_t react_contextual_salience(uint32_t evidence,
                                          const uint32_t *knots) {
    uint32_t i;
    uint32_t prev_ref;
    uint32_t prev_out;
    if (evidence == 0u || !knots) return 0u;
    if (evidence <= knots[0]) return REACT_SALIENCE_FLOOR_Q31;
    prev_ref = knots[0];
    prev_out = REACT_SALIENCE_FLOOR_Q31;
    for (i = 1u; i < REACT_SALIENCE_KNOT_COUNT; ++i) {
        uint32_t hi_ref = knots[i];
        uint32_t hi_out = i + 1u == REACT_SALIENCE_KNOT_COUNT ? REACT_Q31_ONE :
                          (uint32_t)(((uint64_t)REACT_Q31_ONE * (uint64_t)(i + 1u) + 5u) / 10u);
        if (evidence <= hi_ref) {
            uint32_t in_range;
            uint32_t out_range;
            uint64_t scaled;
            if (hi_ref <= prev_ref) return hi_out;
            in_range = hi_ref - prev_ref;
            out_range = hi_out - prev_out;
            scaled = (uint64_t)(evidence - prev_ref) * (uint64_t)out_range +
                     (uint64_t)in_range / 2u;
            scaled = (uint64_t)prev_out + scaled / (uint64_t)in_range;
            return scaled > REACT_Q31_ONE ? REACT_Q31_ONE : (uint32_t)scaled;
        }
        prev_ref = hi_ref;
        prev_out = hi_out;
    }
    return REACT_Q31_ONE;
}

static void react_top_two_families(const odm_music_reaction_frame *frame,
                                   uint32_t *out_first, uint32_t *out_second) {
    uint32_t first = UINT32_MAX, second = UINT32_MAX;
    uint32_t first_value = 0u, second_value = 0u;
    uint32_t family;
    if (!frame || !out_first || !out_second) return;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        uint32_t value = frame->family_attack_q31[family];
        if (first == UINT32_MAX || value > first_value ||
            (value == first_value && family < first)) {
            second = first;
            second_value = first_value;
            first = family;
            first_value = value;
        } else if (second == UINT32_MAX || value > second_value ||
                   (value == second_value && family < second)) {
            second = family;
            second_value = value;
        }
    }
    *out_first = first;
    *out_second = second;
}

static int32_t react_div_round_nearest_i64(int64_t numerator, int64_t denominator) {
    int64_t magnitude;
    int64_t result;
    if (denominator <= 0) return 0;
    if (numerator >= 0) result = (numerator + denominator / 2) / denominator;
    else {
        magnitude = -numerator;
        result = -((magnitude + denominator / 2) / denominator);
    }
    if (result > INT32_MAX) return INT32_MAX;
    if (result < INT32_MIN) return INT32_MIN;
    return (int32_t)result;
}

static int32_t react_refined_event_offset(const odm_music_reaction_frame *frames,
                                          uint64_t count, uint64_t index,
                                          uint32_t *out_confidence_q31) {
    uint32_t left, peak, right, neighbor_max;
    int64_t curvature;
    int64_t numerator;
    int32_t offset;
    if (out_confidence_q31) *out_confidence_q31 = 0u;
    if (!frames || index == 0u || index + 1u >= count) return 0;
    left = react_event_score_from_frame(&frames[index - 1u]);
    peak = react_event_score_from_frame(&frames[index]);
    right = react_event_score_from_frame(&frames[index + 1u]);
    if (peak == 0u || left > peak || right > peak) return 0;
    neighbor_max = react_max(left, right);
    if (out_confidence_q31)
        *out_confidence_q31 = react_ratio_q31(peak - neighbor_max, peak);
    curvature = (int64_t)2 * (int64_t)peak - (int64_t)left - (int64_t)right;
    if (curvature <= 0) return 0;
    numerator = (int64_t)REACT_SUBTICK_MAX_OFFSET_SAMPLES *
                ((int64_t)right - (int64_t)left);
    offset = react_div_round_nearest_i64(numerator, curvature);
    if (offset > REACT_SUBTICK_MAX_OFFSET_SAMPLES) offset = REACT_SUBTICK_MAX_OFFSET_SAMPLES;
    if (offset < -REACT_SUBTICK_MAX_OFFSET_SAMPLES) offset = -REACT_SUBTICK_MAX_OFFSET_SAMPLES;
    return offset;
}

static uint32_t react_event_flags_for_peak(const odm_music_reaction_frame *frame) {
    uint32_t flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    uint32_t family;
    if (!frame) return 0u;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        if (frame->family_attack_q31[family] >= frame->event_threshold_q31)
            flags |= UINT32_C(1) << family;
    }
    return flags;
}

static void react_fill_event_record(const odm_music_reaction_frame *frames,
                                    uint64_t count, uint64_t index,
                                    uint64_t event_index, uint32_t macro_gate,
                                    const uint32_t *salience_knots,
                                    odm_music_reaction_event *event) {
    const odm_music_reaction_frame *frame = &frames[index];
    uint32_t timing_confidence = 0u;
    int32_t offset = react_refined_event_offset(frames, count, index, &timing_confidence);
    uint32_t score = react_event_score_from_frame(frame);
    uint32_t basis = react_event_strength_basis(frame);
    uint64_t refined_sample = frame->center_sample;
    uint32_t strongest = UINT32_MAX, second = UINT32_MAX;
    memset(event, 0, sizeof(*event));
    if (offset < 0) refined_sample -= (uint64_t)(-(int64_t)offset);
    else refined_sample += (uint64_t)offset;
    react_top_two_families(frame, &strongest, &second);
    event->schema_version = ODM_MUSIC_REACTION_EVENT_SCHEMA_VERSION;
    event->event_flags = react_event_flags_for_peak(frame);
    event->event_index = event_index;
    event->peak_tick_index = frame->tick_index;
    event->peak_center_sample = frame->center_sample;
    event->refined_sample = refined_sample;
    event->refined_offset_samples = offset;
    event->timing_confidence_q31 = timing_confidence;
    event->score_q31 = score;
    event->strength_basis_q31 = basis;
    event->local_threshold_q31 = frame->event_threshold_q31;
    event->macro_gate_q31 = macro_gate;
    event->contextual_salience_q31 = react_contextual_salience(basis, salience_knots);
    event->strongest_family = strongest;
    event->second_family = second;
    memcpy(event->family_attack_q31, frame->family_attack_q31, sizeof(event->family_attack_q31));
    event->broadband_attack_q31 = frame->broadband_attack_q31;
    event->frame_flags = ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
}

static void react_top_two_event_families(const odm_music_reaction_event *event,
                                         uint32_t *out_first, uint32_t *out_second,
                                         uint32_t *out_first_value,
                                         uint32_t *out_second_value) {
    uint32_t first = UINT32_MAX, second = UINT32_MAX;
    uint32_t first_value = 0u, second_value = 0u;
    uint32_t family;
    if (!event || !out_first || !out_second || !out_first_value || !out_second_value) return;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        uint32_t value = event->family_attack_q31[family];
        if (first == UINT32_MAX || value > first_value ||
            (value == first_value && family < first)) {
            second = first;
            second_value = first_value;
            first = family;
            first_value = value;
        } else if (second == UINT32_MAX || value > second_value ||
                   (value == second_value && family < second)) {
            second = family;
            second_value = value;
        }
    }
    *out_first = first;
    *out_second = second;
    *out_first_value = first_value;
    *out_second_value = second_value;
}

static int react_event_record_valid(const odm_music_reaction_event *event) {
    const uint32_t allowed_events = ODM_MUSIC_REACTION_EVENT_SUB |
                                    ODM_MUSIC_REACTION_EVENT_BASS |
                                    ODM_MUSIC_REACTION_EVENT_LOW_MID |
                                    ODM_MUSIC_REACTION_EVENT_BODY |
                                    ODM_MUSIC_REACTION_EVENT_PRESENCE |
                                    ODM_MUSIC_REACTION_EVENT_AIR |
                                    ODM_MUSIC_REACTION_EVENT_ANY |
                                    ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    uint32_t first, second, first_value, second_value, expected_flags;
    uint32_t expected_score, expected_basis;
    uint64_t refined_expected;
    uint64_t sum;
    uint32_t family;
    if (!event || event->schema_version != ODM_MUSIC_REACTION_EVENT_SCHEMA_VERSION ||
        (event->event_flags & ~allowed_events) != 0u ||
        (event->event_flags & (ODM_MUSIC_REACTION_EVENT_ANY |
                               ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK)) !=
            (ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK) ||
        !react_center_canonical(event->peak_tick_index, event->peak_center_sample) ||
        event->refined_offset_samples < -REACT_SUBTICK_MAX_OFFSET_SAMPLES ||
        event->refined_offset_samples > REACT_SUBTICK_MAX_OFFSET_SAMPLES ||
        event->timing_confidence_q31 > REACT_Q31_ONE ||
        event->score_q31 > REACT_Q31_ONE ||
        event->strength_basis_q31 > REACT_Q31_ONE ||
        event->local_threshold_q31 > REACT_Q31_ONE ||
        event->macro_gate_q31 > REACT_Q31_ONE ||
        event->contextual_salience_q31 > REACT_Q31_ONE ||
        event->contextual_salience_q31 < REACT_SALIENCE_FLOOR_Q31 ||
        event->strongest_family >= ODM_MUSIC_REACTION_FAMILY_COUNT ||
        event->second_family >= ODM_MUSIC_REACTION_FAMILY_COUNT ||
        event->strongest_family == event->second_family ||
        event->broadband_attack_q31 > REACT_Q31_ONE ||
        event->frame_flags != ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE ||
        !react_zero_u32(event->reserved, 5u)) return 0;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family)
        if (event->family_attack_q31[family] > REACT_Q31_ONE) return 0;

    if (event->refined_offset_samples < 0) {
        uint64_t magnitude = (uint64_t)(-(int64_t)event->refined_offset_samples);
        if (magnitude > event->peak_center_sample) return 0;
        refined_expected = event->peak_center_sample - magnitude;
    } else {
        uint64_t offset = (uint64_t)event->refined_offset_samples;
        if (event->peak_center_sample > UINT64_MAX - offset) return 0;
        refined_expected = event->peak_center_sample + offset;
    }
    if (event->refined_sample != refined_expected) return 0;

    react_top_two_event_families(event, &first, &second, &first_value, &second_value);
    if (event->strongest_family != first || event->second_family != second) return 0;
    expected_score = react_max(second_value, event->broadband_attack_q31);
    sum = (uint64_t)event->broadband_attack_q31 + (uint64_t)first_value + (uint64_t)second_value;
    expected_basis = react_clamp_q31((uint32_t)((sum + 1u) / 3u));
    if (event->score_q31 != expected_score || event->strength_basis_q31 != expected_basis ||
        event->score_q31 <= event->local_threshold_q31 ||
        event->score_q31 < event->macro_gate_q31) return 0;

    expected_flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family)
        if (event->family_attack_q31[family] >= event->local_threshold_q31)
            expected_flags |= UINT32_C(1) << family;
    return event->event_flags == expected_flags;
}

static uint64_t react_event_effective_sample(const odm_music_reaction_event *event,
                                             uint32_t *out_subtick_trusted) {
    uint32_t trusted = 0u;
    uint64_t sample;
    if (!event) return 0u;
    sample = event->peak_center_sample;
    if (event->refined_offset_samples != 0 &&
        event->timing_confidence_q31 >= REACT_SUBTICK_TRUST_MIN_Q31) {
        sample = event->refined_sample;
        trusted = 1u;
    }
    if (out_subtick_trusted) *out_subtick_trusted = trusted;
    return sample;
}

static uint32_t react_event_pulse_at_sample(const odm_music_reaction_event *event,
                                            uint64_t effective_sample,
                                            uint64_t presentation_sample) {
    uint64_t elapsed;
    uint64_t remaining;
    uint64_t scaled;
    if (!event || presentation_sample < effective_sample) return 0u;
    elapsed = presentation_sample - effective_sample;
    if (elapsed >= (uint64_t)REACT_EVENT_PRESENTATION_PULSE_SAMPLES) return 0u;
    remaining = (uint64_t)REACT_EVENT_PRESENTATION_PULSE_SAMPLES - elapsed;
    scaled = (uint64_t)event->contextual_salience_q31 * remaining +
             (uint64_t)REACT_EVENT_PRESENTATION_PULSE_SAMPLES / 2u;
    scaled /= (uint64_t)REACT_EVENT_PRESENTATION_PULSE_SAMPLES;
    return scaled > REACT_Q31_ONE ? REACT_Q31_ONE : (uint32_t)scaled;
}

static int react_frame_valid(const odm_music_reaction_frame *f) {
    uint32_t i;
    const uint32_t allowed_events = ODM_MUSIC_REACTION_EVENT_SUB |
                                    ODM_MUSIC_REACTION_EVENT_BASS |
                                    ODM_MUSIC_REACTION_EVENT_LOW_MID |
                                    ODM_MUSIC_REACTION_EVENT_BODY |
                                    ODM_MUSIC_REACTION_EVENT_PRESENCE |
                                    ODM_MUSIC_REACTION_EVENT_AIR |
                                    ODM_MUSIC_REACTION_EVENT_ANY |
                                    ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    if (!f || f->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        !react_center_canonical(f->tick_index, f->center_sample) ||
        (f->event_flags & ~allowed_events) != 0u ||
        f->broadband_q31 > REACT_Q31_ONE ||
        f->broadband_attack_q31 > REACT_Q31_ONE ||
        f->event_strength_q31 > REACT_Q31_ONE ||
        f->event_pulse_q31 > REACT_Q31_ONE ||
        f->event_threshold_q31 > REACT_Q31_ONE ||
        f->macro_salience_q31 > REACT_Q31_ONE ||
        f->macro_pulse_q31 > REACT_Q31_ONE ||
        (f->frame_flags & ~ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) != 0u ||
        !react_zero_u32(f->reserved, 8u)) return 0;
    if ((f->event_flags & ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK) != 0u &&
        (f->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) == 0u) return 0;
    if ((f->frame_flags & ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) == 0u &&
        (f->macro_salience_q31 != 0u || f->macro_pulse_q31 != 0u)) return 0;
    if (f->macro_salience_q31 != 0u &&
        ((f->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) == 0u ||
         f->event_strength_q31 == 0u || f->macro_pulse_q31 < f->macro_salience_q31)) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
        if (f->lane_q31[i] > REACT_Q31_ONE || f->lane_fast_q31[i] > REACT_Q31_ONE ||
            f->lane_attack_q31[i] > REACT_Q31_ONE || f->lane_fast_q31[i] > f->lane_q31[i]) return 0;
    }
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i) {
        if (f->family_q31[i] > REACT_Q31_ONE || f->family_attack_q31[i] > REACT_Q31_ONE) return 0;
    }
    return 1;
}

static uint64_t react_quantile_rank(uint64_t count, int attack) {
    uint64_t num = attack != 0 ? REACT_ATTACK_QUANTILE_NUM : REACT_QUANTILE_NUM;
    uint64_t den = attack != 0 ? REACT_ATTACK_QUANTILE_DEN : REACT_QUANTILE_DEN;
    uint64_t rank = (count * num + den - 1u) / den;
    return rank == 0u ? 1u : rank;
}

/* Exact unsigned order statistic using four radix passes. This is O(4*N),
 * allocation-free and immune to the very outlier that made max-normalization
 * perceptually fragile in 0.14. */
static uint32_t react_select_lane(const odm_music_reaction_tick *ticks,
                                  uint64_t count, uint32_t lane, int attack) {
    uint32_t prefix = 0u;
    uint32_t mask = 0u;
    uint64_t eligible = count;
    uint64_t rank;
    int shift;
    if (attack != 0) {
        uint64_t k;
        eligible = 0u;
        for (k = 0u; k < count; ++k) {
            uint32_t cur = ticks[k].lane_magnitude_q31[lane];
            uint32_t prev = react_trajectory_previous_ticks(ticks, k, lane);
            if (react_positive_delta(cur, prev) != 0u) ++eligible;
        }
        if (eligible == 0u) return 0u;
    }
    rank = react_quantile_rank(eligible, attack);
    for (shift = 24; shift >= 0; shift -= 8) {
        uint64_t hist[256];
        uint64_t i, acc = 0u, before = 0u;
        uint32_t b, selected = 0u;
        memset(hist, 0, sizeof(hist));
        for (i = 0u; i < count; ++i) {
            uint32_t cur = ticks[i].lane_magnitude_q31[lane];
            uint32_t prev = react_trajectory_previous_ticks(ticks, i, lane);
            uint32_t v = attack != 0 ? react_positive_delta(cur, prev) : cur;
            if (attack != 0 && v == 0u) continue;
            if ((v & mask) == prefix) ++hist[(v >> (unsigned)shift) & 255u];
        }
        for (b = 0u; b < 256u; ++b) {
            acc += hist[b];
            if (acc >= rank) { selected = b; break; }
            before = acc;
        }
        rank -= before;
        prefix |= selected << (unsigned)shift;
        mask |= UINT32_C(255) << (unsigned)shift;
    }
    return react_clamp_q31(prefix);
}

static uint32_t react_select_family(const odm_music_reaction_tick *ticks,
                                    uint64_t count, uint32_t family, int attack) {
    uint32_t prefix = 0u;
    uint32_t mask = 0u;
    uint64_t eligible = count;
    uint64_t rank;
    int shift;
    if (attack != 0) {
        uint64_t k;
        eligible = 0u;
        for (k = 0u; k < count; ++k) {
            uint32_t cur = ticks[k].family_magnitude_q31[family];
            uint32_t prev = k == 0u ? cur : ticks[k - 1u].family_magnitude_q31[family];
            if (react_positive_delta(cur, prev) != 0u) ++eligible;
        }
        if (eligible == 0u) return 0u;
    }
    rank = react_quantile_rank(eligible, attack);
    for (shift = 24; shift >= 0; shift -= 8) {
        uint64_t hist[256];
        uint64_t i, acc = 0u, before = 0u;
        uint32_t b, selected = 0u;
        memset(hist, 0, sizeof(hist));
        for (i = 0u; i < count; ++i) {
            uint32_t cur = ticks[i].family_magnitude_q31[family];
            uint32_t prev = i == 0u ? cur : ticks[i - 1u].family_magnitude_q31[family];
            uint32_t v = attack != 0 ? react_positive_delta(cur, prev) : cur;
            if (attack != 0 && v == 0u) continue;
            if ((v & mask) == prefix) ++hist[(v >> (unsigned)shift) & 255u];
        }
        for (b = 0u; b < 256u; ++b) {
            acc += hist[b];
            if (acc >= rank) { selected = b; break; }
            before = acc;
        }
        rank -= before;
        prefix |= selected << (unsigned)shift;
        mask |= UINT32_C(255) << (unsigned)shift;
    }
    return react_clamp_q31(prefix);
}

static uint32_t react_select_broad(const odm_music_reaction_tick *ticks,
                                   uint64_t count, int attack) {
    uint32_t prefix = 0u;
    uint32_t mask = 0u;
    uint64_t eligible = count;
    uint64_t rank;
    int shift;
    if (attack != 0) {
        uint64_t k;
        eligible = 0u;
        for (k = 0u; k < count; ++k) {
            uint32_t cur = ticks[k].broadband_magnitude_q31;
            uint32_t prev = k == 0u ? cur : ticks[k - 1u].broadband_magnitude_q31;
            if (react_positive_delta(cur, prev) != 0u) ++eligible;
        }
        if (eligible == 0u) return 0u;
    }
    rank = react_quantile_rank(eligible, attack);
    for (shift = 24; shift >= 0; shift -= 8) {
        uint64_t hist[256];
        uint64_t i, acc = 0u, before = 0u;
        uint32_t b, selected = 0u;
        memset(hist, 0, sizeof(hist));
        for (i = 0u; i < count; ++i) {
            uint32_t cur = ticks[i].broadband_magnitude_q31;
            uint32_t prev = i == 0u ? cur : ticks[i - 1u].broadband_magnitude_q31;
            uint32_t v = attack != 0 ? react_positive_delta(cur, prev) : cur;
            if (attack != 0 && v == 0u) continue;
            if ((v & mask) == prefix) ++hist[(v >> (unsigned)shift) & 255u];
        }
        for (b = 0u; b < 256u; ++b) {
            acc += hist[b];
            if (acc >= rank) { selected = b; break; }
            before = acc;
        }
        rank -= before;
        prefix |= selected << (unsigned)shift;
        mask |= UINT32_C(255) << (unsigned)shift;
    }
    return react_clamp_q31(prefix);
}

odm_status odm_music_reaction_lane_bounds(uint32_t lane,
                                          uint32_t *out_start_bin,
                                          uint32_t *out_end_bin) {
    if (!out_start_bin || !out_end_bin || lane >= ODM_MUSIC_REACTION_LANE_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_start_bin = reaction_lane_edges[lane];
    *out_end_bin = reaction_lane_edges[lane + 1u];
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_extract_tick(
    const odm_music_analysis_tick *base_tick,
    const odm_music_analysis_scratch *scratch,
    odm_music_reaction_tick *out_tick) {
    odm_music_reaction_tick t;
    uint64_t broad_sum = 0u;
    uint32_t lane, family;
    if (!base_tick || !scratch || !out_tick) return ODM_STATUS_INVALID_ARGUMENT;
    if (base_tick->silence > 1u ||
        !react_center_canonical(base_tick->tick_index, base_tick->center_sample))
        return ODM_STATUS_INVALID_DATA;
    for (lane = reaction_lane_edges[0];
         lane < reaction_lane_edges[ODM_MUSIC_REACTION_LANE_COUNT]; ++lane) {
        if (scratch->magnitude_q31[lane] > REACT_Q31_ONE) return ODM_STATUS_INVALID_DATA;
    }
    memset(&t, 0, sizeof(t));
    t.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    t.tick_index = base_tick->tick_index;
    t.center_sample = base_tick->center_sample;
    for (lane = 0u; lane < ODM_MUSIC_REACTION_LANE_COUNT; ++lane) {
        uint32_t a = reaction_lane_edges[lane];
        uint32_t b = reaction_lane_edges[lane + 1u];
        t.lane_magnitude_q31[lane] = react_average(scratch->magnitude_q31, a, b);
        broad_sum += t.lane_magnitude_q31[lane];
    }
    t.broadband_magnitude_q31 = (uint32_t)((broad_sum +
        (uint64_t)ODM_MUSIC_REACTION_LANE_COUNT / 2u) /
        (uint64_t)ODM_MUSIC_REACTION_LANE_COUNT);
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        t.family_magnitude_q31[family] = react_average(
            scratch->magnitude_q31,
            reaction_family_edges[family], reaction_family_edges[family + 1u]);
    }
    *out_tick = t;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_profile_build(
    const odm_music_reaction_tick *ticks,
    uint64_t tick_count,
    odm_music_reaction_profile *out_profile) {
    odm_music_reaction_profile p;
    uint64_t i, expected;
    uint32_t lane, family;
    if (!ticks || !out_profile || tick_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    expected = ticks[0].tick_index;
    for (i = 0u; i < tick_count; ++i) {
        if (!react_tick_valid(&ticks[i]) || ticks[i].tick_index != expected ||
            ticks[i].tick_index == UINT64_MAX) {
            return ticks[i].tick_index == UINT64_MAX ? ODM_STATUS_OVERFLOW : ODM_STATUS_INVALID_DATA;
        }
        ++expected;
    }
    memset(&p, 0, sizeof(p));
    p.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    p.tick_count = tick_count;
    p.first_tick_index = ticks[0].tick_index;
    p.last_tick_index = ticks[tick_count - 1u].tick_index;
    for (lane = 0u; lane < ODM_MUSIC_REACTION_LANE_COUNT; ++lane) {
        p.lane_magnitude_ref[lane] = react_select_lane(ticks, tick_count, lane, 0);
        p.lane_attack_ref[lane] = react_select_lane(ticks, tick_count, lane, 1);
    }
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        p.family_magnitude_ref[family] = react_select_family(ticks, tick_count, family, 0);
        p.family_attack_ref[family] = react_select_family(ticks, tick_count, family, 1);
    }
    p.broadband_magnitude_ref = react_select_broad(ticks, tick_count, 0);
    p.broadband_attack_ref = react_select_broad(ticks, tick_count, 1);
    *out_profile = p;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_profile_bytes(
    const odm_music_reaction_profile *profile,
    uint8_t *buffer, uint64_t capacity, uint64_t *out_required) {
    odm_wire_writer w;
    uint64_t written = 0u;
    uint32_t i;
    odm_sha256_digest policy_sha;
    odm_status st;
    if (!profile || !out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_MUSIC_REACTION_PROFILE_BYTES;
    if (!buffer || capacity < ODM_MUSIC_REACTION_PROFILE_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;
    if (!react_profile_valid(profile)) return ODM_STATUS_INVALID_DATA;
    memset(buffer, 0, ODM_MUSIC_REACTION_PROFILE_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_MUSIC_REACTION_PROFILE_BYTES);
    if (st != ODM_STATUS_OK) return st;
#define RW(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    RW(odm_wire_write_bytes(&w, (const uint8_t *)"ODMRXN1\0", 8u));
    RW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_POLICY_VERSION));
    RW(odm_wire_write_u32(&w, profile->schema_version));
    RW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_LANE_COUNT));
    RW(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_FAMILY_COUNT));
    RW(odm_music_reaction_policy_current_sha256(&policy_sha));
    RW(odm_wire_write_bytes(&w, policy_sha.bytes, sizeof(policy_sha.bytes)));
    RW(odm_wire_write_u64(&w, profile->tick_count));
    RW(odm_wire_write_u64(&w, profile->first_tick_index));
    RW(odm_wire_write_u64(&w, profile->last_tick_index));
    for (i=0u;i<ODM_MUSIC_REACTION_LANE_COUNT;++i) RW(odm_wire_write_u32(&w,profile->lane_magnitude_ref[i]));
    for (i=0u;i<ODM_MUSIC_REACTION_LANE_COUNT;++i) RW(odm_wire_write_u32(&w,profile->lane_attack_ref[i]));
    for (i=0u;i<ODM_MUSIC_REACTION_FAMILY_COUNT;++i) RW(odm_wire_write_u32(&w,profile->family_magnitude_ref[i]));
    for (i=0u;i<ODM_MUSIC_REACTION_FAMILY_COUNT;++i) RW(odm_wire_write_u32(&w,profile->family_attack_ref[i]));
    RW(odm_wire_write_u32(&w,profile->broadband_magnitude_ref));
    RW(odm_wire_write_u32(&w,profile->broadband_attack_ref));
    for (i=0u;i<ODM_MUSIC_REACTION_LANE_COUNT+1u;++i) RW(odm_wire_write_u16(&w,reaction_lane_edges[i]));
    for (i=0u;i<ODM_MUSIC_REACTION_FAMILY_COUNT+1u;++i) RW(odm_wire_write_u16(&w,reaction_family_edges[i]));
    RW(odm_wire_writer_finish(&w,&written));
#undef RW
    if (written > ODM_MUSIC_REACTION_PROFILE_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_policy_bytes(
    uint8_t *buffer, uint64_t capacity, uint64_t *out_required) {
    return react_policy_encode(buffer, capacity, out_required);
}

odm_status odm_music_reaction_policy_current_sha256(odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_MUSIC_REACTION_POLICY_BYTES];
    uint64_t required = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = react_policy_encode(bytes, sizeof(bytes), &required);
    if (st != ODM_STATUS_OK) return st;
    if (required != sizeof(bytes)) return ODM_STATUS_INVARIANT_BROKEN;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}

odm_status odm_music_reaction_profile_sha256(
    const odm_music_reaction_profile *profile,
    odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_MUSIC_REACTION_PROFILE_BYTES];
    uint64_t required = 0u;
    odm_status st;
    if (!profile || !out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_music_reaction_profile_bytes(profile, bytes, sizeof(bytes), &required);
    if (st != ODM_STATUS_OK) return st;
    if (required != sizeof(bytes)) return ODM_STATUS_INVARIANT_BROKEN;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}

odm_status odm_music_reaction_state_init(odm_music_reaction_state *state) {
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    state->schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_resolve_tick(
    odm_music_reaction_state *state,
    const odm_music_reaction_profile *profile,
    const odm_music_reaction_tick *tick,
    odm_music_reaction_frame *out_frame) {
    odm_music_reaction_state next;
    odm_music_reaction_frame f;
    uint32_t lane, family;
    uint32_t event_score;
    uint32_t threshold;
    uint32_t event_strength = 0u;
    uint32_t event_flags = 0u;
    if (!state || !profile || !tick || !out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (state->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        profile->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        tick->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (!react_state_valid(state) || !react_profile_valid(profile) ||
        !react_tick_valid(tick) ||
        tick->tick_index < profile->first_tick_index ||
        tick->tick_index > profile->last_tick_index)
        return ODM_STATUS_INVALID_DATA;
    if (state->initialized != 0u && tick->tick_index != state->next_tick_index)
        return ODM_STATUS_INVALID_DATA;
    if (tick->tick_index == UINT64_MAX) return ODM_STATUS_OVERFLOW;

    next = *state;
    memset(&f, 0, sizeof(f));
    f.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    f.tick_index = tick->tick_index;
    f.center_sample = tick->center_sample;

    for (lane = 0u; lane < ODM_MUSIC_REACTION_LANE_COUNT; ++lane) {
        uint32_t raw_norm = react_shape(react_norm(tick->lane_magnitude_q31[lane],
                                                  profile->lane_magnitude_ref[lane]));
        uint32_t previous_raw = react_trajectory_previous_state(
            &next, lane, tick->lane_magnitude_q31[lane]);
        uint32_t attack_raw = react_positive_delta(tick->lane_magnitude_q31[lane], previous_raw);
        uint32_t attack = react_shape(react_attack_norm(attack_raw, profile->lane_attack_ref[lane]));
        uint32_t family_index = react_lane_family(lane);
        uint32_t env = next.initialized == 0u ? raw_norm :
                       react_release(next.lane_envelope_q31[lane], raw_norm,
                                     react_family_release_divisor[family_index]);
        uint32_t fast = next.initialized == 0u ? raw_norm :
                        react_release(next.lane_fast_envelope_q31[lane], raw_norm,
                                      react_family_fast_release_divisor[family_index]);
        if (fast > env) return ODM_STATUS_INVARIANT_BROKEN;
        f.lane_q31[lane] = env;
        f.lane_fast_q31[lane] = fast;
        f.lane_attack_q31[lane] = attack;
        next.previous_lane_q31[lane] = tick->lane_magnitude_q31[lane];
        next.lane_envelope_q31[lane] = env;
        next.lane_fast_envelope_q31[lane] = fast;
    }
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        uint32_t raw_norm = react_shape(react_norm(tick->family_magnitude_q31[family],
                                                  profile->family_magnitude_ref[family]));
        uint32_t previous_raw = next.initialized == 0u ? tick->family_magnitude_q31[family]
                                                       : next.previous_family_q31[family];
        uint32_t attack_raw = react_positive_delta(tick->family_magnitude_q31[family], previous_raw);
        uint32_t attack = react_shape(react_attack_norm(attack_raw, profile->family_attack_ref[family]));
        uint32_t env = next.initialized == 0u ? raw_norm :
                       react_release(next.family_envelope_q31[family], raw_norm,
                                     react_family_release_divisor[family]);
        f.family_q31[family] = env;
        f.family_attack_q31[family] = attack;
        next.previous_family_q31[family] = tick->family_magnitude_q31[family];
        next.family_envelope_q31[family] = env;
    }
    {
        uint32_t previous_raw = next.initialized == 0u ? tick->broadband_magnitude_q31
                                                       : next.previous_broadband_q31;
        uint32_t raw_norm = react_shape(react_norm(tick->broadband_magnitude_q31,
                                                  profile->broadband_magnitude_ref));
        uint32_t attack_raw = react_positive_delta(tick->broadband_magnitude_q31, previous_raw);
        uint32_t attack = react_shape(react_attack_norm(attack_raw, profile->broadband_attack_ref));
        f.broadband_q31 = next.initialized == 0u ? raw_norm :
                          react_release(next.broadband_envelope_q31, raw_norm,
                                        REACT_BROADBAND_RELEASE_DIVISOR);
        f.broadband_attack_q31 = attack;
        next.previous_broadband_q31 = tick->broadband_magnitude_q31;
        next.broadband_envelope_q31 = f.broadband_q31;
    }

    /* Global impact requires coherent evidence. Streaming and offline paths
     * deliberately share the exact same score semantics: broadband evidence
     * or the second-strongest family attack. A single narrow family can shape
     * local geometry immediately but cannot drag the whole scene. */
    event_score = react_event_score_from_frame(&f);
    /* Threshold is computed from history BEFORE the current attack is folded
     * into the baseline. This preserves same-tick onset sensitivity. */
    threshold = next.event_baseline_q31 > REACT_Q31_ONE / REACT_EVENT_THRESHOLD_MULTIPLIER ?
                REACT_Q31_ONE : next.event_baseline_q31 * REACT_EVENT_THRESHOLD_MULTIPLIER;
    if (threshold < REACT_Q31_030) threshold = REACT_Q31_030;
    if (threshold > REACT_Q31_ONE) threshold = REACT_Q31_ONE;
    f.event_threshold_q31 = threshold;

    if (next.refractory_ticks != 0u) --next.refractory_ticks;
    if (event_score > threshold && next.refractory_ticks == 0u) {
        event_strength = react_event_strength_basis(&f);
        event_flags = ODM_MUSIC_REACTION_EVENT_ANY;
        for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
            if (f.family_attack_q31[family] >= threshold) event_flags |= UINT32_C(1) << family;
        }
        next.refractory_ticks = REACT_EVENT_REFRACTORY_TICKS;
    }
    /* Slow causal baseline over the complete attack-score stream. Zero-score
     * ticks are meaningful spacing between transients, but the independent
     * absolute threshold above prevents quiet gaps from collapsing sensitivity
     * toward noise. Current evidence is folded only after event resolution. */
    if (next.initialized == 0u) next.event_baseline_q31 = event_score;
    else {
        int64_t delta = (int64_t)event_score - (int64_t)next.event_baseline_q31;
        int64_t adjusted = (int64_t)next.event_baseline_q31 +
                           delta / (int64_t)REACT_EVENT_BASELINE_DIVISOR;
        if (adjusted < 0) adjusted = 0;
        if ((uint64_t)adjusted > REACT_Q31_ONE) adjusted = REACT_Q31_ONE;
        next.event_baseline_q31 = (uint32_t)adjusted;
    }
    {
        uint32_t decay = next.event_pulse_q31 / REACT_EVENT_PULSE_DECAY_DIVISOR;
        if (decay == 0u && next.event_pulse_q31 != 0u) decay = 1u;
        next.event_pulse_q31 = next.event_pulse_q31 > decay ?
                               next.event_pulse_q31 - decay : 0u;
        if (event_strength > next.event_pulse_q31) next.event_pulse_q31 = event_strength;
    }
    f.event_strength_q31 = event_strength;
    f.event_pulse_q31 = next.event_pulse_q31;
    f.event_flags = event_flags;

    next.initialized = 1u;
    next.next_tick_index = tick->tick_index + 1u;
    *state = next;
    *out_frame = f;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_build_events_offline(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    odm_music_reaction_event *out_events,
    uint64_t event_capacity,
    uint64_t *out_event_count,
    uint32_t *out_macro_gate_q31) {
    uint64_t i;
    uint64_t required = 0u;
    uint64_t next_allowed = 0u;
    uint64_t event_index = 0u;
    uint32_t macro_gate;
    uint32_t salience_knots[REACT_SALIENCE_KNOT_COUNT];
    if (!out_event_count || !out_macro_gate_q31) return ODM_STATUS_INVALID_ARGUMENT;
    *out_event_count = 0u;
    *out_macro_gate_q31 = 0u;
    if (frame_count == 0u) return ODM_STATUS_OK;
    if (!frames) return ODM_STATUS_INVALID_ARGUMENT;
    if (!out_events && event_capacity != 0u) return ODM_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < frame_count; ++i) {
        if (!react_frame_valid(&frames[i])) return ODM_STATUS_INVALID_DATA;
        if (i != 0u) {
            if (frames[i - 1u].tick_index == UINT64_MAX ||
                frames[i].tick_index != frames[i - 1u].tick_index + 1u)
                return ODM_STATUS_INVALID_DATA;
        }
    }
    macro_gate = react_select_event_score(frames, frame_count);
    for (i = 0u; i < REACT_SALIENCE_KNOT_COUNT; ++i)
        salience_knots[i] = react_select_salience_basis(frames, frame_count, macro_gate,
                                                        react_salience_quantile_num[i],
                                                        REACT_SALIENCE_QUANTILE_DEN);
    for (i = 0u; i < frame_count; ++i) {
        uint32_t score = react_event_score_from_frame(&frames[i]);
        int is_peak = react_event_local_peak(frames, frame_count, i);
        if (score < macro_gate || i < next_allowed) is_peak = 0;
        if (is_peak != 0) {
            ++required;
            next_allowed = i > UINT64_MAX - REACT_EVENT_REFRACTORY_TICKS ?
                           UINT64_MAX : i + REACT_EVENT_REFRACTORY_TICKS;
        }
    }
    *out_event_count = required;
    *out_macro_gate_q31 = macro_gate;
    if (!out_events && event_capacity == 0u) return ODM_STATUS_OK;
    if (event_capacity < required) return ODM_STATUS_BUFFER_TOO_SMALL;
    next_allowed = 0u;
    for (i = 0u; i < frame_count; ++i) {
        uint32_t score = react_event_score_from_frame(&frames[i]);
        int is_peak = react_event_local_peak(frames, frame_count, i);
        if (score < macro_gate || i < next_allowed) is_peak = 0;
        if (is_peak != 0) {
            react_fill_event_record(frames, frame_count, i, event_index, macro_gate,
                                    salience_knots, &out_events[event_index]);
            ++event_index;
            next_allowed = i > UINT64_MAX - REACT_EVENT_REFRACTORY_TICKS ?
                           UINT64_MAX : i + REACT_EVENT_REFRACTORY_TICKS;
        }
    }
    return event_index == required ? ODM_STATUS_OK : ODM_STATUS_INVARIANT_BROKEN;
}

odm_status odm_music_reaction_refine_events_offline(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    odm_music_reaction_frame *out_frames,
    uint64_t frame_capacity) {
    uint64_t i;
    uint64_t bytes;
    uint64_t next_allowed = 0u;
    uint32_t pulse = 0u;
    uint32_t macro_pulse = 0u;
    uint32_t macro_gate;
    uint32_t salience_knots[REACT_SALIENCE_KNOT_COUNT];
    if (frame_count == 0u) return ODM_STATUS_OK;
    if (!frames || !out_frames) return ODM_STATUS_INVALID_ARGUMENT;
    if (frame_capacity < frame_count) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (frame_count > UINT64_MAX / (uint64_t)sizeof(*frames)) return ODM_STATUS_OVERFLOW;
    bytes = frame_count * (uint64_t)sizeof(*frames);
    if (bytes > (uint64_t)SIZE_MAX) return ODM_STATUS_OVERFLOW;

    /* Full preflight before publication. This also makes exact in-place use
     * safe: all future evidence fields are known valid before any event field
     * is rewritten. */
    for (i = 0u; i < frame_count; ++i) {
        if (!react_frame_valid(&frames[i])) return ODM_STATUS_INVALID_DATA;
        if (i != 0u) {
            if (frames[i - 1u].tick_index == UINT64_MAX ||
                frames[i].tick_index != frames[i - 1u].tick_index + 1u)
                return ODM_STATUS_INVALID_DATA;
        }
    }

    memmove(out_frames, frames, (size_t)bytes);
    macro_gate = react_select_event_score(out_frames, frame_count);
    for (i = 0u; i < REACT_SALIENCE_KNOT_COUNT; ++i)
        salience_knots[i] = react_select_salience_basis(out_frames, frame_count, macro_gate,
                                                        react_salience_quantile_num[i],
                                                        REACT_SALIENCE_QUANTILE_DEN);
    for (i = 0u; i < frame_count; ++i) {
        out_frames[i].event_strength_q31 = 0u;
        out_frames[i].event_pulse_q31 = 0u;
        out_frames[i].event_flags = 0u;
        out_frames[i].macro_salience_q31 = 0u;
        out_frames[i].macro_pulse_q31 = 0u;
        out_frames[i].frame_flags = ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
    }

    for (i = 0u; i < frame_count; ++i) {
        uint32_t score = react_event_score_from_frame(&out_frames[i]);
        uint32_t threshold = out_frames[i].event_threshold_q31;
        uint32_t strength = 0u;
        uint32_t salience = 0u;
        uint32_t flags = 0u;
        int is_peak = react_event_local_peak(out_frames, frame_count, i);

        if (score < macro_gate || i < next_allowed) is_peak = 0;
        if (is_peak != 0) {
            uint32_t family;
            strength = react_event_strength_basis(&out_frames[i]);
            salience = react_contextual_salience(strength, salience_knots);
            flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
            for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
                if (out_frames[i].family_attack_q31[family] >= threshold)
                    flags |= UINT32_C(1) << family;
            }
            next_allowed = i > UINT64_MAX - REACT_EVENT_REFRACTORY_TICKS ?
                           UINT64_MAX : i + REACT_EVENT_REFRACTORY_TICKS;
        }

        {
            uint32_t decay = pulse / REACT_EVENT_PULSE_DECAY_DIVISOR;
            uint32_t macro_decay = macro_pulse / REACT_EVENT_PULSE_DECAY_DIVISOR;
            if (decay == 0u && pulse != 0u) decay = 1u;
            if (macro_decay == 0u && macro_pulse != 0u) macro_decay = 1u;
            pulse = pulse > decay ? pulse - decay : 0u;
            macro_pulse = macro_pulse > macro_decay ? macro_pulse - macro_decay : 0u;
            if (strength > pulse) pulse = strength;
            if (salience > macro_pulse) macro_pulse = salience;
        }
        out_frames[i].event_strength_q31 = strength;
        out_frames[i].event_pulse_q31 = pulse;
        out_frames[i].event_flags = flags;
        out_frames[i].macro_salience_q31 = salience;
        out_frames[i].macro_pulse_q31 = macro_pulse;
    }
    return ODM_STATUS_OK;
}

static odm_status react_projection_sequence_preflight(
    const odm_music_reaction_frame *frames, uint64_t frame_count) {
    uint64_t i;
    uint32_t frame_flags;
    if (!frames || frame_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    frame_flags = frames[0].frame_flags;
    for (i = 0u; i < frame_count; ++i) {
        if (!react_frame_valid(&frames[i])) return ODM_STATUS_INVALID_DATA;
        /* A presentation projection is one semantic stream. Mixing streaming
         * evidence-only frames with full-track contextual-salience frames
         * would make macro authority depend on the presentation interval. */
        if (frames[i].frame_flags != frame_flags) return ODM_STATUS_INVALID_DATA;
        if (i != 0u) {
            if (frames[i - 1u].tick_index == UINT64_MAX ||
                frames[i].tick_index != frames[i - 1u].tick_index + 1u)
                return ODM_STATUS_INVALID_DATA;
        }
    }
    return ODM_STATUS_OK;
}

static odm_status react_projection_validate_interval(
    const odm_music_reaction_frame *frames, uint64_t frame_count,
    int64_t previous_sample, uint64_t presentation_sample,
    uint64_t *out_source_index, uint64_t *out_begin_index,
    uint64_t *out_end_index, uint32_t *out_capture_count) {
    uint64_t source_tick, first_tick, last_tick, begin_tick, end_tick;
    uint64_t gap;
    if (!frames || frame_count == 0u || !out_source_index || !out_begin_index ||
        !out_end_index || !out_capture_count) return ODM_STATUS_INVALID_ARGUMENT;
    if (presentation_sample > (uint64_t)INT64_MAX || previous_sample < -1)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (previous_sample == -1 && presentation_sample != 0u)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (previous_sample >= 0 && (uint64_t)previous_sample >= presentation_sample)
        return ODM_STATUS_INVALID_ARGUMENT;
    gap = previous_sample < 0 ? presentation_sample + 1u :
          presentation_sample - (uint64_t)previous_sample;
    if (gap == 0u || gap > (uint64_t)ODM_MUSIC_REACTION_PROJECTION_MAX_GAP_SAMPLES)
        return ODM_STATUS_BUDGET_EXCEEDED;

    first_tick = frames[0].tick_index;
    last_tick = frames[frame_count - 1u].tick_index;
    source_tick = presentation_sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
    if (source_tick < first_tick || source_tick > last_tick)
        return ODM_STATUS_INVALID_DATA;
    *out_source_index = source_tick - first_tick;

    if (previous_sample < 0) begin_tick = 0u;
    else begin_tick = (uint64_t)previous_sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES + 1u;
    end_tick = source_tick;
    if (begin_tick < first_tick) begin_tick = first_tick;
    if (end_tick > last_tick) end_tick = last_tick;
    if (begin_tick > end_tick) {
        *out_begin_index = 0u;
        *out_end_index = 0u;
        *out_capture_count = 0u;
    } else {
        uint64_t n = end_tick - begin_tick + 1u;
        if (n > UINT32_MAX) return ODM_STATUS_OVERFLOW;
        *out_begin_index = begin_tick - first_tick;
        *out_end_index = end_tick - first_tick;
        *out_capture_count = (uint32_t)n;
    }
    return ODM_STATUS_OK;
}

static void react_projection_build_unchecked(
    const odm_music_reaction_frame *frames,
    int64_t previous_sample, uint64_t presentation_sample,
    uint64_t source_index, uint64_t begin_index, uint64_t end_index,
    uint32_t capture_count, odm_music_reaction_projection *out) {
    const odm_music_reaction_frame *held = &frames[source_index];
    odm_music_reaction_projection p;
    uint32_t lane, family;
    uint64_t i;
    memset(&p, 0, sizeof(p));
    p.schema_version = ODM_MUSIC_REACTION_PROJECTION_SCHEMA_VERSION;
    p.previous_presentation_sample = previous_sample;
    p.presentation_sample = presentation_sample;
    p.source_tick_index = held->tick_index;
    p.source_center_sample = held->center_sample;
    p.captured_tick_count = capture_count;
    memcpy(p.lane_q31, held->lane_q31, sizeof(p.lane_q31));
    memcpy(p.lane_fast_q31, held->lane_fast_q31, sizeof(p.lane_fast_q31));
    memcpy(p.family_q31, held->family_q31, sizeof(p.family_q31));
    p.broadband_q31 = held->broadband_q31;
    p.event_pulse_q31 = held->event_pulse_q31;
    p.event_threshold_q31 = held->event_threshold_q31;
    p.macro_pulse_q31 = held->macro_pulse_q31;
    p.frame_flags = held->frame_flags;
    if (capture_count != 0u) {
        p.first_captured_tick_index = frames[begin_index].tick_index;
        p.last_captured_tick_index = frames[end_index].tick_index;
        for (i = begin_index; i <= end_index; ++i) {
            const odm_music_reaction_frame *f = &frames[i];
            for (lane = 0u; lane < ODM_MUSIC_REACTION_LANE_COUNT; ++lane)
                p.lane_attack_q31[lane] = react_max(p.lane_attack_q31[lane], f->lane_attack_q31[lane]);
            for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family)
                p.family_attack_q31[family] = react_max(p.family_attack_q31[family], f->family_attack_q31[family]);
            p.broadband_attack_q31 = react_max(p.broadband_attack_q31, f->broadband_attack_q31);
            p.event_strength_q31 = react_max(p.event_strength_q31, f->event_strength_q31);
            p.macro_salience_q31 = react_max(p.macro_salience_q31, f->macro_salience_q31);
            p.event_flags |= f->event_flags;
            if (f->event_strength_q31 != 0u ||
                (f->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) != 0u)
                ++p.captured_event_tick_count;
        }
    }
    *out = p;
}

odm_status odm_music_reaction_project_presentation(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    int64_t previous_presentation_sample,
    uint64_t presentation_sample,
    odm_music_reaction_projection *out_projection) {
    uint64_t source_index, begin_index, end_index;
    uint32_t capture_count;
    odm_status st;
    odm_music_reaction_projection p;
    if (!out_projection) return ODM_STATUS_INVALID_ARGUMENT;
    st = react_projection_sequence_preflight(frames, frame_count);
    if (st != ODM_STATUS_OK) return st;
    st = react_projection_validate_interval(frames, frame_count,
                                            previous_presentation_sample,
                                            presentation_sample,
                                            &source_index, &begin_index,
                                            &end_index, &capture_count);
    if (st != ODM_STATUS_OK) return st;
    react_projection_build_unchecked(frames, previous_presentation_sample,
                                     presentation_sample, source_index,
                                     begin_index, end_index, capture_count, &p);
    *out_projection = p;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_project_timeline(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    const uint64_t *presentation_samples,
    uint64_t presentation_count,
    odm_music_reaction_projection *out_projections,
    uint64_t projection_capacity) {
    uint64_t i;
    odm_status st;
    if (presentation_count == 0u) return ODM_STATUS_OK;
    if (!presentation_samples || !out_projections) return ODM_STATUS_INVALID_ARGUMENT;
    if (projection_capacity < presentation_count) return ODM_STATUS_BUFFER_TOO_SMALL;
    st = react_projection_sequence_preflight(frames, frame_count);
    if (st != ODM_STATUS_OK) return st;

    /* Complete schedule preflight: output remains untouched on any failure. */
    for (i = 0u; i < presentation_count; ++i) {
        int64_t prev;
        uint64_t source_index, begin_index, end_index;
        uint32_t capture_count;
        if (i != 0u && presentation_samples[i] <= presentation_samples[i - 1u])
            return ODM_STATUS_INVALID_DATA;
        if (i == 0u) {
            if (presentation_samples[i] > (uint64_t)INT64_MAX) return ODM_STATUS_INVALID_ARGUMENT;
            prev = presentation_samples[i] == 0u ? -1 : (int64_t)(presentation_samples[i] - 1u);
        } else prev = (int64_t)presentation_samples[i - 1u];
        st = react_projection_validate_interval(frames, frame_count, prev,
                                                presentation_samples[i],
                                                &source_index, &begin_index,
                                                &end_index, &capture_count);
        if (st != ODM_STATUS_OK) return st;
    }

    for (i = 0u; i < presentation_count; ++i) {
        int64_t prev = i == 0u ?
            (presentation_samples[i] == 0u ? -1 : (int64_t)(presentation_samples[i] - 1u)) :
            (int64_t)presentation_samples[i - 1u];
        uint64_t source_index, begin_index, end_index;
        uint32_t capture_count;
        st = react_projection_validate_interval(frames, frame_count, prev,
                                                presentation_samples[i],
                                                &source_index, &begin_index,
                                                &end_index, &capture_count);
        if (st != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
        react_projection_build_unchecked(frames, prev, presentation_samples[i],
                                         source_index, begin_index, end_index,
                                         capture_count, &out_projections[i]);
    }
    return ODM_STATUS_OK;
}

static odm_status react_event_projection_preflight(
    const odm_music_reaction_event *events, uint64_t event_count) {
    uint64_t i;
    uint32_t macro_gate = 0u;
    uint64_t previous_effective = 0u;
    if (event_count == 0u) return ODM_STATUS_OK;
    if (!events) return ODM_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < event_count; ++i) {
        uint64_t effective;
        if (!react_event_record_valid(&events[i])) return ODM_STATUS_INVALID_DATA;
        if (events[i].event_index != i) return ODM_STATUS_INVALID_DATA;
        if (i == 0u) macro_gate = events[i].macro_gate_q31;
        else {
            if (events[i].macro_gate_q31 != macro_gate ||
                events[i].peak_tick_index <= events[i - 1u].peak_tick_index)
                return ODM_STATUS_INVALID_DATA;
        }
        effective = react_event_effective_sample(&events[i], NULL);
        if (i != 0u && effective <= previous_effective) return ODM_STATUS_INVALID_DATA;
        previous_effective = effective;
    }
    return ODM_STATUS_OK;
}

static void react_event_projection_empty(odm_music_reaction_event_projection *projection,
                                         int64_t previous_sample,
                                         uint64_t presentation_sample) {
    memset(projection, 0, sizeof(*projection));
    projection->schema_version = ODM_MUSIC_REACTION_EVENT_PROJECTION_SCHEMA_VERSION;
    projection->previous_presentation_sample = previous_sample;
    projection->presentation_sample = presentation_sample;
    projection->first_event_index = UINT64_MAX;
    projection->last_event_index = UINT64_MAX;
    projection->dominant_event_index = UINT64_MAX;
    projection->dominant_effective_sample = UINT64_MAX;
    projection->pulse_owner_event_index = UINT64_MAX;
    projection->pulse_owner_effective_sample = UINT64_MAX;
    projection->strongest_family = UINT32_MAX;
    projection->second_family = UINT32_MAX;
}

static void react_event_projection_build(
    const odm_music_reaction_event *events, uint64_t event_count,
    int64_t previous_sample, uint64_t presentation_sample,
    odm_music_reaction_event_projection *out) {
    odm_music_reaction_event_projection p;
    uint64_t i;
    const odm_music_reaction_event *dominant = NULL;
    uint64_t dominant_effective = UINT64_MAX;
    uint32_t dominant_trusted = 0u;
    uint32_t pulse_best = 0u;
    uint64_t pulse_owner_effective = UINT64_MAX;
    const odm_music_reaction_event *pulse_owner = NULL;

    react_event_projection_empty(&p, previous_sample, presentation_sample);
    for (i = 0u; i < event_count; ++i) {
        uint32_t trusted = 0u;
        uint64_t effective = react_event_effective_sample(&events[i], &trusted);
        uint32_t pulse;
        int captured;
        if (effective > presentation_sample) break;
        captured = previous_sample < 0 ? effective <= presentation_sample :
                   effective > (uint64_t)previous_sample && effective <= presentation_sample;
        if (captured) {
            if (p.captured_event_count == 0u) p.first_event_index = events[i].event_index;
            p.last_event_index = events[i].event_index;
            if (p.captured_event_count != UINT32_MAX) ++p.captured_event_count;
            p.event_flags |= events[i].event_flags;
            if (!dominant || events[i].contextual_salience_q31 > dominant->contextual_salience_q31 ||
                (events[i].contextual_salience_q31 == dominant->contextual_salience_q31 &&
                 events[i].event_index < dominant->event_index)) {
                dominant = &events[i];
                dominant_effective = effective;
                dominant_trusted = trusted;
            }
        }
        pulse = react_event_pulse_at_sample(&events[i], effective, presentation_sample);
        if (pulse > pulse_best ||
            (pulse != 0u && pulse == pulse_best && pulse_owner &&
             events[i].event_index < pulse_owner->event_index)) {
            pulse_best = pulse;
            pulse_owner = &events[i];
            pulse_owner_effective = effective;
        }
    }
    if (dominant) {
        p.dominant_event_index = dominant->event_index;
        p.dominant_effective_sample = dominant_effective;
        p.event_salience_q31 = dominant->contextual_salience_q31;
        p.timing_confidence_q31 = dominant->timing_confidence_q31;
        p.strongest_family = dominant->strongest_family;
        p.second_family = dominant->second_family;
        p.broadband_attack_q31 = dominant->broadband_attack_q31;
        p.score_q31 = dominant->score_q31;
        p.strength_basis_q31 = dominant->strength_basis_q31;
        p.dominant_pulse_q31 = react_event_pulse_at_sample(
            dominant, dominant_effective, presentation_sample);
        if (dominant_trusted != 0u) p.event_flags |= ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED;
    }
    if (pulse_owner && pulse_best != 0u) {
        p.event_pulse_q31 = pulse_best;
        p.pulse_owner_event_index = pulse_owner->event_index;
        p.pulse_owner_effective_sample = pulse_owner_effective;
        p.pulse_owner_salience_q31 = pulse_owner->contextual_salience_q31;
    }
    *out = p;
}

odm_status odm_music_reaction_project_events_offline(
    const odm_music_reaction_event *events,
    uint64_t event_count,
    const uint64_t *presentation_samples,
    uint64_t presentation_count,
    odm_music_reaction_event_projection *out_projections,
    uint64_t projection_capacity) {
    uint64_t i;
    odm_status st;
    if (presentation_count == 0u) return ODM_STATUS_OK;
    if (!presentation_samples || !out_projections) return ODM_STATUS_INVALID_ARGUMENT;
    if (projection_capacity < presentation_count) return ODM_STATUS_BUFFER_TOO_SMALL;
    st = react_event_projection_preflight(events, event_count);
    if (st != ODM_STATUS_OK) return st;

    /* Full schedule preflight preserves transactional publication. */
    for (i = 0u; i < presentation_count; ++i) {
        if (presentation_samples[i] > (uint64_t)INT64_MAX) return ODM_STATUS_INVALID_ARGUMENT;
        if (i != 0u && presentation_samples[i] <= presentation_samples[i - 1u])
            return ODM_STATUS_INVALID_DATA;
    }
    for (i = 0u; i < presentation_count; ++i) {
        int64_t previous = i == 0u ?
            (presentation_samples[i] == 0u ? -1 : (int64_t)(presentation_samples[i] - 1u)) :
            (int64_t)presentation_samples[i - 1u];
        react_event_projection_build(events, event_count, previous,
                                     presentation_samples[i], &out_projections[i]);
    }
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_trace_radial(
    const odm_music_reaction_frame *frame,
    uint32_t radial_index,
    odm_music_reaction_trace *out_trace) {
    odm_music_reaction_trace t;
    uint32_t a, b;
    if (!frame || !out_trace || frame->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        radial_index >= 48u) return ODM_STATUS_INVALID_ARGUMENT;
    a = radial_index * 2u;
    b = a + 1u;
    memset(&t, 0, sizeof(t));
    t.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    t.radial_index = radial_index;
    t.lane_a = a;
    t.lane_b = b;
    t.direct_level_q31 = (uint32_t)(((uint64_t)frame->lane_q31[a] +
                                     (uint64_t)frame->lane_q31[b] + 1u) / 2u);
    t.direct_fast_level_q31 = (uint32_t)(((uint64_t)frame->lane_fast_q31[a] +
                                          (uint64_t)frame->lane_fast_q31[b] + 1u) / 2u);
    t.direct_attack_q31 = react_max(frame->lane_attack_q31[a], frame->lane_attack_q31[b]);
    t.event_strength_q31 = frame->event_strength_q31;
    t.event_flags = frame->event_flags;
    t.tick_index = frame->tick_index;
    t.center_sample = frame->center_sample;
    t.macro_salience_q31 = frame->macro_salience_q31;
    t.macro_pulse_q31 = frame->macro_pulse_q31;
    t.frame_flags = frame->frame_flags;
    *out_trace = t;
    return ODM_STATUS_OK;
}

odm_status odm_music_reaction_trace_hires_radial(
    const odm_music_reaction_frame *frame,
    uint32_t radial_index,
    odm_music_reaction_trace *out_trace) {
    odm_music_reaction_trace t;
    if (!frame || !out_trace || frame->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        radial_index >= ODM_MUSIC_REACTION_LANE_COUNT) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&t, 0, sizeof(t));
    t.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    t.radial_index = radial_index;
    t.lane_a = radial_index;
    t.lane_b = radial_index;
    t.direct_level_q31 = frame->lane_q31[radial_index];
    t.direct_fast_level_q31 = frame->lane_fast_q31[radial_index];
    t.direct_attack_q31 = frame->lane_attack_q31[radial_index];
    t.event_strength_q31 = frame->event_strength_q31;
    t.event_flags = frame->event_flags;
    t.tick_index = frame->tick_index;
    t.center_sample = frame->center_sample;
    t.macro_salience_q31 = frame->macro_salience_q31;
    t.macro_pulse_q31 = frame->macro_pulse_q31;
    t.frame_flags = frame->frame_flags;
    *out_trace = t;
    return ODM_STATUS_OK;
}


odm_status odm_music_reaction_trace_event(
    const odm_music_reaction_frame *frame,
    odm_music_reaction_event_trace *out_trace) {
    odm_music_reaction_event_trace t;
    uint32_t family;
    uint32_t primary = 0u, secondary = 0u;
    uint32_t primary_value = 0u, secondary_value = 0u;
    if (!frame || !out_trace) return ODM_STATUS_INVALID_ARGUMENT;
    if (!react_frame_valid(frame)) return ODM_STATUS_INVALID_DATA;
    for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family) {
        uint32_t v = frame->family_attack_q31[family];
        if (v > primary_value) {
            secondary = primary; secondary_value = primary_value;
            primary = family; primary_value = v;
        } else if (v > secondary_value) {
            secondary = family; secondary_value = v;
        }
    }
    memset(&t, 0, sizeof(t));
    t.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    t.event_flags = frame->event_flags;
    t.tick_index = frame->tick_index;
    t.center_sample = frame->center_sample;
    t.event_score_q31 = react_max(frame->broadband_attack_q31, secondary_value);
    t.event_strength_q31 = frame->event_strength_q31;
    t.event_threshold_q31 = frame->event_threshold_q31;
    t.broadband_attack_q31 = frame->broadband_attack_q31;
    t.primary_family = primary;
    t.primary_family_attack_q31 = primary_value;
    t.secondary_family = secondary;
    t.secondary_family_attack_q31 = secondary_value;
    t.macro_salience_q31 = frame->macro_salience_q31;
    t.macro_pulse_q31 = frame->macro_pulse_q31;
    t.frame_flags = frame->frame_flags;
    *out_trace = t;
    return ODM_STATUS_OK;
}

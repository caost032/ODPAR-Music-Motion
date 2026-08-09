#include "test_harness.h"

#include "odm_music_reaction.h"
#include "odm_visual.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static void reaction_tick_init(odm_music_reaction_tick *t, uint64_t index) {
    memset(t, 0, sizeof(*t));
    t->schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    t->tick_index = index;
    t->center_sample = index * UINT64_C(480);
}

static void test_lane_plan_and_extraction(odm_test_context *context) {
    odm_music_analysis_tick base;
    odm_music_analysis_scratch scratch;
    odm_music_reaction_tick out;
    uint32_t i;
    uint32_t previous_end = 0u;
    memset(&base, 0, sizeof(base));
    memset(&scratch, 0, sizeof(scratch));
    base.tick_index = 7u;
    base.center_sample = UINT64_C(3360);
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
        uint32_t a = 0u, b = 0u;
        ODM_TEST_CHECK(context, odm_music_reaction_lane_bounds(i, &a, &b) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, a < b);
        if (i == 0u) ODM_TEST_CHECK(context, a == 1u);
        else ODM_TEST_CHECK(context, a == previous_end);
        previous_end = b;
    }
    ODM_TEST_CHECK(context, previous_end == 768u);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_lane_bounds(ODM_MUSIC_REACTION_LANE_COUNT,
                                                  &i, &previous_end) == ODM_STATUS_INVALID_ARGUMENT);

    scratch.magnitude_q31[10] = (uint32_t)INT32_MAX;
    ODM_TEST_CHECK(context,
                   odm_music_reaction_extract_tick(&base, &scratch, &out) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, out.tick_index == base.tick_index &&
                            out.center_sample == base.center_sample);
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
        if (i == 9u) ODM_TEST_CHECK(context, out.lane_magnitude_q31[i] == (uint32_t)INT32_MAX);
        else ODM_TEST_CHECK(context, out.lane_magnitude_q31[i] == 0u);
    }
    ODM_TEST_CHECK(context,
                   out.family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_LOW_MID] > 0u);
    ODM_TEST_CHECK(context, out.family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_SUB] == 0u);
}

static void test_robust_profile_and_events(odm_test_context *context) {
    odm_music_reaction_tick ticks[220];
    odm_music_reaction_profile profile;
    odm_music_reaction_state state_a, state_b;
    odm_music_reaction_frame frame_a, frame_b;
    odm_sha256_digest h1, h2;
    odm_sha256_digest policy_hash;
    uint8_t bytes[ODM_MUSIC_REACTION_PROFILE_BYTES];
    uint8_t policy_bytes[ODM_MUSIC_REACTION_POLICY_BYTES];
    uint64_t required = 0u;
    uint32_t i, lane, family;
    uint32_t event_count = 0u;

    for (i = 0u; i < 220u; ++i) {
        uint32_t base = UINT32_C(1000000);
        reaction_tick_init(&ticks[i], i);
        if ((i % 10u) == 1u) base = UINT32_C(1100000);
        if (i == 219u) base = (uint32_t)INT32_MAX;
        for (lane = 0u; lane < ODM_MUSIC_REACTION_LANE_COUNT; ++lane)
            ticks[i].lane_magnitude_q31[lane] =
                base == (uint32_t)INT32_MAX ? (uint32_t)INT32_MAX : base + lane;
        for (family = 0u; family < ODM_MUSIC_REACTION_FAMILY_COUNT; ++family)
            ticks[i].family_magnitude_q31[family] =
                base == (uint32_t)INT32_MAX ? (uint32_t)INT32_MAX : base + family;
        ticks[i].broadband_magnitude_q31 = base;
    }
    ODM_TEST_CHECK(context,
                   odm_music_reaction_profile_build(ticks, 220u, &profile) == ODM_STATUS_OK);
    /* One extreme final outlier must not become the magnitude reference. */
    ODM_TEST_CHECK(context, profile.lane_magnitude_ref[0] == UINT32_C(1100000));
    ODM_TEST_CHECK(context, profile.broadband_magnitude_ref == UINT32_C(1100000));
    /* Positive-attack quantile ignores zero deltas and rejects the single outlier. */
    ODM_TEST_CHECK(context, profile.lane_attack_ref[0] == UINT32_C(100000));
    ODM_TEST_CHECK(context, profile.broadband_attack_ref == UINT32_C(100000));

    ODM_TEST_CHECK(context,
                   odm_music_reaction_profile_bytes(&profile, bytes, sizeof(bytes),
                                                    &required) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == sizeof(bytes));
    ODM_TEST_CHECK(context, memcmp(bytes, "ODMRXN1\0", 8u) == 0);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_policy_bytes(policy_bytes, sizeof(policy_bytes),
                                                   &required) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == sizeof(policy_bytes));
    ODM_TEST_CHECK(context, memcmp(policy_bytes, "ODMRXP11", 8u) == 0);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_policy_current_sha256(&policy_hash) == ODM_STATUS_OK);
    /* Profile identity embeds the exact current policy SHA immediately after
     * its fixed magic/version/schema/topology header. */
    ODM_TEST_CHECK(context, memcmp(bytes + 24u, policy_hash.bytes,
                                   sizeof(policy_hash.bytes)) == 0);
    ODM_TEST_CHECK(context, odm_music_reaction_profile_sha256(&profile, &h1) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_reaction_profile_sha256(&profile, &h2) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_sha256_equal(&h1, &h2) != 0);

    ODM_TEST_CHECK(context, odm_music_reaction_state_init(&state_a) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_reaction_state_init(&state_b) == ODM_STATUS_OK);
    for (i = 0u; i < 40u; ++i) {
        ODM_TEST_CHECK(context,
                       odm_music_reaction_resolve_tick(&state_a, &profile, &ticks[i],
                                                       &frame_a) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_music_reaction_resolve_tick(&state_b, &profile, &ticks[i],
                                                       &frame_b) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&frame_a, &frame_b, sizeof(frame_a)) == 0);
        if ((frame_a.event_flags & ODM_MUSIC_REACTION_EVENT_ANY) != 0u) {
            ++event_count;
            ODM_TEST_CHECK(context, frame_a.event_strength_q31 > 0u);
            ODM_TEST_CHECK(context, frame_a.event_pulse_q31 >= frame_a.event_strength_q31);
        }
        ODM_TEST_CHECK(context, frame_a.lane_q31[0] <= (uint32_t)INT32_MAX);
        ODM_TEST_CHECK(context, frame_a.lane_fast_q31[0] <= frame_a.lane_q31[0]);
        ODM_TEST_CHECK(context, frame_a.lane_attack_q31[0] <= (uint32_t)INT32_MAX);
    }
    ODM_TEST_CHECK(context, event_count >= 2u);
}


static void reaction_frame_init(odm_music_reaction_frame *f, uint64_t index) {
    memset(f, 0, sizeof(*f));
    f->schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    f->tick_index = index;
    f->center_sample = index * UINT64_C(480);
    f->event_threshold_q31 = UINT32_C(644245094); /* 0.30 */
}

static void test_frequency_aware_release(odm_test_context *context) {
    odm_music_reaction_profile profile;
    odm_music_reaction_state state;
    odm_music_reaction_tick t0, t1;
    odm_music_reaction_frame f0, f1;
    uint32_t i;
    memset(&profile, 0, sizeof(profile));
    profile.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    profile.tick_count = 2u;
    profile.first_tick_index = 0u;
    profile.last_tick_index = 1u;
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i)
        profile.lane_magnitude_ref[i] = UINT32_C(1000000000);
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i)
        profile.family_magnitude_ref[i] = UINT32_C(1000000000);
    profile.broadband_magnitude_ref = UINT32_C(1000000000);

    reaction_tick_init(&t0, 0u);
    reaction_tick_init(&t1, 1u);
    t0.lane_magnitude_q31[0] = UINT32_C(1000000000);  /* SUB */
    t0.lane_magnitude_q31[95] = UINT32_C(1000000000); /* AIR */
    t0.family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_SUB] = UINT32_C(1000000000);
    t0.family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_AIR] = UINT32_C(1000000000);
    t0.broadband_magnitude_q31 = UINT32_C(1000000000);

    ODM_TEST_CHECK(context, odm_music_reaction_state_init(&state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_reaction_resolve_tick(&state, &profile, &t0, &f0) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_reaction_resolve_tick(&state, &profile, &t1, &f1) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, f1.lane_q31[0] > f1.lane_q31[95]);
    ODM_TEST_CHECK(context,
                   f1.family_q31[ODM_MUSIC_REACTION_FAMILY_SUB] >
                   f1.family_q31[ODM_MUSIC_REACTION_FAMILY_AIR]);
    ODM_TEST_CHECK(context, f1.broadband_q31 > 0u && f1.broadband_q31 < f0.broadband_q31);
}

static void test_offline_peak_refinement(odm_test_context *context) {
    odm_music_reaction_frame in[40], out[40], inplace[40], sent[40];
    uint32_t i;
    const uint32_t scores[5] = {
        UINT32_C(858993459),  /* .40 */
        UINT32_C(1288490188), /* .60 */
        UINT32_C(1932735282), /* .90 */
        UINT32_C(1503238553), /* .70 */
        UINT32_C(858993459)   /* .40 */
    };
    for (i = 0u; i < 40u; ++i) reaction_frame_init(&in[i], i);
    for (i = 0u; i < 5u; ++i) {
        in[5u + i].broadband_attack_q31 = scores[i];
        in[5u + i].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS] = scores[i];
        in[5u + i].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = scores[i];
        /* Deliberately emulate streaming publication on the first crossing;
         * offline refinement must move authority to the true local maximum. */
        if (i == 0u) {
            in[5u].event_flags = ODM_MUSIC_REACTION_EVENT_ANY;
            in[5u].event_strength_q31 = UINT32_C(100000000);
            in[5u].event_pulse_q31 = UINT32_C(100000000);
        }
    }
    /* A second independent peak after the 140 ms refractory horizon. */
    in[24].broadband_attack_q31 = UINT32_C(1073741824);
    in[25].broadband_attack_q31 = UINT32_C(1932735282);
    in[25].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS] = UINT32_C(1932735282);
    in[25].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = UINT32_C(1932735282);
    in[26].broadband_attack_q31 = UINT32_C(1073741824);

    ODM_TEST_CHECK(context,
                   odm_music_reaction_refine_events_offline(in, 40u, out, 40u) == ODM_STATUS_OK);
    for (i = 0u; i < 40u; ++i) {
        ODM_TEST_CHECK(context, out[i].frame_flags == ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE);
        if (i == 7u || i == 25u) {
            ODM_TEST_CHECK(context, (out[i].event_flags & ODM_MUSIC_REACTION_EVENT_ANY) != 0u);
            ODM_TEST_CHECK(context, (out[i].event_flags & ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK) != 0u);
            ODM_TEST_CHECK(context, out[i].event_strength_q31 > 0u);
            ODM_TEST_CHECK(context, out[i].macro_salience_q31 > 0u);
            ODM_TEST_CHECK(context, out[i].macro_pulse_q31 >= out[i].macro_salience_q31);
        } else {
            ODM_TEST_CHECK(context, out[i].event_strength_q31 == 0u);
            ODM_TEST_CHECK(context, out[i].macro_salience_q31 == 0u);
            ODM_TEST_CHECK(context, out[i].event_flags == 0u);
        }
        /* Refinement cannot rewrite local spectral evidence. */
        ODM_TEST_CHECK(context,
                       memcmp(out[i].lane_q31, in[i].lane_q31, sizeof(in[i].lane_q31)) == 0);
        ODM_TEST_CHECK(context,
                       memcmp(out[i].family_attack_q31, in[i].family_attack_q31,
                              sizeof(in[i].family_attack_q31)) == 0);
        ODM_TEST_CHECK(context, out[i].event_threshold_q31 == in[i].event_threshold_q31);
    }
    ODM_TEST_CHECK(context, out[6].event_pulse_q31 == 0u);
    ODM_TEST_CHECK(context, out[7].event_pulse_q31 == out[7].event_strength_q31);
    ODM_TEST_CHECK(context, out[8].event_pulse_q31 > 0u &&
                            out[8].event_pulse_q31 < out[7].event_pulse_q31);
    ODM_TEST_CHECK(context, out[8].macro_pulse_q31 > 0u &&
                            out[8].macro_pulse_q31 < out[7].macro_pulse_q31);

    memcpy(inplace, in, sizeof(in));
    ODM_TEST_CHECK(context,
                   odm_music_reaction_refine_events_offline(inplace, 40u, inplace, 40u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(inplace, out, sizeof(out)) == 0);

    memset(sent, 0x5a, sizeof(sent));
    in[17].center_sample += 1u;
    ODM_TEST_CHECK(context,
                   odm_music_reaction_refine_events_offline(in, 40u, sent, 40u) == ODM_STATUS_INVALID_DATA);
    for (i = 0u; i < sizeof(sent); ++i)
        ODM_TEST_CHECK(context, ((const unsigned char *)sent)[i] == 0x5au);
}


static void test_event_provenance_stream(odm_test_context *context) {
    odm_music_reaction_frame frames[52];
    odm_music_reaction_event events[4];
    odm_music_reaction_event sent[4];
    uint64_t required = 0u;
    uint32_t gate = 0u;
    uint32_t i;
    const uint32_t q30 = UINT32_C(644245094);
    const uint32_t q50 = UINT32_C(1073741824);
    const uint32_t q60 = UINT32_C(1288490188);
    const uint32_t q70 = UINT32_C(1503238553);
    const uint32_t q80 = UINT32_C(1717986918);
    const uint32_t q90 = UINT32_C(1932735282);
    for (i = 0u; i < 52u; ++i) reaction_frame_init(&frames[i], i);

#define SET_SCORE(idx, value) do { \
        frames[(idx)].broadband_attack_q31 = (value); \
        frames[(idx)].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS] = (value); \
        frames[(idx)].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = (value); \
        frames[(idx)].event_threshold_q31 = q30; \
    } while (0)
    SET_SCORE(6u, q50); SET_SCORE(7u, q90); SET_SCORE(8u, q70);
    SET_SCORE(24u, q50); SET_SCORE(25u, q60); SET_SCORE(26u, q50);
    SET_SCORE(42u, q60); SET_SCORE(43u, q80); SET_SCORE(44u, q50);
#undef SET_SCORE

    ODM_TEST_CHECK(context, sizeof(odm_music_reaction_event) ==
                            ODM_MUSIC_REACTION_EVENT_RECORD_BYTES);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_build_events_offline(frames, 52u, NULL, 0u,
                                                           &required, &gate) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == 2u);
    ODM_TEST_CHECK(context, gate == q80);

    memset(events, 0, sizeof(events));
    ODM_TEST_CHECK(context,
                   odm_music_reaction_build_events_offline(frames, 52u, events, 4u,
                                                           &required, &gate) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == 2u);
    ODM_TEST_CHECK(context, events[0].schema_version == ODM_MUSIC_REACTION_EVENT_SCHEMA_VERSION);
    ODM_TEST_CHECK(context, events[0].event_index == 0u && events[1].event_index == 1u);
    ODM_TEST_CHECK(context, events[0].peak_tick_index == 7u && events[1].peak_tick_index == 43u);
    ODM_TEST_CHECK(context, events[0].peak_center_sample == UINT64_C(3360));
    ODM_TEST_CHECK(context, events[0].refined_offset_samples == 80);
    ODM_TEST_CHECK(context, events[0].refined_sample == UINT64_C(3440));
    ODM_TEST_CHECK(context, events[0].timing_confidence_q31 > 0u);
    ODM_TEST_CHECK(context, events[0].score_q31 == q90);
    ODM_TEST_CHECK(context, events[0].macro_gate_q31 == q80);
    ODM_TEST_CHECK(context, events[0].contextual_salience_q31 > events[1].contextual_salience_q31);
    ODM_TEST_CHECK(context, events[0].strongest_family == ODM_MUSIC_REACTION_FAMILY_BASS);
    ODM_TEST_CHECK(context, events[0].second_family == ODM_MUSIC_REACTION_FAMILY_BODY);
    ODM_TEST_CHECK(context, events[0].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS] == q90);
    ODM_TEST_CHECK(context, events[0].broadband_attack_q31 == q90);
    ODM_TEST_CHECK(context, events[0].frame_flags == ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE);
    ODM_TEST_CHECK(context, events[0].reserved[0] == 0u && events[0].reserved[4] == 0u);

    memset(sent, 0x5a, sizeof(sent));
    ODM_TEST_CHECK(context,
                   odm_music_reaction_build_events_offline(frames, 52u, sent, 1u,
                                                           &required, &gate) ==
                   ODM_STATUS_BUFFER_TOO_SMALL);
    for (i = 0u; i < sizeof(sent); ++i)
        ODM_TEST_CHECK(context, ((const unsigned char *)sent)[i] == 0x5au);
}

static void test_causal_presentation_projection(odm_test_context *context) {
    odm_music_reaction_frame frames[300];
    odm_music_reaction_projection projected[400];
    odm_music_analysis_tick base;
    odm_composition_frame_state comp;
    uint64_t samples[400];
    const uint32_t steps[] = {2000u, 1920u, 1600u, 960u, 800u, 480u, 400u};
    uint32_t si, i, lane;
    for (i = 0u; i < 300u; ++i) {
        reaction_frame_init(&frames[i], i);
        frames[i].lane_q31[40] = UINT32_C(1000000) + i;
        frames[i].family_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = UINT32_C(2000000) + i;
        frames[i].broadband_q31 = UINT32_C(3000000) + i;
        /* Deliberately short instantaneous evidence: it exists for one 10 ms
         * source tick only and therefore used to alias away at video FPS. */
        frames[i].lane_attack_q31[40] = (i % 7u) == 3u ? UINT32_C(1500000000) - i : 0u;
        frames[i].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] =
            (i % 11u) == 5u ? UINT32_C(1200000000) - i : 0u;
        frames[i].broadband_attack_q31 = (i % 13u) == 6u ? UINT32_C(1300000000) - i : 0u;
        if (i == 17u || i == 66u || i == 121u || i == 211u) {
            frames[i].event_strength_q31 = UINT32_C(900000000) + i;
            frames[i].event_flags = ODM_MUSIC_REACTION_EVENT_ANY |
                                    ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
        }
    }
    for (si = 0u; si < (uint32_t)(sizeof(steps) / sizeof(steps[0])); ++si) {
        uint64_t count = 0u, sample = 0u, captured_total = 0u;
        const uint64_t last = frames[299].center_sample;
        while (sample <= last && count < 399u) {
            samples[count++] = sample;
            if (sample > UINT64_MAX - steps[si]) break;
            sample += steps[si];
        }
        if (samples[count - 1u] != last) samples[count++] = last;
        ODM_TEST_CHECK(context,
                       odm_music_reaction_project_timeline(frames, 300u, samples, count,
                                                           projected, 400u) == ODM_STATUS_OK);
        for (i = 0u; i < (uint32_t)count; ++i) {
            uint64_t expected_source = samples[i] / UINT64_C(480);
            uint64_t j, begin_tick, end_tick = expected_source;
            uint32_t expected_attack = 0u, expected_events = 0u;
            int64_t prev = i == 0u ? -1 : (int64_t)samples[i - 1u];
            begin_tick = prev < 0 ? 0u : (uint64_t)prev / UINT64_C(480) + 1u;
            ODM_TEST_CHECK(context, projected[i].source_tick_index == expected_source);
            ODM_TEST_CHECK(context, projected[i].source_center_sample == expected_source * UINT64_C(480));
            ODM_TEST_CHECK(context, projected[i].lane_q31[40] == frames[expected_source].lane_q31[40]);
            for (j = begin_tick; j <= end_tick; ++j) {
                if (frames[j].lane_attack_q31[40] > expected_attack)
                    expected_attack = frames[j].lane_attack_q31[40];
                if (frames[j].event_strength_q31 != 0u) ++expected_events;
            }
            ODM_TEST_CHECK(context, projected[i].lane_attack_q31[40] == expected_attack);
            ODM_TEST_CHECK(context, projected[i].captured_event_tick_count == expected_events);
            captured_total += projected[i].captured_tick_count;
            if (projected[i].captured_tick_count != 0u) {
                ODM_TEST_CHECK(context,
                    projected[i].last_captured_tick_index * UINT64_C(480) <= samples[i]);
                if (prev >= 0) ODM_TEST_CHECK(context,
                    projected[i].first_captured_tick_index * UINT64_C(480) > (uint64_t)prev);
            }
        }
        /* Every canonical 100 Hz source tick is consumed exactly once across
         * the presentation schedule, independent of output FPS. */
        ODM_TEST_CHECK(context, captured_total == 300u);
    }

    /* A seek starts a fresh causal view and does not replay historical attacks. */
    samples[0] = UINT64_C(10001);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_timeline(frames, 300u, samples, 1u,
                                                       projected, 1u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, projected[0].captured_tick_count == 0u);
    ODM_TEST_CHECK(context, projected[0].lane_attack_q31[40] == 0u);

    /* Projected composition consumes held state plus interval peaks. */
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_presentation(frames, 300u, 0,
                                                           UINT64_C(2000),
                                                           &projected[0]) == ODM_STATUS_OK);
    memset(&base, 0, sizeof(base));
    base.tick_index = projected[0].source_tick_index;
    base.center_sample = projected[0].source_center_sample;
    ODM_TEST_CHECK(context,
                   odm_composition_resolve_music_reaction_projection(&base, &projected[0],
                                                                     &comp) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, comp.tick_index == 4u && comp.center_sample == UINT64_C(1920));
    ODM_TEST_CHECK(context, comp.radial_q31[40] > 0u);

    /* Contextual salience has its own projection semantics: macro pulse is a
     * held field while instantaneous macro salience is interval-max captured. */
    {
        odm_music_reaction_frame ctx[8];
        odm_music_reaction_projection cp;
        for (i = 0u; i < 8u; ++i) {
            reaction_frame_init(&ctx[i], i);
            ctx[i].frame_flags = ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
        }
        ctx[3].event_strength_q31 = UINT32_C(600000000);
        ctx[3].event_flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
        ctx[3].macro_salience_q31 = UINT32_C(1200000000);
        ctx[3].macro_pulse_q31 = UINT32_C(1200000000);
        ctx[4].macro_pulse_q31 = UINT32_C(960000000);
        ODM_TEST_CHECK(context,
                       odm_music_reaction_project_presentation(ctx, 8u, 0, UINT64_C(2000),
                                                               &cp) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, cp.source_tick_index == 4u);
        ODM_TEST_CHECK(context, cp.frame_flags == ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE);
        ODM_TEST_CHECK(context, cp.macro_salience_q31 == UINT32_C(1200000000));
        ODM_TEST_CHECK(context, cp.macro_pulse_q31 == UINT32_C(960000000));
        ctx[5].frame_flags = 0u;
        ODM_TEST_CHECK(context,
                       odm_music_reaction_project_presentation(ctx, 8u, 0, UINT64_C(2000),
                                                               &cp) == ODM_STATUS_INVALID_DATA);
    }

    /* Transactional batch failure and bounded scan contract. */
    memset(projected, 0x5a, sizeof(projected));
    samples[0] = 0u; samples[1] = 2000u; samples[2] = 1900u;
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_timeline(frames, 300u, samples, 3u,
                                                       projected, 400u) == ODM_STATUS_INVALID_DATA);
    for (lane = 0u; lane < sizeof(projected); ++lane)
        ODM_TEST_CHECK(context, ((const unsigned char *)projected)[lane] == 0x5au);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_presentation(frames, 300u, 0,
                       (uint64_t)ODM_MUSIC_REACTION_PROJECTION_MAX_GAP_SAMPLES + 1u,
                       &projected[0]) == ODM_STATUS_BUDGET_EXCEEDED);
}


static void test_sample_accurate_event_presentation(odm_test_context *context) {
    odm_music_reaction_frame frames[52];
    odm_music_reaction_event events[4];
    odm_music_reaction_event weak[1];
    odm_music_reaction_event broken[4];
    odm_music_reaction_event_projection projected[8];
    odm_music_reaction_event_projection sent[8];
    uint64_t samples[8];
    uint64_t required = 0u;
    uint32_t gate = 0u;
    uint32_t i;
    const uint32_t q30 = UINT32_C(644245094);
    const uint32_t q50 = UINT32_C(1073741824);
    const uint32_t q60 = UINT32_C(1288490188);
    const uint32_t q70 = UINT32_C(1503238553);
    const uint32_t q80 = UINT32_C(1717986918);
    const uint32_t q90 = UINT32_C(1932735282);
    for (i = 0u; i < 52u; ++i) reaction_frame_init(&frames[i], i);
#define SET_PRESENT_SCORE(idx, value) do { \
        frames[(idx)].broadband_attack_q31 = (value); \
        frames[(idx)].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS] = (value); \
        frames[(idx)].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = (value); \
        frames[(idx)].event_threshold_q31 = q30; \
    } while (0)
    SET_PRESENT_SCORE(6u, q50); SET_PRESENT_SCORE(7u, q90); SET_PRESENT_SCORE(8u, q70);
    SET_PRESENT_SCORE(24u, q50); SET_PRESENT_SCORE(25u, q60); SET_PRESENT_SCORE(26u, q50);
    SET_PRESENT_SCORE(42u, q60); SET_PRESENT_SCORE(43u, q80); SET_PRESENT_SCORE(44u, q50);
#undef SET_PRESENT_SCORE

    ODM_TEST_CHECK(context, sizeof(odm_music_reaction_event_projection) ==
                            ODM_MUSIC_REACTION_EVENT_PROJECTION_RECORD_BYTES);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_build_events_offline(frames, 52u, events, 4u,
                                                           &required, &gate) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == 2u);
    ODM_TEST_CHECK(context, events[0].refined_sample == UINT64_C(3440));
    ODM_TEST_CHECK(context, events[0].timing_confidence_q31 > UINT32_C(268435456));
    ODM_TEST_CHECK(context, events[1].timing_confidence_q31 > UINT32_C(268435456));

    samples[0] = UINT64_C(3400);
    samples[1] = events[0].refined_sample;
    samples[2] = UINT64_C(3500);
    samples[3] = events[1].refined_sample;
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_events_offline(events, required, samples, 4u,
                                                             projected, 8u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, projected[0].captured_event_count == 0u &&
                            projected[0].event_pulse_q31 == 0u);
    ODM_TEST_CHECK(context, projected[1].captured_event_count == 1u);
    ODM_TEST_CHECK(context, projected[1].dominant_event_index == 0u);
    ODM_TEST_CHECK(context, projected[1].dominant_effective_sample == events[0].refined_sample);
    ODM_TEST_CHECK(context,
                   (projected[1].event_flags & ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED) != 0u);
    ODM_TEST_CHECK(context, projected[1].event_salience_q31 == events[0].contextual_salience_q31);
    ODM_TEST_CHECK(context, projected[1].dominant_pulse_q31 == events[0].contextual_salience_q31);
    ODM_TEST_CHECK(context, projected[1].event_pulse_q31 == events[0].contextual_salience_q31);
    ODM_TEST_CHECK(context, projected[2].captured_event_count == 0u);
    ODM_TEST_CHECK(context, projected[2].event_pulse_q31 < projected[1].event_pulse_q31 &&
                            projected[2].event_pulse_q31 > 0u);
    ODM_TEST_CHECK(context, projected[2].pulse_owner_event_index == 0u);
    ODM_TEST_CHECK(context, projected[3].captured_event_count == 1u &&
                            projected[3].dominant_event_index == 1u);

    /* Weak curvature evidence must not manufacture sub-tick precision. */
    weak[0] = events[0];
    weak[0].timing_confidence_q31 = UINT32_C(1);
    samples[0] = weak[0].peak_center_sample;
    samples[1] = weak[0].refined_sample;
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_events_offline(weak, 1u, samples, 2u,
                                                             projected, 8u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, projected[0].captured_event_count == 1u);
    ODM_TEST_CHECK(context, projected[0].dominant_effective_sample == weak[0].peak_center_sample);
    ODM_TEST_CHECK(context,
                   (projected[0].event_flags & ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED) == 0u);
    ODM_TEST_CHECK(context, projected[1].captured_event_count == 0u);

    /* A seek does not replay the prior impact, but reconstructs its still-live
     * sample-domain pulse from evidence that occurred before the seek. */
    samples[0] = UINT64_C(3500);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_events_offline(events, required, samples, 1u,
                                                             projected, 1u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, projected[0].captured_event_count == 0u);
    ODM_TEST_CHECK(context, projected[0].event_pulse_q31 > 0u &&
                            projected[0].pulse_owner_event_index == 0u);

    /* Corruption and invalid schedules fail before touching caller output. */
    memcpy(broken, events, sizeof(events));
    broken[0].refined_sample += 1u;
    memset(sent, 0x5a, sizeof(sent));
    samples[0] = UINT64_C(3400); samples[1] = UINT64_C(3440);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_events_offline(broken, required, samples, 2u,
                                                             sent, 8u) == ODM_STATUS_INVALID_DATA);
    for (i = 0u; i < sizeof(sent); ++i)
        ODM_TEST_CHECK(context, ((const unsigned char *)sent)[i] == 0x5au);
    memset(sent, 0x5a, sizeof(sent));
    samples[0] = UINT64_C(3440); samples[1] = UINT64_C(3400);
    ODM_TEST_CHECK(context,
                   odm_music_reaction_project_events_offline(events, required, samples, 2u,
                                                             sent, 8u) == ODM_STATUS_INVALID_DATA);
    for (i = 0u; i < sizeof(sent); ++i)
        ODM_TEST_CHECK(context, ((const unsigned char *)sent)[i] == 0x5au);
}


static void test_radial_causality(odm_test_context *context) {
    odm_music_analysis_tick base_a, base_b;
    odm_music_reaction_frame reaction_a, reaction_b;
    odm_composition_frame_state comp_a, comp_b;
    odm_music_reaction_trace trace;
    uint32_t i;
    memset(&base_a, 0, sizeof(base_a));
    memset(&reaction_a, 0, sizeof(reaction_a));
    base_a.tick_index = 100u;
    base_a.center_sample = UINT64_C(48000);
    reaction_a.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    reaction_a.tick_index = base_a.tick_index;
    reaction_a.center_sample = base_a.center_sample;
    reaction_a.lane_q31[40] = UINT32_C(1000000000);
    reaction_a.lane_fast_q31[40] = UINT32_C(600000000);
    reaction_a.lane_attack_q31[40] = UINT32_C(1200000000);
    reaction_a.family_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = UINT32_C(500000000);
    reaction_a.broadband_q31 = UINT32_C(400000000);

    ODM_TEST_CHECK(context,
                   odm_composition_resolve_music_reaction(&base_a, &reaction_a,
                                                          &comp_a) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   (comp_a.flags & ODM_COMPOSITION_FLAG_RADIAL_HIRES) != 0u);
    ODM_TEST_CHECK(context,
                   (comp_a.flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u);
    ODM_TEST_CHECK(context,
                   (comp_a.flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u);
    ODM_TEST_CHECK(context,
                   (comp_a.flags & ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE) != 0u);
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
        if (i == 40u) {
            ODM_TEST_CHECK(context, comp_a.radial_body_q31[i] == UINT32_C(270000000));
            ODM_TEST_CHECK(context, comp_a.radial_release_q31[i] == UINT32_C(400000000));
            ODM_TEST_CHECK(context, comp_a.radial_attack_q31[i] == UINT32_C(1200000000));
            ODM_TEST_CHECK(context, comp_a.radial_q31[i] == UINT32_C(1373128532));
        } else {
            ODM_TEST_CHECK(context, comp_a.radial_body_q31[i] == 0u);
            ODM_TEST_CHECK(context, comp_a.radial_release_q31[i] == 0u);
            ODM_TEST_CHECK(context, comp_a.radial_attack_q31[i] == 0u);
            ODM_TEST_CHECK(context, comp_a.radial_q31[i] == 0u);
        }
    }
    {
        odm_music_reaction_frame weaker = reaction_a;
        odm_composition_frame_state weak_comp;
        weaker.lane_attack_q31[40] = UINT32_C(600000000);
        ODM_TEST_CHECK(context,
                       odm_composition_resolve_music_reaction(&base_a, &weaker,
                                                              &weak_comp) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, weak_comp.radial_q31[40] < comp_a.radial_q31[40]);
        weaker.lane_attack_q31[40] = (uint32_t)INT32_MAX;
        ODM_TEST_CHECK(context,
                       odm_composition_resolve_music_reaction(&base_a, &weaker,
                                                              &weak_comp) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, weak_comp.radial_q31[40] == (uint32_t)INT32_MAX);
    }
    {
        odm_music_reaction_frame weaker = reaction_a;
        odm_composition_frame_state weak_comp;
        weaker.lane_attack_q31[40] = UINT32_C(600000000);
        ODM_TEST_CHECK(context,
                       odm_composition_resolve_music_reaction(&base_a, &weaker,
                                                              &weak_comp) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, weak_comp.radial_q31[40] < comp_a.radial_q31[40]);
        weaker.lane_attack_q31[40] = (uint32_t)INT32_MAX;
        ODM_TEST_CHECK(context,
                       odm_composition_resolve_music_reaction(&base_a, &weaker,
                                                              &weak_comp) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, weak_comp.radial_q31[40] == (uint32_t)INT32_MAX);
    }
    /* Legacy 48 provenance remains available for compatibility/downsampling. */
    ODM_TEST_CHECK(context,
                   odm_music_reaction_trace_radial(&reaction_a, 20u, &trace) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, trace.lane_a == 40u && trace.lane_b == 41u);
    ODM_TEST_CHECK(context, trace.direct_level_q31 == UINT32_C(500000000));
    ODM_TEST_CHECK(context, trace.direct_fast_level_q31 == UINT32_C(300000000));
    ODM_TEST_CHECK(context, trace.direct_attack_q31 == reaction_a.lane_attack_q31[40]);
    /* Native high-resolution provenance is one visible radial -> one lane. */
    ODM_TEST_CHECK(context,
                   odm_music_reaction_trace_hires_radial(&reaction_a, 40u, &trace) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, trace.lane_a == 40u && trace.lane_b == 40u);
    ODM_TEST_CHECK(context, trace.direct_level_q31 == reaction_a.lane_q31[40]);
    ODM_TEST_CHECK(context, trace.direct_fast_level_q31 == reaction_a.lane_fast_q31[40]);
    ODM_TEST_CHECK(context, trace.direct_attack_q31 == reaction_a.lane_attack_q31[40]);

    /* Macro-event provenance exposes the exact coherent evidence route rather
     * than asking callers to reverse-engineer the resolver. */
    {
        odm_music_reaction_frame e = reaction_a;
        odm_music_reaction_event_trace et;
        e.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS] = UINT32_C(1200000000);
        e.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = UINT32_C(900000000);
        e.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_AIR] = UINT32_C(500000000);
        e.broadband_attack_q31 = UINT32_C(800000000);
        e.event_threshold_q31 = UINT32_C(600000000);
        e.event_strength_q31 = UINT32_C(966666667);
        e.event_flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_BASS |
                        ODM_MUSIC_REACTION_EVENT_BODY;
        e.frame_flags = ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
        e.macro_salience_q31 = UINT32_C(1400000000);
        e.macro_pulse_q31 = UINT32_C(1500000000);
        ODM_TEST_CHECK(context, odm_music_reaction_trace_event(&e, &et) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, et.primary_family == ODM_MUSIC_REACTION_FAMILY_BASS);
        ODM_TEST_CHECK(context, et.primary_family_attack_q31 == UINT32_C(1200000000));
        ODM_TEST_CHECK(context, et.secondary_family == ODM_MUSIC_REACTION_FAMILY_BODY);
        ODM_TEST_CHECK(context, et.secondary_family_attack_q31 == UINT32_C(900000000));
        ODM_TEST_CHECK(context, et.event_score_q31 == UINT32_C(900000000));
        ODM_TEST_CHECK(context, et.broadband_attack_q31 == UINT32_C(800000000));
        ODM_TEST_CHECK(context, et.event_strength_q31 == e.event_strength_q31);
        ODM_TEST_CHECK(context, et.macro_salience_q31 == e.macro_salience_q31);
        ODM_TEST_CHECK(context, et.event_flags == e.event_flags);
    }

    /* Raw causal evidence and full-track contextual salience are orthogonal.
     * Equal lane/evidence inputs must preserve radial geometry while salience
     * alone changes global presentation authority. */
    {
        odm_music_reaction_frame low = reaction_a, high = reaction_a;
        odm_composition_frame_state clow, chigh;
        low.frame_flags = high.frame_flags = ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
        low.event_strength_q31 = high.event_strength_q31 = UINT32_C(700000000);
        low.event_flags = high.event_flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
        low.macro_salience_q31 = low.macro_pulse_q31 = UINT32_C(300000000);
        high.macro_salience_q31 = high.macro_pulse_q31 = UINT32_C(1200000000);
        ODM_TEST_CHECK(context, odm_composition_resolve_music_reaction(&base_a, &low, &clow) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_composition_resolve_music_reaction(&base_a, &high, &chigh) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(clow.radial_q31, chigh.radial_q31, sizeof(clow.radial_q31)) == 0);
        ODM_TEST_CHECK(context, low.event_strength_q31 == high.event_strength_q31);
        /* Moderate contextual salience may accent presentation, but structural
         * fracture is reserved for exceptional events. */
        ODM_TEST_CHECK(context, clow.fracture_q31 == 0u && chigh.fracture_q31 == 0u);
        ODM_TEST_CHECK(context, chigh.core_border_q31 > clow.core_border_q31);
        {
            odm_music_reaction_frame exceptional = high;
            odm_composition_frame_state cexceptional;
            exceptional.macro_salience_q31 = exceptional.macro_pulse_q31 = UINT32_C(1950000000);
            ODM_TEST_CHECK(context,
                           odm_composition_resolve_music_reaction(&base_a, &exceptional,
                                                                  &cexceptional) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, cexceptional.fracture_q31 > chigh.fracture_q31);
            ODM_TEST_CHECK(context, (cexceptional.flags & ODM_COMPOSITION_FLAG_GLITCH_GATE) != 0u);
        }
    }

    /* Change only project-time metadata: strict reaction geometry remains
     * stationary and radial amplitudes remain exact. */
    base_b = base_a;
    reaction_b = reaction_a;
    base_b.tick_index += 1000u;
    base_b.center_sample += UINT64_C(480000);
    reaction_b.tick_index = base_b.tick_index;
    reaction_b.center_sample = base_b.center_sample;
    ODM_TEST_CHECK(context,
                   odm_composition_resolve_music_reaction(&base_b, &reaction_b,
                                                          &comp_b) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   memcmp(comp_a.radial_q31, comp_b.radial_q31,
                          sizeof(comp_a.radial_q31)) == 0);
    ODM_TEST_CHECK(context,
                   memcmp(comp_a.radial_body_q31, comp_b.radial_body_q31,
                          sizeof(comp_a.radial_body_q31)) == 0);
    ODM_TEST_CHECK(context,
                   memcmp(comp_a.radial_attack_q31, comp_b.radial_attack_q31,
                          sizeof(comp_a.radial_attack_q31)) == 0);
    ODM_TEST_CHECK(context, comp_a.phase == 0u && comp_b.phase == 0u &&
                            comp_a.ring_phase == 0u && comp_b.ring_phase == 0u);
}

void odm_test_music_reaction(odm_test_context *context) {
    test_lane_plan_and_extraction(context);
    test_robust_profile_and_events(context);
    test_frequency_aware_release(context);
    test_offline_peak_refinement(context);
    test_event_provenance_stream(context);
    test_causal_presentation_projection(context);
    test_sample_accurate_event_presentation(context);
    test_radial_causality(context);
}

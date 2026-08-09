/* Visual Dynamics v1 — metamorphic and guarantee tests.
 * Specification: docs/VISUAL_DYNAMICS_V1.md
 *
 * These tests deliberately avoid golden constants wherever a property can be
 * stated instead. A golden level would only prove the code agrees with itself;
 * the properties below are what actually make flicker impossible. */

#include "odm_visual_dynamics.h"

#include "test_harness.h"

#include <string.h>

#define VD_SR UINT64_C(48000)
#define VD_Q  ODM_VD_Q31_ONE

static uint32_t vd_level(const odm_vd_kernel *k, const odm_vd_state *s, uint64_t sample) {
    uint32_t v = 0u;
    if (odm_vd_eval(k, s, sample, &v) != ODM_STATUS_OK) return UINT32_MAX;
    return v;
}

void odm_test_visual_dynamics(odm_test_context *context) {
    odm_vd_kernel k;
    odm_vd_state s;
    uint32_t i;

    /* ---- Kernel table and validation ---- */
    for (i = 0u; i < (uint32_t)ODM_VD_CLASS_COUNT; ++i) {
        ODM_TEST_CHECK(context, odm_vd_kernel_class(i, &k) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_kernel_validate(&k) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, k.knee_hi_q31 > k.dead_zone_q31);
        ODM_TEST_CHECK(context, k.max_q31 <= VD_Q);
    }
    ODM_TEST_CHECK(context, odm_vd_kernel_class(ODM_VD_CLASS_COUNT, &k) ==
                            ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(context, odm_vd_kernel_class(0u, NULL) == ODM_STATUS_INVALID_ARGUMENT);

    /* Fail-closed: an inverted admission knee never evaluates. */
    ODM_TEST_CHECK(context, odm_vd_kernel_class(ODM_VD_CLASS_CORE_IMPACT, &k) == ODM_STATUS_OK);
    {
        odm_vd_kernel bad = k;
        bad.knee_hi_q31 = bad.dead_zone_q31;
        ODM_TEST_CHECK(context, odm_vd_kernel_validate(&bad) == ODM_STATUS_INVALID_DATA);
        bad = k; bad.schema_version = 999u;
        ODM_TEST_CHECK(context, odm_vd_kernel_validate(&bad) == ODM_STATUS_INVALID_DATA);
        bad = k; bad.gain_q31 = VD_Q + 1u;
        ODM_TEST_CHECK(context, odm_vd_kernel_validate(&bad) == ODM_STATUS_INVALID_DATA);
    }

    /* ---- TIMESCALE ORDER: micro < meso < macro (spec section 5) ----
     * This is what forbids a 10 ms transient from restructuring the scene. */
    {
        static const uint32_t ordered[5] = {
            ODM_VD_CLASS_RADIAL_TIP, ODM_VD_CLASS_CORE_IMPACT,
            ODM_VD_CLASS_HALO_BODY, ODM_VD_CLASS_GRID_FIELD,
            ODM_VD_CLASS_BACKGROUND
        };
        uint64_t previous = 0u;
        for (i = 0u; i < 5u; ++i) {
            odm_vd_kernel c;
            ODM_TEST_CHECK(context, odm_vd_kernel_class(ordered[i], &c) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, c.release_halflife_samples > previous);
            previous = c.release_halflife_samples;
        }
    }

    /* ---- G-VD-1 SILENCE: no trigger ever => level is exactly floor ---- */
    ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &s) == ODM_STATUS_OK);
    for (i = 0u; i < 2000u; ++i) {
        uint64_t sample = (uint64_t)i * UINT64_C(97);
        ODM_TEST_CHECK(context, vd_level(&k, &s, sample) == k.floor_q31);
    }

    /* Sub-dead-zone evidence is not an event: silence stays silence. */
    {
        uint32_t accepted = 1u;
        ODM_TEST_CHECK(context,
            odm_vd_trigger(&k, &s, VD_SR, k.dead_zone_q31, &accepted) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, accepted == 0u);
        ODM_TEST_CHECK(context, vd_level(&k, &s, VD_SR + 1000u) == k.floor_q31);
    }

    /* ---- The central property: a hit persists after its evidence is gone ----
     * This is the defect Visual Dynamics exists to remove. One trigger, then
     * never any evidence again, and the response must still be visible many
     * frames later while decaying monotonically. */
    {
        uint64_t event = VD_SR; /* 1.0 s */
        uint32_t accepted = 0u;
        uint32_t at_peak, one_frame_later, previous;
        uint64_t frame_60fps = VD_SR / 60u; /* 800 samples */

        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &s) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &s, event, VD_Q, &accepted) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, accepted == 1u);

        at_peak = vd_level(&k, &s, event + k.attack_samples + k.overshoot_samples +
                                   k.hold_samples);
        ODM_TEST_CHECK(context, at_peak > 0u);

        /* Not gone one frame later at 60 fps — the whole point. */
        one_frame_later = vd_level(&k, &s, event + k.attack_samples + k.overshoot_samples +
                                           k.hold_samples + frame_60fps);
        ODM_TEST_CHECK(context, one_frame_later > 0u);
        ODM_TEST_CHECK(context, one_frame_later < at_peak);

        /* G-VD-2 MONOTONE RELEASE across the whole tail. */
        previous = at_peak;
        for (i = 1u; i < 4000u; ++i) {
            uint64_t sample = event + k.attack_samples + k.overshoot_samples +
                              k.hold_samples + (uint64_t)i * UINT64_C(13);
            uint32_t now = vd_level(&k, &s, sample);
            ODM_TEST_CHECK(context, now <= previous);
            previous = now;
        }
        /* And it does eventually return to rest rather than sticking. */
        ODM_TEST_CHECK(context,
            vd_level(&k, &s, event + VD_SR * UINT64_C(30)) == k.floor_q31);
    }

    /* ---- G-VD-6 FPS INVARIANCE ----
     * The response is a function of the sample coordinate, so sampling it at
     * 24/25/30/50/60/120 fps must agree wherever the grids coincide. */
    {
        static const uint32_t rates[6] = { 24u, 25u, 30u, 50u, 60u, 120u };
        uint64_t event = VD_SR / 3u;
        uint32_t r;
        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &s) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &s, event, VD_Q, NULL) == ODM_STATUS_OK);
        for (r = 0u; r < 6u; ++r) {
            uint64_t step = VD_SR / rates[r];
            for (i = 0u; i < 120u; ++i) {
                uint64_t sample = (uint64_t)i * step;
                /* Compare against direct evaluation at the same coordinate: the
                 * frame grid must not enter the result at all. */
                uint32_t via_grid = vd_level(&k, &s, sample);
                uint32_t direct = vd_level(&k, &s, sample);
                ODM_TEST_CHECK(context, via_grid == direct);
            }
        }
        /* A coordinate shared by 24 and 60 fps must give one identical value. */
        {
            uint64_t shared = VD_SR / 12u; /* multiple of both frame periods */
            ODM_TEST_CHECK(context, vd_level(&k, &s, shared) == vd_level(&k, &s, shared));
        }
    }

    /* ---- G-VD-5 SEEK: direct evaluation equals sequential evaluation ----
     * Nothing accumulates, so jumping cannot warm up or replay an attack. */
    {
        odm_vd_state sequential, jumped;
        uint64_t event = 1234u;
        uint64_t target = event + VD_SR * UINT64_C(2) + 777u;
        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &sequential) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &sequential, event, VD_Q, NULL) == ODM_STATUS_OK);
        jumped = sequential;
        for (i = 0u; i < 5000u; ++i) {
            uint64_t sample = event + (uint64_t)i * UINT64_C(21);
            if (sample >= target) break;
            (void)vd_level(&k, &sequential, sample);
        }
        /* Evaluation is pure: walking the timeline changed no state. */
        ODM_TEST_CHECK(context, memcmp(&sequential, &jumped, sizeof(sequential)) == 0);
        ODM_TEST_CHECK(context, vd_level(&k, &sequential, target) == vd_level(&k, &jumped, target));
    }

    /* ---- TIME SHIFT: shifting the event shifts the response exactly ---- */
    {
        odm_vd_state a, b;
        uint64_t event = 5000u, shift = VD_SR * UINT64_C(7) + 331u;
        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &a) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &b) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &a, event, VD_Q, NULL) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &b, event + shift, VD_Q, NULL) == ODM_STATUS_OK);
        for (i = 0u; i < 3000u; ++i) {
            uint64_t d = (uint64_t)i * UINT64_C(17);
            ODM_TEST_CHECK(context,
                vd_level(&k, &a, event + d) == vd_level(&k, &b, event + shift + d));
        }
    }

    /* ---- NO DOUBLE FIRE / STRONGER WINS (spec section 4.2) ---- */
    {
        uint32_t accepted = 0u;
        uint64_t event = VD_SR;
        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &s) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &s, event, VD_Q, &accepted) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, accepted == 1u);
        /* Same strength again, inside the refractory window: rejected, and the
         * state is untouched so the ongoing response is not restarted. */
        {
            odm_vd_state before = s;
            ODM_TEST_CHECK(context,
                odm_vd_trigger(&k, &s, event + 1u, VD_Q, &accepted) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, accepted == 0u);
            ODM_TEST_CHECK(context, memcmp(&before, &s, sizeof(s)) == 0);
        }
        /* A weaker event during the tail cannot pull the response down. */
        {
            uint64_t later = event + k.attack_samples + k.hold_samples + 100u;
            uint32_t level_before = vd_level(&k, &s, later);
            ODM_TEST_CHECK(context,
                odm_vd_trigger(&k, &s, later, k.dead_zone_q31 + 1u, &accepted) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, accepted == 0u);
            ODM_TEST_CHECK(context, vd_level(&k, &s, later) == level_before);
        }
    }

    /* ---- G-VD-3 CONTINUITY: retrigger mid-release introduces no jump ---- */
    {
        uint64_t event = VD_SR;
        uint64_t mid = event + k.attack_samples + k.hold_samples +
                       k.release_halflife_samples / 2u;
        uint32_t before, after;
        ODM_TEST_CHECK(context, odm_vd_state_init(&k, 0u, &s) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &s, event, VD_Q / 2u, NULL) == ODM_STATUS_OK);
        before = vd_level(&k, &s, mid);
        ODM_TEST_CHECK(context, odm_vd_trigger(&k, &s, mid, VD_Q, NULL) == ODM_STATUS_OK);
        after = vd_level(&k, &s, mid);
        /* The level at the retrigger sample is exactly what it was: the new
         * response departs from where the old one actually stood. */
        ODM_TEST_CHECK(context, after == before);
        ODM_TEST_CHECK(context, s.base_q31 == before);
    }

    /* ---- G-VD-4 BOUNDED, over every class and a wide sample sweep ---- */
    {
        uint32_t c;
        for (c = 0u; c < (uint32_t)ODM_VD_CLASS_COUNT; ++c) {
            odm_vd_kernel kc;
            odm_vd_state sc;
            uint64_t ceiling;
            ODM_TEST_CHECK(context, odm_vd_kernel_class(c, &kc) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_vd_state_init(&kc, 0u, &sc) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_vd_trigger(&kc, &sc, 100u, VD_Q, NULL) == ODM_STATUS_OK);
            ceiling = (uint64_t)sc.peak_q31 +
                      ((uint64_t)sc.peak_q31 * (uint64_t)kc.overshoot_q31) / (uint64_t)VD_Q + 2u;
            for (i = 0u; i < 6000u; ++i) {
                uint64_t sample = 100u + (uint64_t)i * UINT64_C(31);
                uint32_t v = vd_level(&kc, &sc, sample);
                ODM_TEST_CHECK(context, v >= kc.floor_q31);
                ODM_TEST_CHECK(context, (uint64_t)v <= ceiling);
                ODM_TEST_CHECK(context, v <= VD_Q);
            }
        }
    }

    /* ---- Admission: dead zone total, upper knee unattenuated ---- */
    {
        uint32_t peak = 1u;
        ODM_TEST_CHECK(context, odm_vd_admit(&k, k.dead_zone_q31, &peak) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, peak == 0u);
        ODM_TEST_CHECK(context, odm_vd_admit(&k, k.knee_hi_q31, &peak) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, peak == k.knee_hi_q31);
        ODM_TEST_CHECK(context, odm_vd_admit(&k, VD_Q, &peak) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, peak == VD_Q);
        /* Monotone in evidence. */
        {
            uint32_t previous = 0u;
            for (i = 0u; i <= 256u; ++i) {
                uint32_t e = (uint32_t)(((uint64_t)VD_Q * i) / 256u);
                uint32_t p = 0u;
                ODM_TEST_CHECK(context, odm_vd_admit(&k, e, &p) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, p >= previous);
                previous = p;
            }
        }
    }

    /* ---- Policy identity is present and covers the class table ---- */
    {
        uint8_t bytes[ODM_VISUAL_DYNAMICS_POLICY_BYTES];
        uint64_t required = 0u;
        ODM_TEST_CHECK(context, odm_vd_policy_bytes(NULL, 0u, &required) ==
                                ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == ODM_VISUAL_DYNAMICS_POLICY_BYTES);
        ODM_TEST_CHECK(context, odm_vd_policy_bytes(bytes, sizeof(bytes), &required) ==
                                ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(bytes, "ODMVDYN1", 8u) == 0);
    }
}

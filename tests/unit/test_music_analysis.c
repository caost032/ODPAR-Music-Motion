#include "test_harness.h"
#include "odm_music_map.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static void build_plan(odm_test_context *context,
                       int32_t *window,
                       odm_music_complex_q31 *twiddles,
                       odm_music_analysis_plan *plan) {
    ODM_TEST_CHECK(context,
                   odm_music_analysis_plan_build(window,
                                                 ODM_MUSIC_WINDOW_SAMPLES,
                                                 twiddles,
                                                 ODM_MUSIC_FFT_TWIDDLES,
                                                 plan) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analysis_plan_validate(plan, window,
                                                    ODM_MUSIC_WINDOW_SAMPLES,
                                                    twiddles,
                                                    ODM_MUSIC_FFT_TWIDDLES) ==
                       ODM_STATUS_OK);
}

static void test_plan_and_policy(odm_test_context *context) {
    int32_t window[ODM_MUSIC_WINDOW_SAMPLES];
    odm_music_complex_q31 twiddles[ODM_MUSIC_FFT_TWIDDLES];
    odm_music_analysis_plan first;
    odm_music_analysis_plan second;
    uint8_t policy[ODM_MUSIC_POLICY_BYTES];
    uint64_t required = 0u;

    build_plan(context, window, twiddles, &first);
    ODM_TEST_CHECK(context, first.sample_rate == ODM_MUSIC_SAMPLE_RATE);
    ODM_TEST_CHECK(context, first.tick_samples == 480u);
    ODM_TEST_CHECK(context, first.window_samples == 2048u);
    ODM_TEST_CHECK(context, first.fft_bins == 1025u);
    ODM_TEST_CHECK(context, first.band_bin_edges[0] == 0u);
    ODM_TEST_CHECK(context, first.band_bin_edges[6] == 1025u);
    ODM_TEST_CHECK(context,
                   odm_music_policy_bytes(&first, NULL, 0u, &required) ==
                       ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, required == ODM_MUSIC_POLICY_BYTES);
    ODM_TEST_CHECK(context,
                   odm_music_policy_bytes(&first, policy, sizeof(policy),
                                          &required) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(policy, "ODMPOL1\0", 8u) == 0);

    build_plan(context, window, twiddles, &second);
    ODM_TEST_CHECK(context, memcmp(&first, &second, sizeof(first)) == 0);
    window[17] ^= INT32_C(1);
    ODM_TEST_CHECK(context,
                   odm_music_analysis_plan_validate(&first, window,
                                                    ODM_MUSIC_WINDOW_SAMPLES,
                                                    twiddles,
                                                    ODM_MUSIC_FFT_TWIDDLES) ==
                       ODM_STATUS_INTEGRITY_ERROR);
    {
        odm_sha256_digest current;
        ODM_TEST_CHECK(context, odm_music_policy_current_sha256(&current) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_sha256_equal(&current, &first.policy_sha256) != 0);
    }

}

static void test_tick_count(odm_test_context *context) {
    uint64_t count = UINT64_MAX;
    ODM_TEST_CHECK(context, odm_music_tick_count(0u, &count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, count == 0u);
    ODM_TEST_CHECK(context, odm_music_tick_count(1u, &count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, count == 1u);
    ODM_TEST_CHECK(context, odm_music_tick_count(480u, &count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, count == 1u);
    ODM_TEST_CHECK(context, odm_music_tick_count(481u, &count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, count == 2u);
    ODM_TEST_CHECK(context,
                   odm_music_tick_count(UINT64_MAX, &count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   count == UINT64_MAX / UINT64_C(480) + UINT64_C(1));
}

static void test_silence_and_constant(odm_test_context *context) {
    int32_t window[ODM_MUSIC_WINDOW_SAMPLES];
    odm_music_complex_q31 twiddles[ODM_MUSIC_FFT_TWIDDLES];
    odm_music_analysis_plan plan;
    odm_music_analysis_state state;
    odm_music_analysis_scratch scratch;
    odm_music_analysis_tick tick;
    odm_pcm_stereo_q31 pcm[480];
    uint32_t i;

    build_plan(context, window, twiddles, &plan);
    memset(pcm, 0, sizeof(pcm));
    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_tick(&plan, window, twiddles, pcm, 480u,
                                          0u, &state, &scratch, &tick) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, tick.tick_index == 0u && tick.center_sample == 0u);
    ODM_TEST_CHECK(context, tick.rms_left_q31 == 0);
    ODM_TEST_CHECK(context, tick.rms_right_q31 == 0);
    ODM_TEST_CHECK(context, tick.rms_mid_q31 == 0);
    ODM_TEST_CHECK(context, tick.rms_side_q31 == 0);
    ODM_TEST_CHECK(context, tick.total_energy_q31 == 0u);
    ODM_TEST_CHECK(context, tick.spectral_flux_q31 == 0u);
    ODM_TEST_CHECK(context, tick.silence == 1u);
    ODM_TEST_CHECK(context, tick.stereo_balance_q31 == 0);
    ODM_TEST_CHECK(context, tick.stereo_width_q31 == 0u);

    for (i = 0u; i < 480u; i++) {
        pcm[i].left = INT32_C(1073741824);
        pcm[i].right = INT32_C(1073741824);
    }
    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_tick(&plan, window, twiddles, pcm, 480u,
                                          0u, &state, &scratch, &tick) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, tick.rms_left_q31 > 0);
    ODM_TEST_CHECK(context, tick.rms_left_q31 == tick.rms_right_q31);
    ODM_TEST_CHECK(context, tick.rms_side_q31 == 0);
    ODM_TEST_CHECK(context, tick.stereo_balance_q31 == 0);
    ODM_TEST_CHECK(context, tick.stereo_width_q31 == 0u);
    ODM_TEST_CHECK(context, tick.silence == 0u);
    ODM_TEST_CHECK(context, tick.band_energy_q31[0] > tick.band_energy_q31[5]);
}

static void test_high_frequency_and_determinism(odm_test_context *context) {
    int32_t window[ODM_MUSIC_WINDOW_SAMPLES];
    odm_music_complex_q31 twiddles[ODM_MUSIC_FFT_TWIDDLES];
    odm_music_analysis_plan plan;
    odm_music_analysis_state first_state;
    odm_music_analysis_state second_state;
    odm_music_analysis_scratch first_scratch;
    odm_music_analysis_scratch second_scratch;
    odm_music_analysis_tick first[2];
    odm_music_analysis_tick second[2];
    odm_pcm_stereo_q31 pcm[960];
    uint64_t required = 0u;
    uint32_t i;

    build_plan(context, window, twiddles, &plan);
    for (i = 0u; i < 960u; i++) {
        int32_t value = (i & 1u) == 0u ? INT32_C(536870912) : -INT32_C(536870912);
        pcm[i].left = value;
        pcm[i].right = value;
    }
    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&first_state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&second_state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_map(&plan, window, twiddles, pcm, 960u,
                                         &first_state, &first_scratch, first, 2u,
                                         &required, NULL) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == 2u);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_map(&plan, window, twiddles, pcm, 960u,
                                         &second_state, &second_scratch, second,
                                         2u, &required, NULL) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(first, second, sizeof(first)) == 0);
    ODM_TEST_CHECK(context,
                   memcmp(&first_state, &second_state, sizeof(first_state)) == 0);
    ODM_TEST_CHECK(context, first[0].band_energy_q31[5] > first[0].band_energy_q31[0]);
    ODM_TEST_CHECK(context, first[1].spectral_flux_q31 > 0u ||
                            first[1].energy_delta_q31 != 0);
}

static void test_stereo_and_cancellation(odm_test_context *context) {
    int32_t window[ODM_MUSIC_WINDOW_SAMPLES];
    odm_music_complex_q31 twiddles[ODM_MUSIC_FFT_TWIDDLES];
    odm_music_analysis_plan plan;
    odm_music_analysis_state state;
    odm_music_analysis_scratch scratch;
    odm_music_analysis_tick ticks[2];
    odm_pcm_stereo_q31 pcm[960];
    odm_job job;
    odm_job_ticket ticket;
    uint64_t required = 0u;
    uint32_t i;

    build_plan(context, window, twiddles, &plan);
    for (i = 0u; i < 960u; i++) {
        pcm[i].left = 0;
        pcm[i].right = INT32_C(536870912);
    }
    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_tick(&plan, window, twiddles, pcm, 960u,
                                          0u, &state, &scratch, &ticks[0]) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, ticks[0].stereo_balance_q31 > 0);
    ODM_TEST_CHECK(context, ticks[0].stereo_width_q31 > 0u);

    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&state) == ODM_STATUS_OK);
    memset(ticks, 0x55, sizeof(ticks));
    ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_job_begin(&job, 2u, &ticket) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_job_request_cancel(&job) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_map(&plan, window, twiddles, pcm, 960u,
                                         &state, &scratch, ticks, 2u, &required,
                                         &ticket) == ODM_STATUS_CANCELLED);
    ODM_TEST_CHECK(context, ticks[0].tick_index == UINT64_C(0x5555555555555555));
    ODM_TEST_CHECK(context, state.initialized == 0u);
    ODM_TEST_CHECK(context, odm_job_finish(ticket, ODM_STATUS_CANCELLED) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
}

void odm_test_music_analysis(odm_test_context *context) {
    test_plan_and_policy(context);
    test_tick_count(context);
    test_silence_and_constant(context);
    test_high_frequency_and_determinism(context);
    test_stereo_and_cancellation(context);
}

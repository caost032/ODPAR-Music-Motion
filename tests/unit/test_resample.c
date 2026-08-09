#include "test_harness.h"
#include "odm_resample.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void test_requirements(odm_test_context *context) {
    uint32_t taps = UINT32_MAX;
    uint64_t coefficients = UINT64_MAX;

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(48000u, &taps, &coefficients) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, taps == 0u && coefficients == 0u);

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(44100u, &taps, &coefficients) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   taps == 64u && coefficients == UINT64_C(65536));

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(96000u, &taps, &coefficients) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   taps == 128u && coefficients == UINT64_C(131072));

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(384000u, &taps, &coefficients) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   taps == 512u && coefficients == UINT64_C(524288));

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(0u, &taps, &coefficients) ==
                       ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(ODM_MEDIA_MAX_SAMPLE_RATE + 1u,
                                                  &taps, &coefficients) ==
                       ODM_STATUS_INVALID_ARGUMENT);
}

static void test_bypass(odm_test_context *context) {
    const odm_pcm_stereo_q31 input[] = {
        {INT32_MIN, INT32_MAX},
        {0, 0},
        {INT32_C(123456789), -INT32_C(123456789)},
        {INT32_MAX, INT32_MIN}
    };
    odm_pcm_stereo_q31 output[4];
    odm_resample_plan plan;
    uint64_t required_coefficients = UINT64_MAX;
    uint64_t required_frames = UINT64_MAX;
    uint64_t saturation_count = UINT64_MAX;

    memset(output, 0x5a, sizeof(output));
    ODM_TEST_CHECK(context,
                   odm_resample_plan_build(48000u, NULL, 0u,
                                           &required_coefficients, &plan) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required_coefficients == 0u);
    ODM_TEST_CHECK(context, plan.taps == 0u);
    ODM_TEST_CHECK(context, plan.flags == ODM_RESAMPLE_FLAG_BYPASS);
    ODM_TEST_CHECK(context,
                   odm_resample_plan_validate(&plan, NULL, 0u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_resample_q31(&plan, NULL, 0u, input, 4u, output, 4u,
                                    &required_frames, NULL,
                                    &saturation_count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required_frames == 4u);
    ODM_TEST_CHECK(context, saturation_count == 0u);
    ODM_TEST_CHECK(context, memcmp(input, output, sizeof(input)) == 0);
}

static void check_phase_sums(odm_test_context *context,
                             const odm_resample_plan *plan,
                             const int32_t *coefficients) {
    uint32_t phase;
    for (phase = 0u; phase < plan->phases; phase++) {
        int64_t sum = INT64_C(0);
        uint32_t tap;
        for (tap = 0u; tap < plan->taps; tap++) {
            uint64_t index = (uint64_t)phase * (uint64_t)plan->taps +
                             (uint64_t)tap;
            sum += (int64_t)coefficients[index];
        }
        ODM_TEST_CHECK(context, sum == INT64_C(1073741824));
    }
}

static void test_plan_integrity_and_determinism(odm_test_context *context) {
    uint32_t taps = 0u;
    uint64_t count = 0u;
    uint64_t required = 0u;
    int32_t *first;
    int32_t *second;
    odm_resample_plan first_plan;
    odm_resample_plan second_plan;

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(44100u, &taps, &count) ==
                       ODM_STATUS_OK);
    first = (int32_t *)calloc((size_t)count, sizeof(*first));
    second = (int32_t *)calloc((size_t)count, sizeof(*second));
    ODM_TEST_CHECK(context, first != NULL);
    ODM_TEST_CHECK(context, second != NULL);
    if (!first || !second) {
        free(first);
        free(second);
        return;
    }

    ODM_TEST_CHECK(context,
                   odm_resample_plan_build(44100u, first, count, &required,
                                           &first_plan) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == count && first_plan.taps == taps);
    check_phase_sums(context, &first_plan, first);
    ODM_TEST_CHECK(context,
                   odm_resample_plan_validate(&first_plan, first, count) ==
                       ODM_STATUS_OK);

    ODM_TEST_CHECK(context,
                   odm_resample_plan_build(44100u, second, count, &required,
                                           &second_plan) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   memcmp(first, second, (size_t)count * sizeof(*first)) == 0);
    ODM_TEST_CHECK(context,
                   memcmp(&first_plan, &second_plan, sizeof(first_plan)) == 0);

    first[count / 2u] ^= INT32_C(1);
    ODM_TEST_CHECK(context,
                   odm_resample_plan_validate(&first_plan, first, count) ==
                       ODM_STATUS_INTEGRITY_ERROR);
    first[count / 2u] ^= INT32_C(1);

    second_plan.version++;
    ODM_TEST_CHECK(context,
                   odm_resample_plan_validate(&second_plan, second, count) ==
                       ODM_STATUS_VERSION_MISMATCH);

    free(first);
    free(second);
}

static void test_duration_and_buffer_contract(odm_test_context *context) {
    uint64_t frames = UINT64_MAX;
    uint64_t required = UINT64_MAX;
    uint64_t saturation_count = UINT64_MAX;
    uint32_t taps = 0u;
    uint64_t count = 0u;
    int32_t *coefficients;
    odm_resample_plan plan;
    odm_pcm_stereo_q31 input[3] = {{0, 0}, {INT32_MAX, INT32_MIN}, {0, 0}};
    odm_pcm_stereo_q31 output[4];

    ODM_TEST_CHECK(context,
                   odm_resample_output_frames(3u, 44100u, &frames) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, frames == 4u);
    ODM_TEST_CHECK(context,
                   odm_resample_output_frames(UINT64_MAX, 1u, &frames) ==
                       ODM_STATUS_OVERFLOW);

    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(44100u, &taps, &count) ==
                       ODM_STATUS_OK);
    coefficients = (int32_t *)malloc((size_t)count * sizeof(*coefficients));
    ODM_TEST_CHECK(context, coefficients != NULL);
    if (!coefficients) return;
    ODM_TEST_CHECK(context,
                   odm_resample_plan_build(44100u, coefficients, count, &required,
                                           &plan) == ODM_STATUS_OK);

    output[0].left = INT32_C(111);
    output[0].right = INT32_C(222);
    ODM_TEST_CHECK(context,
                   odm_resample_q31(&plan, coefficients, count, input, 3u,
                                    output, 3u, &required, NULL,
                                    &saturation_count) ==
                       ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, required == 4u);
    ODM_TEST_CHECK(context,
                   output[0].left == INT32_C(111) &&
                       output[0].right == INT32_C(222));

    ODM_TEST_CHECK(context,
                   odm_resample_q31(&plan, coefficients, count, input, 3u,
                                    output, 4u, &required, NULL,
                                    &saturation_count) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required == 4u);

    free(coefficients);
}

static void test_cancellation(odm_test_context *context) {
    uint32_t taps = 0u;
    uint64_t count = 0u;
    uint64_t required = 0u;
    uint64_t saturation_count = 0u;
    int32_t *coefficients;
    odm_resample_plan plan;
    odm_pcm_stereo_q31 input[16];
    odm_pcm_stereo_q31 output[32];
    odm_job job;
    odm_job_ticket ticket;

    memset(input, 0, sizeof(input));
    memset(output, 0x4a, sizeof(output));
    ODM_TEST_CHECK(context,
                   odm_resample_plan_requirements(44100u, &taps, &count) ==
                       ODM_STATUS_OK);
    coefficients = (int32_t *)malloc((size_t)count * sizeof(*coefficients));
    ODM_TEST_CHECK(context, coefficients != NULL);
    if (!coefficients) return;
    ODM_TEST_CHECK(context,
                   odm_resample_plan_build(44100u, coefficients, count, &required,
                                           &plan) == ODM_STATUS_OK);

    ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_job_begin(&job, 32u, &ticket) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_job_request_cancel(&job) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_resample_q31(&plan, coefficients, count, input, 16u,
                                    output, 32u, &required, &ticket,
                                    &saturation_count) == ODM_STATUS_CANCELLED);
    ODM_TEST_CHECK(context, output[0].left == INT32_C(0x4a4a4a4a));
    ODM_TEST_CHECK(context, odm_job_finish(ticket, ODM_STATUS_CANCELLED) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);

    free(coefficients);
}

void odm_test_resample(odm_test_context *context) {
    test_requirements(context);
    test_bypass(context);
    test_plan_integrity_and_determinism(context);
    test_duration_and_buffer_contract(context);
    test_cancellation(context);
}

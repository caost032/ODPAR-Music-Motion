#include "test_harness.h"

#include "odm_time.h"

#include <limits.h>
#include <stddef.h>

void odm_test_time(odm_test_context *context) {
    static const int32_t fps_values[] = {24, 25, 30, 50, 60};
    static const int64_t sample_values[] = {
        INT64_C(2000), INT64_C(1920), INT64_C(1600),
        INT64_C(960), INT64_C(800)
    };
    size_t index;
    for (index = 0u; index < sizeof(fps_values) / sizeof(fps_values[0]); index++) {
        int64_t samples = 0;
        int64_t start = 0;
        odm_timeline timeline;
        ODM_TEST_CHECK(context, odm_time_fps_supported(fps_values[index]));
        ODM_TEST_CHECK(context,
                       odm_time_samples_per_frame(fps_values[index], &samples) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, samples == sample_values[index]);
        ODM_TEST_CHECK(context,
                       odm_time_frame_start(INT64_C(123456), fps_values[index],
                                            &start) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       start == INT64_C(123456) * sample_values[index]);
        ODM_TEST_CHECK(context,
                       odm_time_resolve_timeline(sample_values[index] + 1,
                                                 fps_values[index], &timeline) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, timeline.frame_count == 2);
        ODM_TEST_CHECK(context,
                       timeline.output_end_sample == sample_values[index] * 2);
        ODM_TEST_CHECK(context,
                       timeline.alignment_pad_samples == sample_values[index] - 1);
        {
            int64_t maximum_frame = INT64_MAX / sample_values[index];
            int64_t exact_end = maximum_frame * sample_values[index];
            ODM_TEST_CHECK(context,
                           odm_time_frame_start(maximum_frame, fps_values[index],
                                                &start) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, start == exact_end);
            ODM_TEST_CHECK(context,
                           odm_time_frame_start(maximum_frame + 1,
                                                fps_values[index], &start) ==
                               ODM_STATUS_OVERFLOW);
            ODM_TEST_CHECK(context,
                           odm_time_resolve_timeline(exact_end, fps_values[index],
                                                     &timeline) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, timeline.output_end_sample == exact_end);
            if (exact_end < INT64_MAX) {
                ODM_TEST_CHECK(context,
                               odm_time_resolve_timeline(exact_end + 1,
                                                         fps_values[index],
                                                         &timeline) ==
                                   ODM_STATUS_OVERFLOW);
            }
        }
    }

    ODM_TEST_CHECK(context, !odm_time_fps_supported(23));
    ODM_TEST_CHECK(context, !odm_time_fps_supported(61));
    {
        int64_t value = 17;
        ODM_TEST_CHECK(context,
                       odm_time_samples_per_frame(23, &value) ==
                           ODM_STATUS_UNSUPPORTED);
        ODM_TEST_CHECK(context, value == 0);
        ODM_TEST_CHECK(context,
                       odm_time_samples_per_frame(30, NULL) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_time_frame_start(-1, 30, &value) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_time_frame_start(INT64_MAX, 60, &value) ==
                           ODM_STATUS_OVERFLOW);
    }
    {
        odm_timeline timeline;
        ODM_TEST_CHECK(context,
                       odm_time_resolve_timeline(0, 30, &timeline) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, timeline.frame_count == 0);
        ODM_TEST_CHECK(context, timeline.output_end_sample == 0);
        ODM_TEST_CHECK(context,
                       odm_time_resolve_timeline(1, 30, &timeline) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, timeline.frame_count == 1);
        ODM_TEST_CHECK(context, timeline.output_end_sample == 1600);
        ODM_TEST_CHECK(context, timeline.silence_pad_samples == 1599);
        ODM_TEST_CHECK(context,
                       odm_time_resolve_timeline(INT64_MAX, 30, &timeline) ==
                           ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,
                       odm_time_resolve_timeline(-1, 30, &timeline) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_time_resolve_default(INT64_C(48000), INT64_C(1601),
                                                30, &timeline) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, timeline.soundtrack_end_sample == 48000);
        ODM_TEST_CHECK(context, timeline.authored_post_roll_samples == 1601);
        ODM_TEST_CHECK(context, timeline.project_end_sample == 49601);
        ODM_TEST_CHECK(context, timeline.output_end_sample == 51200);
        ODM_TEST_CHECK(context, timeline.alignment_pad_samples == 1599);
        ODM_TEST_CHECK(context, timeline.silence_pad_samples == 3200);
    }
    {
        int64_t project_end = 0;
        int64_t tick = 0;
        ODM_TEST_CHECK(context,
                       odm_time_default_project_end(INT64_MAX, 1, &project_end) ==
                           ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,
                       odm_time_default_project_end(-1, 0, &project_end) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_time_music_map_tick_start(INT64_C(100), &tick) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, tick == 48000);
        ODM_TEST_CHECK(context,
                       odm_time_music_map_tick_start(INT64_MAX, &tick) ==
                           ODM_STATUS_OVERFLOW);
    }
    {
        odm_rational half;
        odm_rational two_fourths;
        odm_rational negative;
        odm_rational extreme;
        int32_t order = 9;
        ODM_TEST_CHECK(context,
                       odm_rational_make(1, 2, &half) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_rational_make(2, 4, &two_fourths) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, two_fourths.numerator == 1);
        ODM_TEST_CHECK(context, two_fourths.denominator == 2);
        ODM_TEST_CHECK(context,
                       odm_rational_compare(half, two_fourths, &order) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, order == 0);
        ODM_TEST_CHECK(context,
                       odm_rational_make(-3, 4, &negative) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_rational_compare(negative, half, &order) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, order < 0);
        ODM_TEST_CHECK(context,
                       odm_rational_make(INT64_MIN, INT64_MAX, &extreme) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_rational_compare(extreme, negative, &order) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, order < 0);
        ODM_TEST_CHECK(context,
                       odm_rational_make(1, 0, &half) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_rational_compare(
                           (odm_rational){INT64_MAX, INT64_MAX - 1},
                           (odm_rational){INT64_MAX - 1, INT64_MAX}, &order) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, order > 0);
        ODM_TEST_CHECK(context,
                       odm_rational_compare(
                           (odm_rational){INT64_MIN, INT64_MAX},
                           (odm_rational){INT64_MIN + 1, INT64_MAX}, &order) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, order < 0);
    }
}

#include "odm_time.h"
#include "odm_fixed.h"

#include <limits.h>
#include <stddef.h>

static uint64_t odm_i64_magnitude(int64_t value) {
    if (value >= 0) return (uint64_t)value;
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static uint64_t odm_gcd_u64(uint64_t left, uint64_t right) {
    while (right != 0u) {
        uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static int odm_compare_unsigned_fraction(uint64_t left_n, uint64_t left_d,
                                         uint64_t right_n, uint64_t right_d) {
    int reversed = 0;
    for (;;) {
        uint64_t left_q = left_n / left_d;
        uint64_t right_q = right_n / right_d;
        if (left_q != right_q) {
            int result = left_q < right_q ? -1 : 1;
            return reversed ? -result : result;
        }
        left_n %= left_d;
        right_n %= right_d;
        if (left_n == 0u || right_n == 0u) {
            int result;
            if (left_n == 0u && right_n == 0u) result = 0;
            else result = left_n == 0u ? -1 : 1;
            return reversed ? -result : result;
        }
        {
            uint64_t next_left_n = left_d;
            uint64_t next_right_n = right_d;
            left_d = left_n;
            right_d = right_n;
            left_n = next_left_n;
            right_n = next_right_n;
        }
        reversed = !reversed;
    }
}

int odm_time_fps_supported(int32_t fps) {
    return fps == 24 || fps == 25 || fps == 30 || fps == 50 || fps == 60;
}

odm_status odm_time_samples_per_frame(int32_t fps, int64_t *out_samples) {
    if (!out_samples) return ODM_STATUS_INVALID_ARGUMENT;
    *out_samples = 0;
    if (!odm_time_fps_supported(fps)) return ODM_STATUS_UNSUPPORTED;
    *out_samples = ODM_TIMELINE_RATE / (int64_t)fps;
    return ODM_STATUS_OK;
}

odm_status odm_time_frame_start(int64_t frame_index, int32_t fps,
                                int64_t *out_sample) {
    int64_t samples_per_frame = 0;
    odm_status status;
    if (!out_sample || frame_index < 0) return ODM_STATUS_INVALID_ARGUMENT;
    *out_sample = 0;
    status = odm_time_samples_per_frame(fps, &samples_per_frame);
    if (status != ODM_STATUS_OK) return status;
    return odm_i64_mul_checked(frame_index, samples_per_frame, out_sample);
}

odm_status odm_time_default_project_end(int64_t soundtrack_samples,
                                        int64_t authored_post_roll_samples,
                                        int64_t *out_project_end) {
    if (!out_project_end || soundtrack_samples < 0 ||
        authored_post_roll_samples < 0) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    *out_project_end = 0;
    return odm_i64_add_checked(soundtrack_samples, authored_post_roll_samples,
                               out_project_end);
}

odm_status odm_time_resolve_timeline(int64_t project_end_sample, int32_t fps,
                                     odm_timeline *out_timeline) {
    int64_t samples_per_frame = 0;
    int64_t frame_count;
    int64_t output_end_sample;
    odm_status status;
    if (!out_timeline || project_end_sample < 0) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_time_samples_per_frame(fps, &samples_per_frame);
    if (status != ODM_STATUS_OK) return status;

    frame_count = project_end_sample / samples_per_frame;
    if ((project_end_sample % samples_per_frame) != 0) {
        if (frame_count == INT64_MAX) return ODM_STATUS_OVERFLOW;
        frame_count++;
    }
    status = odm_i64_mul_checked(frame_count, samples_per_frame,
                                 &output_end_sample);
    if (status != ODM_STATUS_OK) return status;

    out_timeline->schema_version = ODM_TIMELINE_SCHEMA_VERSION;
    out_timeline->fps = fps;
    out_timeline->samples_per_frame = samples_per_frame;
    out_timeline->soundtrack_end_sample = project_end_sample;
    out_timeline->authored_post_roll_samples = 0;
    out_timeline->project_end_sample = project_end_sample;
    out_timeline->frame_count = frame_count;
    out_timeline->output_end_sample = output_end_sample;
    out_timeline->alignment_pad_samples = output_end_sample - project_end_sample;
    out_timeline->silence_pad_samples = output_end_sample - project_end_sample;
    return ODM_STATUS_OK;
}

odm_status odm_time_resolve_default(int64_t soundtrack_samples,
                                    int64_t authored_post_roll_samples,
                                    int32_t fps,
                                    odm_timeline *out_timeline) {
    int64_t project_end_sample;
    odm_status status;
    status = odm_time_default_project_end(soundtrack_samples,
                                          authored_post_roll_samples,
                                          &project_end_sample);
    if (status != ODM_STATUS_OK) return status;
    status = odm_time_resolve_timeline(project_end_sample, fps, out_timeline);
    if (status != ODM_STATUS_OK) return status;
    out_timeline->soundtrack_end_sample = soundtrack_samples;
    out_timeline->authored_post_roll_samples = authored_post_roll_samples;
    out_timeline->silence_pad_samples =
        out_timeline->output_end_sample - soundtrack_samples;
    return ODM_STATUS_OK;
}

odm_status odm_time_music_map_tick_start(int64_t tick_index,
                                         int64_t *out_sample) {
    if (!out_sample || tick_index < 0) return ODM_STATUS_INVALID_ARGUMENT;
    *out_sample = 0;
    return odm_i64_mul_checked(tick_index, ODM_MUSIC_MAP_TICK_SAMPLES,
                               out_sample);
}

odm_status odm_rational_make(int64_t numerator, int64_t denominator,
                             odm_rational *out_value) {
    uint64_t divisor;
    if (!out_value || denominator <= 0) return ODM_STATUS_INVALID_ARGUMENT;
    divisor = odm_gcd_u64(odm_i64_magnitude(numerator), (uint64_t)denominator);
    if (divisor == 0u) divisor = 1u;
    out_value->numerator = numerator / (int64_t)divisor;
    out_value->denominator = denominator / (int64_t)divisor;
    return ODM_STATUS_OK;
}

odm_status odm_rational_compare(odm_rational left, odm_rational right,
                                int32_t *out_order) {
    int result;
    if (!out_order || left.denominator <= 0 || right.denominator <= 0) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (left.numerator < 0 && right.numerator >= 0) result = -1;
    else if (left.numerator >= 0 && right.numerator < 0) result = 1;
    else {
        result = odm_compare_unsigned_fraction(
            odm_i64_magnitude(left.numerator), (uint64_t)left.denominator,
            odm_i64_magnitude(right.numerator), (uint64_t)right.denominator);
        if (left.numerator < 0) result = -result;
    }
    *out_order = (int32_t)result;
    return ODM_STATUS_OK;
}

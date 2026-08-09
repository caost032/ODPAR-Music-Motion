#ifndef ODM_TIME_H
#define ODM_TIME_H

#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_TIMELINE_RATE            INT64_C(48000)
#define ODM_MUSIC_MAP_RATE           INT64_C(100)
#define ODM_MUSIC_MAP_TICK_SAMPLES   INT64_C(480)
#define ODM_TIMELINE_SCHEMA_VERSION  UINT32_C(1)

typedef int64_t odm_sample;

typedef struct {
    int64_t numerator;
    int64_t denominator;
} odm_rational;

/* Flat, fixed-width timeline result. No pointers, size_t, enums or floats. */
typedef struct {
    uint32_t schema_version;
    int32_t fps;
    int64_t samples_per_frame;
    int64_t soundtrack_end_sample;
    int64_t authored_post_roll_samples;
    int64_t project_end_sample;
    int64_t frame_count;
    int64_t output_end_sample;
    int64_t alignment_pad_samples;
    int64_t silence_pad_samples;
} odm_timeline;

int odm_time_fps_supported(int32_t fps);
odm_status odm_time_samples_per_frame(int32_t fps, int64_t *out_samples);
odm_status odm_time_frame_start(int64_t frame_index, int32_t fps,
                                int64_t *out_sample);
odm_status odm_time_default_project_end(int64_t soundtrack_samples,
                                        int64_t authored_post_roll_samples,
                                        int64_t *out_project_end);
odm_status odm_time_resolve_timeline(int64_t project_end_sample, int32_t fps,
                                     odm_timeline *out_timeline);
odm_status odm_time_resolve_default(int64_t soundtrack_samples,
                                    int64_t authored_post_roll_samples,
                                    int32_t fps,
                                    odm_timeline *out_timeline);
odm_status odm_time_music_map_tick_start(int64_t tick_index,
                                         int64_t *out_sample);

odm_status odm_rational_make(int64_t numerator, int64_t denominator,
                             odm_rational *out_value);
odm_status odm_rational_compare(odm_rational left, odm_rational right,
                                int32_t *out_order);

#ifdef __cplusplus
}
#endif

#endif /* ODM_TIME_H */

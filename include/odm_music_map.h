#ifndef ODM_MUSIC_MAP_H
#define ODM_MUSIC_MAP_H

#include "odm_hash.h"
#include "odm_job.h"
#include "odm_media.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_MUSIC_ANALYSIS_VERSION 1u
#define ODM_MUSIC_POLICY_VERSION 1u
#define ODM_MUSIC_SAMPLE_RATE 48000u
#define ODM_MUSIC_TICK_RATE 100u
#define ODM_MUSIC_TICK_SAMPLES 480u
#define ODM_MUSIC_WINDOW_SAMPLES 2048u
#define ODM_MUSIC_FFT_BINS 1025u
#define ODM_MUSIC_FFT_TWIDDLES 1024u
#define ODM_MUSIC_BAND_COUNT 6u
#define ODM_MUSIC_POLICY_BYTES 192u
#define ODM_MUSIC_TICK_WIRE_BYTES 128u
#define ODM_MUSIC_ANALYSIS_BIN_KIND UINT32_C(0x50414d4d) /* "MMAP" LE */
#define ODM_MUSIC_MAP_SCHEMA_MAJOR 1u
#define ODM_MUSIC_MAP_SCHEMA_MINOR 0u
#define ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES 128u

/* These are bin starts; edge[6] is one-past the last represented bin. */
typedef struct {
    int32_t real;
    int32_t imag;
} odm_music_complex_q31;

typedef struct {
    uint32_t version;
    uint32_t sample_rate;
    uint32_t tick_rate;
    uint32_t tick_samples;
    uint32_t window_samples;
    uint32_t fft_bins;
    uint32_t feature_schema;
    uint32_t reserved;
    uint32_t band_bin_edges[ODM_MUSIC_BAND_COUNT + 1u];
    uint32_t envelope_divisors[3];
    uint32_t silence_rms_threshold_q31;
    uint32_t reserved2;
    odm_sha256_digest window_sha256;
    odm_sha256_digest twiddle_sha256;
    odm_sha256_digest policy_sha256;
} odm_music_analysis_plan;

typedef struct {
    uint32_t initialized;
    uint32_t reserved;
    uint32_t previous_total_energy_q31;
    uint32_t envelope_short_q31;
    uint32_t envelope_medium_q31;
    uint32_t envelope_long_q31;
    uint32_t previous_magnitude_q31[ODM_MUSIC_FFT_BINS];
} odm_music_analysis_state;

typedef struct {
    odm_music_complex_q31 fft[ODM_MUSIC_WINDOW_SAMPLES];
    uint32_t magnitude_q31[ODM_MUSIC_FFT_BINS];
} odm_music_analysis_scratch;


typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t feature_schema;
    uint32_t flags;
    uint64_t canonical_frames;
    uint64_t tick_count;
    odm_sha256_digest canonical_pcm_sha256;
    odm_sha256_digest policy_sha256;
    odm_sha256_digest record_sha256;
} odm_music_map_info;

typedef struct {
    const uint8_t *record;
    uint64_t record_bytes;
    const uint8_t *tick_bytes;
    uint64_t tick_count;
    odm_music_map_info info;
} odm_music_map_view;

typedef struct {
    uint64_t tick_index;
    uint64_t center_sample;
    int32_t rms_left_q31;
    int32_t rms_right_q31;
    int32_t rms_mid_q31;
    int32_t rms_side_q31;
    uint32_t band_energy_q31[ODM_MUSIC_BAND_COUNT];
    uint32_t total_energy_q31;
    uint32_t envelope_short_q31;
    uint32_t envelope_medium_q31;
    uint32_t envelope_long_q31;
    uint32_t spectral_flux_q31;
    uint32_t spectral_centroid_bin_q16_16;
    int64_t energy_delta_q31;
    uint32_t silence;
    int32_t stereo_balance_q31;
    uint32_t stereo_width_q31;
    uint32_t reserved[7];
} odm_music_analysis_tick;

odm_status odm_music_analysis_plan_build(
    int32_t *window_q31,
    uint64_t window_capacity,
    odm_music_complex_q31 *twiddles_q31,
    uint64_t twiddle_capacity,
    odm_music_analysis_plan *out_plan);

odm_status odm_music_analysis_plan_validate(
    const odm_music_analysis_plan *plan,
    const int32_t *window_q31,
    uint64_t window_count,
    const odm_music_complex_q31 *twiddles_q31,
    uint64_t twiddle_count);

odm_status odm_music_policy_bytes(const odm_music_analysis_plan *plan,
                                  uint8_t *buffer,
                                  uint64_t capacity,
                                  uint64_t *out_required);

odm_status odm_music_policy_current_sha256(odm_sha256_digest *out_policy_sha256);

odm_status odm_music_tick_count(uint64_t canonical_frames,
                                uint64_t *out_tick_count);

odm_status odm_music_analysis_state_init(odm_music_analysis_state *state);

odm_status odm_music_analyze_tick(
    const odm_music_analysis_plan *plan,
    const int32_t *window_q31,
    const odm_music_complex_q31 *twiddles_q31,
    const odm_pcm_stereo_q31 *canonical_pcm,
    uint64_t canonical_frames,
    uint64_t tick_index,
    odm_music_analysis_state *state,
    odm_music_analysis_scratch *scratch,
    odm_music_analysis_tick *out_tick);

odm_status odm_music_analyze_map(
    const odm_music_analysis_plan *plan,
    const int32_t *window_q31,
    const odm_music_complex_q31 *twiddles_q31,
    const odm_pcm_stereo_q31 *canonical_pcm,
    uint64_t canonical_frames,
    odm_music_analysis_state *state,
    odm_music_analysis_scratch *scratch,
    odm_music_analysis_tick *ticks,
    uint64_t tick_capacity,
    uint64_t *out_required_ticks,
    const odm_job_ticket *cancel_ticket);

odm_status odm_music_map_record_write(
    const odm_music_analysis_plan *plan,
    uint64_t canonical_frames,
    const odm_sha256_digest *canonical_pcm_sha256,
    const odm_music_analysis_tick *ticks,
    uint64_t tick_count,
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *out_required,
    odm_sha256_digest *out_record_sha256);

odm_status odm_music_map_record_validate(
    const uint8_t *record,
    uint64_t record_bytes,
    const odm_sha256_digest *expected_policy_sha256,
    odm_music_map_info *out_info,
    uint64_t *out_required_ticks);

/* Validate analysis.bin once and expose a borrow-only, random-access tick view.
 * The caller must keep record unchanged and alive for the view lifetime. */
odm_status odm_music_map_view_open(
    const uint8_t *record,
    uint64_t record_bytes,
    const odm_sha256_digest *expected_policy_sha256,
    odm_music_map_view *out_view);

odm_status odm_music_map_view_read_tick(
    const odm_music_map_view *view,
    uint64_t tick_index,
    odm_music_analysis_tick *out_tick);

odm_status odm_music_map_record_read(
    const uint8_t *record,
    uint64_t record_bytes,
    const odm_sha256_digest *expected_policy_sha256,
    odm_music_map_info *out_info,
    odm_music_analysis_tick *ticks,
    uint64_t tick_capacity,
    uint64_t *out_required_ticks);

#ifdef __cplusplus
}
#endif

#endif /* ODM_MUSIC_MAP_H */

#ifndef ODM_RESAMPLE_H
#define ODM_RESAMPLE_H

#include "odm_hash.h"
#include "odm_job.h"
#include "odm_media.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_RESAMPLE_CANONICAL_RATE 48000u
#define ODM_RESAMPLE_PHASES 1024u
#define ODM_RESAMPLE_BASE_TAPS 64u
#define ODM_RESAMPLE_MAX_TAPS 512u
#define ODM_RESAMPLE_COEFF_Q 30u
#define ODM_RESAMPLE_PLAN_VERSION 1u
#define ODM_RESAMPLE_FLAG_BYPASS 1u

typedef struct {
    uint32_t version;
    uint32_t input_rate;
    uint32_t output_rate;
    uint32_t phases;
    uint32_t taps;
    uint32_t coefficient_q;
    uint32_t flags;
    uint32_t reserved;
    uint64_t coefficient_count;
    odm_sha256_digest table_sha256;
} odm_resample_plan;

odm_status odm_resample_plan_requirements(uint32_t input_rate,
                                          uint32_t *out_taps,
                                          uint64_t *out_coefficients);
odm_status odm_resample_plan_build(uint32_t input_rate,
                                   int32_t *coefficients,
                                   uint64_t coefficient_capacity,
                                   uint64_t *out_required_coefficients,
                                   odm_resample_plan *out_plan);
odm_status odm_resample_plan_validate(const odm_resample_plan *plan,
                                      const int32_t *coefficients,
                                      uint64_t coefficient_count);
odm_status odm_resample_output_frames(uint64_t source_frames,
                                      uint32_t input_rate,
                                      uint64_t *out_frames);
odm_status odm_resample_q31(const odm_resample_plan *plan,
                            const int32_t *coefficients,
                            uint64_t coefficient_count,
                            const odm_pcm_stereo_q31 *source,
                            uint64_t source_frames,
                            odm_pcm_stereo_q31 *output,
                            uint64_t output_capacity,
                            uint64_t *out_required_frames,
                            const odm_job_ticket *cancel_ticket,
                            uint64_t *out_saturation_count);

#ifdef __cplusplus
}
#endif

#endif /* ODM_RESAMPLE_H */

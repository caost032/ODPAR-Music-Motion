#ifndef ODM_FIXED_H
#define ODM_FIXED_H

#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t odm_q1_31;
typedef int64_t odm_q32_32;
typedef int64_t odm_microunit;
typedef uint32_t odm_phase_u32;

#define ODM_MICRO_ONE        INT64_C(1000000)
#define ODM_Q32_ONE          INT64_C(4294967296)
#define ODM_FIXED_ROUNDING   "nearest_ties_away_from_zero"

odm_status odm_i64_add_checked(int64_t left, int64_t right, int64_t *out_value);
odm_status odm_i64_sub_checked(int64_t left, int64_t right, int64_t *out_value);
odm_status odm_i64_mul_checked(int64_t left, int64_t right, int64_t *out_value);

odm_status odm_q1_31_from_ratio(int64_t numerator, int64_t denominator,
                                odm_q1_31 *out_value);
odm_status odm_q1_31_mul(odm_q1_31 left, odm_q1_31 right,
                         odm_q1_31 *out_value);

odm_status odm_q32_32_from_microunits(odm_microunit value,
                                      odm_q32_32 *out_value);
odm_status odm_q32_32_to_microunits(odm_q32_32 value,
                                    odm_microunit *out_value);
odm_status odm_q32_32_add(odm_q32_32 left, odm_q32_32 right,
                          odm_q32_32 *out_value);
odm_status odm_q32_32_sub(odm_q32_32 left, odm_q32_32 right,
                          odm_q32_32 *out_value);
odm_status odm_q32_32_mul(odm_q32_32 left, odm_q32_32 right,
                          odm_q32_32 *out_value);

odm_phase_u32 odm_phase_add(odm_phase_u32 left, odm_phase_u32 right);
odm_status odm_phase_from_microturns(odm_microunit microturns,
                                     odm_phase_u32 *out_phase);

#ifdef __cplusplus
}
#endif

#endif /* ODM_FIXED_H */

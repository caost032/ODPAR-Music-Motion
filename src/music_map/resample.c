#include "odm_resample.h"
#include "odm_fixed.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define RESAMPLE_PI_Q30 UINT64_C(3373259426)
#define RESAMPLE_CORDIC_K_Q31 INT64_C(1304065748)
#define RESAMPLE_QUARTER_PHASE UINT32_C(0x40000000)
#define RESAMPLE_Q30_ONE INT64_C(1073741824)

typedef struct {
    uint64_t hi;
    uint64_t lo;
} rs_u128;

typedef struct {
    rs_u128 positive;
    rs_u128 negative;
} rs_sacc;

static const uint32_t rs_cordic_atan_phase[31] = {
    536870912u, 316933406u, 167458907u, 85004756u, 42667331u,
    21354465u, 10679838u, 5340245u, 2670163u, 1335087u, 667544u,
    333772u, 166886u, 83443u, 41722u, 20861u, 10430u, 5215u, 2608u,
    1304u, 652u, 326u, 163u, 81u, 41u, 20u, 10u, 5u, 3u, 1u, 1u
};

static uint64_t rs_i64_mag(int64_t value) {
    if (value >= 0) return (uint64_t)value;
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static rs_u128 rs_mul_u64(uint64_t left, uint64_t right) {
    const uint64_t mask = UINT64_C(0xffffffff);
    uint64_t left_low = left & mask;
    uint64_t left_high = left >> 32;
    uint64_t right_low = right & mask;
    uint64_t right_high = right >> 32;
    uint64_t t = left_low * right_low;
    uint64_t word0 = t & mask;
    uint64_t carry = t >> 32;
    uint64_t word1;
    uint64_t word2;
    rs_u128 result;

    t = left_high * right_low + carry;
    word1 = t & mask;
    word2 = t >> 32;
    t = left_low * right_high + word1;
    carry = t >> 32;
    result.lo = (t << 32) + word0;
    result.hi = left_high * right_high + word2 + carry;
    return result;
}

static int rs_cmp(rs_u128 left, rs_u128 right) {
    if (left.hi != right.hi) return left.hi > right.hi ? 1 : -1;
    if (left.lo != right.lo) return left.lo > right.lo ? 1 : -1;
    return 0;
}

static rs_u128 rs_sub(rs_u128 left, rs_u128 right) {
    rs_u128 result;
    result.lo = left.lo - right.lo;
    result.hi = left.hi - right.hi - (left.lo < right.lo ? UINT64_C(1) : UINT64_C(0));
    return result;
}

static int rs_shl1_add(rs_u128 *value, uint32_t bit) {
    uint64_t carry;
    if (!value || bit > 1u) return 0;
    carry = value->lo >> 63;
    if ((value->hi >> 63) != 0u) return 0;
    value->hi = (value->hi << 1) | carry;
    value->lo = (value->lo << 1) | (uint64_t)bit;
    return 1;
}

static uint32_t rs_bit(rs_u128 value, unsigned bit) {
    if (bit >= 64u) return (uint32_t)((value.hi >> (bit - 64u)) & UINT64_C(1));
    return (uint32_t)((value.lo >> bit) & UINT64_C(1));
}

static odm_status rs_div_round_u128(rs_u128 numerator, rs_u128 denominator,
                                    uint64_t *out_quotient) {
    rs_u128 remainder = {UINT64_C(0), UINT64_C(0)};
    uint64_t quotient = UINT64_C(0);
    int bit;

    if (!out_quotient ||
        (denominator.hi == UINT64_C(0) && denominator.lo == UINT64_C(0))) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }

    for (bit = 127; bit >= 0; --bit) {
        if (!rs_shl1_add(&remainder, rs_bit(numerator, (unsigned)bit))) {
            return ODM_STATUS_OVERFLOW;
        }
        if (rs_cmp(remainder, denominator) >= 0) {
            remainder = rs_sub(remainder, denominator);
            if (bit >= 64) return ODM_STATUS_OVERFLOW;
            quotient |= UINT64_C(1) << (unsigned)bit;
        }
    }

    {
        rs_u128 twice = remainder;
        int round_up;
        if (rs_shl1_add(&twice, 0u)) round_up = rs_cmp(twice, denominator) >= 0;
        else round_up = 1;
        if (round_up) {
            if (quotient == UINT64_MAX) return ODM_STATUS_OVERFLOW;
            quotient++;
        }
    }

    *out_quotient = quotient;
    return ODM_STATUS_OK;
}

static void rs_add_u64(rs_u128 *value, uint64_t addend) {
    uint64_t old_low = value->lo;
    value->lo += addend;
    if (value->lo < old_low) value->hi++;
}

static void rs_acc_add(rs_sacc *accumulator, int64_t product) {
    if (product < 0) rs_add_u64(&accumulator->negative, rs_i64_mag(product));
    else rs_add_u64(&accumulator->positive, (uint64_t)product);
}

static odm_status rs_acc_to_q31(rs_sacc accumulator, int32_t *out_value,
                                uint64_t *saturation_count) {
    rs_u128 magnitude;
    int negative;
    uint64_t shifted;
    uint64_t remainder;
    int comparison;

    if (!out_value || !saturation_count) return ODM_STATUS_INVALID_ARGUMENT;
    comparison = rs_cmp(accumulator.positive, accumulator.negative);
    if (comparison == 0) {
        *out_value = 0;
        return ODM_STATUS_OK;
    }

    negative = comparison < 0;
    magnitude = negative ? rs_sub(accumulator.negative, accumulator.positive)
                         : rs_sub(accumulator.positive, accumulator.negative);

    if ((magnitude.hi >> 30) != 0u) {
        (*saturation_count)++;
        *out_value = negative ? INT32_MIN : INT32_MAX;
        return ODM_STATUS_OK;
    }

    shifted = (magnitude.hi << 34) | (magnitude.lo >> 30);
    remainder = magnitude.lo & ((UINT64_C(1) << 30) - UINT64_C(1));
    if (remainder >= (UINT64_C(1) << 29) && shifted != UINT64_MAX) shifted++;

    if (negative) {
        if (shifted >= (UINT64_C(1) << 31)) {
            if (shifted > (UINT64_C(1) << 31)) (*saturation_count)++;
            *out_value = INT32_MIN;
        } else {
            *out_value = -(int32_t)shifted;
        }
    } else if (shifted > (uint64_t)INT32_MAX) {
        (*saturation_count)++;
        *out_value = INT32_MAX;
    } else {
        *out_value = (int32_t)shifted;
    }
    return ODM_STATUS_OK;
}

static int32_t rs_cordic_sin_q31(uint32_t phase) {
    uint32_t quadrant = phase >> 30;
    uint32_t offset = phase & UINT32_C(0x3fffffff);
    int sign = quadrant >= 2u ? -1 : 1;
    int64_t z = (quadrant == 0u || quadrant == 2u)
                    ? (int64_t)offset
                    : (int64_t)(RESAMPLE_QUARTER_PHASE - offset);
    int64_t x = RESAMPLE_CORDIC_K_Q31;
    int64_t y = INT64_C(0);
    unsigned i;

    for (i = 0u; i < 31u; i++) {
        int64_t divisor = INT64_C(1) << i;
        int64_t x_shift = x / divisor;
        int64_t y_shift = y / divisor;
        int64_t next_x;
        int64_t next_y;
        if (z >= 0) {
            next_x = x - y_shift;
            next_y = y + x_shift;
            z -= (int64_t)rs_cordic_atan_phase[i];
        } else {
            next_x = x + y_shift;
            next_y = y - x_shift;
            z += (int64_t)rs_cordic_atan_phase[i];
        }
        x = next_x;
        y = next_y;
    }

    if (sign < 0) y = -y;
    if (y > INT32_MAX) y = INT32_MAX;
    if (y < INT32_MIN) y = INT32_MIN;
    return (int32_t)y;
}

static int64_t rs_div_round_i64(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;
    uint64_t remainder_magnitude = rs_i64_mag(remainder);
    uint64_t denominator_magnitude = rs_i64_mag(denominator);

    if (remainder_magnitude != 0u &&
        remainder_magnitude >= denominator_magnitude - remainder_magnitude) {
        quotient += ((numerator < 0) != (denominator < 0)) ? -1 : 1;
    }
    return quotient;
}

static int32_t rs_sinc_q31(int64_t x_q32) {
    uint32_t phase;
    int32_t sine;
    rs_u128 numerator;
    rs_u128 denominator;
    uint64_t quotient;
    int negative;

    if (x_q32 == 0) return INT32_MAX;

    phase = (uint32_t)(((uint64_t)x_q32) >> 1);
    sine = rs_cordic_sin_q31(phase);
    if (sine == 0) return 0;

    negative = (sine < 0) != (x_q32 < 0);
    numerator = rs_mul_u64(rs_i64_mag((int64_t)sine), UINT64_C(1) << 62);
    denominator = rs_mul_u64(rs_i64_mag(x_q32), RESAMPLE_PI_Q30);
    if (rs_div_round_u128(numerator, denominator, &quotient) != ODM_STATUS_OK) {
        return 0;
    }

    if (quotient > UINT64_C(0x80000000)) quotient = UINT64_C(0x80000000);
    if (negative) {
        if (quotient >= UINT64_C(0x80000000)) return INT32_MIN;
        return -(int32_t)quotient;
    }
    if (quotient > (uint64_t)INT32_MAX) quotient = (uint64_t)INT32_MAX;
    return (int32_t)quotient;
}

static odm_status rs_coeff_raw(uint32_t input_rate, uint32_t taps,
                               uint32_t phase, uint32_t tap,
                               int32_t *out_coefficient) {
    int64_t center = (int64_t)(taps / 2u - 1u);
    int64_t fractional = (int64_t)phase << 22;
    int64_t x = ((int64_t)tap - center) * ODM_Q32_ONE - fractional;
    int64_t window_x = rs_div_round_i64(x, (int64_t)(taps / 2u));
    int64_t cutoff_numerator;
    int64_t cutoff_denominator;
    odm_q1_31 cutoff;
    odm_q1_31 sinc_filter;
    odm_q1_31 sinc_window;
    odm_q1_31 product;
    odm_q32_32 scaled_x;
    odm_status status;

    if (!out_coefficient) return ODM_STATUS_INVALID_ARGUMENT;

    if (input_rate <= ODM_RESAMPLE_CANONICAL_RATE) {
        cutoff_numerator = INT64_C(23);
        cutoff_denominator = INT64_C(25);
    } else {
        cutoff_numerator = INT64_C(23) * (int64_t)ODM_RESAMPLE_CANONICAL_RATE;
        cutoff_denominator = INT64_C(25) * (int64_t)input_rate;
    }

    status = odm_q1_31_from_ratio(cutoff_numerator, cutoff_denominator, &cutoff);
    if (status != ODM_STATUS_OK) return status;

    /* Q1.31 -> Q32.32 is an exact left shift by one bit. */
    status = odm_q32_32_mul((odm_q32_32)x,
                            (odm_q32_32)((int64_t)cutoff * INT64_C(2)),
                            &scaled_x);
    if (status != ODM_STATUS_OK) return status;

    sinc_filter = rs_sinc_q31(scaled_x);
    sinc_window = rs_sinc_q31(window_x);
    status = odm_q1_31_mul(cutoff, sinc_filter, &product);
    if (status != ODM_STATUS_OK) return status;
    return odm_q1_31_mul(product, sinc_window, out_coefficient);
}

static odm_status rs_table_hash(const int32_t *coefficients, uint64_t count,
                                odm_sha256_digest *out_digest) {
    odm_sha256_context hash_context = ODM_SHA256_CONTEXT_INITIALIZER;
    uint64_t i;
    odm_status status;

    if ((!coefficients && count != 0u) || !out_digest) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_sha256_init(&hash_context);
    if (status != ODM_STATUS_OK) return status;
    for (i = 0u; i < count; i++) {
        uint32_t value = (uint32_t)coefficients[i];
        uint8_t encoded[4] = {
            (uint8_t)value,
            (uint8_t)(value >> 8),
            (uint8_t)(value >> 16),
            (uint8_t)(value >> 24)
        };
        status = odm_sha256_update(&hash_context, encoded, sizeof(encoded));
        if (status != ODM_STATUS_OK) return status;
    }
    return odm_sha256_final(&hash_context, out_digest);
}

odm_status odm_resample_plan_requirements(uint32_t input_rate,
                                          uint32_t *out_taps,
                                          uint64_t *out_coefficients) {
    uint32_t factor;
    uint32_t taps;

    if (!out_taps || !out_coefficients || input_rate == 0u ||
        input_rate > ODM_MEDIA_MAX_SAMPLE_RATE) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (input_rate == ODM_RESAMPLE_CANONICAL_RATE) {
        *out_taps = 0u;
        *out_coefficients = UINT64_C(0);
        return ODM_STATUS_OK;
    }

    factor = (input_rate + ODM_RESAMPLE_CANONICAL_RATE - 1u) /
             ODM_RESAMPLE_CANONICAL_RATE;
    if (factor == 0u) factor = 1u;
    if (factor > ODM_RESAMPLE_MAX_TAPS / ODM_RESAMPLE_BASE_TAPS) {
        return ODM_STATUS_UNSUPPORTED;
    }

    taps = ODM_RESAMPLE_BASE_TAPS * factor;
    *out_taps = taps;
    *out_coefficients = (uint64_t)taps * (uint64_t)ODM_RESAMPLE_PHASES;
    return ODM_STATUS_OK;
}

odm_status odm_resample_plan_build(uint32_t input_rate,
                                   int32_t *coefficients,
                                   uint64_t coefficient_capacity,
                                   uint64_t *out_required_coefficients,
                                   odm_resample_plan *out_plan) {
    uint32_t taps;
    uint32_t phase;
    uint32_t tap;
    uint64_t required;
    odm_resample_plan plan;
    odm_status status;

    if (!out_required_coefficients || !out_plan) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    memset(&plan, 0, sizeof(plan));

    status = odm_resample_plan_requirements(input_rate, &taps, &required);
    if (status != ODM_STATUS_OK) return status;
    *out_required_coefficients = required;
    if (required != 0u && (!coefficients || coefficient_capacity < required)) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }

    plan.version = ODM_RESAMPLE_PLAN_VERSION;
    plan.input_rate = input_rate;
    plan.output_rate = ODM_RESAMPLE_CANONICAL_RATE;
    plan.phases = ODM_RESAMPLE_PHASES;
    plan.taps = taps;
    plan.coefficient_q = ODM_RESAMPLE_COEFF_Q;
    plan.coefficient_count = required;

    if (required == 0u) {
        plan.flags = ODM_RESAMPLE_FLAG_BYPASS;
        status = odm_sha256(NULL, 0u, &plan.table_sha256);
        if (status != ODM_STATUS_OK) return status;
        *out_plan = plan;
        return ODM_STATUS_OK;
    }

    for (phase = 0u; phase < ODM_RESAMPLE_PHASES; phase++) {
        int64_t sum = INT64_C(0);
        int64_t normalized_sum = INT64_C(0);
        uint32_t center = taps / 2u - 1u;

        for (tap = 0u; tap < taps; tap++) {
            int32_t raw;
            uint64_t index = (uint64_t)phase * (uint64_t)taps + (uint64_t)tap;
            status = rs_coeff_raw(input_rate, taps, phase, tap, &raw);
            if (status != ODM_STATUS_OK) return status;
            coefficients[index] = raw;
            sum += (int64_t)raw;
        }
        if (sum <= 0) return ODM_STATUS_INVARIANT_BROKEN;

        for (tap = 0u; tap < taps; tap++) {
            uint64_t index = (uint64_t)phase * (uint64_t)taps + (uint64_t)tap;
            int64_t numerator = (int64_t)coefficients[index] * RESAMPLE_Q30_ONE;
            int64_t normalized = rs_div_round_i64(numerator, sum);
            if (normalized < INT32_MIN || normalized > INT32_MAX) {
                return ODM_STATUS_OVERFLOW;
            }
            coefficients[index] = (int32_t)normalized;
            normalized_sum += normalized;
        }

        {
            int64_t delta = RESAMPLE_Q30_ONE - normalized_sum;
            uint64_t center_index = (uint64_t)phase * (uint64_t)taps +
                                    (uint64_t)center;
            int64_t corrected = (int64_t)coefficients[center_index] + delta;
            if (corrected < INT32_MIN || corrected > INT32_MAX) {
                return ODM_STATUS_OVERFLOW;
            }
            coefficients[center_index] = (int32_t)corrected;
        }
    }

    status = rs_table_hash(coefficients, required, &plan.table_sha256);
    if (status != ODM_STATUS_OK) return status;
    *out_plan = plan;
    return ODM_STATUS_OK;
}

odm_status odm_resample_plan_validate(const odm_resample_plan *plan,
                                      const int32_t *coefficients,
                                      uint64_t coefficient_count) {
    uint32_t taps;
    uint64_t required;
    odm_sha256_digest digest;
    odm_status status;

    if (!plan) return ODM_STATUS_INVALID_ARGUMENT;
    if (plan->version != ODM_RESAMPLE_PLAN_VERSION ||
        plan->output_rate != ODM_RESAMPLE_CANONICAL_RATE ||
        plan->phases != ODM_RESAMPLE_PHASES ||
        plan->coefficient_q != ODM_RESAMPLE_COEFF_Q ||
        plan->reserved != 0u) {
        return ODM_STATUS_VERSION_MISMATCH;
    }

    status = odm_resample_plan_requirements(plan->input_rate, &taps, &required);
    if (status != ODM_STATUS_OK) return status;
    if (plan->taps != taps || plan->coefficient_count != required ||
        coefficient_count != required) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }

    if (required == 0u) {
        if (plan->flags != ODM_RESAMPLE_FLAG_BYPASS || coefficients != NULL) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        status = odm_sha256(NULL, 0u, &digest);
    } else {
        if (plan->flags != 0u || !coefficients) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        status = rs_table_hash(coefficients, coefficient_count, &digest);
    }
    if (status != ODM_STATUS_OK) return status;
    return odm_sha256_equal(&digest, &plan->table_sha256)
               ? ODM_STATUS_OK
               : ODM_STATUS_INTEGRITY_ERROR;
}

odm_status odm_resample_output_frames(uint64_t source_frames,
                                      uint32_t input_rate,
                                      uint64_t *out_frames) {
    return odm_media_canonical_48k_frames(source_frames, input_rate, out_frames);
}

odm_status odm_resample_q31(const odm_resample_plan *plan,
                            const int32_t *coefficients,
                            uint64_t coefficient_count,
                            const odm_pcm_stereo_q31 *source,
                            uint64_t source_frames,
                            odm_pcm_stereo_q31 *output,
                            uint64_t output_capacity,
                            uint64_t *out_required_frames,
                            const odm_job_ticket *cancel_ticket,
                            uint64_t *out_saturation_count) {
    uint64_t required;
    uint64_t output_index;
    uint64_t saturation_count = UINT64_C(0);
    uint64_t source_base = UINT64_C(0);
    uint64_t remainder = UINT64_C(0);
    odm_status status;

    if (!plan || !out_required_frames || !out_saturation_count ||
        (!source && source_frames != 0u)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    *out_required_frames = UINT64_C(0);
    *out_saturation_count = UINT64_C(0);

    status = odm_resample_plan_validate(plan, coefficients, coefficient_count);
    if (status != ODM_STATUS_OK) return status;
    status = odm_resample_output_frames(source_frames, plan->input_rate, &required);
    if (status != ODM_STATUS_OK) return status;
    *out_required_frames = required;

    if (cancel_ticket) {
        int cancelled = 0;
        status = odm_job_should_cancel(*cancel_ticket, &cancelled);
        if (status != ODM_STATUS_OK) return status;
        if (cancelled) return ODM_STATUS_CANCELLED;
    }

    if (required == 0u) return ODM_STATUS_OK;
    if (!output || output_capacity < required) return ODM_STATUS_BUFFER_TOO_SMALL;
#if SIZE_MAX < UINT64_MAX
    if (required > (uint64_t)(SIZE_MAX / sizeof(*output))) {
        return ODM_STATUS_OVERFLOW;
    }
#endif

    if ((plan->flags & ODM_RESAMPLE_FLAG_BYPASS) != 0u) {
        if (required != source_frames) return ODM_STATUS_INVARIANT_BROKEN;
        memmove(output, source, (size_t)required * sizeof(*output));
        return ODM_STATUS_OK;
    }

    for (output_index = 0u; output_index < required; output_index++) {
        uint64_t phase_numerator =
            remainder * (uint64_t)ODM_RESAMPLE_PHASES +
            (uint64_t)ODM_RESAMPLE_CANONICAL_RATE / UINT64_C(2);
        uint32_t phase = (uint32_t)(phase_numerator /
                                    (uint64_t)ODM_RESAMPLE_CANONICAL_RATE);
        uint32_t tap;
        uint64_t sample_base = source_base;
        int64_t center = (int64_t)(plan->taps / 2u - 1u);
        rs_sacc left_accumulator = {
            {UINT64_C(0), UINT64_C(0)}, {UINT64_C(0), UINT64_C(0)}};
        rs_sacc right_accumulator = {
            {UINT64_C(0), UINT64_C(0)}, {UINT64_C(0), UINT64_C(0)}};

        if (phase == ODM_RESAMPLE_PHASES) {
            phase = 0u;
            if (sample_base == UINT64_MAX) return ODM_STATUS_OVERFLOW;
            sample_base++;
        }

        if (cancel_ticket && (output_index & UINT64_C(0xff)) == 0u) {
            int cancelled = 0;
            status = odm_job_should_cancel(*cancel_ticket, &cancelled);
            if (status != ODM_STATUS_OK) return status;
            if (cancelled) return ODM_STATUS_CANCELLED;
        }

        for (tap = 0u; tap < plan->taps; tap++) {
            int64_t delta = (int64_t)tap - center;
            int valid = 0;
            uint64_t source_index = UINT64_C(0);
            uint64_t coefficient_index =
                (uint64_t)phase * (uint64_t)plan->taps + (uint64_t)tap;
            int32_t coefficient = coefficients[coefficient_index];

            if (delta < 0) {
                uint64_t magnitude = rs_i64_mag(delta);
                if (sample_base >= magnitude) {
                    source_index = sample_base - magnitude;
                    valid = source_index < source_frames;
                }
            } else {
                uint64_t addend = (uint64_t)delta;
                if (addend <= UINT64_MAX - sample_base) {
                    source_index = sample_base + addend;
                    valid = source_index < source_frames;
                }
            }

            if (valid) {
                int64_t left_product = (int64_t)source[source_index].left *
                                       (int64_t)coefficient;
                int64_t right_product = (int64_t)source[source_index].right *
                                        (int64_t)coefficient;
                rs_acc_add(&left_accumulator, left_product);
                rs_acc_add(&right_accumulator, right_product);
            }
        }

        status = rs_acc_to_q31(left_accumulator, &output[output_index].left,
                               &saturation_count);
        if (status != ODM_STATUS_OK) return status;
        status = rs_acc_to_q31(right_accumulator, &output[output_index].right,
                               &saturation_count);
        if (status != ODM_STATUS_OK) return status;

        remainder += (uint64_t)plan->input_rate;
        source_base += remainder / (uint64_t)ODM_RESAMPLE_CANONICAL_RATE;
        remainder %= (uint64_t)ODM_RESAMPLE_CANONICAL_RATE;
    }

    *out_saturation_count = saturation_count;
    return ODM_STATUS_OK;
}

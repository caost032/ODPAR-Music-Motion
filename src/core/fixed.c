#include "odm_fixed.h"

#include <limits.h>

typedef struct {
    uint64_t high;
    uint64_t low;
} odm_u128;

static uint64_t odm_i64_magnitude(int64_t value) {
    if (value >= 0) return (uint64_t)value;
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static odm_u128 odm_u64_multiply_full(uint64_t left, uint64_t right) {
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
    odm_u128 result;

    t = left_high * right_low + carry;
    word1 = t & mask;
    word2 = t >> 32;
    t = left_low * right_high + word1;
    carry = t >> 32;
    result.low = (t << 32) + word0;
    result.high = left_high * right_high + word2 + carry;
    return result;
}

static odm_status odm_signed_from_magnitude(uint64_t magnitude, int negative,
                                            int64_t *out_value) {
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    if (!negative) {
        if (magnitude > (uint64_t)INT64_MAX) return ODM_STATUS_OVERFLOW;
        *out_value = (int64_t)magnitude;
    } else {
        const uint64_t negative_limit = UINT64_C(1) << 63;
        if (magnitude > negative_limit) return ODM_STATUS_OVERFLOW;
        if (magnitude == negative_limit) *out_value = INT64_MIN;
        else *out_value = -(int64_t)magnitude;
    }
    return ODM_STATUS_OK;
}

odm_status odm_i64_add_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_value = left + right;
    return ODM_STATUS_OK;
}

odm_status odm_i64_sub_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    if ((right < 0 && left > INT64_MAX + right) ||
        (right > 0 && left < INT64_MIN + right)) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_value = left - right;
    return ODM_STATUS_OK;
}

odm_status odm_i64_mul_checked(int64_t left, int64_t right, int64_t *out_value) {
    uint64_t product_magnitude;
    odm_u128 product;
    int negative;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    negative = (left < 0) != (right < 0);
    product = odm_u64_multiply_full(odm_i64_magnitude(left),
                                    odm_i64_magnitude(right));
    if (product.high != 0u) return ODM_STATUS_OVERFLOW;
    product_magnitude = product.low;
    return odm_signed_from_magnitude(product_magnitude, negative, out_value);
}

/* Exact binary long division for magnitude/denominator scaled by 2^31. */
static uint32_t odm_fraction_to_q31(uint64_t magnitude, uint64_t denominator,
                                    int *round_up) {
    uint32_t quotient = 0u;
    uint64_t remainder = magnitude;
    unsigned bit;
    for (bit = 0u; bit < 31u; bit++) {
        quotient <<= 1;
        if (remainder >= denominator - remainder) {
            quotient |= 1u;
            remainder -= denominator - remainder;
        } else {
            remainder += remainder;
        }
    }
    *round_up = remainder != 0u && remainder >= denominator - remainder;
    return quotient;
}

odm_status odm_q1_31_from_ratio(int64_t numerator, int64_t denominator,
                                odm_q1_31 *out_value) {
    uint64_t magnitude;
    uint32_t encoded;
    int round_up = 0;
    int negative;
    if (!out_value || denominator <= 0) return ODM_STATUS_INVALID_ARGUMENT;
    magnitude = odm_i64_magnitude(numerator);
    if (magnitude > (uint64_t)denominator) return ODM_STATUS_INVALID_ARGUMENT;
    negative = numerator < 0;
    if (magnitude == (uint64_t)denominator) {
        *out_value = negative ? INT32_MIN : INT32_MAX;
        return ODM_STATUS_OK;
    }
    encoded = odm_fraction_to_q31(magnitude, (uint64_t)denominator, &round_up);
    if (round_up) encoded++;
    if (negative) {
        if (encoded == (UINT32_C(1) << 31)) *out_value = INT32_MIN;
        else *out_value = -(int32_t)encoded;
    } else {
        if (encoded > (uint32_t)INT32_MAX) encoded = (uint32_t)INT32_MAX;
        *out_value = (int32_t)encoded;
    }
    return ODM_STATUS_OK;
}

odm_status odm_q1_31_mul(odm_q1_31 left, odm_q1_31 right,
                         odm_q1_31 *out_value) {
    int64_t product;
    uint64_t magnitude;
    uint64_t rounded;
    int negative;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    product = (int64_t)left * (int64_t)right;
    negative = product < 0;
    magnitude = odm_i64_magnitude(product);
    rounded = (magnitude >> 31) +
              (((magnitude & UINT64_C(0x7fffffff)) >= UINT64_C(0x40000000)) ? 1u : 0u);
    if (!negative && rounded > (uint64_t)INT32_MAX) rounded = (uint64_t)INT32_MAX;
    if (negative && rounded >= (UINT64_C(1) << 31)) *out_value = INT32_MIN;
    else *out_value = negative ? -(int32_t)rounded : (int32_t)rounded;
    return ODM_STATUS_OK;
}

static int64_t odm_round_signed_division(int64_t numerator, int64_t denominator) {
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;
    uint64_t remainder_magnitude = odm_i64_magnitude(remainder);
    uint64_t denominator_magnitude = (uint64_t)denominator;
    if (remainder_magnitude != 0u &&
        remainder_magnitude >= denominator_magnitude - remainder_magnitude) {
        quotient += numerator < 0 ? -1 : 1;
    }
    return quotient;
}

odm_status odm_q32_32_from_microunits(odm_microunit value,
                                      odm_q32_32 *out_value) {
    int64_t whole = value / ODM_MICRO_ONE;
    int64_t remainder = value % ODM_MICRO_ONE;
    int64_t whole_encoded;
    int64_t fractional_encoded;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_i64_mul_checked(whole, ODM_Q32_ONE, &whole_encoded);
    if (status != ODM_STATUS_OK) return status;
    fractional_encoded = odm_round_signed_division(remainder * ODM_Q32_ONE,
                                                    ODM_MICRO_ONE);
    return odm_i64_add_checked(whole_encoded, fractional_encoded, out_value);
}

odm_status odm_q32_32_to_microunits(odm_q32_32 value,
                                    odm_microunit *out_value) {
    int64_t whole = value / ODM_Q32_ONE;
    int64_t remainder = value % ODM_Q32_ONE;
    int64_t whole_micro;
    int64_t fractional_micro;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_i64_mul_checked(whole, ODM_MICRO_ONE, &whole_micro);
    if (status != ODM_STATUS_OK) return status;
    fractional_micro = odm_round_signed_division(remainder * ODM_MICRO_ONE,
                                                  ODM_Q32_ONE);
    return odm_i64_add_checked(whole_micro, fractional_micro, out_value);
}

odm_status odm_q32_32_add(odm_q32_32 left, odm_q32_32 right,
                          odm_q32_32 *out_value) {
    return odm_i64_add_checked(left, right, out_value);
}

odm_status odm_q32_32_sub(odm_q32_32 left, odm_q32_32 right,
                          odm_q32_32 *out_value) {
    return odm_i64_sub_checked(left, right, out_value);
}

odm_status odm_q32_32_mul(odm_q32_32 left, odm_q32_32 right,
                          odm_q32_32 *out_value) {
    odm_u128 product;
    uint64_t magnitude;
    uint64_t shifted;
    uint64_t remainder;
    uint64_t limit;
    int negative;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    negative = (left < 0) != (right < 0);
    product = odm_u64_multiply_full(odm_i64_magnitude(left),
                                    odm_i64_magnitude(right));
    if ((product.high >> 32) != 0u) return ODM_STATUS_OVERFLOW;
    shifted = (product.high << 32) | (product.low >> 32);
    remainder = product.low & UINT64_C(0xffffffff);
    if (remainder >= UINT64_C(0x80000000)) {
        if (shifted == UINT64_MAX) return ODM_STATUS_OVERFLOW;
        shifted++;
    }
    limit = negative ? (UINT64_C(1) << 63) : (uint64_t)INT64_MAX;
    if (shifted > limit) return ODM_STATUS_OVERFLOW;
    magnitude = shifted;
    return odm_signed_from_magnitude(magnitude, negative, out_value);
}

odm_phase_u32 odm_phase_add(odm_phase_u32 left, odm_phase_u32 right) {
    return left + right;
}

odm_status odm_phase_from_microturns(odm_microunit microturns,
                                     odm_phase_u32 *out_phase) {
    int64_t normalized;
    uint64_t numerator;
    uint64_t encoded;
    uint64_t remainder;
    if (!out_phase) return ODM_STATUS_INVALID_ARGUMENT;
    normalized = microturns % ODM_MICRO_ONE;
    if (normalized < 0) normalized += ODM_MICRO_ONE;
    numerator = (uint64_t)normalized * (UINT64_C(1) << 32);
    encoded = numerator / (uint64_t)ODM_MICRO_ONE;
    remainder = numerator % (uint64_t)ODM_MICRO_ONE;
    if (remainder != 0u && remainder >= (uint64_t)ODM_MICRO_ONE - remainder) {
        encoded++;
    }
    *out_phase = (uint32_t)encoded;
    return ODM_STATUS_OK;
}

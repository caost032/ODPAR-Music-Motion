#include "compositor_internal.h"

#include <limits.h>

uint64_t odm_layered_isqrt_u64(uint64_t x) {
    uint64_t res = 0u;
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > x) bit >>= 2;
    while (bit != 0u) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

uint32_t odm_layered_coverage_q31(int64_t distance_q16, int64_t half_width_q16,
                                  int64_t feather_q16) {
    int64_t d, span, num;
    if (distance_q16 < 0) distance_q16 = -distance_q16;
    if (half_width_q16 < 0) half_width_q16 = 0;
    if (feather_q16 <= 0) return distance_q16 <= half_width_q16 ? (uint32_t)INT32_MAX : 0u;
    if (distance_q16 <= half_width_q16) return (uint32_t)INT32_MAX;
    d = distance_q16 - half_width_q16;
    if (d >= feather_q16) return 0u;
    span = feather_q16 - d;
    num = span * (int64_t)INT32_MAX + feather_q16 / 2;
    return (uint32_t)(num / feather_q16);
}

static uint16_t mul_u16_q31(uint16_t v, uint32_t q31) {
    uint64_t p = (uint64_t)v * (uint64_t)q31 + (uint64_t)INT32_MAX / 2u;
    p /= (uint64_t)INT32_MAX;
    if (p > UINT16_MAX) p = UINT16_MAX;
    return (uint16_t)p;
}

static uint16_t mul_u16_u16(uint16_t a, uint16_t b) {
    uint64_t p = (uint64_t)a * (uint64_t)b + UINT64_C(32767);
    p /= UINT64_C(65535);
    return (uint16_t)p;
}

void odm_layered_blend16(odm_layered_pixel16 *dst, const odm_rgba16 *src,
                         uint32_t coverage_q31, uint32_t opacity_q31) {
    uint16_t a, sr, sg, sb, inv;
    uint32_t q;
    uint64_t v;
    if (!dst || !src || coverage_q31 == 0u || opacity_q31 == 0u) return;
    q = (uint32_t)(((uint64_t)coverage_q31 * opacity_q31 + (uint64_t)INT32_MAX / 2u) /
                   (uint64_t)INT32_MAX);
    a = mul_u16_q31(src->a, q);
    if (a == 0u) return;
    sr = mul_u16_u16(src->r, a);
    sg = mul_u16_u16(src->g, a);
    sb = mul_u16_u16(src->b, a);
    inv = (uint16_t)(UINT16_MAX - a);
    v = (uint64_t)sr + ((uint64_t)dst->r * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->r = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
    v = (uint64_t)sg + ((uint64_t)dst->g * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->g = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
    v = (uint64_t)sb + ((uint64_t)dst->b * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->b = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
    v = (uint64_t)a + ((uint64_t)dst->a * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->a = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
}

void odm_layered_blend_premul16(odm_layered_pixel16 *dst,
                                const odm_layered_pixel16 *src,
                                uint32_t opacity_q31) {
    odm_layered_pixel16 s;
    uint16_t inv;
    uint64_t v;
    if (!dst || !src || opacity_q31 == 0u) return;
    s.r = mul_u16_q31(src->r, opacity_q31);
    s.g = mul_u16_q31(src->g, opacity_q31);
    s.b = mul_u16_q31(src->b, opacity_q31);
    s.a = mul_u16_q31(src->a, opacity_q31);
    inv = (uint16_t)(UINT16_MAX - s.a);
    v = (uint64_t)s.r + ((uint64_t)dst->r * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->r = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
    v = (uint64_t)s.g + ((uint64_t)dst->g * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->g = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
    v = (uint64_t)s.b + ((uint64_t)dst->b * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->b = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
    v = (uint64_t)s.a + ((uint64_t)dst->a * inv + UINT64_C(32767)) / UINT64_C(65535);
    dst->a = (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
}

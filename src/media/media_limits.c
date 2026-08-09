#include "media_internal.h"

#include <limits.h>

uint16_t odm_media_read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t odm_media_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t odm_media_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

odm_status odm_media_u64_mul(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (a != 0u && b > UINT64_MAX / a) return ODM_STATUS_OVERFLOW;
    *out = a * b;
    return ODM_STATUS_OK;
}

odm_status odm_media_u64_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (b > UINT64_MAX - a) return ODM_STATUS_OVERFLOW;
    *out = a + b;
    return ODM_STATUS_OK;
}

typedef struct { uint64_t hi; uint64_t lo; } media_u128;

static media_u128 media_mul_u64(uint64_t a, uint64_t b) {
    const uint64_t mask = UINT64_C(0xffffffff);
    uint64_t a0 = a & mask, a1 = a >> 32;
    uint64_t b0 = b & mask, b1 = b >> 32;
    uint64_t t = a0 * b0;
    uint64_t w0 = t & mask;
    uint64_t carry = t >> 32;
    uint64_t w1, w2;
    media_u128 r;
    t = a1 * b0 + carry;
    w1 = t & mask; w2 = t >> 32;
    t = a0 * b1 + w1;
    carry = t >> 32;
    r.lo = (t << 32) + w0;
    r.hi = a1 * b1 + w2 + carry;
    return r;
}

static uint32_t media_u128_bit(media_u128 v, unsigned bit) {
    return bit >= 64u ? (uint32_t)((v.hi >> (bit - 64u)) & 1u)
                      : (uint32_t)((v.lo >> bit) & 1u);
}

odm_status odm_media_ceil_mul_div_u64(uint64_t a, uint64_t b, uint64_t d,
                                      uint64_t *out) {
    media_u128 n;
    uint64_t rem = 0u;
    uint64_t q = 0u;
    int bit;
    if (!out || d == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    n = media_mul_u64(a, b);
    for (bit = 127; bit >= 0; --bit) {
        uint32_t in = media_u128_bit(n, (unsigned)bit);
        uint64_t half = d >> 1;
        int ge;
        if (rem > half) ge = 1;
        else if (rem < half) ge = 0;
        else ge = ((d & 1u) == 0u) || in != 0u;
        if (ge) {
            if (rem >= d - rem) rem = rem - (d - rem) + (uint64_t)in;
            else rem = 0u; /* odd d, rem=floor(d/2), in=1 */
            if (bit >= 64) return ODM_STATUS_OVERFLOW;
            q |= UINT64_C(1) << (unsigned)bit;
        } else {
            rem = rem + rem + (uint64_t)in;
        }
    }
    if (rem != 0u) {
        if (q == UINT64_MAX) return ODM_STATUS_OVERFLOW;
        q++;
    }
    *out = q;
    return ODM_STATUS_OK;
}



odm_status odm_media_canonical_48k_frames(uint64_t source_frames,
                                              uint32_t source_rate,
                                              uint64_t *out_frames) {
    if (!out_frames || source_rate == 0u || source_rate > ODM_MEDIA_MAX_SAMPLE_RATE)
        return ODM_STATUS_INVALID_ARGUMENT;
    return odm_media_ceil_mul_div_u64(source_frames, UINT64_C(48000),
                                      (uint64_t)source_rate, out_frames);
}

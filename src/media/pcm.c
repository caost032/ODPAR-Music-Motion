#include "media_internal.h"

#include <limits.h>

static int32_t pcm_s24(const uint8_t *p) {
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    if ((u & UINT32_C(0x00800000)) != 0u) u |= UINT32_C(0xff000000);
    return (int32_t)u;
}

static uint64_t round_shift_right_u64(uint64_t v, unsigned shift) {
    uint64_t q;
    uint64_t rem;
    uint64_t half;
    if (shift == 0u) return v;
    if (shift >= 64u) return 0u;
    q = v >> shift;
    rem = v & ((UINT64_C(1) << shift) - 1u);
    half = UINT64_C(1) << (shift - 1u);
    if (rem >= half) q++;
    return q;
}

static odm_status ieee32_to_q31(uint32_t bits, int32_t *out) {
    uint32_t sign = bits >> 31;
    uint32_t exp = (bits >> 23) & 0xffu;
    uint32_t frac = bits & UINT32_C(0x7fffff);
    uint64_t mag;
    if (exp == 0xffu) return ODM_STATUS_INVALID_DATA;
    if (exp == 0u) { *out = 0; return ODM_STATUS_OK; }
    if (exp >= 127u) {
        if (exp == 127u && frac == 0u && sign != 0u) *out = INT32_MIN;
        else *out = sign ? INT32_MIN : INT32_MAX;
        return ODM_STATUS_OK;
    }
    mag = (uint64_t)(UINT32_C(0x800000) | frac);
    if (exp >= 119u) mag <<= (unsigned)(exp - 119u);
    else mag = round_shift_right_u64(mag, (unsigned)(119u - exp));
    if (mag > UINT64_C(0x80000000)) mag = UINT64_C(0x80000000);
    if (sign != 0u) {
        *out = mag >= UINT64_C(0x80000000) ? INT32_MIN : -(int32_t)mag;
    } else {
        if (mag > (uint64_t)INT32_MAX) mag = (uint64_t)INT32_MAX;
        *out = (int32_t)mag;
    }
    return ODM_STATUS_OK;
}

static odm_status ieee64_to_q31(uint64_t bits, int32_t *out) {
    uint32_t sign = (uint32_t)(bits >> 63);
    uint32_t exp = (uint32_t)((bits >> 52) & UINT64_C(0x7ff));
    uint64_t frac = bits & UINT64_C(0x000fffffffffffff);
    uint64_t mant;
    uint64_t mag;
    unsigned shift;
    if (exp == 0x7ffu) return ODM_STATUS_INVALID_DATA;
    if (exp == 0u) { *out = 0; return ODM_STATUS_OK; }
    if (exp >= 1023u) {
        if (exp == 1023u && frac == 0u && sign != 0u) *out = INT32_MIN;
        else *out = sign ? INT32_MIN : INT32_MAX;
        return ODM_STATUS_OK;
    }
    mant = UINT64_C(0x0010000000000000) | frac;
    shift = 1044u - exp;
    mag = round_shift_right_u64(mant, shift);
    if (mag > UINT64_C(0x80000000)) mag = UINT64_C(0x80000000);
    if (sign != 0u) *out = mag >= UINT64_C(0x80000000) ? INT32_MIN : -(int32_t)mag;
    else {
        if (mag > (uint64_t)INT32_MAX) mag = (uint64_t)INT32_MAX;
        *out = (int32_t)mag;
    }
    return ODM_STATUS_OK;
}

odm_status odm_pcm_decode_sample(const uint8_t *p, uint32_t bits, int is_float,
                                 int32_t *out) {
    if (!p || !out) return ODM_STATUS_INVALID_ARGUMENT;
    if (!is_float) {
        if (bits == 8u) {
            *out = ((int32_t)p[0] - 128) * INT32_C(16777216);
            return ODM_STATUS_OK;
        }
        if (bits == 16u) {
            uint16_t u = odm_media_read_le16(p);
            int32_t s = (u & UINT16_C(0x8000)) != 0u ? (int32_t)u - 65536 : (int32_t)u;
            *out = s * INT32_C(65536);
            return ODM_STATUS_OK;
        }
        if (bits == 24u) {
            *out = pcm_s24(p) * INT32_C(256);
            return ODM_STATUS_OK;
        }
        if (bits == 32u) {
            uint32_t u = odm_media_read_le32(p);
            if ((u & UINT32_C(0x80000000)) != 0u) {
                uint32_t mag = (~u) + 1u;
                *out = mag == UINT32_C(0x80000000) ? INT32_MIN : -(int32_t)mag;
            } else *out = (int32_t)u;
            return ODM_STATUS_OK;
        }
        return ODM_STATUS_UNSUPPORTED;
    }
    if (bits == 32u) return ieee32_to_q31(odm_media_read_le32(p), out);
    if (bits == 64u) {
        uint64_t u = (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
                     ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
                     ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                     ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
        return ieee64_to_q31(u, out);
    }
    return ODM_STATUS_UNSUPPORTED;
}

odm_status odm_media_hash_q31_frames(const odm_pcm_stereo_q31 *frames,
                                     uint64_t count, odm_sha256_digest *out) {
    odm_sha256_context c = ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status s;
    uint64_t i;
    if ((!frames && count != 0u) || !out) return ODM_STATUS_INVALID_ARGUMENT;
    s = odm_sha256_init(&c); if (s != ODM_STATUS_OK) return s;
    for (i = 0u; i < count; ++i) {
        uint8_t b[8];
        uint32_t l = (uint32_t)frames[i].left;
        uint32_t r = (uint32_t)frames[i].right;
        b[0]=(uint8_t)l; b[1]=(uint8_t)(l>>8); b[2]=(uint8_t)(l>>16); b[3]=(uint8_t)(l>>24);
        b[4]=(uint8_t)r; b[5]=(uint8_t)(r>>8); b[6]=(uint8_t)(r>>16); b[7]=(uint8_t)(r>>24);
        s = odm_sha256_update(&c, b, sizeof(b)); if (s != ODM_STATUS_OK) return s;
    }
    return odm_sha256_final(&c, out);
}

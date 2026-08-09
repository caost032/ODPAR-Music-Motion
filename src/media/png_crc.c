#include "media_internal.h"

#include <string.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint64_t n) {
    uint64_t i;
    for (i = 0u; i < n; ++i) {
        unsigned k;
        crc ^= p[i];
        for (k = 0u; k < 8u; ++k) crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

odm_status odm_png_validate_chunks(const uint8_t *bytes, uint64_t size,
                                   uint32_t *out_width, uint32_t *out_height) {
    static const uint8_t sig[8] = {137u,80u,78u,71u,13u,10u,26u,10u};
    uint64_t pos = 8u;
    int ihdr = 0;
    int idat = 0;
    int iend = 0;
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!bytes || !out_width || !out_height) return ODM_STATUS_INVALID_ARGUMENT;
    if (size < 8u || memcmp(bytes, sig, sizeof(sig)) != 0) return ODM_STATUS_INVALID_DATA;
    while (pos < size) {
        uint32_t len;
        uint64_t total;
        uint32_t expected;
        uint32_t actual;
        const uint8_t *type;
        if (size - pos < 12u) return ODM_STATUS_INVALID_DATA;
        len = odm_media_read_be32(bytes + pos);
        type = bytes + pos + 4u;
        total = (uint64_t)len + 12u;
        if (total > size - pos) return ODM_STATUS_INVALID_DATA;
        expected = odm_media_read_be32(bytes + pos + 8u + len);
        actual = crc32_update(UINT32_MAX, type, (uint64_t)len + 4u) ^ UINT32_MAX;
        if (expected != actual) return ODM_STATUS_INTEGRITY_ERROR;
        if (!ihdr) {
            if (memcmp(type, "IHDR", 4u) != 0 || len != 13u) return ODM_STATUS_INVALID_DATA;
            width = odm_media_read_be32(bytes + pos + 8u);
            height = odm_media_read_be32(bytes + pos + 12u);
            if (width == 0u || height == 0u || width > ODM_MEDIA_MAX_IMAGE_DIMENSION ||
                height > ODM_MEDIA_MAX_IMAGE_DIMENSION) return ODM_STATUS_INVALID_DATA;
            {
                uint64_t pixels;
                if (odm_media_u64_mul(width, height, &pixels) != ODM_STATUS_OK ||
                    pixels > ODM_MEDIA_MAX_IMAGE_PIXELS) return ODM_STATUS_BUDGET_EXCEEDED;
            }
            if (bytes[pos + 18u] != 0u || bytes[pos + 19u] != 0u || bytes[pos + 20u] > 1u)
                return ODM_STATUS_UNSUPPORTED;
            ihdr = 1;
        } else if (memcmp(type, "IHDR", 4u) == 0) return ODM_STATUS_INVALID_DATA;
        if (memcmp(type, "IDAT", 4u) == 0) idat = 1;
        if (memcmp(type, "IEND", 4u) == 0) {
            if (len != 0u || !idat) return ODM_STATUS_INVALID_DATA;
            iend = 1;
            pos += total;
            break;
        }
        pos += total;
    }
    if (!ihdr || !idat || !iend || pos != size) return ODM_STATUS_INVALID_DATA;
    *out_width = width; *out_height = height;
    return ODM_STATUS_OK;
}

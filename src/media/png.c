#include "media_internal.h"

#include <png.h>
#include <stdlib.h>
#include <string.h>

odm_status odm_png_decode_rgba8(const uint8_t *bytes, uint64_t size,
                                uint8_t *rgba, uint64_t rgba_capacity,
                                uint64_t *out_required_bytes,
                                odm_media_facts *out_facts) {
    png_image image;
    uint32_t w;
    uint32_t hgt;
    uint64_t required;
    uint8_t *tmp;
    odm_media_facts facts;
    odm_status s;
    if (!out_required_bytes || !out_facts || (!bytes && size != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required_bytes = 0u;
    s = odm_png_validate_chunks(bytes, size, &w, &hgt); if (s != ODM_STATUS_OK) return s;
    if (odm_media_u64_mul((uint64_t)w, (uint64_t)hgt, &required) != ODM_STATUS_OK ||
        odm_media_u64_mul(required, 4u, &required) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    *out_required_bytes = required;
    if (!rgba || rgba_capacity < required) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (required > (uint64_t)SIZE_MAX || size > (uint64_t)SIZE_MAX) return ODM_STATUS_OVERFLOW;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes, (size_t)size)) return ODM_STATUS_INVALID_DATA;
    if (image.width != w || image.height != hgt) { png_image_free(&image); return ODM_STATUS_INVARIANT_BROKEN; }
    image.format = PNG_FORMAT_RGBA;
    if ((uint64_t)PNG_IMAGE_SIZE(image) != required) { png_image_free(&image); return ODM_STATUS_INVARIANT_BROKEN; }
    tmp = (uint8_t *)malloc((size_t)required);
    if (!tmp) { png_image_free(&image); return ODM_STATUS_OUT_OF_MEMORY; }
    if (!png_image_finish_read(&image, NULL, tmp, 0, NULL)) {
        free(tmp); png_image_free(&image); return ODM_STATUS_INVALID_DATA;
    }
    memset(&facts, 0, sizeof(facts));
    facts.media_kind = ODM_MEDIA_KIND_IMAGE;
    facts.container = ODM_MEDIA_CONTAINER_PNG;
    facts.codec = ODM_MEDIA_CODEC_PNG;
    facts.support = ODM_MEDIA_SUPPORT_SUPPORTED;
    facts.width = w; facts.height = hgt; facts.pixel_channels = 4u;
    facts.bits_per_sample = 8u;
    facts.decoded_bytes = required;
    s = odm_sha256(bytes, size, &facts.source_sha256);
    if (s == ODM_STATUS_OK) s = odm_sha256(tmp, required, &facts.decoded_sha256);
    if (s == ODM_STATUS_OK) s = odm_media_facts_validate(&facts);
    if (s == ODM_STATUS_OK) {
        memcpy(rgba, tmp, (size_t)required);
        *out_facts = facts;
    }
    free(tmp); png_image_free(&image);
    return s;
}

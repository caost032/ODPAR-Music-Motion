#include "media_internal.h"

#include <string.h>

static int has(const uint8_t *b, uint64_t n, const void *sig, size_t m) {
    return n >= (uint64_t)m && memcmp(b, sig, m) == 0;
}

odm_status odm_media_detect(const uint8_t *bytes, uint64_t size,
                            odm_media_probe_result *out_probe) {
    static const uint8_t png_sig[8] = {137u,80u,78u,71u,13u,10u,26u,10u};
    static const uint8_t flac_sig[4] = {'f','L','a','C'};
    static const uint8_t jpeg_sig[3] = {0xffu,0xd8u,0xffu};
    static const uint8_t webm_sig[4] = {0x1au,0x45u,0xdfu,0xa3u};
    odm_media_probe_result tmp = {0};
    if (!out_probe || (!bytes && size != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    if (size >= 12u && memcmp(bytes, "RIFF", 4u) == 0 &&
        memcmp(bytes + 8u, "WAVE", 4u) == 0) {
        tmp.container = ODM_MEDIA_CONTAINER_WAV;
        tmp.support = ODM_MEDIA_SUPPORT_SUPPORTED;
        tmp.media_kind = ODM_MEDIA_KIND_AUDIO;
    } else if (has(bytes, size, png_sig, sizeof(png_sig))) {
        tmp.container = ODM_MEDIA_CONTAINER_PNG;
        tmp.support = ODM_MEDIA_SUPPORT_SUPPORTED;
        tmp.media_kind = ODM_MEDIA_KIND_IMAGE;
    } else if (has(bytes, size, flac_sig, sizeof(flac_sig))) {
        tmp.container = ODM_MEDIA_CONTAINER_FLAC;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_AUDIO;
    } else if (has(bytes, size, jpeg_sig, sizeof(jpeg_sig))) {
        tmp.container = ODM_MEDIA_CONTAINER_JPEG;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_IMAGE;
    } else if (size >= 12u && memcmp(bytes, "RIFF", 4u) == 0 &&
               memcmp(bytes + 8u, "WEBP", 4u) == 0) {
        tmp.container = ODM_MEDIA_CONTAINER_WEBP;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_IMAGE;
    } else if (has(bytes, size, webm_sig, sizeof(webm_sig))) {
        tmp.container = ODM_MEDIA_CONTAINER_WEBM;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_VIDEO;
    } else if (size >= 8u && memcmp(bytes + 4u, "ftyp", 4u) == 0) {
        tmp.container = ODM_MEDIA_CONTAINER_ISOBMFF;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_VIDEO;
    } else if (size >= 3u && memcmp(bytes, "ID3", 3u) == 0) {
        tmp.container = ODM_MEDIA_CONTAINER_MP3;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_AUDIO;
    } else if (size >= 2u && bytes[0] == 0xffu && (bytes[1] & 0xe0u) == 0xe0u) {
        tmp.container = ODM_MEDIA_CONTAINER_MP3;
        tmp.support = ODM_MEDIA_SUPPORT_RECOGNIZED;
        tmp.media_kind = ODM_MEDIA_KIND_AUDIO;
    } else {
        tmp.container = ODM_MEDIA_CONTAINER_UNKNOWN;
        tmp.support = ODM_MEDIA_SUPPORT_UNKNOWN;
        tmp.media_kind = ODM_MEDIA_KIND_UNKNOWN;
    }
    *out_probe = tmp;
    return ODM_STATUS_OK;
}

odm_status odm_media_probe(const uint8_t *bytes, uint64_t size,
                           odm_media_probe_result *out_probe) {
    return odm_media_detect(bytes, size, out_probe);
}

#include "media_internal.h"

#include <string.h>

odm_status odm_media_decode_audio_q31(const uint8_t *bytes, uint64_t size,
                                      odm_pcm_stereo_q31 *frames,
                                      uint64_t frame_capacity,
                                      uint64_t *out_required_frames,
                                      odm_media_facts *out_facts) {
    odm_media_probe_result p;
    odm_status s;
    if (!out_required_frames || !out_facts) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out_facts, 0, sizeof(*out_facts));
    s = odm_media_detect(bytes, size, &p); if (s != ODM_STATUS_OK) return s;
    if (p.media_kind != ODM_MEDIA_KIND_AUDIO) return p.support == ODM_MEDIA_SUPPORT_UNKNOWN ? ODM_STATUS_UNSUPPORTED : ODM_STATUS_INVALID_ARGUMENT;
    if (p.support != ODM_MEDIA_SUPPORT_SUPPORTED) return ODM_STATUS_UNSUPPORTED;
    if (p.container == ODM_MEDIA_CONTAINER_WAV)
        return odm_wav_decode_q31(bytes, size, frames, frame_capacity, out_required_frames, out_facts);
    return ODM_STATUS_UNSUPPORTED;
}

odm_status odm_media_decode_image_rgba8(const uint8_t *bytes, uint64_t size,
                                        uint8_t *rgba, uint64_t rgba_capacity,
                                        uint64_t *out_required_bytes,
                                        odm_media_facts *out_facts) {
    odm_media_probe_result p;
    odm_status s;
    if (!out_required_bytes || !out_facts) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out_facts, 0, sizeof(*out_facts));
    s = odm_media_detect(bytes, size, &p); if (s != ODM_STATUS_OK) return s;
    if (p.media_kind != ODM_MEDIA_KIND_IMAGE) return p.support == ODM_MEDIA_SUPPORT_UNKNOWN ? ODM_STATUS_UNSUPPORTED : ODM_STATUS_INVALID_ARGUMENT;
    if (p.support != ODM_MEDIA_SUPPORT_SUPPORTED) return ODM_STATUS_UNSUPPORTED;
    if (p.container == ODM_MEDIA_CONTAINER_PNG)
        return odm_png_decode_rgba8(bytes, size, rgba, rgba_capacity, out_required_bytes, out_facts);
    return ODM_STATUS_UNSUPPORTED;
}

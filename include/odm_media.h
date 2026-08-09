#ifndef ODM_MEDIA_H
#define ODM_MEDIA_H

#include "odm_fixed.h"
#include "odm_hash.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_MEDIA_KIND_UNKNOWN 0u
#define ODM_MEDIA_KIND_AUDIO   1u
#define ODM_MEDIA_KIND_IMAGE   2u
#define ODM_MEDIA_KIND_VIDEO   3u

#define ODM_MEDIA_CONTAINER_UNKNOWN 0u
#define ODM_MEDIA_CONTAINER_WAV     1u
#define ODM_MEDIA_CONTAINER_PNG     2u
#define ODM_MEDIA_CONTAINER_FLAC    3u
#define ODM_MEDIA_CONTAINER_JPEG    4u
#define ODM_MEDIA_CONTAINER_WEBP    5u
#define ODM_MEDIA_CONTAINER_MP3     6u
#define ODM_MEDIA_CONTAINER_ISOBMFF 7u
#define ODM_MEDIA_CONTAINER_WEBM    8u
#define ODM_MEDIA_CONTAINER_OGG     9u
#define ODM_MEDIA_CONTAINER_AIFF    10u
#define ODM_MEDIA_CONTAINER_MATROSKA 11u

#define ODM_MEDIA_CODEC_UNKNOWN     0u
#define ODM_MEDIA_CODEC_PCM_INT     1u
#define ODM_MEDIA_CODEC_PCM_FLOAT   2u
#define ODM_MEDIA_CODEC_PNG         3u

#define ODM_MEDIA_SUPPORT_UNKNOWN    0u
#define ODM_MEDIA_SUPPORT_SUPPORTED  1u
#define ODM_MEDIA_SUPPORT_RECOGNIZED 2u

#define ODM_MEDIA_FACTS_SCHEMA_MAJOR 1u
#define ODM_MEDIA_FACTS_SCHEMA_MINOR 0u
#define ODM_MEDIA_FACTS_PAYLOAD_BYTES 160u
#define ODM_MEDIA_FACTS_WIRE_KIND UINT32_C(0x4341464d) /* MFAC little endian */

#define ODM_MEDIA_MAX_AUDIO_CHANNELS 2u
#define ODM_MEDIA_MAX_SAMPLE_RATE 384000u
#define ODM_MEDIA_MAX_IMAGE_DIMENSION 16384u
#define ODM_MEDIA_MAX_IMAGE_PIXELS UINT64_C(67108864)

typedef struct {
    uint32_t media_kind;
    uint32_t container;
    uint32_t codec;
    uint32_t support;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t bits_per_sample;
    uint32_t reserved0;
    uint64_t source_frames;
    uint64_t canonical_48k_frames;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_channels;
    uint32_t reserved1;
    uint64_t decoded_bytes;
    odm_sha256_digest source_sha256;
    odm_sha256_digest decoded_sha256;
} odm_media_facts;

typedef struct {
    uint32_t container;
    uint32_t support;
    uint32_t media_kind;
    uint32_t reserved;
} odm_media_probe_result;

typedef struct {
    int32_t left;
    int32_t right;
} odm_pcm_stereo_q31;

odm_status odm_media_probe(const uint8_t *bytes, uint64_t size,
                           odm_media_probe_result *out_probe);
odm_status odm_media_canonical_48k_frames(uint64_t source_frames,
                                              uint32_t source_rate,
                                              uint64_t *out_frames);
odm_status odm_media_facts_validate(const odm_media_facts *facts);
odm_status odm_media_facts_write_record(const odm_media_facts *facts,
                                        uint8_t *buffer, uint64_t capacity,
                                        uint64_t *out_required,
                                        odm_sha256_digest *out_record_sha256);
odm_status odm_media_facts_read_record(const uint8_t *record,
                                       uint64_t record_bytes,
                                       odm_media_facts *out_facts);

odm_status odm_media_decode_audio_q31(const uint8_t *bytes, uint64_t size,
                                      odm_pcm_stereo_q31 *frames,
                                      uint64_t frame_capacity,
                                      uint64_t *out_required_frames,
                                      odm_media_facts *out_facts);
odm_status odm_media_decode_image_rgba8(const uint8_t *bytes, uint64_t size,
                                        uint8_t *rgba, uint64_t rgba_capacity,
                                        uint64_t *out_required_bytes,
                                        odm_media_facts *out_facts);

#ifdef __cplusplus
}
#endif

#endif /* ODM_MEDIA_H */

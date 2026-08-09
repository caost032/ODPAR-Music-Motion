#ifndef ODM_MEDIA_INTERNAL_H
#define ODM_MEDIA_INTERNAL_H

#include "odm_media.h"

#include <stdint.h>
#include <stddef.h>

uint16_t odm_media_read_le16(const uint8_t *p);
uint32_t odm_media_read_le32(const uint8_t *p);
uint32_t odm_media_read_be32(const uint8_t *p);
odm_status odm_media_u64_mul(uint64_t a, uint64_t b, uint64_t *out);
odm_status odm_media_u64_add(uint64_t a, uint64_t b, uint64_t *out);
odm_status odm_media_ceil_mul_div_u64(uint64_t a, uint64_t b, uint64_t d,
                                      uint64_t *out);
odm_status odm_pcm_decode_sample(const uint8_t *p, uint32_t bits, int is_float,
                                 int32_t *out);
odm_status odm_media_hash_q31_frames(const odm_pcm_stereo_q31 *frames,
                                     uint64_t count, odm_sha256_digest *out);
odm_status odm_png_validate_chunks(const uint8_t *bytes, uint64_t size,
                                   uint32_t *out_width, uint32_t *out_height);
odm_status odm_media_detect(const uint8_t *bytes, uint64_t size,
                            odm_media_probe_result *out_probe);
odm_status odm_wav_decode_q31(const uint8_t *bytes, uint64_t size,
                              odm_pcm_stereo_q31 *frames,
                              uint64_t frame_capacity,
                              uint64_t *out_required_frames,
                              odm_media_facts *out_facts);
odm_status odm_png_decode_rgba8(const uint8_t *bytes, uint64_t size,
                                uint8_t *rgba, uint64_t rgba_capacity,
                                uint64_t *out_required_bytes,
                                odm_media_facts *out_facts);

#endif

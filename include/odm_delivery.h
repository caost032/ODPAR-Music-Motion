#ifndef ODM_DELIVERY_H
#define ODM_DELIVERY_H

#include "odm_hash.h"
#include "odm_master.h"
#include "odm_status.h"
#include "odm_wire.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_DELIVERY_CONTRACT_KIND UINT32_C(0x52564c44) /* "DLVR" LE */
#define ODM_DELIVERY_CONTRACT_SCHEMA_MAJOR 1u
#define ODM_DELIVERY_CONTRACT_SCHEMA_MINOR 0u
#define ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES UINT32_C(320)
#define ODM_DELIVERY_CONTRACT_RECORD_BYTES \
    (ODM_WIRE_RECORD_HEADER_BYTES + ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES)

#define ODM_DELIVERY_ARTIFACT_KIND UINT32_C(0x41564c44) /* "DLVA" LE */
#define ODM_DELIVERY_ARTIFACT_SCHEMA_MAJOR 1u
#define ODM_DELIVERY_ARTIFACT_SCHEMA_MINOR 0u
#define ODM_DELIVERY_ARTIFACT_PAYLOAD_BYTES UINT32_C(192)
#define ODM_DELIVERY_ARTIFACT_RECORD_BYTES \
    (ODM_WIRE_RECORD_HEADER_BYTES + ODM_DELIVERY_ARTIFACT_PAYLOAD_BYTES)

#define ODM_DELIVERY_CONTAINER_MP4 UINT32_C(1)
#define ODM_DELIVERY_VIDEO_H264 UINT32_C(1)
#define ODM_DELIVERY_AUDIO_AAC UINT32_C(1)
#define ODM_DELIVERY_COLOR_SDR_BT709 UINT32_C(1)
#define ODM_DELIVERY_PIXEL_YUV420P UINT32_C(1)
#define ODM_DELIVERY_RATE_CONTROL_CRF UINT32_C(1)
#define ODM_DELIVERY_AUDIO_SAMPLE_RATE UINT32_C(48000)
#define ODM_DELIVERY_MAX_DIMENSION UINT32_C(8192)
#define ODM_DELIVERY_DESCRIPTOR_MAX_BYTES UINT32_C(1024)

#define ODM_DELIVERY_DESCRIPTOR_VIDEO_ENCODER UINT32_C(1)
#define ODM_DELIVERY_DESCRIPTOR_AUDIO_ENCODER UINT32_C(2)
#define ODM_DELIVERY_DESCRIPTOR_SETTINGS UINT32_C(3)

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t container;
    uint32_t video_codec;
    uint32_t audio_codec;
    uint32_t color_profile;
    uint32_t pixel_format;
    uint32_t rate_control;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t audio_sample_rate;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t frame_count;
    int64_t output_end_sample;
    odm_sha256_digest render_id;
    odm_sha256_digest render_receipt_sha256;
    odm_sha256_digest video_encoder_identity_sha256;
    odm_sha256_digest audio_encoder_identity_sha256;
    odm_sha256_digest encoder_settings_sha256;
    odm_sha256_digest delivery_contract_id;
    odm_sha256_digest record_sha256;
} odm_delivery_contract_info;

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t file_bytes;
    odm_sha256_digest delivery_contract_id;
    odm_sha256_digest file_sha256;
    odm_sha256_digest artifact_id;
    odm_sha256_digest record_sha256;
} odm_delivery_artifact_info;

odm_status odm_delivery_descriptor_sha256(
    uint32_t descriptor_kind,
    const uint8_t *descriptor,
    uint32_t descriptor_bytes,
    odm_sha256_digest *out_sha256);

odm_status odm_delivery_contract_write(
    const uint8_t *render_receipt_record,
    uint64_t render_receipt_bytes,
    const odm_sha256_digest *video_encoder_identity_sha256,
    const odm_sha256_digest *audio_encoder_identity_sha256,
    const odm_sha256_digest *encoder_settings_sha256,
    uint8_t *contract_record,
    uint64_t contract_capacity,
    uint64_t *out_required,
    odm_delivery_contract_info *out_info);

odm_status odm_delivery_contract_validate(
    const uint8_t *contract_record,
    uint64_t contract_bytes,
    const uint8_t *render_receipt_record,
    uint64_t render_receipt_bytes,
    odm_delivery_contract_info *out_info);

odm_status odm_delivery_artifact_write(
    const uint8_t *contract_record,
    uint64_t contract_bytes,
    const odm_sha256_digest *file_sha256,
    uint64_t file_bytes,
    uint8_t *artifact_record,
    uint64_t artifact_capacity,
    uint64_t *out_required,
    odm_delivery_artifact_info *out_info);

odm_status odm_delivery_artifact_validate(
    const uint8_t *artifact_record,
    uint64_t artifact_bytes,
    const odm_sha256_digest *expected_delivery_contract_id,
    odm_delivery_artifact_info *out_info);

#ifdef __cplusplus
}
#endif
#endif /* ODM_DELIVERY_H */

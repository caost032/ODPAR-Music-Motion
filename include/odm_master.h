#ifndef ODM_MASTER_H
#define ODM_MASTER_H

#include "odm_hash.h"
#include "odm_ir.h"
#include "odm_job.h"
#include "odm_media.h"
#include "odm_package.h"
#include "odm_renderer.h"
#include "odm_score.h"
#include "odm_status.h"
#include "odm_wire.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_MASTER_SCHEMA_MAJOR 1u
#define ODM_MASTER_SCHEMA_MINOR 0u
#define ODM_MASTER_PROFILE_REFERENCE_V1 UINT32_C(1)
#define ODM_MASTER_AUDIO_PCM_Q31_STEREO_LE UINT32_C(1)
#define ODM_MASTER_WORK_SCHEMA_OUTPUT_VOLUME_V1 UINT32_C(1)
#define ODM_MASTER_AUDIO_CHUNK_FRAMES UINT32_C(1024)

#define ODM_RENDER_RECEIPT_KIND UINT32_C(0x54504352) /* "RCPT" LE */
#define ODM_RENDER_RECEIPT_SCHEMA_MAJOR 1u
#define ODM_RENDER_RECEIPT_SCHEMA_MINOR 0u
#define ODM_RENDER_RECEIPT_PAYLOAD_BYTES UINT32_C(512)
#define ODM_RENDER_RECEIPT_RECORD_BYTES \
    (ODM_WIRE_RECORD_HEADER_BYTES + ODM_RENDER_RECEIPT_PAYLOAD_BYTES)

#define ODM_CONTROLLER_SIGNATURE_SCHEMA_MAJOR 1u
#define ODM_CONTROLLER_SIGNATURE_SCHEMA_MINOR 0u
#define ODM_CONTROLLER_SIGNATURE_ED25519 UINT32_C(1)
#define ODM_CONTROLLER_SIGNATURE_BYTES UINT32_C(160)

typedef struct {
    const uint8_t *package_bytes;
    uint64_t package_size;
    const uint8_t *expected_package_public_key;
    const odm_score_view *score;
    const odm_render_asset_set *assets;
    const odm_pcm_stereo_q31 *canonical_pcm;
    uint64_t canonical_pcm_frames;
    uint32_t output_profile;
    uint32_t flags;
    const odm_job_ticket *job_ticket;
} odm_master_request;

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t output_profile;
    uint32_t pixel_format;
    uint32_t audio_format;
    uint32_t work_schema;
    uint32_t renderer_version_major;
    uint32_t renderer_version_minor;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t flags;
    uint64_t frame_count;
    int64_t samples_per_frame;
    int64_t project_end_sample;
    int64_t output_end_sample;
    uint64_t canonical_music_frames;
    uint64_t frame_bytes;
    uint64_t renderer_scratch_bytes;
    uint64_t quoted_work_units;
    uint64_t quoted_output_bytes;
    uint64_t project_seed;
    odm_sha256_digest package_sha256;
    odm_sha256_digest score_sha256;
    odm_sha256_digest capability_sha256;
    odm_sha256_digest music_map_sha256;
    odm_sha256_digest music_policy_sha256;
    odm_sha256_digest render_ir_sha256;
    odm_sha256_digest assets_sha256;
    odm_sha256_digest canonical_pcm_sha256;
    odm_sha256_digest render_id;
    uint32_t reserved[8];
} odm_master_plan;

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t output_profile;
    uint32_t pixel_format;
    uint32_t audio_format;
    uint32_t work_schema;
    uint32_t renderer_version_major;
    uint32_t renderer_version_minor;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t flags;
    uint64_t frame_count;
    int64_t samples_per_frame;
    int64_t project_end_sample;
    int64_t output_end_sample;
    uint64_t canonical_music_frames;
    uint64_t quoted_work_units;
    uint64_t observed_work_units;
    uint64_t project_seed;
    odm_sha256_digest package_sha256;
    odm_sha256_digest score_sha256;
    odm_sha256_digest capability_sha256;
    odm_sha256_digest music_map_sha256;
    odm_sha256_digest music_policy_sha256;
    odm_sha256_digest render_ir_sha256;
    odm_sha256_digest assets_sha256;
    odm_sha256_digest canonical_pcm_sha256;
    odm_sha256_digest soundtrack_sha256;
    odm_sha256_digest frame_root_sha256;
    odm_sha256_digest render_id;
    odm_sha256_digest receipt_sha256;
} odm_render_receipt_info;

typedef odm_status (*odm_master_sink_begin_fn)(void *user,
                                                const odm_master_plan *plan);
typedef odm_status (*odm_master_sink_frame_fn)(void *user,
                                                const odm_reference_frame_info *info,
                                                const uint8_t *rgba16le,
                                                uint64_t bytes);
typedef odm_status (*odm_master_sink_audio_fn)(void *user,
                                                uint64_t start_frame,
                                                const uint8_t *pcm_q31_stereo_le,
                                                uint64_t frame_count);
typedef odm_status (*odm_master_sink_commit_fn)(void *user,
                                                 const uint8_t *receipt_record,
                                                 uint64_t receipt_bytes);
typedef void (*odm_master_sink_abort_fn)(void *user, odm_status reason);

typedef struct {
    void *user;
    odm_master_sink_begin_fn begin;
    odm_master_sink_frame_fn write_frame;
    odm_master_sink_audio_fn write_audio;
    odm_master_sink_commit_fn commit;
    odm_master_sink_abort_fn abort;
} odm_master_sink;

/* Canonical identity of stereo Q1.31 48 kHz PCM. Integers are hashed in LE form
 * under a versioned domain, never as native struct bytes. */
odm_status odm_master_pcm_sha256(const odm_pcm_stereo_q31 *frames,
                                 uint64_t frame_count,
                                 odm_sha256_digest *out_sha256);

/* Revalidate package + authored Score, recompile Score->IR byte-for-byte,
 * validate analysis.bin/PCM/assets and produce a deterministic work quote. */
odm_status odm_master_plan_build(const odm_master_request *request,
                                 odm_master_plan *out_plan);

/* Execute a complete canonical master into a transactional sink. frame_buffer
 * and renderer_scratch are caller-owned and sized from the plan. The receipt
 * is copied to receipt_buffer only after sink commit succeeds. */
odm_status odm_master_run(const odm_master_request *request,
                          const odm_master_sink *sink,
                          uint8_t *frame_buffer,
                          uint64_t frame_capacity,
                          void *renderer_scratch,
                          uint64_t renderer_scratch_capacity,
                          uint8_t *receipt_buffer,
                          uint64_t receipt_capacity,
                          uint64_t *out_receipt_required,
                          odm_render_receipt_info *out_receipt_info);

odm_status odm_render_receipt_validate(const uint8_t *record,
                                       uint64_t record_bytes,
                                       const odm_sha256_digest *expected_render_id,
                                       odm_render_receipt_info *out_info);

/* Controller signature is deliberately separate from rendering. The envelope
 * signs the complete canonical Render Receipt; the renderer never owns a key. */
odm_status odm_controller_signature_public_from_seed(
    const uint8_t seed[ODM_ODPARMS_SIGNING_SEED_BYTES],
    uint8_t out_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES]);
odm_status odm_controller_signature_write(
    const uint8_t seed[ODM_ODPARMS_SIGNING_SEED_BYTES],
    const uint8_t *receipt_record,
    uint64_t receipt_bytes,
    uint8_t out_envelope[ODM_CONTROLLER_SIGNATURE_BYTES]);
odm_status odm_controller_signature_verify(
    const uint8_t expected_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    const uint8_t *receipt_record,
    uint64_t receipt_bytes,
    const uint8_t envelope[ODM_CONTROLLER_SIGNATURE_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* ODM_MASTER_H */

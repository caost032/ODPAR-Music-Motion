#ifndef ODM_STUDIO_H
#define ODM_STUDIO_H

#include "odm_master.h"
#include "odm_package.h"
#include "odm_preview.h"
#include "odm_score.h"
#include "odm_status.h"
#include "odm_wire.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_STUDIO_REVISION_KIND UINT32_C(0x52555453) /* "STUR" LE */
#define ODM_STUDIO_REVISION_SCHEMA_MAJOR 1u
#define ODM_STUDIO_REVISION_SCHEMA_MINOR 0u
#define ODM_STUDIO_REVISION_PAYLOAD_BYTES UINT32_C(512)
#define ODM_STUDIO_REVISION_RECORD_BYTES (ODM_WIRE_RECORD_HEADER_BYTES + ODM_STUDIO_REVISION_PAYLOAD_BYTES)

#define ODM_STUDIO_APPROVAL_KIND UINT32_C(0x41555453) /* "STUA" LE */
#define ODM_STUDIO_APPROVAL_SCHEMA_MAJOR 1u
#define ODM_STUDIO_APPROVAL_SCHEMA_MINOR 0u
#define ODM_STUDIO_APPROVAL_PAYLOAD_BYTES UINT32_C(256)
#define ODM_STUDIO_APPROVAL_RECORD_BYTES (ODM_WIRE_RECORD_HEADER_BYTES + ODM_STUDIO_APPROVAL_PAYLOAD_BYTES)
#define ODM_STUDIO_APPROVAL_SEMANTICS_EXACT UINT32_C(1)

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint64_t revision_number;
    uint32_t asset_count;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t reserved0;
    int64_t project_end_sample;
    uint64_t project_seed;
    odm_sha256_digest parent_revision_id;
    odm_sha256_digest package_sha256;
    odm_sha256_digest score_sha256;
    odm_sha256_digest capability_sha256;
    odm_sha256_digest music_map_sha256;
    odm_sha256_digest music_policy_sha256;
    odm_sha256_digest render_ir_sha256;
    odm_sha256_digest asset_index_sha256;
    odm_sha256_digest package_content_sha256;
    odm_sha256_digest project_id;
    uint8_t package_signer_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES];
    odm_sha256_digest revision_id;
    odm_sha256_digest record_sha256;
} odm_studio_revision_info;

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t raster_class;
    uint32_t pixel_format;
    uint32_t semantics_class;
    uint32_t flags;
    int64_t frame_index;
    int64_t sample;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t output_width;
    uint32_t output_height;
    int32_t fps;
    uint32_t reserved0;
    int64_t project_end_sample;
    uint64_t revision_number;
    odm_sha256_digest revision_id;
    odm_sha256_digest render_ir_sha256;
    odm_sha256_digest canonical_frame_sha256;
    odm_sha256_digest preview_pixel_sha256;
    odm_sha256_digest approval_id;
    odm_sha256_digest record_sha256;
} odm_studio_preview_approval_info;

/* Canonical authored asset identity: resource id/kind/flags/content SHA.
 * Local file paths are deliberately excluded from semantic identity. */
odm_status odm_studio_asset_index_sha256(const odm_score_view *score,
                                          odm_sha256_digest *out_sha256);

/* Freeze one immutable revision from a verified signed .odparms + authored Score.
 * parent_revision_record is NULL/0 only for genesis. A no-op/re-sign-only child
 * is rejected so revision numbers denote actual semantic project changes. */
odm_status odm_studio_revision_freeze(
    const uint8_t *package_bytes,
    uint64_t package_size,
    const uint8_t expected_package_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    const odm_score_view *score,
    const uint8_t *parent_revision_record,
    uint64_t parent_revision_bytes,
    uint8_t *revision_record,
    uint64_t revision_capacity,
    uint64_t *out_required,
    odm_studio_revision_info *out_info);

odm_status odm_studio_revision_validate(
    const uint8_t *revision_record,
    uint64_t revision_bytes,
    odm_studio_revision_info *out_info);

/* Revision-bound Preview.  The caller provides the immutable Render IR and
 * analysis.bin belonging to the revision; Studio validates both identities and
 * selects the canonical Music Map tick internally.  UI code never supplies an
 * arbitrary analysis tick. */
odm_status odm_studio_preview_plan(
    const uint8_t *revision_record,
    uint64_t revision_bytes,
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    const uint8_t *analysis_bin,
    uint64_t analysis_bin_bytes,
    int64_t frame_index,
    const odm_preview_config *config,
    uint64_t resource_count,
    uint64_t frame_count,
    odm_preview_plan *out_plan);

odm_status odm_studio_preview_render(
    const uint8_t *revision_record,
    uint64_t revision_bytes,
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    const uint8_t *analysis_bin,
    uint64_t analysis_bin_bytes,
    int64_t frame_index,
    const odm_preview_config *config,
    const odm_preview_asset_resource *resources,
    uint64_t resource_count,
    const odm_preview_asset_frame *frames,
    uint64_t frame_count,
    const uint8_t *pixel_blob,
    uint64_t pixel_blob_bytes,
    const odm_job_ticket *job_ticket,
    void *scratch,
    uint64_t scratch_bytes,
    uint8_t *rgba8,
    uint64_t rgba8_capacity,
    uint64_t *out_required_rgba8,
    uint64_t *out_required_scratch,
    odm_preview_frame_info *out_info);

/* Approval binds three authorities simultaneously:
 * frozen revision, Preview plan (which carries Render-IR identity), and the
 * actually rendered frame hashes. No subjective UI state enters the record. */
odm_status odm_studio_preview_approval_write(
    const uint8_t *revision_record,
    uint64_t revision_bytes,
    const odm_preview_plan *plan,
    const odm_preview_frame_info *preview,
    uint8_t *approval_record,
    uint64_t approval_capacity,
    uint64_t *out_required,
    odm_studio_preview_approval_info *out_info);

odm_status odm_studio_preview_approval_validate(
    const uint8_t *approval_record,
    uint64_t approval_bytes,
    const odm_sha256_digest *expected_revision_id,
    odm_studio_preview_approval_info *out_info);

/* Gate 9 stays authoritative. These wrappers merely require a frozen revision
 * to match the fully revalidated master plan before any master can begin. */
odm_status odm_studio_master_plan_build(
    const uint8_t *revision_record,
    uint64_t revision_bytes,
    const odm_master_request *request,
    odm_master_plan *out_plan);

odm_status odm_studio_master_run(
    const uint8_t *revision_record,
    uint64_t revision_bytes,
    const odm_master_request *request,
    const odm_master_sink *sink,
    uint8_t *frame_buffer,
    uint64_t frame_capacity,
    void *renderer_scratch,
    uint64_t renderer_scratch_capacity,
    uint8_t *receipt_buffer,
    uint64_t receipt_capacity,
    uint64_t *out_receipt_required,
    odm_render_receipt_info *out_receipt_info);

#ifdef __cplusplus
}
#endif
#endif /* ODM_STUDIO_H */

#ifndef ODM_RENDERER_H
#define ODM_RENDERER_H

#include "odm_hash.h"
#include "odm_ir.h"
#include "odm_job.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_REFERENCE_RENDERER_VERSION_MAJOR 1u
#define ODM_REFERENCE_RENDERER_VERSION_MINOR 0u

#define ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL UINT32_C(1)
#define ODM_RENDER_SURFACE_RGBA8 UINT32_C(1)
#define ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL UINT32_C(2)
#define ODM_COLOR_PRIMARIES_BT709 UINT32_C(1)
#define ODM_COLOR_TRANSFER_LINEAR UINT32_C(1)
#define ODM_COLOR_TRANSFER_SRGB UINT32_C(2)
#define ODM_ALPHA_STRAIGHT UINT32_C(1)
#define ODM_ALPHA_PREMULTIPLIED UINT32_C(2)

#define ODM_RENDER_MAX_SURFACE_FRAMES UINT32_C(1048576)
#define ODM_RENDER_MAX_REFERENCE_PIXELS UINT64_C(67108864)
#define ODM_REFERENCE_SCRATCH_ALIGNMENT UINT32_C(8)

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t primaries;
    uint32_t transfer;
    uint32_t alpha_mode;
    uint32_t flags;
    uint32_t reserved0;
    int64_t start_sample;
    int64_t end_sample;
    uint64_t pixel_bytes;
    const uint8_t *pixels;
    uint32_t reserved[4];
} odm_render_surface_frame;

typedef struct {
    uint32_t resource_id;
    uint32_t kind;
    uint32_t frame_count;
    uint32_t flags;
    odm_sha256_digest source_sha256;
    const odm_render_surface_frame *frames;
    uint32_t reserved[4];
} odm_render_resource_view;

typedef struct {
    uint32_t resource_count;
    uint32_t flags;
    const odm_render_resource_view *resources;
    uint32_t reserved[4];
} odm_render_asset_set;

typedef struct {
    uint32_t renderer_version_major;
    uint32_t renderer_version_minor;
    uint32_t pixel_format;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t reserved0;
    int64_t frame_index;
    int64_t sample;
    uint64_t pixel_bytes;
    odm_sha256_digest assets_sha256;
    odm_sha256_digest pixel_sha256;
    odm_sha256_digest frame_sha256;
    uint32_t reserved[4];
} odm_reference_frame_info;

typedef struct {
    odm_sha256_context hash;
    odm_sha256_digest ir_sha256;
    odm_sha256_digest assets_sha256;
    uint64_t expected_frames;
    uint64_t next_frame;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t state;
    int64_t samples_per_frame;
    int64_t output_end_sample;
    uint32_t reserved[4];
} odm_reference_frame_root_context;

odm_status odm_render_asset_set_sha256(const odm_render_asset_set *assets,
                                       odm_sha256_digest *out_sha256);

odm_status odm_reference_render_requirements(
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    int64_t frame_index,
    const odm_music_analysis_tick *music_tick,
    uint64_t *out_frame_bytes,
    uint64_t *out_scratch_bytes);

/* Conservative maximum scratch for any frame in this canonical IR.
 * Parses/validates the IR once and uses total canonical node/transfer counts as
 * a safe upper bound; rendering still computes exact per-frame requirements. */
odm_status odm_reference_render_max_requirements(
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    uint64_t *out_frame_bytes,
    uint64_t *out_scratch_bytes);

odm_status odm_reference_render_frame(
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    int64_t frame_index,
    const odm_music_analysis_tick *music_tick,
    const odm_render_asset_set *assets,
    const odm_job_ticket *job_ticket,
    void *scratch,
    uint64_t scratch_bytes,
    uint8_t *frame_rgba16le,
    uint64_t frame_capacity,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes,
    odm_reference_frame_info *out_info);

odm_status odm_reference_frame_root_init(
    odm_reference_frame_root_context *context,
    const odm_render_ir_info *ir,
    const odm_sha256_digest *assets_sha256);

odm_status odm_reference_frame_root_update(
    odm_reference_frame_root_context *context,
    const odm_reference_frame_info *frame);

odm_status odm_reference_frame_root_final(
    odm_reference_frame_root_context *context,
    odm_sha256_digest *out_root);

#ifdef __cplusplus
}
#endif

#endif /* ODM_RENDERER_H */

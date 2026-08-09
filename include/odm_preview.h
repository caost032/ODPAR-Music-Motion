#ifndef ODM_PREVIEW_H
#define ODM_PREVIEW_H

#include "odm_hash.h"
#include "odm_job.h"
#include "odm_music_map.h"
#include "odm_renderer.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_PREVIEW_CONFIG_SCHEMA_VERSION 1u
#define ODM_PREVIEW_PLAN_SCHEMA_VERSION 1u
#define ODM_PREVIEW_FRAME_INFO_SCHEMA_VERSION 1u

#define ODM_PREVIEW_RASTER_EXACT   UINT32_C(0)
#define ODM_PREVIEW_RASTER_HALF    UINT32_C(1)
#define ODM_PREVIEW_RASTER_QUARTER UINT32_C(2)
#define ODM_PREVIEW_RASTER_EIGHTH  UINT32_C(3)

#define ODM_PREVIEW_PIXEL_RGBA8_SRGB_STRAIGHT UINT32_C(1)
#define ODM_PREVIEW_SEMANTICS_EXACT UINT32_C(1)
#define ODM_PREVIEW_MAX_RESOURCES UINT32_C(4096)
#define ODM_PREVIEW_MAX_FRAMES UINT32_C(1048576)
#define ODM_PREVIEW_SCRATCH_ALIGNMENT UINT32_C(8)

typedef struct {
    uint32_t schema_version;
    uint32_t raster_class;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t reserved[4];
} odm_preview_config;

typedef struct {
    uint32_t resource_id;
    uint32_t kind;
    uint32_t frame_count;
    uint32_t first_frame;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
    odm_sha256_digest source_sha256;
} odm_preview_asset_resource;

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
    uint64_t pixel_offset;
    uint64_t pixel_bytes;
    uint32_t reserved[4];
} odm_preview_asset_frame;

typedef struct {
    uint32_t schema_version;
    uint32_t raster_class;
    uint32_t pixel_format;
    uint32_t semantics_class;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t divisor;
    int32_t fps;
    int64_t frame_index;
    int64_t sample;
    uint64_t canonical_frame_bytes;
    uint64_t output_frame_bytes;
    uint64_t scratch_bytes;
    uint64_t work_units;
    odm_sha256_digest render_ir_sha256;
    uint32_t reserved[2];
} odm_preview_plan;

typedef struct {
    uint32_t schema_version;
    uint32_t raster_class;
    uint32_t pixel_format;
    uint32_t semantics_class;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t output_width;
    uint32_t output_height;
    int32_t fps;
    uint32_t flags;
    int64_t frame_index;
    int64_t sample;
    uint64_t output_bytes;
    odm_sha256_digest canonical_frame_sha256;
    odm_sha256_digest preview_pixel_sha256;
} odm_preview_frame_info;

odm_status odm_preview_plan_frame(
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    int64_t frame_index,
    const odm_music_analysis_tick *music_tick,
    const odm_preview_config *config,
    uint64_t resource_count,
    uint64_t frame_count,
    odm_preview_plan *out_plan);

odm_status odm_preview_render_frame(
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    int64_t frame_index,
    const odm_music_analysis_tick *music_tick,
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

#ifdef __cplusplus
}
#endif

#endif /* ODM_PREVIEW_H */

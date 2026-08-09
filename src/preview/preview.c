#include "odm_preview.h"

#include "renderer_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ODM_PREVIEW_ALIGN UINT64_C(8)

_Static_assert(sizeof(odm_preview_config) == 32u,
               "preview config ABI changed");
_Static_assert(sizeof(odm_preview_asset_resource) == 64u,
               "preview resource ABI changed");
_Static_assert(sizeof(odm_preview_asset_frame) == 80u,
               "preview frame ABI changed");
_Static_assert(sizeof(odm_preview_plan) == 128u,
               "preview plan ABI changed");
_Static_assert(sizeof(odm_preview_frame_info) == 128u,
               "preview frame info ABI changed");

static odm_status preview_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (UINT64_MAX - a < b) return ODM_STATUS_OVERFLOW;
    *out = a + b;
    return ODM_STATUS_OK;
}

static odm_status preview_mul(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (a != 0u && b > UINT64_MAX / a) return ODM_STATUS_OVERFLOW;
    *out = a * b;
    return ODM_STATUS_OK;
}

static odm_status preview_align8(uint64_t value, uint64_t *out) {
    uint64_t next;
    odm_status status = preview_add(value, ODM_PREVIEW_ALIGN - 1u, &next);
    if (status != ODM_STATUS_OK) return status;
    *out = next & ~(ODM_PREVIEW_ALIGN - 1u);
    return ODM_STATUS_OK;
}

static int preview_zero_u32(const uint32_t *values, uint32_t count) {
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        if (values[index] != 0u) return 0;
    }
    return 1;
}

static int preview_digest_zero(const odm_sha256_digest *digest) {
    uint32_t index;
    if (!digest) return 1;
    for (index = 0u; index < ODM_SHA256_DIGEST_BYTES; ++index) {
        if (digest->bytes[index] != 0u) return 0;
    }
    return 1;
}

static int preview_ranges_overlap(const void *a, uint64_t a_bytes,
                                  const void *b, uint64_t b_bytes) {
    uintptr_t a_start, b_start, a_end, b_end;
    if (a_bytes == 0u || b_bytes == 0u) return 0;
    if (!a || !b || a_bytes > (uint64_t)UINTPTR_MAX ||
        b_bytes > (uint64_t)UINTPTR_MAX) return 1;
    a_start = (uintptr_t)a;
    b_start = (uintptr_t)b;
    if ((uintptr_t)a_bytes > UINTPTR_MAX - a_start ||
        (uintptr_t)b_bytes > UINTPTR_MAX - b_start) return 1;
    a_end = a_start + (uintptr_t)a_bytes;
    b_end = b_start + (uintptr_t)b_bytes;
    return a_start < b_end && b_start < a_end;
}

static odm_status preview_config_validate(const odm_preview_config *config,
                                          uint32_t *out_divisor) {
    uint32_t divisor;
    if (!config || !out_divisor ||
        config->schema_version != ODM_PREVIEW_CONFIG_SCHEMA_VERSION ||
        config->pixel_format != ODM_PREVIEW_PIXEL_RGBA8_SRGB_STRAIGHT ||
        config->flags != 0u || !preview_zero_u32(config->reserved, 4u)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    switch (config->raster_class) {
        case ODM_PREVIEW_RASTER_EXACT: divisor = 1u; break;
        case ODM_PREVIEW_RASTER_HALF: divisor = 2u; break;
        case ODM_PREVIEW_RASTER_QUARTER: divisor = 4u; break;
        case ODM_PREVIEW_RASTER_EIGHTH: divisor = 8u; break;
        default: return ODM_STATUS_UNSUPPORTED;
    }
    *out_divisor = divisor;
    return ODM_STATUS_OK;
}

static odm_status preview_output_geometry(uint32_t width, uint32_t height,
                                          uint32_t divisor,
                                          uint32_t *out_width,
                                          uint32_t *out_height,
                                          uint64_t *out_bytes,
                                          uint64_t *out_pixels) {
    uint64_t pixels, bytes;
    uint32_t ow, oh;
    if (!out_width || !out_height || !out_bytes || !out_pixels || divisor == 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    ow = width / divisor + ((width % divisor) != 0u ? 1u : 0u);
    oh = height / divisor + ((height % divisor) != 0u ? 1u : 0u);
    if (ow == 0u || oh == 0u) return ODM_STATUS_INVARIANT_BROKEN;
    if (preview_mul((uint64_t)ow, (uint64_t)oh, &pixels) != ODM_STATUS_OK ||
        preview_mul(pixels, UINT64_C(4), &bytes) != ODM_STATUS_OK) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_width = ow;
    *out_height = oh;
    *out_bytes = bytes;
    *out_pixels = pixels;
    return ODM_STATUS_OK;
}

static odm_status preview_scratch_layout(uint64_t resource_count,
                                         uint64_t frame_count,
                                         uint64_t reference_scratch,
                                         uint64_t canonical_frame,
                                         uint64_t preview_frame,
                                         uint64_t *out_resource_offset,
                                         uint64_t *out_frame_offset,
                                         uint64_t *out_reference_offset,
                                         uint64_t *out_canonical_offset,
                                         uint64_t *out_preview_offset,
                                         uint64_t *out_total) {
    uint64_t off = 0u, bytes;
    odm_status status;
    if (resource_count > ODM_PREVIEW_MAX_RESOURCES ||
        frame_count > ODM_PREVIEW_MAX_FRAMES) return ODM_STATUS_BUDGET_EXCEEDED;
    if (!out_total) return ODM_STATUS_INVALID_ARGUMENT;
    if (out_resource_offset) *out_resource_offset = off;
    status = preview_mul(resource_count, (uint64_t)sizeof(odm_render_resource_view), &bytes);
    if (status != ODM_STATUS_OK || preview_add(off, bytes, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (preview_align8(off, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (out_frame_offset) *out_frame_offset = off;
    status = preview_mul(frame_count, (uint64_t)sizeof(odm_render_surface_frame), &bytes);
    if (status != ODM_STATUS_OK || preview_add(off, bytes, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (preview_align8(off, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (out_reference_offset) *out_reference_offset = off;
    if (preview_add(off, reference_scratch, &off) != ODM_STATUS_OK ||
        preview_align8(off, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (out_canonical_offset) *out_canonical_offset = off;
    if (preview_add(off, canonical_frame, &off) != ODM_STATUS_OK ||
        preview_align8(off, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (out_preview_offset) *out_preview_offset = off;
    if (preview_add(off, preview_frame, &off) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    *out_total = off;
    return ODM_STATUS_OK;
}

static odm_status preview_plan_internal(const uint8_t *render_ir,
                                        uint64_t render_ir_bytes,
                                        int64_t frame_index,
                                        const odm_music_analysis_tick *music_tick,
                                        const odm_preview_config *config,
                                        uint64_t resource_count,
                                        uint64_t frame_count,
                                        odm_preview_plan *out_plan,
                                        uint64_t *out_reference_scratch) {
    odm_render_ir_info ir_info;
    odm_preview_plan plan;
    odm_sha256_digest ir_sha;
    uint64_t canonical_frame, reference_scratch, output_bytes, output_pixels;
    uint64_t source_pixels, work_units, scratch_total;
    uint32_t divisor, output_width, output_height;
    odm_status status;
    if (!render_ir || render_ir_bytes == 0u || !out_plan) return ODM_STATUS_INVALID_ARGUMENT;
    status = preview_config_validate(config, &divisor);
    if (status != ODM_STATUS_OK) return status;
    status = odm_render_ir_validate(render_ir, render_ir_bytes, NULL, NULL, &ir_info);
    if (status != ODM_STATUS_OK) return status;
    if (frame_index < 0 || frame_index >= ir_info.frame_count ||
        ir_info.samples_per_frame <= 0 ||
        frame_index > INT64_MAX / ir_info.samples_per_frame) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_reference_render_requirements(render_ir, render_ir_bytes, frame_index,
                                               music_tick, &canonical_frame,
                                               &reference_scratch);
    if (status != ODM_STATUS_OK) return status;
    status = preview_output_geometry(ir_info.width, ir_info.height, divisor,
                                     &output_width, &output_height,
                                     &output_bytes, &output_pixels);
    if (status != ODM_STATUS_OK) return status;
    status = preview_mul((uint64_t)ir_info.width, (uint64_t)ir_info.height,
                         &source_pixels);
    if (status != ODM_STATUS_OK ||
        preview_add(source_pixels, output_pixels, &work_units) != ODM_STATUS_OK ||
        preview_add(work_units, UINT64_C(1), &work_units) != ODM_STATUS_OK) {
        return ODM_STATUS_OVERFLOW;
    }
    status = preview_scratch_layout(resource_count, frame_count, reference_scratch,
                                    canonical_frame, output_bytes,
                                    NULL, NULL, NULL, NULL, NULL, &scratch_total);
    if (status != ODM_STATUS_OK) return status;
    status = odm_sha256(render_ir, render_ir_bytes, &ir_sha);
    if (status != ODM_STATUS_OK) return status;
    memset(&plan, 0, sizeof(plan));
    plan.schema_version = ODM_PREVIEW_PLAN_SCHEMA_VERSION;
    plan.raster_class = config->raster_class;
    plan.pixel_format = config->pixel_format;
    plan.semantics_class = ODM_PREVIEW_SEMANTICS_EXACT;
    plan.source_width = ir_info.width;
    plan.source_height = ir_info.height;
    plan.output_width = output_width;
    plan.output_height = output_height;
    plan.divisor = divisor;
    plan.fps = ir_info.fps;
    plan.frame_index = frame_index;
    plan.sample = frame_index * ir_info.samples_per_frame;
    plan.canonical_frame_bytes = canonical_frame;
    plan.output_frame_bytes = output_bytes;
    plan.scratch_bytes = scratch_total;
    plan.work_units = work_units;
    plan.render_ir_sha256 = ir_sha;
    *out_plan = plan;
    if (out_reference_scratch) *out_reference_scratch = reference_scratch;
    return ODM_STATUS_OK;
}

odm_status odm_preview_plan_frame(const uint8_t *render_ir,
                                  uint64_t render_ir_bytes,
                                  int64_t frame_index,
                                  const odm_music_analysis_tick *music_tick,
                                  const odm_preview_config *config,
                                  uint64_t resource_count,
                                  uint64_t frame_count,
                                  odm_preview_plan *out_plan) {
    return preview_plan_internal(render_ir, render_ir_bytes, frame_index,
                                 music_tick, config, resource_count, frame_count,
                                 out_plan, NULL);
}

static odm_status preview_validate_flat_assets(
    const odm_preview_asset_resource *resources, uint64_t resource_count,
    const odm_preview_asset_frame *frames, uint64_t frame_count,
    const uint8_t *pixel_blob, uint64_t pixel_blob_bytes) {
    uint64_t resource_index, frame_index, next_frame = 0u, pixel_cursor = 0u;
    uint32_t previous_id = 0u;
    if (resource_count > ODM_PREVIEW_MAX_RESOURCES || frame_count > ODM_PREVIEW_MAX_FRAMES) {
        return ODM_STATUS_BUDGET_EXCEEDED;
    }
    if ((resource_count != 0u && !resources) || (frame_count != 0u && !frames) ||
        (pixel_blob_bytes != 0u && !pixel_blob)) return ODM_STATUS_INVALID_ARGUMENT;
    if (resource_count == 0u && frame_count != 0u) return ODM_STATUS_INVALID_DATA;
    for (resource_index = 0u; resource_index < resource_count; ++resource_index) {
        const odm_preview_asset_resource *resource = &resources[resource_index];
        uint64_t end_frame;
        if (resource->resource_id == 0u || resource->resource_id <= previous_id ||
            (resource->kind != ODM_SCORE_RESOURCE_IMAGE &&
             resource->kind != ODM_SCORE_RESOURCE_VIDEO) ||
            resource->frame_count == 0u || resource->flags != 0u ||
            resource->reserved0 != 0u || resource->reserved1 != 0u ||
            resource->reserved2 != 0u || preview_digest_zero(&resource->source_sha256) ||
            (uint64_t)resource->first_frame != next_frame) return ODM_STATUS_INVALID_DATA;
        if (resource->kind == ODM_SCORE_RESOURCE_IMAGE && resource->frame_count != 1u) {
            return ODM_STATUS_INVALID_DATA;
        }
        if (preview_add((uint64_t)resource->first_frame,
                        (uint64_t)resource->frame_count, &end_frame) != ODM_STATUS_OK ||
            end_frame > frame_count) return ODM_STATUS_INVALID_DATA;
        next_frame = end_frame;
        previous_id = resource->resource_id;
    }
    if (next_frame != frame_count) return ODM_STATUS_INVALID_DATA;
    for (frame_index = 0u; frame_index < frame_count; ++frame_index) {
        const odm_preview_asset_frame *frame = &frames[frame_index];
        uint64_t pixels, bytes, end;
        if (frame->width == 0u || frame->height == 0u ||
            frame->width > 16384u || frame->height > 16384u ||
            frame->pixel_format != ODM_RENDER_SURFACE_RGBA8 ||
            frame->primaries != ODM_COLOR_PRIMARIES_BT709 ||
            (frame->transfer != ODM_COLOR_TRANSFER_LINEAR &&
             frame->transfer != ODM_COLOR_TRANSFER_SRGB) ||
            frame->alpha_mode != ODM_ALPHA_STRAIGHT || frame->flags != 0u ||
            frame->reserved0 != 0u || !preview_zero_u32(frame->reserved, 4u) ||
            frame->pixel_offset != pixel_cursor) return ODM_STATUS_INVALID_DATA;
        if (preview_mul((uint64_t)frame->width, (uint64_t)frame->height, &pixels) != ODM_STATUS_OK ||
            pixels > ODM_RENDER_MAX_REFERENCE_PIXELS ||
            preview_mul(pixels, UINT64_C(4), &bytes) != ODM_STATUS_OK ||
            bytes != frame->pixel_bytes ||
            preview_add(frame->pixel_offset, frame->pixel_bytes, &end) != ODM_STATUS_OK ||
            end > pixel_blob_bytes) return ODM_STATUS_INVALID_DATA;
        pixel_cursor = end;
    }
    if (pixel_cursor != pixel_blob_bytes) return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static odm_status preview_build_native_assets(
    const odm_preview_asset_resource *resources, uint64_t resource_count,
    const odm_preview_asset_frame *frames, uint64_t frame_count,
    const uint8_t *pixel_blob,
    odm_render_resource_view *native_resources,
    odm_render_surface_frame *native_frames,
    odm_render_asset_set *out_assets) {
    uint64_t index;
    if (!out_assets) return ODM_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < frame_count; ++index) {
        const odm_preview_asset_frame *source = &frames[index];
        odm_render_surface_frame *target = &native_frames[index];
        memset(target, 0, sizeof(*target));
        target->width = source->width;
        target->height = source->height;
        target->pixel_format = source->pixel_format;
        target->primaries = source->primaries;
        target->transfer = source->transfer;
        target->alpha_mode = source->alpha_mode;
        target->start_sample = source->start_sample;
        target->end_sample = source->end_sample;
        target->pixel_bytes = source->pixel_bytes;
        target->pixels = pixel_blob + source->pixel_offset;
    }
    for (index = 0u; index < resource_count; ++index) {
        const odm_preview_asset_resource *source = &resources[index];
        odm_render_resource_view *target = &native_resources[index];
        memset(target, 0, sizeof(*target));
        target->resource_id = source->resource_id;
        target->kind = source->kind;
        target->frame_count = source->frame_count;
        target->source_sha256 = source->source_sha256;
        target->frames = &native_frames[source->first_frame];
    }
    memset(out_assets, 0, sizeof(*out_assets));
    out_assets->resource_count = (uint32_t)resource_count;
    out_assets->resources = native_resources;
    return ODM_STATUS_OK;
}

static uint16_t preview_get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint16_t preview_average_u16(uint64_t sum, uint64_t count) {
    return (uint16_t)((sum + count / UINT64_C(2)) / count);
}

static uint16_t preview_unpremultiply_u16(uint16_t premul, uint16_t alpha) {
    uint64_t value;
    if (alpha == 0u) return 0u;
    value = ((uint64_t)premul * UINT64_C(65535) + (uint64_t)alpha / UINT64_C(2)) /
            (uint64_t)alpha;
    if (value > UINT64_C(65535)) value = UINT64_C(65535);
    return (uint16_t)value;
}

static uint8_t preview_alpha_u16_to_u8(uint16_t alpha) {
    return (uint8_t)(((uint64_t)alpha * UINT64_C(255) + UINT64_C(32767)) /
                     UINT64_C(65535));
}

static odm_status preview_cancel(const odm_job_ticket *ticket) {
    int cancelled = 0;
    odm_status status;
    if (!ticket) return ODM_STATUS_OK;
    status = odm_job_should_cancel(*ticket, &cancelled);
    if (status != ODM_STATUS_OK) return status;
    return cancelled ? ODM_STATUS_CANCELLED : ODM_STATUS_OK;
}

static odm_status preview_progress_validate(const odm_job_ticket *ticket,
                                            uint64_t work_units) {
    odm_progress_snapshot snapshot;
    odm_status status;
    if (!ticket) return ODM_STATUS_OK;
    status = odm_job_snapshot(ticket->job, &snapshot);
    if (status != ODM_STATUS_OK) return status;
    if (snapshot.state != ODM_JOB_RUNNING || snapshot.generation != ticket->generation ||
        snapshot.work_total != work_units) return ODM_STATUS_INVALID_STATE;
    return odm_job_publish(*ticket, ODM_PHASE_PREPARE, 0u);
}

static odm_status preview_downsample(const uint8_t *canonical,
                                     const odm_preview_plan *plan,
                                     const odm_job_ticket *job_ticket,
                                     uint8_t *staging) {
    uint32_t oy, ox, divisor = plan->divisor;
    uint64_t source_pixels;
    odm_status status;
    if (preview_mul((uint64_t)plan->source_width, (uint64_t)plan->source_height,
                    &source_pixels) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    for (oy = 0u; oy < plan->output_height; ++oy) {
        uint32_t sy0 = oy * divisor;
        uint32_t sy1 = sy0 + divisor;
        if (sy1 > plan->source_height || sy1 < sy0) sy1 = plan->source_height;
        status = preview_cancel(job_ticket);
        if (status != ODM_STATUS_OK) return status;
        for (ox = 0u; ox < plan->output_width; ++ox) {
            uint32_t sx0 = ox * divisor;
            uint32_t sx1 = sx0 + divisor;
            uint32_t sy, sx;
            uint64_t sr = 0u, sg = 0u, sb = 0u, sa = 0u, count = 0u;
            uint16_t ar, ag, ab, aa;
            uint8_t *dst;
            if (sx1 > plan->source_width || sx1 < sx0) sx1 = plan->source_width;
            for (sy = sy0; sy < sy1; ++sy) {
                for (sx = sx0; sx < sx1; ++sx) {
                    const uint8_t *src = canonical +
                        (((uint64_t)sy * plan->source_width + sx) * UINT64_C(8));
                    sr += preview_get16(src);
                    sg += preview_get16(src + 2u);
                    sb += preview_get16(src + 4u);
                    sa += preview_get16(src + 6u);
                    ++count;
                }
            }
            if (count == 0u) return ODM_STATUS_INVARIANT_BROKEN;
            ar = preview_average_u16(sr, count);
            ag = preview_average_u16(sg, count);
            ab = preview_average_u16(sb, count);
            aa = preview_average_u16(sa, count);
            ar = preview_unpremultiply_u16(ar, aa);
            ag = preview_unpremultiply_u16(ag, aa);
            ab = preview_unpremultiply_u16(ab, aa);
            dst = staging + (((uint64_t)oy * plan->output_width + ox) * UINT64_C(4));
            dst[0] = odm_renderer_linear_u16_to_srgb8(ar);
            dst[1] = odm_renderer_linear_u16_to_srgb8(ag);
            dst[2] = odm_renderer_linear_u16_to_srgb8(ab);
            dst[3] = preview_alpha_u16_to_u8(aa);
        }
        if (job_ticket) {
            uint64_t row_done, done;
            if (preview_mul((uint64_t)(oy + 1u), (uint64_t)plan->output_width,
                            &row_done) != ODM_STATUS_OK ||
                preview_add(source_pixels, row_done, &done) != ODM_STATUS_OK) {
                return ODM_STATUS_OVERFLOW;
            }
            status = odm_job_publish(*job_ticket, ODM_PHASE_EXECUTE, done);
            if (status != ODM_STATUS_OK) return status;
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_preview_render_frame(
    const uint8_t *render_ir, uint64_t render_ir_bytes, int64_t frame_index,
    const odm_music_analysis_tick *music_tick, const odm_preview_config *config,
    const odm_preview_asset_resource *resources, uint64_t resource_count,
    const odm_preview_asset_frame *frames, uint64_t frame_count,
    const uint8_t *pixel_blob, uint64_t pixel_blob_bytes,
    const odm_job_ticket *job_ticket, void *scratch, uint64_t scratch_bytes,
    uint8_t *rgba8, uint64_t rgba8_capacity, uint64_t *out_required_rgba8,
    uint64_t *out_required_scratch, odm_preview_frame_info *out_info) {
    odm_preview_plan plan;
    odm_render_asset_set assets;
    odm_render_resource_view *native_resources;
    odm_render_surface_frame *native_frames;
    odm_reference_frame_info reference_info;
    odm_preview_frame_info preview_info;
    odm_sha256_digest preview_sha;
    uint64_t reference_scratch, resource_off, frame_off, reference_off;
    uint64_t canonical_off, preview_off, total_scratch;
    uint64_t got_frame = 0u, got_scratch = 0u, source_pixels;
    uint8_t *canonical, *staging;
    odm_status status;
    if (!out_required_rgba8 || !out_required_scratch || !out_info) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = preview_plan_internal(render_ir, render_ir_bytes, frame_index,
                                   music_tick, config, resource_count, frame_count,
                                   &plan, &reference_scratch);
    if (status != ODM_STATUS_OK) return status;
    *out_required_rgba8 = plan.output_frame_bytes;
    *out_required_scratch = plan.scratch_bytes;
    status = preview_validate_flat_assets(resources, resource_count, frames,
                                          frame_count, pixel_blob, pixel_blob_bytes);
    if (status != ODM_STATUS_OK) return status;
    if (!scratch || scratch_bytes < plan.scratch_bytes || !rgba8 ||
        rgba8_capacity < plan.output_frame_bytes) return ODM_STATUS_BUFFER_TOO_SMALL;
    if ((((uintptr_t)scratch) & (uintptr_t)(ODM_PREVIEW_SCRATCH_ALIGNMENT - 1u)) != 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (plan.scratch_bytes > (uint64_t)SIZE_MAX ||
        plan.output_frame_bytes > (uint64_t)SIZE_MAX) return ODM_STATUS_OVERFLOW;
    if (preview_ranges_overlap(scratch, plan.scratch_bytes, rgba8, plan.output_frame_bytes) ||
        preview_ranges_overlap(scratch, plan.scratch_bytes, render_ir, render_ir_bytes) ||
        preview_ranges_overlap(rgba8, plan.output_frame_bytes, render_ir, render_ir_bytes) ||
        preview_ranges_overlap(scratch, plan.scratch_bytes, resources,
                               resource_count * (uint64_t)sizeof(*resources)) ||
        preview_ranges_overlap(rgba8, plan.output_frame_bytes, resources,
                               resource_count * (uint64_t)sizeof(*resources)) ||
        preview_ranges_overlap(scratch, plan.scratch_bytes, frames,
                               frame_count * (uint64_t)sizeof(*frames)) ||
        preview_ranges_overlap(rgba8, plan.output_frame_bytes, frames,
                               frame_count * (uint64_t)sizeof(*frames)) ||
        preview_ranges_overlap(scratch, plan.scratch_bytes, pixel_blob, pixel_blob_bytes) ||
        preview_ranges_overlap(rgba8, plan.output_frame_bytes, pixel_blob, pixel_blob_bytes)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = preview_scratch_layout(resource_count, frame_count, reference_scratch,
                                    plan.canonical_frame_bytes, plan.output_frame_bytes,
                                    &resource_off, &frame_off, &reference_off,
                                    &canonical_off, &preview_off, &total_scratch);
    if (status != ODM_STATUS_OK || total_scratch != plan.scratch_bytes) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    native_resources = (odm_render_resource_view *)((uint8_t *)scratch + resource_off);
    native_frames = (odm_render_surface_frame *)((uint8_t *)scratch + frame_off);
    canonical = (uint8_t *)scratch + canonical_off;
    staging = (uint8_t *)scratch + preview_off;
    status = preview_build_native_assets(resources, resource_count, frames, frame_count,
                                         pixel_blob, native_resources, native_frames,
                                         &assets);
    if (status != ODM_STATUS_OK) return status;
    status = preview_progress_validate(job_ticket, plan.work_units);
    if (status != ODM_STATUS_OK) return status;
    if (job_ticket) {
        status = odm_job_publish(*job_ticket, ODM_PHASE_VALIDATE, 0u);
        if (status != ODM_STATUS_OK) return status;
    }
    status = odm_reference_render_frame(render_ir, render_ir_bytes, frame_index,
                                        music_tick, &assets, job_ticket,
                                        (uint8_t *)scratch + reference_off,
                                        reference_scratch, canonical,
                                        plan.canonical_frame_bytes, &got_frame,
                                        &got_scratch, &reference_info);
    if (status != ODM_STATUS_OK) return status;
    if (got_frame != plan.canonical_frame_bytes || got_scratch != reference_scratch ||
        reference_info.frame_index != frame_index || reference_info.sample != plan.sample ||
        reference_info.width != plan.source_width ||
        reference_info.height != plan.source_height) return ODM_STATUS_INVARIANT_BROKEN;
    if (preview_mul((uint64_t)plan.source_width, (uint64_t)plan.source_height,
                    &source_pixels) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    if (job_ticket) {
        status = odm_job_publish(*job_ticket, ODM_PHASE_EXECUTE, source_pixels);
        if (status != ODM_STATUS_OK) return status;
    }
    status = preview_downsample(canonical, &plan, job_ticket, staging);
    if (status != ODM_STATUS_OK) return status;
    status = preview_cancel(job_ticket);
    if (status != ODM_STATUS_OK) return status;
    status = odm_sha256(staging, plan.output_frame_bytes, &preview_sha);
    if (status != ODM_STATUS_OK) return status;
    if (job_ticket) {
        status = odm_job_publish(*job_ticket, ODM_PHASE_FINALIZE, plan.work_units);
        if (status != ODM_STATUS_OK) return status;
    }
    memset(&preview_info, 0, sizeof(preview_info));
    preview_info.schema_version = ODM_PREVIEW_FRAME_INFO_SCHEMA_VERSION;
    preview_info.raster_class = plan.raster_class;
    preview_info.pixel_format = plan.pixel_format;
    preview_info.semantics_class = ODM_PREVIEW_SEMANTICS_EXACT;
    preview_info.source_width = plan.source_width;
    preview_info.source_height = plan.source_height;
    preview_info.output_width = plan.output_width;
    preview_info.output_height = plan.output_height;
    preview_info.fps = plan.fps;
    preview_info.frame_index = frame_index;
    preview_info.sample = reference_info.sample;
    preview_info.output_bytes = plan.output_frame_bytes;
    preview_info.canonical_frame_sha256 = reference_info.pixel_sha256;
    preview_info.preview_pixel_sha256 = preview_sha;
    memcpy(rgba8, staging, (size_t)plan.output_frame_bytes);
    *out_info = preview_info;
    return ODM_STATUS_OK;
}

#include "odm_studio.h"

#include "odm_ir.h"
#include "odm_music_map.h"
#include "odm_time.h"

#include <stdint.h>
#include <string.h>

static odm_status studio_revision_core(
    const uint8_t *revision, uint64_t revision_bytes,
    const uint8_t *render_ir, uint64_t render_ir_bytes,
    const uint8_t *analysis_bin, uint64_t analysis_bin_bytes,
    odm_studio_revision_info *out_revision,
    odm_render_ir_info *out_ir,
    odm_music_map_view *out_map) {
    odm_studio_revision_info rev;
    odm_render_ir_info ir;
    odm_music_map_view map;
    odm_status st;
    if (!revision || !render_ir || !analysis_bin || !out_revision || !out_ir || !out_map) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    st = odm_studio_revision_validate(revision, revision_bytes, &rev);
    if (st != ODM_STATUS_OK) return st;
    st = odm_render_ir_validate(render_ir, render_ir_bytes, &rev.score_sha256,
                                &rev.music_policy_sha256, &ir);
    if (st != ODM_STATUS_OK) return st;
    if (!odm_sha256_equal(&ir.record_sha256, &rev.render_ir_sha256) ||
        !odm_sha256_equal(&ir.capability_sha256, &rev.capability_sha256) ||
        !odm_sha256_equal(&ir.music_map_sha256, &rev.music_map_sha256) ||
        !odm_sha256_equal(&ir.music_policy_sha256, &rev.music_policy_sha256) ||
        ir.width != rev.width || ir.height != rev.height || ir.fps != rev.fps ||
        ir.project_end_sample != rev.project_end_sample || ir.project_seed != rev.project_seed) {
        return ODM_STATUS_INTEGRITY_ERROR;
    }
    st = odm_music_map_view_open(analysis_bin, analysis_bin_bytes,
                                 &rev.music_policy_sha256, &map);
    if (st != ODM_STATUS_OK) return st;
    if (!odm_sha256_equal(&map.info.record_sha256, &rev.music_map_sha256) ||
        map.info.canonical_frames != ir.canonical_music_frames) {
        return ODM_STATUS_INTEGRITY_ERROR;
    }
    *out_revision = rev;
    *out_ir = ir;
    *out_map = map;
    return ODM_STATUS_OK;
}

static odm_status studio_frame_tick(const odm_render_ir_info *ir,
                                    const odm_music_map_view *map,
                                    int64_t frame_index,
                                    odm_music_analysis_tick *tick,
                                    const odm_music_analysis_tick **out_tick) {
    int64_t sample;
    uint64_t tick_index;
    odm_status st;
    if (!ir || !map || !tick || !out_tick) return ODM_STATUS_INVALID_ARGUMENT;
    if (frame_index < 0 || frame_index >= ir->frame_count) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_time_frame_start(frame_index, ir->fps, &sample);
    if (st != ODM_STATUS_OK) return st;
    if (sample < 0) return ODM_STATUS_INVARIANT_BROKEN;
    if ((uint64_t)sample >= ir->canonical_music_frames) {
        *out_tick = NULL;
        return ODM_STATUS_OK;
    }
    tick_index = (uint64_t)sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
    if (tick_index >= map->tick_count) return ODM_STATUS_INVARIANT_BROKEN;
    st = odm_music_map_view_read_tick(map, tick_index, tick);
    if (st != ODM_STATUS_OK) return st;
    *out_tick = tick;
    return ODM_STATUS_OK;
}

odm_status odm_studio_preview_plan(
    const uint8_t *revision, uint64_t revision_bytes,
    const uint8_t *render_ir, uint64_t render_ir_bytes,
    const uint8_t *analysis_bin, uint64_t analysis_bin_bytes,
    int64_t frame_index, const odm_preview_config *config,
    uint64_t resource_count, uint64_t frame_count,
    odm_preview_plan *out_plan) {
    odm_studio_revision_info rev;
    odm_render_ir_info ir;
    odm_music_map_view map;
    odm_music_analysis_tick tick;
    const odm_music_analysis_tick *tick_ptr = NULL;
    odm_status st;
    if (!out_plan) return ODM_STATUS_INVALID_ARGUMENT;
    st = studio_revision_core(revision, revision_bytes, render_ir, render_ir_bytes,
                              analysis_bin, analysis_bin_bytes, &rev, &ir, &map);
    if (st != ODM_STATUS_OK) return st;
    (void)rev;
    st = studio_frame_tick(&ir, &map, frame_index, &tick, &tick_ptr);
    if (st != ODM_STATUS_OK) return st;
    return odm_preview_plan_frame(render_ir, render_ir_bytes, frame_index, tick_ptr,
                                  config, resource_count, frame_count, out_plan);
}

odm_status odm_studio_preview_render(
    const uint8_t *revision, uint64_t revision_bytes,
    const uint8_t *render_ir, uint64_t render_ir_bytes,
    const uint8_t *analysis_bin, uint64_t analysis_bin_bytes,
    int64_t frame_index, const odm_preview_config *config,
    const odm_preview_asset_resource *resources, uint64_t resource_count,
    const odm_preview_asset_frame *frames, uint64_t frame_count,
    const uint8_t *pixel_blob, uint64_t pixel_blob_bytes,
    const odm_job_ticket *job_ticket, void *scratch, uint64_t scratch_bytes,
    uint8_t *rgba8, uint64_t rgba8_capacity,
    uint64_t *out_required_rgba8, uint64_t *out_required_scratch,
    odm_preview_frame_info *out_info) {
    odm_studio_revision_info rev;
    odm_render_ir_info ir;
    odm_music_map_view map;
    odm_music_analysis_tick tick;
    const odm_music_analysis_tick *tick_ptr = NULL;
    odm_status st;
    st = studio_revision_core(revision, revision_bytes, render_ir, render_ir_bytes,
                              analysis_bin, analysis_bin_bytes, &rev, &ir, &map);
    if (st != ODM_STATUS_OK) return st;
    (void)rev;
    st = studio_frame_tick(&ir, &map, frame_index, &tick, &tick_ptr);
    if (st != ODM_STATUS_OK) return st;
    return odm_preview_render_frame(render_ir, render_ir_bytes, frame_index, tick_ptr,
                                    config, resources, resource_count, frames, frame_count,
                                    pixel_blob, pixel_blob_bytes, job_ticket,
                                    scratch, scratch_bytes, rgba8, rgba8_capacity,
                                    out_required_rgba8, out_required_scratch, out_info);
}

static odm_status master_match(const odm_studio_revision_info *r,
                               const odm_master_request *q,
                               const odm_master_plan *p) {
    if (!r || !q || !p || !q->expected_package_public_key) return ODM_STATUS_INVALID_ARGUMENT;
    if (memcmp(q->expected_package_public_key, r->package_signer_public_key,
               ODM_ODPARMS_PUBLIC_KEY_BYTES) != 0 ||
        !odm_sha256_equal(&p->package_sha256, &r->package_sha256) ||
        !odm_sha256_equal(&p->score_sha256, &r->score_sha256) ||
        !odm_sha256_equal(&p->capability_sha256, &r->capability_sha256) ||
        !odm_sha256_equal(&p->music_map_sha256, &r->music_map_sha256) ||
        !odm_sha256_equal(&p->music_policy_sha256, &r->music_policy_sha256) ||
        !odm_sha256_equal(&p->render_ir_sha256, &r->render_ir_sha256) ||
        p->width != r->width || p->height != r->height || p->fps != r->fps ||
        p->project_end_sample != r->project_end_sample || p->project_seed != r->project_seed) {
        return ODM_STATUS_INTEGRITY_ERROR;
    }
    return ODM_STATUS_OK;
}

odm_status odm_studio_master_plan_build(const uint8_t *revision,
                                        uint64_t revision_bytes,
                                        const odm_master_request *request,
                                        odm_master_plan *out) {
    odm_studio_revision_info r;
    odm_master_plan p;
    odm_status st;
    if (!revision || !request || !out) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_studio_revision_validate(revision, revision_bytes, &r);
    if (st != ODM_STATUS_OK) return st;
    st = odm_master_plan_build(request, &p);
    if (st != ODM_STATUS_OK) return st;
    st = master_match(&r, request, &p);
    if (st != ODM_STATUS_OK) return st;
    *out = p;
    return ODM_STATUS_OK;
}

odm_status odm_studio_master_run(
    const uint8_t *revision, uint64_t revision_bytes,
    const odm_master_request *request, const odm_master_sink *sink,
    uint8_t *frame, uint64_t frame_cap, void *scratch, uint64_t scratch_cap,
    uint8_t *receipt, uint64_t receipt_cap, uint64_t *out_required,
    odm_render_receipt_info *out_info) {
    odm_master_plan p;
    odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    /* Studio-level revision/package coherence is resolved before Gate 9 can
     * call sink->begin().  No Studio mismatch is discovered after commit. */
    st = odm_studio_master_plan_build(revision, revision_bytes, request, &p);
    if (st != ODM_STATUS_OK) return st;
    if (frame_cap < p.frame_bytes || scratch_cap < p.renderer_scratch_bytes) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    return odm_master_run(request, sink, frame, frame_cap, scratch, scratch_cap,
                          receipt, receipt_cap, out_required, out_info);
}

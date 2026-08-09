#include "test_harness.h"

#include "odm_ffi.h"
#include "odm_preview.h"
#include "odm_score.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void preview_node_init(odm_score_node *node, uint32_t id, uint32_t role,
                              uint32_t capability, int32_t z_index) {
    memset(node, 0, sizeof(*node));
    node->node_id = id;
    node->scene_id = 1u;
    node->role = role;
    node->capability_id = capability;
    node->capability_major = 1u;
    node->state_model = ODM_STATELESS;
    node->z_index = z_index;
    node->start_sample = 0;
    node->end_sample = 1600;
    node->scale_x_micro = ODM_MICRO_ONE;
    node->scale_y_micro = ODM_MICRO_ONE;
    node->opacity_micro = ODM_MICRO_ONE;
    node->seed_domain = UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)id;
}

static uint8_t *preview_build_ir(uint64_t *out_bytes) {
    odm_score_header header;
    odm_score_act act;
    odm_score_scene scene;
    odm_score_node nodes[3];
    odm_score_view score;
    odm_music_map_binding music;
    uint8_t *ir;
    uint64_t required = 0u;
    odm_status status;
    if (!out_bytes) return NULL;
    memset(&header, 0, sizeof(header));
    memset(&act, 0, sizeof(act));
    memset(&scene, 0, sizeof(scene));
    memset(nodes, 0, sizeof(nodes));
    memset(&score, 0, sizeof(score));
    memset(&music, 0, sizeof(music));
    header.schema_version_major = ODM_SCORE_SCHEMA_MAJOR;
    header.schema_version_minor = ODM_SCORE_SCHEMA_MINOR;
    header.fps = 30;
    header.width = 10u;
    header.height = 8u;
    header.project_end_sample = 1600;
    header.project_seed = UINT64_C(0x4f44504152505256);
    header.act_count = 1u;
    header.scene_count = 1u;
    header.node_count = 3u;
    act.act_id = 1u;
    act.start_sample = 0;
    act.end_sample = 1600;
    scene.scene_id = 1u;
    scene.act_id = 1u;
    scene.environment_node_id = 1u;
    scene.primary_node_id = 2u;
    scene.start_sample = 0;
    scene.end_sample = 1600;
    preview_node_init(&nodes[0], 1u, ODM_SCORE_NODE_ENVIRONMENT,
                      ODM_CAP_BLACK_VOID, -100);
    preview_node_init(&nodes[1], 2u, ODM_SCORE_NODE_PRIMARY,
                      ODM_CAP_PRIMARY_EMPTY, 0);
    preview_node_init(&nodes[2], 3u, ODM_SCORE_NODE_EFFECT,
                      ODM_CAP_GRID_PROOF, 10);
    score.header = &header;
    score.acts = &act;
    score.scenes = &scene;
    score.nodes = nodes;
    music.canonical_frames = 1600u;
    if (odm_sha256("preview-map", 11u, &music.music_map_sha256) != ODM_STATUS_OK ||
        odm_music_policy_current_sha256(&music.music_policy_sha256) != ODM_STATUS_OK) {
        return NULL;
    }
    status = odm_score_validate(&score);
    if (status != ODM_STATUS_OK) return NULL;
    status = odm_score_compile_render_ir(&score, &music, NULL, 0u, &required, NULL);
    if (status != ODM_STATUS_BUFFER_TOO_SMALL || required == 0u || required > (uint64_t)SIZE_MAX) {
        return NULL;
    }
    ir = (uint8_t *)malloc((size_t)required);
    if (!ir) return NULL;
    status = odm_score_compile_render_ir(&score, &music, ir, required, &required, NULL);
    if (status != ODM_STATUS_OK) {
        free(ir);
        return NULL;
    }
    *out_bytes = required;
    return ir;
}

static void preview_config(odm_preview_config *config, uint32_t raster_class) {
    memset(config, 0, sizeof(*config));
    config->schema_version = ODM_PREVIEW_CONFIG_SCHEMA_VERSION;
    config->raster_class = raster_class;
    config->pixel_format = ODM_PREVIEW_PIXEL_RGBA8_SRGB_STRAIGHT;
}

static void *aligned_job_storage(uint64_t bytes, uint32_t alignment, void **out_base) {
    uintptr_t raw, aligned;
    uint8_t *base;
    if (!out_base || alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
        bytes > (uint64_t)SIZE_MAX - (uint64_t)alignment) return NULL;
    base = (uint8_t *)malloc((size_t)bytes + (size_t)alignment);
    if (!base) return NULL;
    raw = (uintptr_t)base;
    aligned = (raw + (uintptr_t)alignment - 1u) & ~((uintptr_t)alignment - 1u);
    *out_base = base;
    return (void *)aligned;
}

void odm_test_preview(odm_test_context *context) {
    uint64_t ir_bytes = 0u;
    uint8_t *ir = preview_build_ir(&ir_bytes);
    odm_preview_config config;
    odm_preview_plan plan;
    static const uint32_t classes[4] = {
        ODM_PREVIEW_RASTER_EXACT, ODM_PREVIEW_RASTER_HALF,
        ODM_PREVIEW_RASTER_QUARTER, ODM_PREVIEW_RASTER_EIGHTH
    };
    static const uint32_t widths[4] = {10u, 5u, 3u, 2u};
    static const uint32_t heights[4] = {8u, 4u, 2u, 1u};
    uint32_t i;
    ODM_TEST_CHECK(context, ir != NULL);
    if (!ir) return;

    for (i = 0u; i < 4u; ++i) {
        uint8_t *scratch = NULL, *out1 = NULL, *out2 = NULL;
        uint64_t req_out = 0u, req_scratch = 0u;
        odm_preview_frame_info info1, info2;
        preview_config(&config, classes[i]);
        ODM_TEST_CHECK(context,
                       odm_preview_plan_frame(ir, ir_bytes, 0, NULL, &config,
                                              0u, 0u, &plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, plan.schema_version == ODM_PREVIEW_PLAN_SCHEMA_VERSION);
        ODM_TEST_CHECK(context, plan.source_width == 10u && plan.source_height == 8u);
        ODM_TEST_CHECK(context, plan.output_width == widths[i] &&
                                plan.output_height == heights[i]);
        ODM_TEST_CHECK(context, plan.output_frame_bytes ==
                                (uint64_t)widths[i] * (uint64_t)heights[i] * UINT64_C(4));
        ODM_TEST_CHECK(context, plan.semantics_class == ODM_PREVIEW_SEMANTICS_EXACT);
        scratch = (uint8_t *)malloc((size_t)plan.scratch_bytes + 8u);
        out1 = (uint8_t *)malloc((size_t)plan.output_frame_bytes);
        out2 = (uint8_t *)malloc((size_t)plan.output_frame_bytes);
        ODM_TEST_CHECK(context, scratch != NULL && out1 != NULL && out2 != NULL);
        if (scratch && out1 && out2) {
            memset(out1, 0xa5, (size_t)plan.output_frame_bytes);
            memset(&info1, 0xa5, sizeof(info1));
            ODM_TEST_CHECK(context,
                           odm_preview_render_frame(
                               ir, ir_bytes, 0, NULL, &config, NULL, 0u, NULL, 0u,
                               NULL, 0u, NULL, scratch, plan.scratch_bytes,
                               out1, plan.output_frame_bytes - 1u, &req_out,
                               &req_scratch, &info1) == ODM_STATUS_BUFFER_TOO_SMALL);
            ODM_TEST_CHECK(context, out1[0] == 0xa5u && req_out == plan.output_frame_bytes &&
                                    req_scratch == plan.scratch_bytes);
            memset(out1, 0, (size_t)plan.output_frame_bytes);
            ODM_TEST_CHECK(context,
                           odm_preview_render_frame(
                               ir, ir_bytes, 0, NULL, &config, NULL, 0u, NULL, 0u,
                               NULL, 0u, NULL, scratch, plan.scratch_bytes,
                               out1, plan.output_frame_bytes, &req_out,
                               &req_scratch, &info1) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, info1.output_width == widths[i] &&
                                    info1.output_height == heights[i] &&
                                    info1.output_bytes == plan.output_frame_bytes);
            ODM_TEST_CHECK(context, info1.frame_index == 0 && info1.sample == 0 &&
                                    info1.semantics_class == ODM_PREVIEW_SEMANTICS_EXACT);
            ODM_TEST_CHECK(context,
                           odm_preview_render_frame(
                               ir, ir_bytes, 0, NULL, &config, NULL, 0u, NULL, 0u,
                               NULL, 0u, NULL, scratch, plan.scratch_bytes,
                               out2, plan.output_frame_bytes, &req_out,
                               &req_scratch, &info2) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)plan.output_frame_bytes) == 0);
            ODM_TEST_CHECK(context,
                           odm_sha256_equal(&info1.preview_pixel_sha256,
                                            &info2.preview_pixel_sha256) != 0 &&
                           odm_sha256_equal(&info1.canonical_frame_sha256,
                                            &info2.canonical_frame_sha256) != 0);
            ODM_TEST_CHECK(context,
                           odm_preview_render_frame(
                               ir, ir_bytes, 0, NULL, &config, NULL, 0u, NULL, 0u,
                               NULL, 0u, NULL, scratch, plan.scratch_bytes,
                               scratch, plan.output_frame_bytes, &req_out,
                               &req_scratch, &info2) == ODM_STATUS_INVALID_ARGUMENT);
        }
        free(out2); free(out1); free(scratch);
    }

    preview_config(&config, ODM_PREVIEW_RASTER_HALF);
    config.flags = 1u;
    ODM_TEST_CHECK(context,
                   odm_preview_plan_frame(ir, ir_bytes, 0, NULL, &config,
                                          0u, 0u, &plan) == ODM_STATUS_INVALID_ARGUMENT);
    preview_config(&config, UINT32_C(99));
    ODM_TEST_CHECK(context,
                   odm_preview_plan_frame(ir, ir_bytes, 0, NULL, &config,
                                          0u, 0u, &plan) == ODM_STATUS_UNSUPPORTED);

    /* Separate preview ABI plus caller-owned job lifecycle. */
    {
        odm_ffi_preview_abi_info abi, sentinel;
        odm_progress_snapshot snapshot;
        uint64_t required = 0u, generation = 0u, req_out = 0u, req_scratch = 0u;
        void *base = NULL, *job_storage = NULL;
        uint8_t *scratch = NULL, *pixels = NULL;
        odm_preview_frame_info info;
        preview_config(&config, ODM_PREVIEW_RASTER_HALF);
        ODM_TEST_CHECK(context,
                       odm_ffi_preview_abi_query(ODM_FFI_PREVIEW_ABI_SCHEMA_VERSION,
                                                 NULL, 0u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == sizeof(odm_ffi_preview_abi_info));
        memset(&sentinel, 0xa5, sizeof(sentinel)); abi = sentinel;
        ODM_TEST_CHECK(context,
                       odm_ffi_preview_abi_query(ODM_FFI_PREVIEW_ABI_SCHEMA_VERSION,
                                                 &abi, required - 1u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, memcmp(&abi, &sentinel, sizeof(abi)) == 0);
        ODM_TEST_CHECK(context,
                       odm_ffi_preview_abi_query(ODM_FFI_PREVIEW_ABI_SCHEMA_VERSION,
                                                 &abi, sizeof(abi), &required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, abi.struct_size == 128u && abi.ffi_abi_version == 1u &&
                                abi.preview_plan_size == sizeof(odm_preview_plan) &&
                                abi.job_storage_bytes >= 56u && abi.job_storage_alignment != 0u && abi.music_analysis_tick_size == sizeof(odm_music_analysis_tick));
        ODM_TEST_CHECK(context,
                       (abi.feature_bits & ODM_FFI_PREVIEW_FEATURE_CANCELLATION) != 0u &&
                       (abi.feature_bits & ODM_FFI_PREVIEW_FEATURE_FLAT_ASSETS) != 0u);
        job_storage = aligned_job_storage(abi.job_storage_bytes,
                                          abi.job_storage_alignment, &base);
        ODM_TEST_CHECK(context, job_storage != NULL);
        if (job_storage) {
            ODM_TEST_CHECK(context,
                           odm_ffi_job_init((uint8_t *)job_storage + 1u,
                                            abi.job_storage_bytes) == ODM_STATUS_INVALID_ARGUMENT);
            ODM_TEST_CHECK(context,
                           odm_ffi_job_init(job_storage, abi.job_storage_bytes - 1u) ==
                               ODM_STATUS_INVALID_ARGUMENT);
            ODM_TEST_CHECK(context,
                           odm_ffi_job_init(job_storage, abi.job_storage_bytes) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_ffi_preview_plan_frame(ir, ir_bytes, 0, NULL, &config,
                                                      0u, 0u, &plan) == ODM_STATUS_OK);
            scratch = (uint8_t *)malloc((size_t)plan.scratch_bytes);
            pixels = (uint8_t *)malloc((size_t)plan.output_frame_bytes);
            ODM_TEST_CHECK(context, scratch != NULL && pixels != NULL);
            if (scratch && pixels) {
                ODM_TEST_CHECK(context,
                               odm_ffi_job_begin(job_storage, abi.job_storage_bytes,
                                                 plan.work_units, &generation) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                               odm_ffi_preview_render_frame(
                                   ir, ir_bytes, 0, NULL, &config, NULL, 0u, NULL, 0u,
                                   NULL, 0u, job_storage, abi.job_storage_bytes,
                                   generation, scratch, plan.scratch_bytes,
                                   pixels, plan.output_frame_bytes, &req_out,
                                   &req_scratch, &info) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                               odm_ffi_job_snapshot(job_storage, abi.job_storage_bytes,
                                                    &snapshot) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_RUNNING &&
                                        snapshot.generation == generation &&
                                        snapshot.work_done == plan.work_units &&
                                        snapshot.phase == ODM_PHASE_FINALIZE);
                ODM_TEST_CHECK(context,
                               odm_ffi_job_finish(job_storage, abi.job_storage_bytes,
                                                  generation, ODM_STATUS_OK) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                               odm_ffi_job_begin(job_storage, abi.job_storage_bytes,
                                                 plan.work_units, &generation) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                               odm_ffi_job_request_cancel(job_storage,
                                                          abi.job_storage_bytes) == ODM_STATUS_OK);
                memset(pixels, 0xa5, (size_t)plan.output_frame_bytes);
                ODM_TEST_CHECK(context,
                               odm_ffi_preview_render_frame(
                                   ir, ir_bytes, 0, NULL, &config, NULL, 0u, NULL, 0u,
                                   NULL, 0u, job_storage, abi.job_storage_bytes,
                                   generation, scratch, plan.scratch_bytes,
                                   pixels, plan.output_frame_bytes, &req_out,
                                   &req_scratch, &info) == ODM_STATUS_CANCELLED);
                ODM_TEST_CHECK(context, pixels[0] == 0xa5u);
                ODM_TEST_CHECK(context,
                               odm_ffi_job_finish(job_storage, abi.job_storage_bytes,
                                                  generation, ODM_STATUS_CANCELLED) == ODM_STATUS_OK);
            }
            ODM_TEST_CHECK(context,
                           odm_ffi_job_destroy(job_storage, abi.job_storage_bytes) == ODM_STATUS_OK);
        }
        free(pixels); free(scratch); free(base);
    }

    free(ir);
}

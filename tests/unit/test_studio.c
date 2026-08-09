#include "test_harness.h"

#include "odm_studio.h"
#include "odm_music_map.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    odm_score_header header;
    odm_score_act act;
    odm_score_scene scene;
    odm_score_node nodes[2];
    odm_score_view score;
    odm_music_analysis_plan analysis_plan;
    int32_t *window;
    odm_music_complex_q31 *twiddles;
    odm_music_analysis_tick ticks[4];
    odm_pcm_stereo_q31 *pcm;
    odm_sha256_digest pcm_sha;
    odm_sha256_digest map_sha;
    uint8_t *analysis;
    uint64_t analysis_bytes;
    uint8_t *ir;
    uint64_t ir_bytes;
    odm_render_asset_set assets;
} studio_fixture;

typedef struct {
    uint32_t began;
    uint32_t committed;
    uint32_t aborted;
    uint32_t frames;
    uint64_t audio_frames;
} studio_sink_state;

static odm_status studio_sink_begin(void *user, const odm_master_plan *plan) {
    studio_sink_state *state = (studio_sink_state *)user;
    if (!state || !plan || state->began != 0u) return ODM_STATUS_INVALID_STATE;
    state->began = 1u;
    return ODM_STATUS_OK;
}

static odm_status studio_sink_frame(void *user, const odm_reference_frame_info *info,
                                    const uint8_t *rgba16le, uint64_t bytes) {
    studio_sink_state *state = (studio_sink_state *)user;
    if (!state || !info || !rgba16le || state->began != 1u || bytes != info->pixel_bytes) {
        return ODM_STATUS_INVALID_DATA;
    }
    state->frames++;
    return ODM_STATUS_OK;
}

static odm_status studio_sink_audio(void *user, uint64_t start_frame,
                                    const uint8_t *pcm, uint64_t frame_count) {
    studio_sink_state *state = (studio_sink_state *)user;
    if (!state || !pcm || state->began != 1u || start_frame != state->audio_frames || frame_count == 0u) {
        return ODM_STATUS_INVALID_DATA;
    }
    state->audio_frames += frame_count;
    return ODM_STATUS_OK;
}

static odm_status studio_sink_commit(void *user, const uint8_t *receipt, uint64_t bytes) {
    studio_sink_state *state = (studio_sink_state *)user;
    if (!state || !receipt || bytes != ODM_RENDER_RECEIPT_RECORD_BYTES || state->began != 1u) {
        return ODM_STATUS_INVALID_DATA;
    }
    state->committed = 1u;
    return ODM_STATUS_OK;
}

static void studio_sink_abort(void *user, odm_status reason) {
    studio_sink_state *state = (studio_sink_state *)user;
    if (state && reason != ODM_STATUS_OK) state->aborted++;
}

static void studio_node_init(odm_score_node *node, uint32_t id, uint32_t role,
                             uint32_t capability, int32_t z_index) {
    memset(node, 0, sizeof(*node));
    node->node_id = id;
    node->scene_id = 1u;
    node->role = role;
    node->capability_id = capability;
    node->capability_major = 1u;
    node->capability_minor = 0u;
    node->state_model = ODM_STATELESS;
    node->z_index = z_index;
    node->start_sample = 0;
    node->end_sample = 3200;
    node->scale_x_micro = ODM_MICRO_ONE;
    node->scale_y_micro = ODM_MICRO_ONE;
    node->opacity_micro = ODM_MICRO_ONE;
    node->seed_domain = (uint64_t)id;
}

static int studio_fixture_init(studio_fixture *f) {
    odm_music_map_binding binding;
    uint64_t i;
    if (!f) return 0;
    memset(f, 0, sizeof(*f));
    f->window = (int32_t *)malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES * sizeof(*f->window));
    f->twiddles = (odm_music_complex_q31 *)malloc((size_t)ODM_MUSIC_FFT_TWIDDLES * sizeof(*f->twiddles));
    f->pcm = (odm_pcm_stereo_q31 *)calloc(1600u, sizeof(*f->pcm));
    if (!f->window || !f->twiddles || !f->pcm) return 0;
    if (odm_music_analysis_plan_build(f->window, ODM_MUSIC_WINDOW_SAMPLES,
                                      f->twiddles, ODM_MUSIC_FFT_TWIDDLES,
                                      &f->analysis_plan) != ODM_STATUS_OK) return 0;
    for (i = 0u; i < 4u; ++i) {
        f->ticks[i].tick_index = i;
        f->ticks[i].center_sample = i * ODM_MUSIC_TICK_SAMPLES;
        f->ticks[i].silence = 1u;
    }
    if (odm_master_pcm_sha256(f->pcm, 1600u, &f->pcm_sha) != ODM_STATUS_OK) return 0;
    if (odm_music_map_record_write(&f->analysis_plan, 1600u, &f->pcm_sha,
                                   f->ticks, 4u, NULL, 0u,
                                   &f->analysis_bytes, NULL) != ODM_STATUS_BUFFER_TOO_SMALL) return 0;
    f->analysis = (uint8_t *)malloc((size_t)f->analysis_bytes);
    if (!f->analysis) return 0;
    if (odm_music_map_record_write(&f->analysis_plan, 1600u, &f->pcm_sha,
                                   f->ticks, 4u, f->analysis, f->analysis_bytes,
                                   &f->analysis_bytes, &f->map_sha) != ODM_STATUS_OK) return 0;

    f->header.schema_version_major = ODM_SCORE_SCHEMA_MAJOR;
    f->header.schema_version_minor = ODM_SCORE_SCHEMA_MINOR;
    f->header.fps = 30;
    f->header.width = 4u;
    f->header.height = 4u;
    f->header.project_end_sample = 3200;
    f->header.project_seed = UINT64_C(0x53545544494f3031);
    f->header.act_count = 1u;
    f->header.scene_count = 1u;
    f->header.node_count = 2u;
    f->act.act_id = 1u;
    f->act.start_sample = 0;
    f->act.end_sample = 3200;
    f->scene.scene_id = 1u;
    f->scene.act_id = 1u;
    f->scene.environment_node_id = 1u;
    f->scene.primary_node_id = 2u;
    f->scene.start_sample = 0;
    f->scene.end_sample = 3200;
    studio_node_init(&f->nodes[0], 1u, ODM_SCORE_NODE_ENVIRONMENT, ODM_CAP_BLACK_VOID, -100);
    studio_node_init(&f->nodes[1], 2u, ODM_SCORE_NODE_PRIMARY, ODM_CAP_PRIMARY_EMPTY, 0);
    f->score.header = &f->header;
    f->score.acts = &f->act;
    f->score.scenes = &f->scene;
    f->score.nodes = f->nodes;
    if (odm_score_validate(&f->score) != ODM_STATUS_OK) return 0;
    memset(&binding, 0, sizeof(binding));
    binding.canonical_frames = 1600u;
    binding.music_map_sha256 = f->map_sha;
    binding.music_policy_sha256 = f->analysis_plan.policy_sha256;
    if (odm_score_compile_render_ir(&f->score, &binding, NULL, 0u,
                                    &f->ir_bytes, NULL) != ODM_STATUS_BUFFER_TOO_SMALL) return 0;
    f->ir = (uint8_t *)malloc((size_t)f->ir_bytes);
    if (!f->ir) return 0;
    if (odm_score_compile_render_ir(&f->score, &binding, f->ir, f->ir_bytes,
                                    &f->ir_bytes, NULL) != ODM_STATUS_OK) return 0;
    memset(&f->assets, 0, sizeof(f->assets));
    return 1;
}

static void studio_fixture_destroy(studio_fixture *f) {
    if (!f) return;
    free(f->ir);
    free(f->analysis);
    free(f->pcm);
    free(f->twiddles);
    free(f->window);
    memset(f, 0, sizeof(*f));
}

static uint8_t *studio_package_build(studio_fixture *f, const uint8_t seed[32],
                                     const uint8_t *data, uint64_t data_bytes,
                                     uint64_t *out_bytes, odm_odparms_info *out_info) {
    odm_odparms_asset asset;
    uint8_t *package;
    uint64_t required = 0u;
    odm_status st;
    if (!f || !seed || !data || !out_bytes || !out_info) return NULL;
    memset(&asset, 0, sizeof(asset));
    asset.kind = ODM_ODPARMS_ENTRY_DATA;
    asset.path = "notes/meta.txt";
    asset.data = data;
    asset.data_bytes = data_bytes;
    st = odm_odparms_build(&f->score, f->ir, f->ir_bytes,
                           f->analysis, f->analysis_bytes,
                           &asset, 1u, seed, NULL, 0u, &required, out_info);
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || required == 0u || required > (uint64_t)SIZE_MAX) return NULL;
    package = (uint8_t *)malloc((size_t)required);
    if (!package) return NULL;
    st = odm_odparms_build(&f->score, f->ir, f->ir_bytes,
                           f->analysis, f->analysis_bytes,
                           &asset, 1u, seed, package, required, &required, out_info);
    if (st != ODM_STATUS_OK) {
        free(package);
        return NULL;
    }
    *out_bytes = required;
    return package;
}

static odm_status studio_rewrap_revision(uint8_t record[ODM_STUDIO_REVISION_RECORD_BYTES]) {
    uint8_t rebuilt[ODM_STUDIO_REVISION_RECORD_BYTES];
    uint64_t required = 0u;
    odm_status st = odm_wire_record_write(ODM_STUDIO_REVISION_KIND,
                                          ODM_STUDIO_REVISION_SCHEMA_MAJOR,
                                          ODM_STUDIO_REVISION_SCHEMA_MINOR,
                                          record + ODM_WIRE_RECORD_HEADER_BYTES,
                                          ODM_STUDIO_REVISION_PAYLOAD_BYTES,
                                          rebuilt, sizeof(rebuilt), &required, NULL);
    if (st != ODM_STATUS_OK) return st;
    if (required != ODM_STUDIO_REVISION_RECORD_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    memcpy(record, rebuilt, sizeof(rebuilt));
    return ODM_STATUS_OK;
}

void odm_test_studio(odm_test_context *context) {
    static const uint8_t seed_a[32] = {
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60
    };
    static const uint8_t seed_b[32] = {
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
    };
    static const uint8_t data_a[] = "studio-author-data-v1";
    static const uint8_t data_b[] = "studio-author-data-v2";
    studio_fixture f;
    uint8_t *pkg_a = NULL, *pkg_b = NULL, *pkg_resigned = NULL;
    uint64_t pkg_a_bytes = 0u, pkg_b_bytes = 0u, pkg_resigned_bytes = 0u;
    odm_odparms_info info_a, info_b, info_resigned;
    odm_sha256_digest content_a, content_b, content_resigned;
    uint8_t rev0[ODM_STUDIO_REVISION_RECORD_BYTES];
    uint8_t rev1[ODM_STUDIO_REVISION_RECORD_BYTES];
    uint8_t tampered[ODM_STUDIO_REVISION_RECORD_BYTES];
    uint64_t rev_required = 0u;
    odm_studio_revision_info r0, r1, checked;
    odm_preview_config preview_config;
    odm_preview_plan preview_plan;
    odm_preview_frame_info preview_info;
    uint8_t *preview_scratch = NULL, *preview_pixels = NULL;
    uint64_t req_pixels = 0u, req_scratch = 0u;
    uint8_t approval[ODM_STUDIO_APPROVAL_RECORD_BYTES];
    uint64_t approval_required = 0u;
    odm_studio_preview_approval_info approval_info, approval_checked;
    odm_master_request request;
    odm_master_plan master_plan;
    odm_master_sink sink;
    studio_sink_state sink_state;
    uint8_t *frame = NULL, *scratch = NULL;
    uint8_t receipt[ODM_RENDER_RECEIPT_RECORD_BYTES];
    uint64_t receipt_required = 0u;
    odm_render_receipt_info receipt_info;

    memset(&info_a, 0, sizeof(info_a));
    memset(&info_b, 0, sizeof(info_b));
    memset(&info_resigned, 0, sizeof(info_resigned));
    memset(&r0, 0, sizeof(r0));
    memset(&r1, 0, sizeof(r1));
    memset(&checked, 0, sizeof(checked));
    memset(&preview_config, 0, sizeof(preview_config));
    memset(&preview_plan, 0, sizeof(preview_plan));
    memset(&preview_info, 0, sizeof(preview_info));
    memset(&approval_info, 0, sizeof(approval_info));
    memset(&approval_checked, 0, sizeof(approval_checked));
    memset(&request, 0, sizeof(request));
    memset(&master_plan, 0, sizeof(master_plan));
    memset(&sink, 0, sizeof(sink));
    memset(&sink_state, 0, sizeof(sink_state));
    memset(&receipt_info, 0, sizeof(receipt_info));

    ODM_TEST_CHECK(context, studio_fixture_init(&f) != 0);
    if (!f.ir) goto cleanup;
    pkg_a = studio_package_build(&f, seed_a, data_a, sizeof(data_a) - 1u, &pkg_a_bytes, &info_a);
    pkg_b = studio_package_build(&f, seed_a, data_b, sizeof(data_b) - 1u, &pkg_b_bytes, &info_b);
    pkg_resigned = studio_package_build(&f, seed_b, data_a, sizeof(data_a) - 1u,
                                         &pkg_resigned_bytes, &info_resigned);
    ODM_TEST_CHECK(context, pkg_a != NULL && pkg_b != NULL && pkg_resigned != NULL);
    if (!pkg_a || !pkg_b || !pkg_resigned) goto cleanup;

    ODM_TEST_CHECK(context,
                   odm_odparms_content_sha256(pkg_a, pkg_a_bytes, info_a.signer_public_key,
                                              &content_a) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_odparms_content_sha256(pkg_b, pkg_b_bytes, info_b.signer_public_key,
                                              &content_b) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_odparms_content_sha256(pkg_resigned, pkg_resigned_bytes,
                                              info_resigned.signer_public_key,
                                              &content_resigned) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_sha256_equal(&content_a, &content_b) == 0);
    ODM_TEST_CHECK(context, odm_sha256_equal(&content_a, &content_resigned) != 0);
    ODM_TEST_CHECK(context, odm_sha256_equal(&info_a.package_sha256, &info_resigned.package_sha256) == 0);

    ODM_TEST_CHECK(context,
                   odm_studio_revision_freeze(pkg_a, pkg_a_bytes, info_a.signer_public_key,
                                              &f.score, NULL, 0u, NULL, 0u,
                                              &rev_required, NULL) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, rev_required == ODM_STUDIO_REVISION_RECORD_BYTES);
    ODM_TEST_CHECK(context,
                   odm_studio_revision_freeze(pkg_a, pkg_a_bytes, info_a.signer_public_key,
                                              &f.score, NULL, 0u, rev0, sizeof(rev0),
                                              &rev_required, &r0) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, r0.revision_number == 0u && r0.asset_count == 0u);
    ODM_TEST_CHECK(context, odm_sha256_equal(&r0.package_content_sha256, &content_a) != 0);
    ODM_TEST_CHECK(context,
                   odm_studio_revision_validate(rev0, sizeof(rev0), &checked) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_sha256_equal(&checked.revision_id, &r0.revision_id) != 0 &&
                            odm_sha256_equal(&checked.project_id, &r0.project_id) != 0);

    ODM_TEST_CHECK(context,
                   odm_studio_revision_freeze(pkg_a, pkg_a_bytes, info_a.signer_public_key,
                                              &f.score, rev0, sizeof(rev0), rev1, sizeof(rev1),
                                              &rev_required, &r1) == ODM_STATUS_INVALID_STATE);
    ODM_TEST_CHECK(context,
                   odm_studio_revision_freeze(pkg_resigned, pkg_resigned_bytes,
                                              info_resigned.signer_public_key,
                                              &f.score, rev0, sizeof(rev0), rev1, sizeof(rev1),
                                              &rev_required, &r1) == ODM_STATUS_INTEGRITY_ERROR);
    ODM_TEST_CHECK(context,
                   odm_studio_revision_freeze(pkg_b, pkg_b_bytes, info_b.signer_public_key,
                                              &f.score, rev0, sizeof(rev0), rev1, sizeof(rev1),
                                              &rev_required, &r1) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, r1.revision_number == 1u &&
                            odm_sha256_equal(&r1.parent_revision_id, &r0.revision_id) != 0 &&
                            odm_sha256_equal(&r1.project_id, &r0.project_id) != 0 &&
                            odm_sha256_equal(&r1.package_content_sha256, &content_b) != 0);

    memcpy(tampered, rev1, sizeof(tampered));
    tampered[ODM_WIRE_RECORD_HEADER_BYTES + 384u] ^= 1u; /* project_id byte */
    ODM_TEST_CHECK(context, studio_rewrap_revision(tampered) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_studio_revision_validate(tampered, sizeof(tampered), &checked) == ODM_STATUS_INTEGRITY_ERROR);

    preview_config.schema_version = ODM_PREVIEW_CONFIG_SCHEMA_VERSION;
    preview_config.raster_class = ODM_PREVIEW_RASTER_HALF;
    preview_config.pixel_format = ODM_PREVIEW_PIXEL_RGBA8_SRGB_STRAIGHT;
    ODM_TEST_CHECK(context,
                   odm_studio_preview_plan(rev1, sizeof(rev1), f.ir, f.ir_bytes,
                                           f.analysis, f.analysis_bytes, 0,
                                           &preview_config, 0u, 0u,
                                           &preview_plan) == ODM_STATUS_OK);
    f.analysis[ODM_WIRE_RECORD_HEADER_BYTES + 5u] ^= 1u;
    ODM_TEST_CHECK(context,
                   odm_studio_preview_plan(rev1, sizeof(rev1), f.ir, f.ir_bytes,
                                           f.analysis, f.analysis_bytes, 0,
                                           &preview_config, 0u, 0u,
                                           &preview_plan) != ODM_STATUS_OK);
    f.analysis[ODM_WIRE_RECORD_HEADER_BYTES + 5u] ^= 1u;
    ODM_TEST_CHECK(context,
                   odm_studio_preview_plan(rev1, sizeof(rev1), f.ir, f.ir_bytes,
                                           f.analysis, f.analysis_bytes, 0,
                                           &preview_config, 0u, 0u,
                                           &preview_plan) == ODM_STATUS_OK);
    preview_scratch = (uint8_t *)malloc((size_t)preview_plan.scratch_bytes);
    preview_pixels = (uint8_t *)malloc((size_t)preview_plan.output_frame_bytes);
    ODM_TEST_CHECK(context, preview_scratch != NULL && preview_pixels != NULL);
    if (preview_scratch && preview_pixels) {
        ODM_TEST_CHECK(context,
                       odm_studio_preview_render(rev1, sizeof(rev1), f.ir, f.ir_bytes,
                                                 f.analysis, f.analysis_bytes, 0,
                                                 &preview_config, NULL, 0u, NULL, 0u,
                                                 NULL, 0u, NULL,
                                                 preview_scratch, preview_plan.scratch_bytes,
                                                 preview_pixels, preview_plan.output_frame_bytes,
                                                 &req_pixels, &req_scratch,
                                                 &preview_info) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_studio_preview_approval_write(rev1, sizeof(rev1), &preview_plan,
                                                         &preview_info, approval, sizeof(approval),
                                                         &approval_required, &approval_info) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, approval_required == ODM_STUDIO_APPROVAL_RECORD_BYTES &&
                                odm_sha256_equal(&approval_info.revision_id, &r1.revision_id) != 0);
        ODM_TEST_CHECK(context,
                       odm_studio_preview_approval_validate(approval, sizeof(approval), &r1.revision_id,
                                                            &approval_checked) == ODM_STATUS_OK);
        preview_plan.render_ir_sha256.bytes[0] ^= 1u;
        ODM_TEST_CHECK(context,
                       odm_studio_preview_approval_write(rev1, sizeof(rev1), &preview_plan,
                                                         &preview_info, approval, sizeof(approval),
                                                         &approval_required, &approval_info) == ODM_STATUS_INTEGRITY_ERROR);
        preview_plan.render_ir_sha256.bytes[0] ^= 1u;
    }

    request.package_bytes = pkg_b;
    request.package_size = pkg_b_bytes;
    request.expected_package_public_key = info_b.signer_public_key;
    request.score = &f.score;
    request.assets = &f.assets;
    request.canonical_pcm = f.pcm;
    request.canonical_pcm_frames = 1600u;
    request.output_profile = ODM_MASTER_PROFILE_REFERENCE_V1;
    ODM_TEST_CHECK(context,
                   odm_studio_master_plan_build(rev1, sizeof(rev1), &request, &master_plan) == ODM_STATUS_OK);
    frame = (uint8_t *)malloc((size_t)master_plan.frame_bytes);
    scratch = (uint8_t *)malloc((size_t)master_plan.renderer_scratch_bytes);
    ODM_TEST_CHECK(context, frame != NULL && scratch != NULL);
    sink.user = &sink_state;
    sink.begin = studio_sink_begin;
    sink.write_frame = studio_sink_frame;
    sink.write_audio = studio_sink_audio;
    sink.commit = studio_sink_commit;
    sink.abort = studio_sink_abort;
    if (frame && scratch) {
        ODM_TEST_CHECK(context,
                       odm_studio_master_run(rev1, sizeof(rev1), &request, &sink,
                                             frame, master_plan.frame_bytes,
                                             scratch, master_plan.renderer_scratch_bytes,
                                             receipt, sizeof(receipt), &receipt_required,
                                             &receipt_info) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, sink_state.began == 1u && sink_state.committed == 1u &&
                                sink_state.aborted == 0u && sink_state.frames == master_plan.frame_count);
    }

    memset(&sink_state, 0, sizeof(sink_state));
    request.package_bytes = pkg_a;
    request.package_size = pkg_a_bytes;
    request.expected_package_public_key = info_a.signer_public_key;
    ODM_TEST_CHECK(context,
                   odm_studio_master_run(rev1, sizeof(rev1), &request, &sink,
                                         frame, master_plan.frame_bytes,
                                         scratch, master_plan.renderer_scratch_bytes,
                                         receipt, sizeof(receipt), &receipt_required,
                                         &receipt_info) == ODM_STATUS_INTEGRITY_ERROR);
    ODM_TEST_CHECK(context, sink_state.began == 0u && sink_state.committed == 0u && sink_state.aborted == 0u);

cleanup:
    free(scratch);
    free(frame);
    free(preview_pixels);
    free(preview_scratch);
    free(pkg_resigned);
    free(pkg_b);
    free(pkg_a);
    studio_fixture_destroy(&f);
}

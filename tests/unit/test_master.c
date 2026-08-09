#include "test_harness.h"

#include "odm_master.h"
#include "odm_wire.h"
#include "package_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


static void master_test_put_le64(uint8_t *p, uint64_t value) {
    uint32_t i;
    for (i = 0u; i < 8u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static odm_status master_test_rewrap_receipt(uint8_t record[ODM_RENDER_RECEIPT_RECORD_BYTES]) {
    uint8_t rebuilt[ODM_RENDER_RECEIPT_RECORD_BYTES];
    uint64_t required = 0u;
    odm_status st = odm_wire_record_write(ODM_RENDER_RECEIPT_KIND,
                                          ODM_RENDER_RECEIPT_SCHEMA_MAJOR,
                                          ODM_RENDER_RECEIPT_SCHEMA_MINOR,
                                          record + ODM_WIRE_RECORD_HEADER_BYTES,
                                          ODM_RENDER_RECEIPT_PAYLOAD_BYTES,
                                          rebuilt, sizeof(rebuilt), &required, NULL);
    if (st != ODM_STATUS_OK) return st;
    if (required != ODM_RENDER_RECEIPT_RECORD_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    memcpy(record, rebuilt, sizeof(rebuilt));
    return ODM_STATUS_OK;
}

typedef struct {
    uint32_t began;
    uint32_t frames;
    uint32_t audio_calls;
    uint32_t committed;
    uint32_t aborted;
    uint32_t fail_frame;
    uint64_t audio_frames;
    uint64_t next_audio;
    odm_sha256_digest render_id;
} master_sink_state;

static odm_status sink_begin(void *user, const odm_master_plan *plan) {
    master_sink_state *s = (master_sink_state *)user;
    if (!s || !plan || s->began != 0u) return ODM_STATUS_INVALID_STATE;
    s->began = 1u;
    s->render_id = plan->render_id;
    return ODM_STATUS_OK;
}

static odm_status sink_frame(void *user, const odm_reference_frame_info *info,
                             const uint8_t *rgba16le, uint64_t bytes) {
    master_sink_state *s = (master_sink_state *)user;
    uint64_t i;
    if (!s || !info || !rgba16le || s->began != 1u || bytes != info->pixel_bytes ||
        info->frame_index < 0 || (uint64_t)info->frame_index != s->frames) return ODM_STATUS_INVALID_DATA;
    if (s->fail_frame != UINT32_MAX && s->frames == s->fail_frame) return ODM_STATUS_INTERNAL_ERROR;
    for (i = 0u; i < bytes; ++i) {
        if (rgba16le[i] != 0u && (i & 7u) != 6u && (i & 7u) != 7u) return ODM_STATUS_INVALID_DATA;
    }
    s->frames++;
    return ODM_STATUS_OK;
}

static odm_status sink_audio(void *user, uint64_t start_frame,
                             const uint8_t *pcm, uint64_t frame_count) {
    master_sink_state *s = (master_sink_state *)user;
    uint64_t i;
    if (!s || !pcm || frame_count == 0u || start_frame != s->next_audio) return ODM_STATUS_INVALID_DATA;
    for (i = 0u; i < frame_count * 8u; ++i) if (pcm[i] != 0u) return ODM_STATUS_INVALID_DATA;
    s->audio_calls++;
    s->audio_frames += frame_count;
    s->next_audio += frame_count;
    return ODM_STATUS_OK;
}

static odm_status sink_commit(void *user, const uint8_t *receipt, uint64_t bytes) {
    master_sink_state *s = (master_sink_state *)user;
    odm_render_receipt_info info;
    if (!s || !receipt || s->began != 1u) return ODM_STATUS_INVALID_STATE;
    if (odm_render_receipt_validate(receipt, bytes, &s->render_id, &info) != ODM_STATUS_OK) return ODM_STATUS_INVALID_DATA;
    s->committed = 1u;
    return ODM_STATUS_OK;
}

static void sink_abort(void *user, odm_status reason) {
    master_sink_state *s = (master_sink_state *)user;
    if (s && reason != ODM_STATUS_OK) s->aborted++;
}

static void init_node(odm_score_node *node, uint32_t id, uint32_t role,
                      uint32_t capability, int32_t z) {
    memset(node, 0, sizeof(*node));
    node->node_id = id;
    node->scene_id = 1u;
    node->role = role;
    node->capability_id = capability;
    node->capability_major = 1u;
    node->capability_minor = 0u;
    node->state_model = ODM_STATELESS;
    node->z_index = z;
    node->start_sample = 0;
    node->end_sample = 3200;
    node->scale_x_micro = ODM_MICRO_ONE;
    node->scale_y_micro = ODM_MICRO_ONE;
    node->opacity_micro = ODM_MICRO_ONE;
    node->seed_domain = id;
}

void odm_test_master(odm_test_context *context) {
    static const uint8_t package_seed[32] = {
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60
    };
    static const uint8_t controller_seed[32] = {
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
    };
    odm_score_header header;
    odm_score_act act;
    odm_score_scene scene;
    odm_score_node nodes[2];
    odm_score_view score;
    odm_music_analysis_plan analysis_plan;
    int32_t *window = NULL;
    odm_music_complex_q31 *twiddles = NULL;
    odm_music_analysis_tick ticks[4];
    odm_pcm_stereo_q31 *pcm = NULL;
    odm_sha256_digest pcm_sha, map_sha;
    uint8_t *analysis = NULL, *ir = NULL, *package = NULL;
    uint64_t analysis_bytes = 0u, ir_bytes = 0u, package_bytes = 0u;
    odm_music_map_binding binding;
    odm_odparms_info package_info;
    odm_render_asset_set assets;
    odm_master_request request;
    odm_master_plan plan;
    odm_master_sink sink;
    master_sink_state sink_state;
    uint8_t *frame = NULL, *scratch = NULL;
    uint8_t receipt[ODM_RENDER_RECEIPT_RECORD_BYTES], receipt_before[ODM_RENDER_RECEIPT_RECORD_BYTES];
    uint64_t receipt_bytes = 0u;
    odm_render_receipt_info receipt_info, checked;
    uint8_t controller_pk[32], envelope[ODM_CONTROLLER_SIGNATURE_BYTES], wrong_pk[32];
    odm_job job;
    odm_job_ticket ticket;
    odm_progress_snapshot snapshot;
    uint64_t i;

    memset(&header, 0, sizeof(header)); memset(&act, 0, sizeof(act)); memset(&scene, 0, sizeof(scene));
    memset(nodes, 0, sizeof(nodes)); memset(&score, 0, sizeof(score)); memset(&analysis_plan, 0, sizeof(analysis_plan));
    memset(ticks, 0, sizeof(ticks)); memset(&binding, 0, sizeof(binding)); memset(&package_info, 0, sizeof(package_info));
    memset(&assets, 0, sizeof(assets)); memset(&request, 0, sizeof(request)); memset(&plan, 0, sizeof(plan));
    memset(&sink, 0, sizeof(sink)); memset(&sink_state, 0, sizeof(sink_state)); memset(&receipt_info, 0, sizeof(receipt_info));

    window = (int32_t *)malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES * sizeof(*window));
    twiddles = (odm_music_complex_q31 *)malloc((size_t)ODM_MUSIC_FFT_TWIDDLES * sizeof(*twiddles));
    pcm = (odm_pcm_stereo_q31 *)calloc(1600u, sizeof(*pcm));
    ODM_TEST_CHECK(context, window != NULL && twiddles != NULL && pcm != NULL); if (!window || !twiddles || !pcm) goto cleanup;
    ODM_TEST_CHECK(context, odm_music_analysis_plan_build(window, ODM_MUSIC_WINDOW_SAMPLES, twiddles, ODM_MUSIC_FFT_TWIDDLES, &analysis_plan) == ODM_STATUS_OK);
    for (i=0u;i<4u;++i){ticks[i].tick_index=i;ticks[i].center_sample=i*ODM_MUSIC_TICK_SAMPLES;ticks[i].silence=1u;}
    ODM_TEST_CHECK(context, odm_master_pcm_sha256(pcm,1600u,&pcm_sha)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_map_record_write(&analysis_plan,1600u,&pcm_sha,ticks,4u,NULL,0u,&analysis_bytes,NULL)==ODM_STATUS_BUFFER_TOO_SMALL);
    analysis=(uint8_t*)malloc((size_t)analysis_bytes); ODM_TEST_CHECK(context,analysis!=NULL); if(!analysis)goto cleanup;
    ODM_TEST_CHECK(context, odm_music_map_record_write(&analysis_plan,1600u,&pcm_sha,ticks,4u,analysis,analysis_bytes,&analysis_bytes,&map_sha)==ODM_STATUS_OK);

    header.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;header.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;header.fps=30;header.width=4u;header.height=4u;header.project_end_sample=3200;header.project_seed=UINT64_C(0x1122334455667788);header.act_count=1u;header.scene_count=1u;header.node_count=2u;
    act.act_id=1u;act.start_sample=0;act.end_sample=3200;
    scene.scene_id=1u;scene.act_id=1u;scene.environment_node_id=1u;scene.primary_node_id=2u;scene.start_sample=0;scene.end_sample=3200;
    init_node(&nodes[0],1u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,-100);
    init_node(&nodes[1],2u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,0);
    score.header=&header;score.acts=&act;score.scenes=&scene;score.nodes=nodes;
    ODM_TEST_CHECK(context,odm_score_validate(&score)==ODM_STATUS_OK);
    binding.canonical_frames=1600u;binding.music_map_sha256=map_sha;binding.music_policy_sha256=analysis_plan.policy_sha256;
    ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&binding,NULL,0u,&ir_bytes,NULL)==ODM_STATUS_BUFFER_TOO_SMALL);
    ir=(uint8_t*)malloc((size_t)ir_bytes);ODM_TEST_CHECK(context,ir!=NULL);if(!ir)goto cleanup;
    ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&binding,ir,ir_bytes,&ir_bytes,NULL)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_odparms_build(&score,ir,ir_bytes,analysis,analysis_bytes,NULL,0u,package_seed,NULL,0u,&package_bytes,&package_info)==ODM_STATUS_BUFFER_TOO_SMALL);
    package=(uint8_t*)malloc((size_t)package_bytes);ODM_TEST_CHECK(context,package!=NULL);if(!package)goto cleanup;
    ODM_TEST_CHECK(context,odm_odparms_build(&score,ir,ir_bytes,analysis,analysis_bytes,NULL,0u,package_seed,package,package_bytes,&package_bytes,&package_info)==ODM_STATUS_OK);

    request.package_bytes=package;request.package_size=package_bytes;request.expected_package_public_key=package_info.signer_public_key;request.score=&score;request.assets=&assets;request.canonical_pcm=pcm;request.canonical_pcm_frames=1600u;request.output_profile=ODM_MASTER_PROFILE_REFERENCE_V1;
    ODM_TEST_CHECK(context,odm_master_plan_build(&request,&plan)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,plan.width==4u&&plan.height==4u&&plan.fps==30&&plan.frame_count==2u&&plan.output_end_sample==3200);
    ODM_TEST_CHECK(context,plan.quoted_work_units==3232u&&plan.frame_bytes==128u&&plan.quoted_output_bytes==26432u);
    ODM_TEST_CHECK(context,odm_sha256_equal(&plan.canonical_pcm_sha256,&pcm_sha)!=0);
    frame=(uint8_t*)malloc((size_t)plan.frame_bytes);scratch=(uint8_t*)malloc((size_t)plan.renderer_scratch_bytes);ODM_TEST_CHECK(context,frame!=NULL&&scratch!=NULL);if(!frame||!scratch)goto cleanup;

    sink_state.fail_frame=UINT32_MAX;sink.user=&sink_state;sink.begin=sink_begin;sink.write_frame=sink_frame;sink.write_audio=sink_audio;sink.commit=sink_commit;sink.abort=sink_abort;
    memset(receipt,0xa5,sizeof(receipt));
    ODM_TEST_CHECK(context,odm_master_run(&request,&sink,frame,plan.frame_bytes,scratch,plan.renderer_scratch_bytes,receipt,sizeof(receipt),&receipt_bytes,&receipt_info)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,receipt_bytes==ODM_RENDER_RECEIPT_RECORD_BYTES&&sink_state.began==1u&&sink_state.frames==2u&&sink_state.audio_frames==3200u&&sink_state.committed==1u&&sink_state.aborted==0u);
    ODM_TEST_CHECK(context,receipt_info.quoted_work_units==3232u&&receipt_info.observed_work_units==3232u&&odm_sha256_equal(&receipt_info.render_id,&plan.render_id)!=0);
    ODM_TEST_CHECK(context,odm_render_receipt_validate(receipt,receipt_bytes,&plan.render_id,&checked)==ODM_STATUS_OK&&odm_sha256_equal(&checked.receipt_sha256,&receipt_info.receipt_sha256)!=0);

    /* A valid ODMC digest is not enough: semantic lies must fail even after
     * an attacker recomputes the outer payload hash. */
    {
        uint8_t forged[ODM_RENDER_RECEIPT_RECORD_BYTES];
        uint8_t *payload;
        memcpy(forged, receipt, sizeof(forged));
        payload = forged + ODM_WIRE_RECORD_HEADER_BYTES;
        master_test_put_le64(payload + 96u, UINT64_C(3233));
        master_test_put_le64(payload + 104u, UINT64_C(3233));
        ODM_TEST_CHECK(context, master_test_rewrap_receipt(forged) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_render_receipt_validate(forged, sizeof(forged), NULL, &checked) == ODM_STATUS_INVALID_DATA);

        memcpy(forged, receipt, sizeof(forged));
        payload = forged + ODM_WIRE_RECORD_HEADER_BYTES;
        master_test_put_le64(payload + 64u, UINT64_C(1599));
        ODM_TEST_CHECK(context, master_test_rewrap_receipt(forged) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_render_receipt_validate(forged, sizeof(forged), NULL, &checked) == ODM_STATUS_INVALID_DATA);

        memcpy(forged, receipt, sizeof(forged));
        payload = forged + ODM_WIRE_RECORD_HEADER_BYTES;
        payload[440u] ^= UINT8_C(1);
        ODM_TEST_CHECK(context, master_test_rewrap_receipt(forged) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_render_receipt_validate(forged, sizeof(forged), NULL, &checked) == ODM_STATUS_INTEGRITY_ERROR);
    }

    ODM_TEST_CHECK(context,odm_controller_signature_public_from_seed(controller_seed,controller_pk)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_controller_signature_write(controller_seed,receipt,receipt_bytes,envelope)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_controller_signature_verify(controller_pk,receipt,receipt_bytes,envelope)==ODM_STATUS_OK);
    memcpy(wrong_pk,controller_pk,sizeof(wrong_pk));wrong_pk[0]^=1u;
    ODM_TEST_CHECK(context,odm_controller_signature_verify(wrong_pk,receipt,receipt_bytes,envelope)==ODM_STATUS_INTEGRITY_ERROR);
    receipt[100]^=1u;ODM_TEST_CHECK(context,odm_controller_signature_verify(controller_pk,receipt,receipt_bytes,envelope)!=ODM_STATUS_OK);receipt[100]^=1u;

    /* NO_PARTIAL_MASTER: sink failure after begin aborts and never publishes receipt. */
    memset(&sink_state,0,sizeof(sink_state));sink_state.fail_frame=1u;memset(receipt,0x6d,sizeof(receipt));memcpy(receipt_before,receipt,sizeof(receipt));
    ODM_TEST_CHECK(context,odm_master_run(&request,&sink,frame,plan.frame_bytes,scratch,plan.renderer_scratch_bytes,receipt,sizeof(receipt),&receipt_bytes,&receipt_info)==ODM_STATUS_INTERNAL_ERROR);
    ODM_TEST_CHECK(context,sink_state.began==1u&&sink_state.frames==1u&&sink_state.committed==0u&&sink_state.aborted==1u&&memcmp(receipt,receipt_before,sizeof(receipt))==0);

    /* Cancellation before begin cannot create an active sink. The master only
     * publishes progress; the caller owns job terminalization. */
    ODM_TEST_CHECK(context,odm_job_init(&job)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_job_begin(&job,plan.quoted_work_units,&ticket)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_job_request_cancel(&job)==ODM_STATUS_OK);
    request.job_ticket=&ticket;memset(&sink_state,0,sizeof(sink_state));sink_state.fail_frame=UINT32_MAX;
    ODM_TEST_CHECK(context,odm_master_run(&request,&sink,frame,plan.frame_bytes,scratch,plan.renderer_scratch_bytes,receipt,sizeof(receipt),&receipt_bytes,&receipt_info)==ODM_STATUS_CANCELLED);
    ODM_TEST_CHECK(context,sink_state.began==0u&&sink_state.committed==0u&&sink_state.aborted==0u);
    ODM_TEST_CHECK(context,odm_job_finish(ticket,ODM_STATUS_CANCELLED)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_job_snapshot(&job,&snapshot)==ODM_STATUS_OK&&snapshot.state==ODM_JOB_CANCELLED);ODM_TEST_CHECK(context,odm_job_destroy(&job)==ODM_STATUS_OK);request.job_ticket=NULL;

cleanup:
    free(scratch);free(frame);free(package);free(ir);free(analysis);free(pcm);free(twiddles);free(window);
}

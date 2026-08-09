#include "test_harness.h"

#include "odm_ir.h"
#include "odm_wire.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t t_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void t_write_u32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}

static void t_write_u64(uint8_t *p, uint64_t v) {
    uint32_t i; for(i=0u;i<8u;++i)p[i]=(uint8_t)(v>>(8u*i));
}

static uint8_t *t_find_section(uint8_t *payload, uint64_t payload_bytes,
                               uint32_t wanted_id, uint32_t *out_count,
                               uint32_t *out_record_bytes) {
    uint64_t pos=ODM_RENDER_IR_HEADER_BYTES;
    uint32_t i;
    for(i=0u;i<ODM_RENDER_IR_SECTION_COUNT;++i){
        uint32_t id,count,rb;uint64_t bytes;
        if(pos>payload_bytes||payload_bytes-pos<ODM_RENDER_IR_SECTION_HEADER_BYTES)return NULL;
        id=t_read_u32(payload+pos);count=t_read_u32(payload+pos+8u);rb=t_read_u32(payload+pos+12u);
        pos+=ODM_RENDER_IR_SECTION_HEADER_BYTES;bytes=(uint64_t)count*(uint64_t)rb;
        if(bytes>payload_bytes-pos)return NULL;
        if(id==wanted_id){if(out_count)*out_count=count;if(out_record_bytes)*out_record_bytes=rb;return payload+pos;}
        pos+=bytes;
    }
    return NULL;
}

static odm_status t_resign_payload(uint8_t *record,uint64_t capacity,
                                   uint8_t *payload,uint64_t payload_bytes,
                                   uint64_t *out_required) {
    return odm_wire_record_write(ODM_RENDER_IR_KIND,ODM_RENDER_IR_SCHEMA_MAJOR,
                                 ODM_RENDER_IR_SCHEMA_MINOR,payload,payload_bytes,
                                 record,capacity,out_required,NULL);
}

static void init_node(odm_score_node *n,uint32_t id,uint32_t scene,uint32_t role,
                      uint32_t cap,uint32_t resource,int64_t start,int64_t end,
                      int32_t z) {
    memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;
    n->capability_id=cap;n->capability_major=1u;n->capability_minor=0u;
    n->resource_id=resource;n->state_model=ODM_STATELESS;n->z_index=z;
    n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;
    n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;
    n->seed_domain=(uint64_t)id*UINT64_C(0x9e3779b97f4a7c15);
}

void odm_test_score_ir(odm_test_context *context) {
    odm_score_header h={0};odm_score_resource resources[1];odm_score_act acts[1];
    odm_score_scene scenes[2];odm_score_node nodes[5];odm_score_cue cues[1];
    odm_score_modulator mods[1];odm_score_automation autos[1];
    odm_score_transition transitions[1];odm_score_state_transfer transfers[1];
    odm_score_view score;odm_music_map_binding music;odm_sha256_digest score_sha,cap_sha,ir_sha;
    odm_render_ir_info info;uint8_t *ir=NULL,*hostile=NULL;uint64_t required=0u,off=0u,payload_off=0u;
    odm_wire_record_info wi;odm_frame_state fs;odm_frame_node_state frame_nodes[5];
    odm_frame_transfer_event frame_transfers[1];uint64_t need_nodes=0u,need_transfers=0u;
    odm_music_analysis_tick tick;uint32_t i;

    memset(resources,0,sizeof(resources));memset(acts,0,sizeof(acts));memset(scenes,0,sizeof(scenes));
    memset(nodes,0,sizeof(nodes));memset(cues,0,sizeof(cues));memset(mods,0,sizeof(mods));
    memset(autos,0,sizeof(autos));memset(transitions,0,sizeof(transitions));memset(transfers,0,sizeof(transfers));
    memset(&score,0,sizeof(score));memset(&music,0,sizeof(music));

    h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;
    h.fps=30;h.width=1920u;h.height=1080u;h.project_end_sample=48000;h.project_seed=UINT64_C(0x123456789abcdef0);
    h.resource_count=1u;h.act_count=1u;h.scene_count=2u;h.node_count=5u;h.cue_count=1u;
    h.modulator_count=1u;h.automation_count=1u;h.transition_count=1u;h.transfer_count=1u;
    resources[0].resource_id=1u;resources[0].kind=ODM_SCORE_RESOURCE_IMAGE;
    ODM_TEST_CHECK(context,odm_sha256("image-one",9u,&resources[0].content_sha256)==ODM_STATUS_OK);
    acts[0].act_id=1u;acts[0].start_sample=0;acts[0].end_sample=48000;
    scenes[0].scene_id=1u;scenes[0].act_id=1u;scenes[0].environment_node_id=1u;scenes[0].primary_node_id=2u;scenes[0].start_sample=0;scenes[0].end_sample=30000;
    scenes[1].scene_id=2u;scenes[1].act_id=1u;scenes[1].environment_node_id=3u;scenes[1].primary_node_id=4u;scenes[1].start_sample=24000;scenes[1].end_sample=48000;
    init_node(&nodes[0],1u,1u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0u,0,30000,-100);
    init_node(&nodes[1],2u,1u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,0u,0,30000,0);
    init_node(&nodes[2],3u,2u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0u,24000,48000,-100);
    init_node(&nodes[3],4u,2u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1u,24000,48000,0);
    init_node(&nodes[4],5u,2u,ODM_SCORE_NODE_EFFECT,ODM_CAP_RESIDUAL_EVENT_FIELD,0u,24000,48000,10);
    nodes[4].state_model=ODM_EVENT_HISTORY;nodes[4].state_slot=1u;
    nodes[4].opacity_micro=500000;
    cues[0].cue_id=1u;cues[0].scene_id=1u;cues[0].kind=ODM_SCORE_CUE_GENERIC;cues[0].sample=1000;
    mods[0].modulator_id=1u;mods[0].node_id=5u;mods[0].target_parameter=ODM_PARAM_OPACITY;mods[0].source=ODM_MOD_RMS_MID;mods[0].scale_micro=64000000;
    autos[0].automation_id=1u;autos[0].node_id=5u;autos[0].target_parameter=ODM_PARAM_X;autos[0].interpolation=ODM_AUTOMATION_LINEAR;autos[0].start_sample=24000;autos[0].end_sample=48000;autos[0].from_micro=0;autos[0].to_micro=1000000;
    transitions[0].transition_id=1u;transitions[0].from_scene_id=1u;transitions[0].to_scene_id=2u;transitions[0].kind=ODM_TRANSITION_CROSSFADE;transitions[0].start_sample=24000;transitions[0].end_sample=30000;
    transfers[0].transfer_id=1u;transfers[0].from_scene_id=1u;transfers[0].to_scene_id=2u;transfers[0].state_slot=1u;transfers[0].mode=ODM_TRANSFER_RESET;transfers[0].at_sample=24000;
    score.header=&h;score.resources=resources;score.acts=acts;score.scenes=scenes;score.nodes=nodes;score.cues=cues;score.modulators=mods;score.automation=autos;score.transitions=transitions;score.transfers=transfers;

    ODM_TEST_CHECK(context,odm_score_validate(&score)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_score_hash(&score,&score_sha)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_score_capability_hash(&score,&cap_sha)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_sha256("music-map",9u,&music.music_map_sha256)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_music_policy_current_sha256(&music.music_policy_sha256)==ODM_STATUS_OK);
    music.canonical_frames=30000u;
    {
        odm_music_map_binding wrong=music;
        ODM_TEST_CHECK(context,odm_sha256("wrong-policy",12u,&wrong.music_policy_sha256)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&wrong,NULL,0u,&required,NULL)==ODM_STATUS_VERSION_MISMATCH);
    }
    {
        odm_music_map_binding too_long=music; too_long.canonical_frames=48001u;
        ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&too_long,NULL,0u,&required,NULL)==ODM_STATUS_INVALID_ARGUMENT);
    }

    ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&music,NULL,0u,&required,NULL)==ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context,required>ODM_WIRE_RECORD_HEADER_BYTES+ODM_RENDER_IR_HEADER_BYTES);
    ir=(uint8_t*)malloc((size_t)required+1u);hostile=(uint8_t*)malloc((size_t)required+1u);
    ODM_TEST_CHECK(context,ir!=NULL);ODM_TEST_CHECK(context,hostile!=NULL);if(!ir||!hostile)goto cleanup;
    memset(ir,0xa5,(size_t)required+1u);
    ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&music,ir,required,&required,&ir_sha)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,ir[required]==UINT8_C(0xa5));
    ODM_TEST_CHECK(context,odm_render_ir_validate(ir,required,&score_sha,&music.music_policy_sha256,&info)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,info.fps==30&&info.samples_per_frame==1600&&info.frame_count==30&&info.output_end_sample==48000);
    ODM_TEST_CHECK(context,odm_sha256_equal(&info.capability_sha256,&cap_sha)!=0);
    ODM_TEST_CHECK(context,odm_sha256_equal(&info.record_sha256,&ir_sha)!=0);

    memset(&tick,0,sizeof(tick));tick.tick_index=53u;tick.center_sample=53u*480u;tick.rms_mid_q31=INT32_MAX/2;
    ODM_TEST_CHECK(context,odm_frame_state_requirements(ir,required,16,&tick,&need_nodes,&need_transfers)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,need_nodes==5u&&need_transfers==0u);
    {
        uint8_t before_nodes[sizeof(frame_nodes)]; odm_frame_state before_state;
        memset(frame_nodes,0x41,sizeof(frame_nodes));memcpy(before_nodes,frame_nodes,sizeof(frame_nodes));
        memset(&fs,0x42,sizeof(fs));before_state=fs;
        ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,required,16,&tick,frame_nodes,4u,frame_transfers,1u,&need_nodes,&need_transfers,&fs)==ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context,need_nodes==5u&&need_transfers==0u);
        ODM_TEST_CHECK(context,memcmp(frame_nodes,before_nodes,sizeof(frame_nodes))==0);
        ODM_TEST_CHECK(context,memcmp(&fs,&before_state,sizeof(fs))==0);
    }
    memset(&fs,0xcc,sizeof(fs));memset(frame_nodes,0xcc,sizeof(frame_nodes));
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,required,16,&tick,frame_nodes,5u,frame_transfers,1u,&need_nodes,&need_transfers,&fs)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,fs.sample==25600&&fs.scene_a_id==1u&&fs.scene_b_id==2u&&fs.transition_id==1u&&fs.transition_progress_q31>0);
    ODM_TEST_CHECK(context,frame_nodes[4].node_id==5u&&frame_nodes[4].x_q32_32>0&&frame_nodes[4].opacity_q31>0&&frame_nodes[4].scene_weight_q31==fs.transition_progress_q31);
    ODM_TEST_CHECK(context,frame_nodes[4].opacity_q31==frame_nodes[4].scene_weight_q31);
    tick.tick_index=52u;tick.center_sample=52u*480u;
    ODM_TEST_CHECK(context,odm_frame_state_requirements(ir,required,16,&tick,&need_nodes,&need_transfers)==ODM_STATUS_INVALID_ARGUMENT);
    {
        uint8_t before_nodes[sizeof(frame_nodes)]; odm_frame_state before_state;
        memset(frame_nodes,0x7b,sizeof(frame_nodes));memcpy(before_nodes,frame_nodes,sizeof(frame_nodes));
        memset(&fs,0x6c,sizeof(fs));before_state=fs;
        ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,required,16,&tick,frame_nodes,4u,frame_transfers,1u,&need_nodes,&need_transfers,&fs)==ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,memcmp(frame_nodes,before_nodes,sizeof(frame_nodes))==0);
        ODM_TEST_CHECK(context,memcmp(&fs,&before_state,sizeof(fs))==0);
    }

    memset(&tick,0,sizeof(tick));tick.tick_index=50u;tick.center_sample=24000u;
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,required,15,&tick,frame_nodes,5u,frame_transfers,1u,&need_nodes,&need_transfers,&fs)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,need_transfers==1u&&frame_transfers[0].transfer_id==1u&&fs.sample==24000);
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,required,20,NULL,frame_nodes,5u,frame_transfers,1u,&need_nodes,&need_transfers,&fs)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,fs.sample==32000&&fs.scene_a_id==2u&&fs.scene_b_id==0u);
    ODM_TEST_CHECK(context,odm_frame_state_resolve(ir,required,30,NULL,frame_nodes,5u,frame_transfers,1u,&need_nodes,&need_transfers,&fs)==ODM_STATUS_INVALID_ARGUMENT);

    ODM_TEST_CHECK(context,odm_wire_record_read(ir,required,&wi,&off,NULL)==ODM_STATUS_OK);
    payload_off=off;
    memcpy(hostile,ir+payload_off,(size_t)wi.payload_bytes);hostile[216u]=1u;
    ODM_TEST_CHECK(context,odm_wire_record_write(ODM_RENDER_IR_KIND,ODM_RENDER_IR_SCHEMA_MAJOR,ODM_RENDER_IR_SCHEMA_MINOR,hostile,wi.payload_bytes,hostile,required,&off,NULL)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_render_ir_validate(hostile,required,NULL,NULL,&info)==ODM_STATUS_INVALID_DATA);

    /* Re-signed hostile payloads must still fail semantic validation. */
    {
        uint8_t *payload=hostile+ODM_WIRE_RECORD_HEADER_BYTES;uint64_t rr=0u;
        memcpy(payload,ir+payload_off,(size_t)wi.payload_bytes);
        t_write_u64(payload+72u,48001u);
        ODM_TEST_CHECK(context,t_resign_payload(hostile,required,payload,wi.payload_bytes,&rr)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,odm_render_ir_validate(hostile,required,NULL,NULL,&info)==ODM_STATUS_INVALID_DATA);
    }
    {
        uint8_t *payload=hostile+ODM_WIRE_RECORD_HEADER_BYTES;uint64_t rr=0u;
        memcpy(payload,ir+payload_off,(size_t)wi.payload_bytes);payload[184u]^=UINT8_C(1);
        ODM_TEST_CHECK(context,t_resign_payload(hostile,required,payload,wi.payload_bytes,&rr)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,odm_render_ir_validate(hostile,required,NULL,NULL,&info)==ODM_STATUS_VERSION_MISMATCH);
    }
    {
        uint8_t *payload=hostile+ODM_WIRE_RECORD_HEADER_BYTES,*timeline;uint64_t rr=0u;uint32_t count=0u,rb=0u;
        memcpy(payload,ir+payload_off,(size_t)wi.payload_bytes);timeline=t_find_section(payload,wi.payload_bytes,ODM_IR_SECTION_TIMELINE,&count,&rb);
        ODM_TEST_CHECK(context,timeline!=NULL&&count==4u&&rb==ODM_IR_TIMELINE_RECORD_BYTES);
        if(timeline){t_write_u64(timeline+(uint64_t)2u*rb+24u,47000u);ODM_TEST_CHECK(context,t_resign_payload(hostile,required,payload,wi.payload_bytes,&rr)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_render_ir_validate(hostile,required,NULL,NULL,&info)==ODM_STATUS_INVALID_DATA);}
    }
    {
        uint8_t *payload=hostile+ODM_WIRE_RECORD_HEADER_BYTES,*trs;uint64_t rr=0u;uint32_t count=0u,rb=0u;
        memcpy(payload,ir+payload_off,(size_t)wi.payload_bytes);trs=t_find_section(payload,wi.payload_bytes,ODM_IR_SECTION_TRANSITIONS,&count,&rb);
        ODM_TEST_CHECK(context,trs!=NULL&&count==1u&&rb==ODM_IR_TRANSITION_RECORD_BYTES);
        if(trs){t_write_u32(trs+4u,2u);ODM_TEST_CHECK(context,t_resign_payload(hostile,required,payload,wi.payload_bytes,&rr)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_render_ir_validate(hostile,required,NULL,NULL,&info)==ODM_STATUS_INVALID_DATA);}
    }

    {
        odm_score_view other=score;odm_score_node copied[5];odm_sha256_digest d;
        memcpy(copied,nodes,sizeof(copied));other.nodes=copied;
        ODM_TEST_CHECK(context,odm_score_hash(&other,&d)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,odm_sha256_equal(&d,&score_sha)!=0);
    }
    {
        odm_score_node saved=nodes[4];nodes[4].role=ODM_SCORE_NODE_PRIMARY;
        ODM_TEST_CHECK(context,odm_score_validate(&score)==ODM_STATUS_UNSUPPORTED||odm_score_validate(&score)==ODM_STATUS_INVALID_DATA);
        nodes[4]=saved;
    }
    {
        odm_score_automation saved=autos[0];
        autos[0].interpolation=ODM_AUTOMATION_STEP;
        ODM_TEST_CHECK(context,odm_score_validate(&score)==ODM_STATUS_INVALID_DATA);
        autos[0]=saved;
    }
    for(i=0u;i<10u;++i){odm_sha256_digest d;uint64_t r=required;ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&music,hostile,required,&r,&d)==ODM_STATUS_OK);ODM_TEST_CHECK(context,r==required&&memcmp(hostile,ir,(size_t)required)==0&&odm_sha256_equal(&d,&ir_sha)!=0);}

cleanup:
    free(hostile);free(ir);
}

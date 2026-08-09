#include "odm_ir.h"
#include "odm_fixed.h"
#include "odm_time.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define ODM_IR_HEADER_FLAGS 0u

static int u64_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return 0;
    *out = a + b; return 1;
}
static int u64_mul(uint64_t a, uint64_t b, uint64_t *out) {
    if (a != 0u && b > UINT64_MAX / a) return 0;
    *out = a * b; return 1;
}

static int digest_nonzero(const odm_sha256_digest *d) {
    uint32_t i; for (i=0u;i<ODM_SHA256_DIGEST_BYTES;++i) if(d->bytes[i]!=0u)return 1; return 0;
}

static int score_uses_cap(const odm_score_view *score, uint32_t id) {
    uint32_t i;
    for(i=0u;i<score->header->node_count;++i) if(score->nodes[i].capability_id==id)return 1;
    if(id==ODM_CAP_AUTOMATION && score->header->automation_count!=0u)return 1;
    if(id==ODM_CAP_MUSIC_MODULATOR && score->header->modulator_count!=0u)return 1;
    if(id==ODM_CAP_CROSSFADE && score->header->transition_count!=0u)return 1;
    if(id==ODM_CAP_STATE_TRANSFER && score->header->transfer_count!=0u)return 1;
    if(id==ODM_CAP_TIMELINE_CUE && score->header->cue_count!=0u)return 1;
    return 0;
}

static odm_status zeroes(odm_wire_writer *w, uint64_t count) {
    static const uint8_t z[64]={0};
    while(count!=0u){uint64_t n=count>sizeof(z)?sizeof(z):count;odm_status s=odm_wire_write_bytes(w,z,n);if(s!=ODM_STATUS_OK)return s;count-=n;}
    return ODM_STATUS_OK;
}

static odm_status write_section_header(odm_wire_writer *w, uint32_t id,
                                       uint32_t count, uint32_t record_bytes) {
    odm_status s;
    s=odm_wire_write_u32(w,id); if(s!=ODM_STATUS_OK)return s;
    s=odm_wire_write_u32(w,1u); if(s!=ODM_STATUS_OK)return s;
    s=odm_wire_write_u32(w,count); if(s!=ODM_STATUS_OK)return s;
    return odm_wire_write_u32(w,record_bytes);
}

static odm_status value_to_ir(uint32_t parameter, int64_t micro, int64_t *out) {
    odm_q32_32 q32; odm_q1_31 q31; odm_phase_u32 phase; odm_status s;
    if(!out)return ODM_STATUS_INVALID_ARGUMENT;
    switch(parameter){
      case ODM_PARAM_X: case ODM_PARAM_Y: case ODM_PARAM_SCALE_X: case ODM_PARAM_SCALE_Y:
        s=odm_q32_32_from_microunits(micro,&q32);if(s!=ODM_STATUS_OK)return s;*out=q32;return ODM_STATUS_OK;
      case ODM_PARAM_OPACITY:
        s=odm_q1_31_from_ratio(micro,ODM_MICRO_ONE,&q31);if(s!=ODM_STATUS_OK)return s;*out=(int64_t)q31;return ODM_STATUS_OK;
      case ODM_PARAM_PHASE:
        s=odm_phase_from_microturns(micro,&phase);if(s!=ODM_STATUS_OK)return s;*out=(int64_t)(uint64_t)phase;return ODM_STATUS_OK;
      default:return ODM_STATUS_INVALID_DATA;
    }
}

static odm_status calc_required(const odm_score_view *score, uint32_t cap_count,
                                uint64_t *out_payload, uint64_t *out_total) {
    uint64_t payload=ODM_RENDER_IR_HEADER_BYTES, timeline_count, part;
    const odm_score_header*h=score->header;
#define ADD_SECTION(count, bytes) do{if(!u64_add(payload,ODM_RENDER_IR_SECTION_HEADER_BYTES,&payload)||!u64_mul((uint64_t)(count),(uint64_t)(bytes),&part)||!u64_add(payload,part,&payload))return ODM_STATUS_OVERFLOW;}while(0)
    if(!u64_add((uint64_t)h->act_count,(uint64_t)h->scene_count,&timeline_count)||
       !u64_add(timeline_count,(uint64_t)h->cue_count,&timeline_count) || timeline_count>UINT32_MAX) return ODM_STATUS_OVERFLOW;
    ADD_SECTION(cap_count,ODM_IR_CAPABILITY_RECORD_BYTES);
    ADD_SECTION(h->resource_count,ODM_IR_RESOURCE_RECORD_BYTES);
    ADD_SECTION((uint32_t)timeline_count,ODM_IR_TIMELINE_RECORD_BYTES);
    ADD_SECTION(h->node_count,ODM_IR_NODE_RECORD_BYTES);
    ADD_SECTION(h->modulator_count,ODM_IR_MODULATOR_RECORD_BYTES);
    ADD_SECTION(h->automation_count,ODM_IR_AUTOMATION_RECORD_BYTES);
    ADD_SECTION(h->transition_count,ODM_IR_TRANSITION_RECORD_BYTES);
    ADD_SECTION(h->transfer_count,ODM_IR_TRANSFER_RECORD_BYTES);
    ADD_SECTION(1u,ODM_IR_OUTPUT_RECORD_BYTES);
#undef ADD_SECTION
    if(!u64_add(payload,ODM_WIRE_RECORD_HEADER_BYTES,out_total))return ODM_STATUS_OVERFLOW;
    *out_payload=payload; return ODM_STATUS_OK;
}

static odm_status write_header(odm_wire_writer*w,const odm_score_view*score,
                               const odm_music_map_binding*music,const odm_timeline*tl,
                               const odm_sha256_digest*score_sha,const odm_sha256_digest*cap_sha) {
    static const uint8_t magic[8]={'O','D','R','I','R','V','0',0}; odm_status s;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
    W(odm_wire_write_bytes(w,magic,8u));W(odm_wire_write_u32(w,ODM_RENDER_IR_SCHEMA_MAJOR));W(odm_wire_write_u32(w,ODM_RENDER_IR_SCHEMA_MINOR));
    W(odm_wire_write_u32(w,ODM_IR_HEADER_FLAGS));W(odm_wire_write_i32(w,score->header->fps));W(odm_wire_write_u32(w,score->header->width));W(odm_wire_write_u32(w,score->header->height));W(odm_wire_write_u32(w,ODM_MUSIC_SAMPLE_RATE));W(odm_wire_write_u32(w,ODM_RENDER_IR_SECTION_COUNT));
    W(odm_wire_write_i64(w,tl->samples_per_frame));W(odm_wire_write_i64(w,score->header->project_end_sample));W(odm_wire_write_i64(w,tl->frame_count));W(odm_wire_write_i64(w,tl->output_end_sample));
    W(odm_wire_write_u64(w,music->canonical_frames));W(odm_wire_write_u64(w,score->header->project_seed));
    W(odm_wire_write_bytes(w,score_sha->bytes,32u));W(odm_wire_write_bytes(w,cap_sha->bytes,32u));W(odm_wire_write_bytes(w,music->music_map_sha256.bytes,32u));W(odm_wire_write_bytes(w,music->music_policy_sha256.bytes,32u));
    W(zeroes(w,40u));
#undef W
    return ODM_STATUS_OK;
}

static odm_status write_caps(odm_wire_writer*w,const odm_score_view*score,uint32_t cap_count){
    uint32_t i,n;const odm_visual_capability_info*caps=odm_visual_capabilities(&n);odm_status s;
    s=write_section_header(w,ODM_IR_SECTION_CAPABILITIES,cap_count,ODM_IR_CAPABILITY_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;
    for(i=0u;i<n;++i){const odm_visual_capability_info*c=&caps[i];if(!score_uses_cap(score,c->capability_id))continue;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
      W(odm_wire_write_u32(w,c->capability_id));W(odm_wire_write_u32(w,c->version_major));W(odm_wire_write_u32(w,c->version_minor));W(odm_wire_write_u32(w,c->state_model));W(odm_wire_write_u32(w,c->allowed_roles_mask));W(odm_wire_write_u32(w,c->resource_kind));W(zeroes(w,8u));
#undef W
    } return ODM_STATUS_OK;
}

static odm_status write_resources(odm_wire_writer*w,const odm_score_view*score){uint32_t i;odm_status s;s=write_section_header(w,ODM_IR_SECTION_RESOURCES,score->header->resource_count,ODM_IR_RESOURCE_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->resource_count;++i){const odm_score_resource*r=&score->resources[i];s=odm_wire_write_u32(w,r->resource_id);if(s!=ODM_STATUS_OK)return s;s=odm_wire_write_u32(w,r->kind);if(s!=ODM_STATUS_OK)return s;s=odm_wire_write_u32(w,0u);if(s!=ODM_STATUS_OK)return s;s=odm_wire_write_u32(w,0u);if(s!=ODM_STATUS_OK)return s;s=odm_wire_write_bytes(w,r->content_sha256.bytes,32u);if(s!=ODM_STATUS_OK)return s;s=zeroes(w,16u);if(s!=ODM_STATUS_OK)return s;}return ODM_STATUS_OK;}

static odm_status timeline_record(odm_wire_writer*w,uint32_t kind,uint32_t id,uint32_t parent,int64_t start,int64_t end,uint32_t a,uint32_t b){odm_status s;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(odm_wire_write_u32(w,kind));W(odm_wire_write_u32(w,id));W(odm_wire_write_u32(w,parent));W(odm_wire_write_u32(w,0u));W(odm_wire_write_i64(w,start));W(odm_wire_write_i64(w,end));W(odm_wire_write_u32(w,a));W(odm_wire_write_u32(w,b));W(zeroes(w,24u));
#undef W
 return ODM_STATUS_OK;}

static odm_status write_timeline(odm_wire_writer*w,const odm_score_view*score){uint32_t i;uint64_t total=(uint64_t)score->header->act_count+score->header->scene_count+score->header->cue_count;odm_status s=write_section_header(w,ODM_IR_SECTION_TIMELINE,(uint32_t)total,ODM_IR_TIMELINE_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->act_count;++i){const odm_score_act*a=&score->acts[i];s=timeline_record(w,ODM_IR_TIMELINE_ACT,a->act_id,0u,a->start_sample,a->end_sample,0u,0u);if(s!=ODM_STATUS_OK)return s;}for(i=0u;i<score->header->scene_count;++i){const odm_score_scene*x=&score->scenes[i];s=timeline_record(w,ODM_IR_TIMELINE_SCENE,x->scene_id,x->act_id,x->start_sample,x->end_sample,x->environment_node_id,x->primary_node_id);if(s!=ODM_STATUS_OK)return s;}for(i=0u;i<score->header->cue_count;++i){const odm_score_cue*q=&score->cues[i];s=timeline_record(w,ODM_IR_TIMELINE_CUE,q->cue_id,q->scene_id,q->sample,q->sample,q->kind,0u);if(s!=ODM_STATUS_OK)return s;}return ODM_STATUS_OK;}

static odm_status write_nodes(odm_wire_writer*w,const odm_score_view*score){uint32_t i;odm_status s=write_section_header(w,ODM_IR_SECTION_NODES,score->header->node_count,ODM_IR_NODE_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->node_count;++i){const odm_score_node*n=&score->nodes[i];odm_q32_32 x,y,sx,sy;odm_q1_31 opacity;odm_phase_u32 phase;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(odm_q32_32_from_microunits(n->x_micro,&x));W(odm_q32_32_from_microunits(n->y_micro,&y));W(odm_q32_32_from_microunits(n->scale_x_micro,&sx));W(odm_q32_32_from_microunits(n->scale_y_micro,&sy));W(odm_q1_31_from_ratio(n->opacity_micro,ODM_MICRO_ONE,&opacity));W(odm_phase_from_microturns(n->phase_microturns,&phase));
 W(odm_wire_write_u32(w,n->node_id));W(odm_wire_write_u32(w,n->scene_id));W(odm_wire_write_u32(w,n->role));W(odm_wire_write_u32(w,n->capability_id));W(odm_wire_write_u32(w,n->capability_major));W(odm_wire_write_u32(w,n->capability_minor));W(odm_wire_write_u32(w,n->resource_id));W(odm_wire_write_u32(w,n->state_model));W(odm_wire_write_i32(w,n->z_index));W(odm_wire_write_u32(w,0u));W(odm_wire_write_i64(w,n->start_sample));W(odm_wire_write_i64(w,n->end_sample));W(odm_wire_write_i64(w,x));W(odm_wire_write_i64(w,y));W(odm_wire_write_i64(w,sx));W(odm_wire_write_i64(w,sy));W(odm_wire_write_i32(w,opacity));W(odm_wire_write_u32(w,phase));W(odm_wire_write_u64(w,n->seed_domain));W(odm_wire_write_u32(w,n->state_slot));W(odm_wire_write_u32(w,n->entry_kind));W(odm_wire_write_u32(w,n->exit_kind));W(odm_wire_write_u32(w,0u));W(odm_wire_write_u32(w,n->entry_duration_samples));W(odm_wire_write_u32(w,n->exit_duration_samples));
#undef W
 }return ODM_STATUS_OK;}

static odm_status write_modulators(odm_wire_writer*w,const odm_score_view*score){uint32_t i;odm_status s=write_section_header(w,ODM_IR_SECTION_MODULATORS,score->header->modulator_count,ODM_IR_MODULATOR_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->modulator_count;++i){const odm_score_modulator*m=&score->modulators[i];odm_q32_32 scale,bias;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(odm_q32_32_from_microunits(m->scale_micro,&scale));W(odm_q32_32_from_microunits(m->bias_micro,&bias));W(odm_wire_write_u32(w,m->modulator_id));W(odm_wire_write_u32(w,m->node_id));W(odm_wire_write_u32(w,m->target_parameter));W(odm_wire_write_u32(w,m->source));W(odm_wire_write_u32(w,m->band_index));W(odm_wire_write_u32(w,0u));W(odm_wire_write_i64(w,scale));W(odm_wire_write_i64(w,bias));W(zeroes(w,24u));
#undef W
 }return ODM_STATUS_OK;}

static odm_status write_automation(odm_wire_writer*w,const odm_score_view*score){uint32_t i;odm_status s=write_section_header(w,ODM_IR_SECTION_AUTOMATION,score->header->automation_count,ODM_IR_AUTOMATION_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->automation_count;++i){const odm_score_automation*a=&score->automation[i];int64_t from,to;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(value_to_ir(a->target_parameter,a->from_micro,&from));W(value_to_ir(a->target_parameter,a->to_micro,&to));W(odm_wire_write_u32(w,a->automation_id));W(odm_wire_write_u32(w,a->node_id));W(odm_wire_write_u32(w,a->target_parameter));W(odm_wire_write_u32(w,a->interpolation));W(odm_wire_write_u32(w,0u));W(odm_wire_write_u32(w,0u));W(odm_wire_write_i64(w,a->start_sample));W(odm_wire_write_i64(w,a->end_sample));W(odm_wire_write_i64(w,from));W(odm_wire_write_i64(w,to));W(zeroes(w,8u));
#undef W
 }return ODM_STATUS_OK;}

static odm_status write_transitions(odm_wire_writer*w,const odm_score_view*score){uint32_t i;odm_status s=write_section_header(w,ODM_IR_SECTION_TRANSITIONS,score->header->transition_count,ODM_IR_TRANSITION_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->transition_count;++i){const odm_score_transition*t=&score->transitions[i];
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(odm_wire_write_u32(w,t->transition_id));W(odm_wire_write_u32(w,t->from_scene_id));W(odm_wire_write_u32(w,t->to_scene_id));W(odm_wire_write_u32(w,t->kind));W(odm_wire_write_i64(w,t->start_sample));W(odm_wire_write_i64(w,t->end_sample));W(odm_wire_write_u32(w,0u));W(zeroes(w,28u));
#undef W
 }return ODM_STATUS_OK;}

static odm_status write_transfers(odm_wire_writer*w,const odm_score_view*score){uint32_t i;odm_status s=write_section_header(w,ODM_IR_SECTION_STATE_TRANSFER,score->header->transfer_count,ODM_IR_TRANSFER_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;for(i=0u;i<score->header->transfer_count;++i){const odm_score_state_transfer*t=&score->transfers[i];
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(odm_wire_write_u32(w,t->transfer_id));W(odm_wire_write_u32(w,t->from_scene_id));W(odm_wire_write_u32(w,t->to_scene_id));W(odm_wire_write_u32(w,t->state_slot));W(odm_wire_write_u32(w,t->mode));W(odm_wire_write_u32(w,0u));W(odm_wire_write_i64(w,t->at_sample));W(zeroes(w,32u));
#undef W
 }return ODM_STATUS_OK;}

static odm_status write_output(odm_wire_writer*w,const odm_score_view*score,const odm_timeline*tl){odm_status s=write_section_header(w,ODM_IR_SECTION_OUTPUT,1u,ODM_IR_OUTPUT_RECORD_BYTES);if(s!=ODM_STATUS_OK)return s;
#define W(call) do{s=(call);if(s!=ODM_STATUS_OK)return s;}while(0)
 W(odm_wire_write_u32(w,score->header->width));W(odm_wire_write_u32(w,score->header->height));W(odm_wire_write_i32(w,score->header->fps));W(odm_wire_write_u32(w,ODM_MUSIC_SAMPLE_RATE));W(odm_wire_write_i64(w,tl->samples_per_frame));W(odm_wire_write_i64(w,score->header->project_end_sample));W(odm_wire_write_i64(w,tl->frame_count));W(odm_wire_write_i64(w,tl->output_end_sample));W(zeroes(w,16u));
#undef W
 return ODM_STATUS_OK;}

odm_status odm_score_compile_render_ir(const odm_score_view *score,
                                       const odm_music_map_binding *music,
                                       uint8_t *buffer,uint64_t capacity,
                                       uint64_t *out_required,
                                       odm_sha256_digest *out_ir_sha256) {
    odm_status s;odm_sha256_digest score_sha,cap_sha,current_policy;odm_timeline tl;uint32_t cap_count=0u,n,i;uint64_t payload,total,written;odm_wire_writer w=ODM_WIRE_WRITER_INITIALIZER;const odm_visual_capability_info*caps;
    if(!out_required||!music)return ODM_STATUS_INVALID_ARGUMENT;
    s=odm_score_validate(score);if(s!=ODM_STATUS_OK)return s;
    if(music->canonical_frames==0u || music->canonical_frames>(uint64_t)score->header->project_end_sample ||
       !digest_nonzero(&music->music_map_sha256)||!digest_nonzero(&music->music_policy_sha256))return ODM_STATUS_INVALID_ARGUMENT;
    s=odm_music_policy_current_sha256(&current_policy);if(s!=ODM_STATUS_OK)return s;
    if(!odm_sha256_equal(&current_policy,&music->music_policy_sha256))return ODM_STATUS_VERSION_MISMATCH;
    s=odm_score_hash(score,&score_sha);if(s!=ODM_STATUS_OK)return s;s=odm_score_capability_hash(score,&cap_sha);if(s!=ODM_STATUS_OK)return s;
    s=odm_time_resolve_timeline(score->header->project_end_sample,score->header->fps,&tl);if(s!=ODM_STATUS_OK)return s;
    caps=odm_visual_capabilities(&n);for(i=0u;i<n;++i)if(score_uses_cap(score,caps[i].capability_id))++cap_count;
    s=calc_required(score,cap_count,&payload,&total);if(s!=ODM_STATUS_OK)return s;*out_required=total;
    if(!buffer||capacity<total)return ODM_STATUS_BUFFER_TOO_SMALL;
    s=odm_wire_writer_init(&w,buffer+ODM_WIRE_RECORD_HEADER_BYTES,capacity-ODM_WIRE_RECORD_HEADER_BYTES);if(s!=ODM_STATUS_OK)return s;
    s=write_header(&w,score,music,&tl,&score_sha,&cap_sha);if(s!=ODM_STATUS_OK)return s;
    s=write_caps(&w,score,cap_count);if(s!=ODM_STATUS_OK)return s;s=write_resources(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_timeline(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_nodes(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_modulators(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_automation(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_transitions(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_transfers(&w,score);if(s!=ODM_STATUS_OK)return s;s=write_output(&w,score,&tl);if(s!=ODM_STATUS_OK)return s;
    s=odm_wire_writer_finish(&w,&written);if(s!=ODM_STATUS_OK)return s;if(written!=payload)return ODM_STATUS_INVARIANT_BROKEN;
    return odm_wire_record_write(ODM_RENDER_IR_KIND,ODM_RENDER_IR_SCHEMA_MAJOR,ODM_RENDER_IR_SCHEMA_MINOR,buffer+ODM_WIRE_RECORD_HEADER_BYTES,payload,buffer,capacity,out_required,out_ir_sha256);
}

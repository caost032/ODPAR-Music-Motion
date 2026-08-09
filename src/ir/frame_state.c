#include "ir_internal.h"
#include "frame_state_internal.h"

#include "odm_fixed.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static const uint8_t *find_transition(const odm_ir_parsed *p,
                                      uint32_t from_scene,
                                      uint32_t to_scene) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_TRANSITIONS - 1u];
    uint32_t i;
    for (i = 0u; i < s->count; ++i) {
        const uint8_t *r = s->records + (uint64_t)i * s->record_bytes;
        if (odm_ir_read_u32(r + 4u) == from_scene &&
            odm_ir_read_u32(r + 8u) == to_scene) return r;
    }
    return NULL;
}

static odm_status frame_sample(const odm_ir_parsed *p, int64_t frame_index,
                               int64_t *out_sample, int64_t *out_end) {
    int64_t sample, end;
    odm_status st;
    if (frame_index < 0 || frame_index >= p->info.frame_count) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_i64_mul_checked(frame_index, p->info.samples_per_frame, &sample);
    if (st != ODM_STATUS_OK) return st;
    st = odm_i64_add_checked(sample, p->info.samples_per_frame, &end);
    if (st != ODM_STATUS_OK) return st;
    if (end > p->info.output_end_sample) end = p->info.output_end_sample;
    *out_sample = sample; *out_end = end; return ODM_STATUS_OK;
}

static odm_status active_scenes(const odm_ir_parsed *p, int64_t sample,
                                uint32_t *out_a, uint32_t *out_b,
                                uint32_t *out_transition,
                                odm_q1_31 *out_progress) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_TIMELINE - 1u];
    uint32_t i, a = 0u, b = 0u, count = 0u;
    for (i = 0u; i < s->count; ++i) {
        const uint8_t *r = s->records + (uint64_t)i * s->record_bytes;
        if (odm_ir_read_u32(r) != ODM_IR_TIMELINE_SCENE) continue;
        if (sample >= odm_ir_read_i64(r + 16u) && sample < odm_ir_read_i64(r + 24u)) {
            if (count == 0u) a = odm_ir_read_u32(r + 4u);
            else if (count == 1u) b = odm_ir_read_u32(r + 4u);
            else return ODM_STATUS_INVARIANT_BROKEN;
            ++count;
        }
    }
    if (count == 0u) return ODM_STATUS_INVALID_DATA;
    *out_a = a; *out_b = b; *out_transition = 0u; *out_progress = 0;
    if (count == 2u) {
        const uint8_t *tr = find_transition(p, a, b);
        int64_t start, end;
        odm_status st;
        if (!tr) return ODM_STATUS_INVALID_DATA;
        start = odm_ir_read_i64(tr + 16u); end = odm_ir_read_i64(tr + 24u);
        st = odm_q1_31_from_ratio(sample - start, end - start, out_progress);
        if (st != ODM_STATUS_OK) return st;
        *out_transition = odm_ir_read_u32(tr);
    }
    return ODM_STATUS_OK;
}

static int node_visible(const uint8_t *r, int64_t sample, uint32_t a, uint32_t b) {
    uint32_t scene = odm_ir_read_u32(r + 4u);
    return (scene == a || (b != 0u && scene == b)) &&
           sample >= odm_ir_read_i64(r + 40u) && sample < odm_ir_read_i64(r + 48u);
}

static uint32_t count_visible_modulators(const odm_ir_parsed *p, int64_t sample,
                                         uint32_t a, uint32_t b) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_MODULATORS - 1u];
    uint32_t i, count = 0u;
    for (i = 0u; i < s->count; ++i) {
        const uint8_t *m = s->records + (uint64_t)i * s->record_bytes;
        uint32_t node_id = odm_ir_read_u32(m + 4u);
        const odm_ir_section_view *ns = &p->sections[ODM_IR_SECTION_NODES - 1u];
        uint32_t j;
        for (j = 0u; j < ns->count; ++j) {
            const uint8_t *n = ns->records + (uint64_t)j * ns->record_bytes;
            if (odm_ir_read_u32(n) == node_id && node_visible(n, sample, a, b)) { ++count; break; }
        }
    }
    return count;
}

static odm_q1_31 unsigned_q31(uint32_t value) {
    return value > (uint32_t)INT32_MAX ? INT32_MAX : (odm_q1_31)value;
}

static odm_status music_source(const odm_music_analysis_tick *tick,
                               int synthetic_silence,
                               uint32_t source, uint32_t band,
                               odm_q1_31 *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (synthetic_silence) {
        *out = source == ODM_MOD_SILENCE ? INT32_MAX : 0;
        return ODM_STATUS_OK;
    }
    if (!tick) return ODM_STATUS_INVALID_ARGUMENT;
    switch (source) {
      case ODM_MOD_RMS_LEFT: *out=tick->rms_left_q31; break;
      case ODM_MOD_RMS_RIGHT: *out=tick->rms_right_q31; break;
      case ODM_MOD_RMS_MID: *out=tick->rms_mid_q31; break;
      case ODM_MOD_RMS_SIDE: *out=tick->rms_side_q31; break;
      case ODM_MOD_ENVELOPE_SHORT: *out=unsigned_q31(tick->envelope_short_q31); break;
      case ODM_MOD_ENVELOPE_MEDIUM: *out=unsigned_q31(tick->envelope_medium_q31); break;
      case ODM_MOD_ENVELOPE_LONG: *out=unsigned_q31(tick->envelope_long_q31); break;
      case ODM_MOD_SPECTRAL_FLUX: *out=unsigned_q31(tick->spectral_flux_q31); break;
      case ODM_MOD_STEREO_WIDTH: *out=unsigned_q31(tick->stereo_width_q31); break;
      case ODM_MOD_STEREO_BALANCE: *out=tick->stereo_balance_q31; break;
      case ODM_MOD_BAND_ENERGY:
        if (band >= ODM_MUSIC_BAND_COUNT) return ODM_STATUS_INVALID_DATA;
        *out=unsigned_q31(tick->band_energy_q31[band]); break;
      case ODM_MOD_SILENCE: *out=tick->silence != 0u ? INT32_MAX : 0; break;
      default: return ODM_STATUS_INVALID_DATA;
    }
    return ODM_STATUS_OK;
}

static odm_status lerp_q32(odm_q32_32 from, odm_q32_32 to,
                           int64_t elapsed, int64_t duration,
                           odm_q32_32 *out) {
    odm_q1_31 f31; odm_q32_32 delta, scaled, f32; odm_status st;
    st=odm_q1_31_from_ratio(elapsed,duration,&f31); if(st!=ODM_STATUS_OK)return st;
    f32=(odm_q32_32)((int64_t)f31 << 1);
    st=odm_q32_32_sub(to,from,&delta);if(st!=ODM_STATUS_OK)return st;
    st=odm_q32_32_mul(delta,f32,&scaled);if(st!=ODM_STATUS_OK)return st;
    return odm_q32_32_add(from,scaled,out);
}

static odm_status apply_automation(const odm_ir_parsed *p, uint32_t node_id,
                                   int64_t sample, uint32_t param,
                                   int64_t base, int64_t *out) {
    const odm_ir_section_view *s=&p->sections[ODM_IR_SECTION_AUTOMATION-1u];uint32_t i;
    *out=base;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;
        if(odm_ir_read_u32(r+4u)!=node_id||odm_ir_read_u32(r+8u)!=param)continue;
        if(sample>=odm_ir_read_i64(r+24u)&&sample<odm_ir_read_i64(r+32u)){
            int64_t from=odm_ir_read_i64(r+40u),to=odm_ir_read_i64(r+48u);uint32_t interp=odm_ir_read_u32(r+12u);
            if(interp==ODM_AUTOMATION_STEP){*out=from;return ODM_STATUS_OK;}
            if(param==ODM_PARAM_OPACITY){odm_q32_32 q;odm_status st=lerp_q32(from<<1,to<<1,sample-odm_ir_read_i64(r+24u),odm_ir_read_i64(r+32u)-odm_ir_read_i64(r+24u),&q);if(st!=ODM_STATUS_OK)return st;*out=q>>1;return ODM_STATUS_OK;}
            return lerp_q32(from,to,sample-odm_ir_read_i64(r+24u),odm_ir_read_i64(r+32u)-odm_ir_read_i64(r+24u),(odm_q32_32*)out);
        }
    }
    return ODM_STATUS_OK;
}

static odm_status apply_modulators(const odm_ir_parsed*p,uint32_t node_id,uint32_t param,
                                   const odm_music_analysis_tick*tick,int synthetic,
                                   int64_t base,int64_t*out){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_MODULATORS-1u];uint32_t i;odm_q32_32 value;
    if(param==ODM_PARAM_OPACITY)value=(odm_q32_32)(base<<1);else value=(odm_q32_32)base;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;odm_q1_31 src;odm_q32_32 src32,term,next;odm_status st;
        if(odm_ir_read_u32(r+4u)!=node_id||odm_ir_read_u32(r+8u)!=param)continue;
        st=music_source(tick,synthetic,odm_ir_read_u32(r+12u),odm_ir_read_u32(r+16u),&src);if(st!=ODM_STATUS_OK)return st;
        st=odm_i64_mul_checked((int64_t)src,INT64_C(2),&src32);if(st!=ODM_STATUS_OK)return st;st=odm_q32_32_mul(odm_ir_read_i64(r+24u),src32,&term);if(st!=ODM_STATUS_OK)return st;
        st=odm_q32_32_add(term,odm_ir_read_i64(r+32u),&term);if(st!=ODM_STATUS_OK)return st;st=odm_q32_32_add(value,term,&next);if(st!=ODM_STATUS_OK)return st;value=next;
    }
    if(param==ODM_PARAM_OPACITY){if(value<0)value=0;if(value>((odm_q32_32)INT32_MAX<<1))value=((odm_q32_32)INT32_MAX<<1);*out=value>>1;}else *out=value;
    return ODM_STATUS_OK;
}


static odm_status q31_mul_identity(odm_q1_31 a, odm_q1_31 b, odm_q1_31 *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (a == 0 || b == 0) { *out = 0; return ODM_STATUS_OK; }
    if (a == INT32_MAX) { *out = b; return ODM_STATUS_OK; }
    if (b == INT32_MAX) { *out = a; return ODM_STATUS_OK; }
    return odm_q1_31_mul(a, b, out);
}

static odm_status node_entry_exit_weights(const uint8_t *r, int64_t sample,
                                          odm_q1_31 *entry, odm_q1_31 *exit_weight) {
    int64_t start = odm_ir_read_i64(r + 40u);
    int64_t end = odm_ir_read_i64(r + 48u);
    uint32_t entry_kind = odm_ir_read_u32(r + 108u);
    uint32_t exit_kind = odm_ir_read_u32(r + 112u);
    uint32_t entry_duration = odm_ir_read_u32(r + 120u);
    uint32_t exit_duration = odm_ir_read_u32(r + 124u);
    odm_status st;
    *entry = INT32_MAX;
    *exit_weight = INT32_MAX;
    if (entry_kind == ODM_ENTRY_FADE_SCALE && (uint64_t)(sample - start) < (uint64_t)entry_duration) {
        st = odm_q1_31_from_ratio(sample - start, (int64_t)entry_duration, entry);
        if (st != ODM_STATUS_OK) return st;
    }
    if (exit_kind == ODM_EXIT_FADE_SCALE && (uint64_t)(end - sample) < (uint64_t)exit_duration) {
        st = odm_q1_31_from_ratio(end - sample, (int64_t)exit_duration, exit_weight);
        if (st != ODM_STATUS_OK) return st;
    }
    return ODM_STATUS_OK;
}

static int64_t clamp_i64(int64_t value, int64_t low, int64_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void clamp_parameter(uint32_t param, int64_t *value) {
    const int64_t transform = INT64_C(64) * ODM_Q32_ONE;
    const int64_t scale = INT64_C(16) * ODM_Q32_ONE;
    if (param == ODM_PARAM_X || param == ODM_PARAM_Y) {
        *value = clamp_i64(*value, -transform, transform);
    } else if (param == ODM_PARAM_SCALE_X || param == ODM_PARAM_SCALE_Y) {
        *value = clamp_i64(*value, 0, scale);
    } else if (param == ODM_PARAM_OPACITY) {
        *value = clamp_i64(*value, 0, INT32_MAX);
    }
}

odm_status odm_frame_node_compute_internal(const odm_ir_parsed*p,const uint8_t*r,int64_t sample,
                               uint32_t scene_a,uint32_t scene_b,odm_q1_31 progress,
                               const odm_music_analysis_tick*tick,int synthetic,
                               int apply_scene_weight, odm_frame_node_state*out){
    int64_t x=odm_ir_read_i64(r+56u),y=odm_ir_read_i64(r+64u),sx=odm_ir_read_i64(r+72u),sy=odm_ir_read_i64(r+80u),op=(int64_t)odm_ir_read_i32(r+88u),phase=(int64_t)(uint64_t)odm_ir_read_u32(r+92u);uint32_t id=odm_ir_read_u32(r),scene=odm_ir_read_u32(r+4u);odm_q1_31 weight=INT32_MAX,entry_weight=INT32_MAX,exit_weight=INT32_MAX,env_weight=INT32_MAX,final_op;odm_status st;
#define A(param,var) do{int64_t _v;st=apply_automation(p,id,sample,(param),(var),&_v);if(st!=ODM_STATUS_OK)return st;st=apply_modulators(p,id,(param),tick,synthetic,_v,&(var));if(st!=ODM_STATUS_OK)return st;clamp_parameter((param),&(var));}while(0)
    A(ODM_PARAM_X,x);A(ODM_PARAM_Y,y);A(ODM_PARAM_SCALE_X,sx);A(ODM_PARAM_SCALE_Y,sy);A(ODM_PARAM_OPACITY,op);
#undef A
    st=apply_automation(p,id,sample,ODM_PARAM_PHASE,phase,&phase);if(st!=ODM_STATUS_OK)return st;
    if(apply_scene_weight && scene_b!=0u){weight=scene==scene_a?(odm_q1_31)(INT32_MAX-progress):progress;}
    st=node_entry_exit_weights(r,sample,&entry_weight,&exit_weight);if(st!=ODM_STATUS_OK)return st;
    st=q31_mul_identity(entry_weight,exit_weight,&env_weight);if(st!=ODM_STATUS_OK)return st;
    st=q31_mul_identity((odm_q1_31)op,weight,&final_op);if(st!=ODM_STATUS_OK)return st;
    st=q31_mul_identity(final_op,env_weight,&final_op);if(st!=ODM_STATUS_OK)return st;
    if(out){memset(out,0,sizeof(*out));out->node_id=id;out->scene_id=scene;out->role=odm_ir_read_u32(r+8u);out->capability_id=odm_ir_read_u32(r+12u);out->resource_id=odm_ir_read_u32(r+24u);out->z_index=odm_ir_read_i32(r+32u);out->x_q32_32=x;out->y_q32_32=y;out->scale_x_q32_32=sx;out->scale_y_q32_32=sy;out->opacity_q31=final_op;out->phase=(odm_phase_u32)(uint64_t)phase;out->seed_domain=odm_ir_read_u64(r+96u);out->state_slot=odm_ir_read_u32(r+104u);out->entry_kind=odm_ir_read_u32(r+108u);out->exit_kind=odm_ir_read_u32(r+112u);out->entry_duration_samples=odm_ir_read_u32(r+120u);out->exit_duration_samples=odm_ir_read_u32(r+124u);out->entry_weight_q31=entry_weight;out->exit_weight_q31=exit_weight;out->scene_weight_q31=weight;}
    return ODM_STATUS_OK;
}

static odm_status requirements_parsed(const odm_ir_parsed*p,int64_t frame_index,
                                      const odm_music_analysis_tick*tick,
                                      uint64_t*out_nodes,uint64_t*out_transfers,
                                      int64_t*out_sample,int64_t*out_end,
                                      uint32_t*out_a,uint32_t*out_b,uint32_t*out_tr,
                                      odm_q1_31*out_progress){
    const odm_ir_section_view*ns=&p->sections[ODM_IR_SECTION_NODES-1u],*ts=&p->sections[ODM_IR_SECTION_STATE_TRANSFER-1u];uint32_t i;uint64_t nc=0u,tc=0u;odm_status st;uint64_t expected;uint32_t visible_mods;
    st=frame_sample(p,frame_index,out_sample,out_end);if(st!=ODM_STATUS_OK)return st;st=active_scenes(p,*out_sample,out_a,out_b,out_tr,out_progress);if(st!=ODM_STATUS_OK)return st;
    for(i=0u;i<ns->count;++i){const uint8_t*r=ns->records+(uint64_t)i*ns->record_bytes;if(node_visible(r,*out_sample,*out_a,*out_b))++nc;}
    for(i=0u;i<ts->count;++i){const uint8_t*r=ts->records+(uint64_t)i*ts->record_bytes;int64_t at=odm_ir_read_i64(r+24u);if(at>=*out_sample&&at<*out_end)++tc;}
    visible_mods=count_visible_modulators(p,*out_sample,*out_a,*out_b);expected=(uint64_t)*out_sample/(uint64_t)ODM_MUSIC_TICK_SAMPLES;
    if(visible_mods!=0u&&(uint64_t)*out_sample<p->info.canonical_music_frames){if(!tick||tick->tick_index!=expected||tick->center_sample!=expected*(uint64_t)ODM_MUSIC_TICK_SAMPLES)return ODM_STATUS_INVALID_ARGUMENT;}
    *out_nodes=nc;*out_transfers=tc;return ODM_STATUS_OK;
}

odm_status odm_frame_state_requirements(const uint8_t*record,uint64_t record_bytes,int64_t frame_index,const odm_music_analysis_tick*music_tick,uint64_t*out_required_nodes,uint64_t*out_required_transfers){odm_ir_parsed p;int64_t sample,end;uint32_t a,b,tr;odm_q1_31 progress;odm_status st;if(!out_required_nodes||!out_required_transfers)return ODM_STATUS_INVALID_ARGUMENT;st=odm_ir_parse_internal(record,record_bytes,&p);if(st!=ODM_STATUS_OK)return st;return requirements_parsed(&p,frame_index,music_tick,out_required_nodes,out_required_transfers,&sample,&end,&a,&b,&tr,&progress);}

odm_status odm_frame_state_resolve(const uint8_t*record,uint64_t record_bytes,int64_t frame_index,const odm_music_analysis_tick*music_tick,odm_frame_node_state*nodes,uint64_t node_capacity,odm_frame_transfer_event*transfers,uint64_t transfer_capacity,uint64_t*out_required_nodes,uint64_t*out_required_transfers,odm_frame_state*out_state){
    odm_ir_parsed p;int64_t sample,end;uint32_t a,b,tr,i;odm_q1_31 progress;uint64_t nr,trr,ni=0u,ti=0u,expected;int synthetic;odm_status st;const odm_ir_section_view*ns,*ts;
    if(!out_required_nodes||!out_required_transfers||!out_state)return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_ir_parse_internal(record,record_bytes,&p);if(st!=ODM_STATUS_OK)return st;
    st=requirements_parsed(&p,frame_index,music_tick,&nr,&trr,&sample,&end,&a,&b,&tr,&progress);if(st!=ODM_STATUS_OK)return st;
    *out_required_nodes=nr;*out_required_transfers=trr;
    if((nr!=0u&&!nodes)||node_capacity<nr||(trr!=0u&&!transfers)||transfer_capacity<trr)return ODM_STATUS_BUFFER_TOO_SMALL;
    ns=&p.sections[ODM_IR_SECTION_NODES-1u];ts=&p.sections[ODM_IR_SECTION_STATE_TRANSFER-1u];synthetic=(uint64_t)sample>=p.info.canonical_music_frames;expected=(uint64_t)sample/(uint64_t)ODM_MUSIC_TICK_SAMPLES;
    /* Preflight all arithmetic before publishing any output. */
    for(i=0u;i<ns->count;++i){const uint8_t*r=ns->records+(uint64_t)i*ns->record_bytes;if(node_visible(r,sample,a,b)){st=odm_frame_node_compute_internal(&p,r,sample,a,b,progress,music_tick,synthetic,1,NULL);if(st!=ODM_STATUS_OK)return st;}}
    for(i=0u;i<ns->count;++i){const uint8_t*r=ns->records+(uint64_t)i*ns->record_bytes;if(node_visible(r,sample,a,b)){st=odm_frame_node_compute_internal(&p,r,sample,a,b,progress,music_tick,synthetic,1,&nodes[ni]);if(st!=ODM_STATUS_OK)return ODM_STATUS_INVARIANT_BROKEN;++ni;}}
    for(i=0u;i<ts->count;++i){const uint8_t*r=ts->records+(uint64_t)i*ts->record_bytes;int64_t at=odm_ir_read_i64(r+24u);if(at>=sample&&at<end){odm_frame_transfer_event e;memset(&e,0,sizeof(e));e.transfer_id=odm_ir_read_u32(r);e.from_scene_id=odm_ir_read_u32(r+4u);e.to_scene_id=odm_ir_read_u32(r+8u);e.state_slot=odm_ir_read_u32(r+12u);e.mode=odm_ir_read_u32(r+16u);transfers[ti++]=e;}}
    memset(out_state,0,sizeof(*out_state));out_state->schema_version=1u;out_state->fps=p.info.fps;out_state->frame_index=frame_index;out_state->sample=sample;out_state->scene_a_id=a;out_state->scene_b_id=b;out_state->transition_id=tr;out_state->transition_progress_q31=progress;out_state->expected_music_tick=expected;out_state->node_count=(uint32_t)nr;out_state->transfer_count=(uint32_t)trr;return ODM_STATUS_OK;
}

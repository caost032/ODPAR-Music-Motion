#include "ir_internal.h"

#include "odm_time.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

uint32_t odm_ir_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
int32_t odm_ir_read_i32(const uint8_t *p) { return (int32_t)odm_ir_read_u32(p); }
uint64_t odm_ir_read_u64(const uint8_t *p) {
    uint64_t v = 0u; uint32_t i;
    for (i = 0u; i < 8u; ++i) v |= ((uint64_t)p[i]) << (8u * i);
    return v;
}
int64_t odm_ir_read_i64(const uint8_t *p) { return (int64_t)odm_ir_read_u64(p); }

static int bytes_zero(const uint8_t *p, uint64_t n) {
    uint64_t i; for (i = 0u; i < n; ++i) if (p[i] != 0u) return 0; return 1;
}
static int digest_zero(const odm_sha256_digest *d) {
    return bytes_zero(d->bytes, ODM_SHA256_DIGEST_BYTES);
}
static const odm_visual_capability_info *find_cap(uint32_t id) {
    const odm_visual_capability_info *caps; uint32_t i, n;
    caps = odm_visual_capabilities(&n);
    for (i = 0u; i < n; ++i) if (caps[i].capability_id == id) return &caps[i];
    return NULL;
}
static int resource_exists(const odm_ir_parsed *p, uint32_t id, uint32_t kind) {
    const odm_ir_section_view *s=&p->sections[ODM_IR_SECTION_RESOURCES-1u]; uint32_t i;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;
        if(odm_ir_read_u32(r)==id)return odm_ir_read_u32(r+4u)==kind;}
    return 0;
}
static const uint8_t *timeline_find(const odm_ir_parsed *p,uint32_t kind,uint32_t id){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_TIMELINE-1u];uint32_t i;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;
        if(odm_ir_read_u32(r)==kind&&odm_ir_read_u32(r+4u)==id)return r;}
    return NULL;
}
static const uint8_t *node_find(const odm_ir_parsed *p,uint32_t id){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_NODES-1u];uint32_t i;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;
        if(odm_ir_read_u32(r)==id)return r;}
    return NULL;
}


static int cap_declared(const odm_ir_parsed *p, uint32_t id) {
    const odm_ir_section_view *s=&p->sections[ODM_IR_SECTION_CAPABILITIES-1u]; uint32_t i;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;if(odm_ir_read_u32(r)==id)return 1;}
    return 0;
}
static int cap_used(const odm_ir_parsed *p, uint32_t id) {
    const odm_ir_section_view *n=&p->sections[ODM_IR_SECTION_NODES-1u]; uint32_t i;
    for(i=0u;i<n->count;++i){const uint8_t*r=n->records+(uint64_t)i*n->record_bytes;if(odm_ir_read_u32(r+12u)==id)return 1;}
    if(id==ODM_CAP_AUTOMATION&&p->sections[ODM_IR_SECTION_AUTOMATION-1u].count!=0u)return 1;
    if(id==ODM_CAP_MUSIC_MODULATOR&&p->sections[ODM_IR_SECTION_MODULATORS-1u].count!=0u)return 1;
    if(id==ODM_CAP_CROSSFADE&&p->sections[ODM_IR_SECTION_TRANSITIONS-1u].count!=0u)return 1;
    if(id==ODM_CAP_STATE_TRANSFER&&p->sections[ODM_IR_SECTION_STATE_TRANSFER-1u].count!=0u)return 1;
    if(id==ODM_CAP_TIMELINE_CUE){const odm_ir_section_view*t=&p->sections[ODM_IR_SECTION_TIMELINE-1u];for(i=0u;i<t->count;++i){const uint8_t*r=t->records+(uint64_t)i*t->record_bytes;if(odm_ir_read_u32(r)==ODM_IR_TIMELINE_CUE)return 1;}}
    return 0;
}
static odm_status validate_cap_usage(const odm_ir_parsed *p) {
    const odm_ir_section_view *c=&p->sections[ODM_IR_SECTION_CAPABILITIES-1u];
    const odm_ir_section_view *n=&p->sections[ODM_IR_SECTION_NODES-1u]; uint32_t i;
    for(i=0u;i<c->count;++i){const uint8_t*r=c->records+(uint64_t)i*c->record_bytes;if(!cap_used(p,odm_ir_read_u32(r)))return ODM_STATUS_INVALID_DATA;}
    for(i=0u;i<n->count;++i){const uint8_t*r=n->records+(uint64_t)i*n->record_bytes;if(!cap_declared(p,odm_ir_read_u32(r+12u)))return ODM_STATUS_INVALID_DATA;}
    if(p->sections[ODM_IR_SECTION_AUTOMATION-1u].count!=0u&&!cap_declared(p,ODM_CAP_AUTOMATION))return ODM_STATUS_INVALID_DATA;
    if(p->sections[ODM_IR_SECTION_MODULATORS-1u].count!=0u&&!cap_declared(p,ODM_CAP_MUSIC_MODULATOR))return ODM_STATUS_INVALID_DATA;
    if(p->sections[ODM_IR_SECTION_TRANSITIONS-1u].count!=0u&&!cap_declared(p,ODM_CAP_CROSSFADE))return ODM_STATUS_INVALID_DATA;
    if(p->sections[ODM_IR_SECTION_STATE_TRANSFER-1u].count!=0u&&!cap_declared(p,ODM_CAP_STATE_TRANSFER))return ODM_STATUS_INVALID_DATA;
    if(cap_used(p,ODM_CAP_TIMELINE_CUE)&&!cap_declared(p,ODM_CAP_TIMELINE_CUE))return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}
static int q32_abs_within(int64_t v, int64_t limit) {
    if(v>=0)return v<=limit;
    if(v==INT64_MIN)return 0;
    return -v<=limit;
}
static int ir_value_valid(uint32_t param,int64_t v) {
    const int64_t transform=INT64_C(64)*ODM_Q32_ONE;
    const int64_t scale=INT64_C(16)*ODM_Q32_ONE;
    switch(param){
      case ODM_PARAM_X: case ODM_PARAM_Y:return q32_abs_within(v,transform);
      case ODM_PARAM_SCALE_X: case ODM_PARAM_SCALE_Y:return v>=0&&v<=scale;
      case ODM_PARAM_OPACITY:return v>=0&&v<=INT32_MAX;
      case ODM_PARAM_PHASE:return v>=0&&v<=(int64_t)UINT32_MAX;
      default:return 0;
    }
}

static odm_status hash_caps(const odm_ir_section_view *s,odm_sha256_digest*out){
    static const uint8_t domain[]={'O','D','P','A','R','_','C','A','P','S','_','V','1',0};
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;uint8_t b[4];uint32_t i,j;odm_status st;
    st=odm_sha256_init(&c);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,domain,sizeof(domain));if(st!=ODM_STATUS_OK)return st;
    b[0]=(uint8_t)s->count;b[1]=(uint8_t)(s->count>>8);b[2]=(uint8_t)(s->count>>16);b[3]=(uint8_t)(s->count>>24);
    st=odm_sha256_update(&c,b,4u);if(st!=ODM_STATUS_OK)return st;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;
        for(j=0u;j<6u;++j){st=odm_sha256_update(&c,r+(uint64_t)j*4u,4u);if(st!=ODM_STATUS_OK)return st;}}
    return odm_sha256_final(&c,out);
}

static odm_status validate_caps(odm_ir_parsed *p){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_CAPABILITIES-1u];uint32_t i,prev=0u;odm_sha256_digest d;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;uint32_t id=odm_ir_read_u32(r);const odm_visual_capability_info*c=find_cap(id);
        if(id==0u||id<=prev||!c)return ODM_STATUS_UNSUPPORTED;
        if(odm_ir_read_u32(r+4u)!=c->version_major||odm_ir_read_u32(r+8u)!=c->version_minor||
           odm_ir_read_u32(r+12u)!=c->state_model||odm_ir_read_u32(r+16u)!=c->allowed_roles_mask||
           odm_ir_read_u32(r+20u)!=c->resource_kind||!bytes_zero(r+24u,8u))return ODM_STATUS_UNSUPPORTED;
        prev=id;
    }
    if(hash_caps(s,&d)!=ODM_STATUS_OK)return ODM_STATUS_INTERNAL_ERROR;
    return odm_sha256_equal(&d,&p->info.capability_sha256)?ODM_STATUS_OK:ODM_STATUS_INTEGRITY_ERROR;
}

static odm_status validate_resources(const odm_ir_parsed*p){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_RESOURCES-1u];uint32_t i,prev=0u;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;uint32_t id=odm_ir_read_u32(r),kind=odm_ir_read_u32(r+4u);odm_sha256_digest d;
        memcpy(d.bytes,r+16u,32u);
        if(id==0u||id<=prev||(kind!=ODM_SCORE_RESOURCE_IMAGE&&kind!=ODM_SCORE_RESOURCE_VIDEO)||
           odm_ir_read_u32(r+8u)!=0u||odm_ir_read_u32(r+12u)!=0u||digest_zero(&d)||!bytes_zero(r+48u,16u))return ODM_STATUS_INVALID_DATA;
        prev=id;
    }return ODM_STATUS_OK;
}

static odm_status validate_timeline(const odm_ir_parsed *p,
                                    uint32_t *out_acts,
                                    uint32_t *out_scenes,
                                    uint32_t *out_cues,
                                    uint32_t *out_overlaps) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_TIMELINE - 1u];
    uint32_t i, phase = 0u, acts = 0u, scenes = 0u, cues = 0u;
    uint32_t prev_id = 0u, overlaps = 0u, prev_act = 0u;
    int64_t act_end = 0, prev_scene_end = -1, prev_prev_scene_end = -1;
    for (i = 0u; i < s->count; ++i) {
        const uint8_t *r = s->records + (uint64_t)i * s->record_bytes;
        uint32_t kind = odm_ir_read_u32(r), id = odm_ir_read_u32(r + 4u);
        uint32_t parent = odm_ir_read_u32(r + 8u);
        int64_t start = odm_ir_read_i64(r + 16u), end = odm_ir_read_i64(r + 24u);
        if (id == 0u || odm_ir_read_u32(r + 12u) != 0u || !bytes_zero(r + 40u, 24u))
            return ODM_STATUS_INVALID_DATA;
        if (kind == ODM_IR_TIMELINE_ACT) {
            if (phase != 0u || id <= prev_id || parent != 0u || start != act_end ||
                end <= start || odm_ir_read_u32(r + 32u) != 0u ||
                odm_ir_read_u32(r + 36u) != 0u) return ODM_STATUS_INVALID_DATA;
            act_end = end; ++acts; prev_id = id;
        } else if (kind == ODM_IR_TIMELINE_SCENE) {
            const uint8_t *a;
            uint32_t env = odm_ir_read_u32(r + 32u), pri = odm_ir_read_u32(r + 36u);
            if (phase == 2u) return ODM_STATUS_INVALID_DATA;
            if (phase == 0u) { phase = 1u; prev_id = 0u; }
            if (id <= prev_id || parent == 0u || end <= start || env == 0u || pri == 0u)
                return ODM_STATUS_INVALID_DATA;
            a = timeline_find(p, ODM_IR_TIMELINE_ACT, parent);
            if (!a || start < odm_ir_read_i64(a + 16u) || end > odm_ir_read_i64(a + 24u))
                return ODM_STATUS_INVALID_DATA;
            if (prev_act == 0u || parent != prev_act) {
                if (prev_act != 0u) {
                    const uint8_t *pa = timeline_find(p, ODM_IR_TIMELINE_ACT, prev_act);
                    if (!pa || prev_scene_end != odm_ir_read_i64(pa + 24u) || parent <= prev_act)
                        return ODM_STATUS_INVALID_DATA;
                }
                if (start != odm_ir_read_i64(a + 16u)) return ODM_STATUS_INVALID_DATA;
                prev_prev_scene_end = -1;
                prev_scene_end = end;
                prev_act = parent;
            } else {
                if (start > prev_scene_end) return ODM_STATUS_INVALID_DATA;
                if (start < prev_scene_end) ++overlaps;
                if (prev_prev_scene_end >= 0 && start < prev_prev_scene_end)
                    return ODM_STATUS_INVALID_DATA; /* triple-scene mix */
                prev_prev_scene_end = prev_scene_end;
                if (end > prev_scene_end) prev_scene_end = end;
            }
            prev_id = id; ++scenes;
        } else if (kind == ODM_IR_TIMELINE_CUE) {
            const uint8_t *sc;
            if (phase < 2u) {
                const uint8_t *pa;
                phase = 2u; prev_id = 0u;
                if (prev_act == 0u) return ODM_STATUS_INVALID_DATA;
                pa = timeline_find(p, ODM_IR_TIMELINE_ACT, prev_act);
                if (!pa || prev_scene_end != odm_ir_read_i64(pa + 24u))
                    return ODM_STATUS_INVALID_DATA;
            }
            if (id <= prev_id || parent == 0u || start != end ||
                odm_ir_read_u32(r + 32u) < ODM_SCORE_CUE_GENERIC ||
                odm_ir_read_u32(r + 32u) > ODM_SCORE_CUE_SECTION_AUTHORED ||
                odm_ir_read_u32(r + 36u) != 0u) return ODM_STATUS_INVALID_DATA;
            sc = timeline_find(p, ODM_IR_TIMELINE_SCENE, parent);
            if (!sc || start < odm_ir_read_i64(sc + 16u) || start >= odm_ir_read_i64(sc + 24u))
                return ODM_STATUS_INVALID_DATA;
            prev_id = id; ++cues;
        } else return ODM_STATUS_UNSUPPORTED;
    }
    if (acts == 0u || scenes == 0u || act_end != p->info.project_end_sample)
        return ODM_STATUS_INVALID_DATA;
    if (phase == 1u) {
        const uint8_t *pa = timeline_find(p, ODM_IR_TIMELINE_ACT, prev_act);
        if (!pa || prev_scene_end != odm_ir_read_i64(pa + 24u)) return ODM_STATUS_INVALID_DATA;
    }
    *out_acts = acts; *out_scenes = scenes; *out_cues = cues; *out_overlaps = overlaps;
    return ODM_STATUS_OK;
}
static odm_status validate_nodes(const odm_ir_parsed*p,uint32_t scene_count){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_NODES-1u];uint32_t i,prev=0u,prev_scene=0u,seen_scenes=0u,envc=0u,pric=0u;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;uint32_t id=odm_ir_read_u32(r),scene=odm_ir_read_u32(r+4u),role=odm_ir_read_u32(r+8u),capid=odm_ir_read_u32(r+12u),res=odm_ir_read_u32(r+24u),state=odm_ir_read_u32(r+28u);const odm_visual_capability_info*c=find_cap(capid);const uint8_t*sc=timeline_find(p,ODM_IR_TIMELINE_SCENE,scene);int64_t start=odm_ir_read_i64(r+40u),end=odm_ir_read_i64(r+48u);int32_t op=odm_ir_read_i32(r+88u);
        {uint32_t slot=odm_ir_read_u32(r+104u),entry=odm_ir_read_u32(r+108u),exit_kind=odm_ir_read_u32(r+112u),entry_d=odm_ir_read_u32(r+120u),exit_d=odm_ir_read_u32(r+124u);uint64_t duration=(uint64_t)(end-start);
        if(id==0u||id<=prev||scene<prev_scene||role<ODM_SCORE_NODE_ENVIRONMENT||role>ODM_SCORE_NODE_PARTICLE||!c||!sc||odm_ir_read_u32(r+36u)!=0u||odm_ir_read_u32(r+116u)!=0u)return ODM_STATUS_INVALID_DATA;
        if((state==ODM_STATELESS&&slot!=0u)||(state!=ODM_STATELESS&&slot==0u)||(entry!=ODM_ENTRY_NONE&&entry!=ODM_ENTRY_FADE_SCALE)||(exit_kind!=ODM_EXIT_NONE&&exit_kind!=ODM_EXIT_FADE_SCALE)||(entry==ODM_ENTRY_NONE&&entry_d!=0u)||(exit_kind==ODM_EXIT_NONE&&exit_d!=0u)||(entry!=ODM_ENTRY_NONE&&entry_d==0u)||(exit_kind!=ODM_EXIT_NONE&&exit_d==0u)||(uint64_t)entry_d>duration||(uint64_t)exit_d>duration)return ODM_STATUS_INVALID_DATA;
        }
        if(odm_ir_read_u32(r+16u)!=c->version_major||odm_ir_read_u32(r+20u)!=c->version_minor||state!=c->state_model||(c->allowed_roles_mask&(UINT32_C(1)<<role))==0u)return ODM_STATUS_UNSUPPORTED;
        if(start<odm_ir_read_i64(sc+16u)||end>odm_ir_read_i64(sc+24u)||end<=start||!ir_value_valid(ODM_PARAM_X,odm_ir_read_i64(r+56u))||!ir_value_valid(ODM_PARAM_Y,odm_ir_read_i64(r+64u))||!ir_value_valid(ODM_PARAM_SCALE_X,odm_ir_read_i64(r+72u))||!ir_value_valid(ODM_PARAM_SCALE_Y,odm_ir_read_i64(r+80u))||!ir_value_valid(ODM_PARAM_OPACITY,(int64_t)op))return ODM_STATUS_INVALID_DATA;
        if(c->resource_kind!=0u){if(!resource_exists(p,res,c->resource_kind))return ODM_STATUS_INVALID_DATA;}else if(res!=0u)return ODM_STATUS_INVALID_DATA;
        if(scene!=prev_scene){if(prev_scene!=0u&&(envc!=1u||pric!=1u))return ODM_STATUS_INVALID_DATA;++seen_scenes;envc=0u;pric=0u;}
        if(role==ODM_SCORE_NODE_ENVIRONMENT){++envc;if(id!=odm_ir_read_u32(sc+32u)||capid!=ODM_CAP_BLACK_VOID||start!=odm_ir_read_i64(sc+16u)||end!=odm_ir_read_i64(sc+24u))return ODM_STATUS_INVALID_DATA;}
        if(role==ODM_SCORE_NODE_PRIMARY){++pric;if(id!=odm_ir_read_u32(sc+36u)||(capid!=ODM_CAP_PRIMARY_EMPTY&&capid!=ODM_CAP_PRIMARY_IMAGE&&capid!=ODM_CAP_PRIMARY_VIDEO)||start!=odm_ir_read_i64(sc+16u)||end!=odm_ir_read_i64(sc+24u))return ODM_STATUS_INVALID_DATA;}
        prev=id;prev_scene=scene;
    }
    if(s->count==0u||envc!=1u||pric!=1u||seen_scenes!=scene_count)return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static int valid_param(uint32_t x){return x>=ODM_PARAM_X&&x<=ODM_PARAM_PHASE;}
static int valid_mod(uint32_t x){return x>=ODM_MOD_RMS_LEFT&&x<=ODM_MOD_SILENCE;}
static odm_status validate_modulators(const odm_ir_parsed*p){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_MODULATORS-1u];uint32_t i,prev=0u,pnode=0u,pparam=0u;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;uint32_t id=odm_ir_read_u32(r),node=odm_ir_read_u32(r+4u),param=odm_ir_read_u32(r+8u),src=odm_ir_read_u32(r+12u),band=odm_ir_read_u32(r+16u);
      if(id==0u||id<=prev||node<pnode||(node==pnode&&param<pparam)||!node_find(p,node)||!valid_param(param)||param==ODM_PARAM_PHASE||!valid_mod(src)||odm_ir_read_u32(r+20u)!=0u||!bytes_zero(r+40u,24u))return ODM_STATUS_INVALID_DATA;
      if((src==ODM_MOD_BAND_ENERGY&&band>=ODM_MUSIC_BAND_COUNT)||(src!=ODM_MOD_BAND_ENERGY&&band!=0u)||!q32_abs_within(odm_ir_read_i64(r+24u),INT64_C(64)*ODM_Q32_ONE)||!q32_abs_within(odm_ir_read_i64(r+32u),INT64_C(64)*ODM_Q32_ONE))return ODM_STATUS_INVALID_DATA;
      prev=id;pnode=node;pparam=param;
    }return ODM_STATUS_OK;
}
static odm_status validate_automation(const odm_ir_parsed*p){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_AUTOMATION-1u];uint32_t i,prev=0u,pnode=0u,pparam=0u;int64_t prev_end=-1;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;uint32_t id=odm_ir_read_u32(r),node=odm_ir_read_u32(r+4u),param=odm_ir_read_u32(r+8u),interp=odm_ir_read_u32(r+12u);const uint8_t*n=node_find(p,node);int64_t start=odm_ir_read_i64(r+24u),end=odm_ir_read_i64(r+32u);int same=(node==pnode&&param==pparam);
      if(id==0u||id<=prev||node<pnode||(node==pnode&&param<pparam)||!n||!valid_param(param)||(interp!=ODM_AUTOMATION_STEP&&interp!=ODM_AUTOMATION_LINEAR)||(param==ODM_PARAM_PHASE&&interp!=ODM_AUTOMATION_STEP)||odm_ir_read_u32(r+16u)!=0u||odm_ir_read_u32(r+20u)!=0u||!bytes_zero(r+56u,8u)||start<odm_ir_read_i64(n+40u)||end>odm_ir_read_i64(n+48u)||end<=start||(same&&start<prev_end))return ODM_STATUS_INVALID_DATA;
      if(!ir_value_valid(param,odm_ir_read_i64(r+40u))||!ir_value_valid(param,odm_ir_read_i64(r+48u)))return ODM_STATUS_INVALID_DATA;
      prev=id;pnode=node;pparam=param;prev_end=end;
    }return ODM_STATUS_OK;
}
static odm_status validate_transitions(const odm_ir_parsed *p, uint32_t overlaps) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_TRANSITIONS - 1u];
    const odm_ir_section_view *timeline = &p->sections[ODM_IR_SECTION_TIMELINE - 1u];
    const uint8_t *prev_scene = NULL;
    uint32_t i, t = 0u, prev_id = 0u;
    if (s->count != overlaps) return ODM_STATUS_INVALID_DATA;
    for (i = 0u; i < timeline->count; ++i) {
        const uint8_t *scene = timeline->records + (uint64_t)i * timeline->record_bytes;
        if (odm_ir_read_u32(scene) != ODM_IR_TIMELINE_SCENE) continue;
        if (prev_scene && odm_ir_read_u32(prev_scene + 8u) == odm_ir_read_u32(scene + 8u) &&
            odm_ir_read_i64(scene + 16u) < odm_ir_read_i64(prev_scene + 24u)) {
            const uint8_t *r;
            uint32_t id;
            if (t >= s->count) return ODM_STATUS_INVALID_DATA;
            r = s->records + (uint64_t)t * s->record_bytes;
            id = odm_ir_read_u32(r);
            if (id == 0u || id <= prev_id ||
                odm_ir_read_u32(r + 4u) != odm_ir_read_u32(prev_scene + 4u) ||
                odm_ir_read_u32(r + 8u) != odm_ir_read_u32(scene + 4u) ||
                odm_ir_read_u32(r + 12u) != ODM_TRANSITION_CROSSFADE ||
                odm_ir_read_i64(r + 16u) != odm_ir_read_i64(scene + 16u) ||
                odm_ir_read_i64(r + 24u) != odm_ir_read_i64(prev_scene + 24u) ||
                odm_ir_read_i64(r + 24u) <= odm_ir_read_i64(r + 16u) ||
                odm_ir_read_u32(r + 32u) != 0u || !bytes_zero(r + 36u, 28u))
                return ODM_STATUS_INVALID_DATA;
            prev_id = id; ++t;
        }
        prev_scene = scene;
    }
    return t == s->count ? ODM_STATUS_OK : ODM_STATUS_INVALID_DATA;
}
static int ir_scene_has_state_slot(const odm_ir_parsed *p,uint32_t scene,uint32_t slot){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_NODES-1u];uint32_t i;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;if(odm_ir_read_u32(r+4u)==scene&&odm_ir_read_u32(r+104u)==slot&&odm_ir_read_u32(r+28u)!=ODM_STATELESS)return 1;}
    return 0;
}
static odm_status validate_transfers(const odm_ir_parsed*p){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_STATE_TRANSFER-1u];uint32_t i,prev=0u,pto=0u,pslot=0u;int64_t ps=-1;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;uint32_t id=odm_ir_read_u32(r),from=odm_ir_read_u32(r+4u),to=odm_ir_read_u32(r+8u),slot=odm_ir_read_u32(r+12u),mode=odm_ir_read_u32(r+16u);const uint8_t*b=timeline_find(p,ODM_IR_TIMELINE_SCENE,to);int64_t at=odm_ir_read_i64(r+24u);
      {const uint8_t*a=timeline_find(p,ODM_IR_TIMELINE_SCENE,from);
      if(id==0u||id<=prev||!a||!b||from==to||slot==0u||(mode!=ODM_TRANSFER_COPY&&mode!=ODM_TRANSFER_RESET)||odm_ir_read_u32(r+20u)!=0u||at!=odm_ir_read_i64(b+16u)||at<ps||!bytes_zero(r+32u,32u)||odm_ir_read_i64(a+16u)>=odm_ir_read_i64(b+16u)||!ir_scene_has_state_slot(p,to,slot)||(mode==ODM_TRANSFER_COPY&&!ir_scene_has_state_slot(p,from,slot)))return ODM_STATUS_INVALID_DATA;
      if(at==ps&&(to<pto||(to==pto&&slot<=pslot)))return ODM_STATUS_INVALID_DATA;
      }
      prev=id;ps=at;pto=to;pslot=slot;
    }return ODM_STATUS_OK;
}
static odm_status validate_output(const odm_ir_parsed*p){const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_OUTPUT-1u];const uint8_t*r;if(s->count!=1u)return ODM_STATUS_INVALID_DATA;r=s->records;
    if(odm_ir_read_u32(r)!=p->info.width||odm_ir_read_u32(r+4u)!=p->info.height||odm_ir_read_i32(r+8u)!=p->info.fps||odm_ir_read_u32(r+12u)!=ODM_MUSIC_SAMPLE_RATE||odm_ir_read_i64(r+16u)!=p->info.samples_per_frame||odm_ir_read_i64(r+24u)!=p->info.project_end_sample||odm_ir_read_i64(r+32u)!=p->info.frame_count||odm_ir_read_i64(r+40u)!=p->info.output_end_sample||!bytes_zero(r+48u,16u))return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;}

odm_status odm_ir_parse_internal(const uint8_t *record,uint64_t record_bytes,odm_ir_parsed*out){
    static const uint8_t magic[8]={'O','D','R','I','R','V','0',0};
    static const uint32_t ids[ODM_RENDER_IR_SECTION_COUNT]={ODM_IR_SECTION_CAPABILITIES,ODM_IR_SECTION_RESOURCES,ODM_IR_SECTION_TIMELINE,ODM_IR_SECTION_NODES,ODM_IR_SECTION_MODULATORS,ODM_IR_SECTION_AUTOMATION,ODM_IR_SECTION_TRANSITIONS,ODM_IR_SECTION_STATE_TRANSFER,ODM_IR_SECTION_OUTPUT};
    static const uint32_t sizes[ODM_RENDER_IR_SECTION_COUNT]={ODM_IR_CAPABILITY_RECORD_BYTES,ODM_IR_RESOURCE_RECORD_BYTES,ODM_IR_TIMELINE_RECORD_BYTES,ODM_IR_NODE_RECORD_BYTES,ODM_IR_MODULATOR_RECORD_BYTES,ODM_IR_AUTOMATION_RECORD_BYTES,ODM_IR_TRANSITION_RECORD_BYTES,ODM_IR_TRANSFER_RECORD_BYTES,ODM_IR_OUTPUT_RECORD_BYTES};
    odm_wire_record_info wi;odm_sha256_digest record_sha;uint64_t off,pos,payload_end;const uint8_t*h;uint32_t i,acts,scenes,cues,overlaps;odm_timeline tl;odm_status st;
    if(!record||!out)return ODM_STATUS_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    st=odm_wire_record_read(record,record_bytes,&wi,&off,&record_sha);if(st!=ODM_STATUS_OK)return st;
    if(wi.kind!=ODM_RENDER_IR_KIND)return ODM_STATUS_INVALID_DATA;
    if(wi.schema_version_major!=ODM_RENDER_IR_SCHEMA_MAJOR||wi.schema_version_minor!=ODM_RENDER_IR_SCHEMA_MINOR)return ODM_STATUS_VERSION_MISMATCH;
    if(wi.payload_bytes<ODM_RENDER_IR_HEADER_BYTES)return ODM_STATUS_INVALID_DATA;
    h=record+off;
    if(memcmp(h,magic,8u)!=0)return ODM_STATUS_INVALID_DATA;
    out->info.schema_version_major=odm_ir_read_u32(h+8u);out->info.schema_version_minor=odm_ir_read_u32(h+12u);if(out->info.schema_version_major!=ODM_RENDER_IR_SCHEMA_MAJOR||out->info.schema_version_minor!=ODM_RENDER_IR_SCHEMA_MINOR)return ODM_STATUS_VERSION_MISMATCH;
    if(odm_ir_read_u32(h+16u)!=0u)return ODM_STATUS_UNSUPPORTED;
    out->info.fps=odm_ir_read_i32(h+20u);out->info.width=odm_ir_read_u32(h+24u);out->info.height=odm_ir_read_u32(h+28u);
    if(odm_ir_read_u32(h+32u)!=ODM_MUSIC_SAMPLE_RATE||odm_ir_read_u32(h+36u)!=ODM_RENDER_IR_SECTION_COUNT)return ODM_STATUS_VERSION_MISMATCH;
    out->info.section_count=ODM_RENDER_IR_SECTION_COUNT;out->info.samples_per_frame=odm_ir_read_i64(h+40u);out->info.project_end_sample=odm_ir_read_i64(h+48u);out->info.frame_count=odm_ir_read_i64(h+56u);out->info.output_end_sample=odm_ir_read_i64(h+64u);out->info.canonical_music_frames=odm_ir_read_u64(h+72u);out->info.project_seed=odm_ir_read_u64(h+80u);
    memcpy(out->info.score_sha256.bytes,h+88u,32u);memcpy(out->info.capability_sha256.bytes,h+120u,32u);memcpy(out->info.music_map_sha256.bytes,h+152u,32u);memcpy(out->info.music_policy_sha256.bytes,h+184u,32u);out->info.record_sha256=record_sha;
    if(!bytes_zero(h+216u,40u)||digest_zero(&out->info.score_sha256)||digest_zero(&out->info.capability_sha256)||digest_zero(&out->info.music_map_sha256)||digest_zero(&out->info.music_policy_sha256))return ODM_STATUS_INVALID_DATA;
    if(out->info.canonical_music_frames==0u || out->info.canonical_music_frames>(uint64_t)out->info.project_end_sample)return ODM_STATUS_INVALID_DATA;
    {odm_sha256_digest current_policy;st=odm_music_policy_current_sha256(&current_policy);if(st!=ODM_STATUS_OK)return st;if(!odm_sha256_equal(&current_policy,&out->info.music_policy_sha256))return ODM_STATUS_VERSION_MISMATCH;}
    st=odm_time_resolve_timeline(out->info.project_end_sample,out->info.fps,&tl);if(st!=ODM_STATUS_OK)return st;if(tl.samples_per_frame!=out->info.samples_per_frame||tl.frame_count!=out->info.frame_count||tl.output_end_sample!=out->info.output_end_sample)return ODM_STATUS_INVALID_DATA;
    if(out->info.width<2u||out->info.width>16384u||out->info.height<2u||out->info.height>16384u||(out->info.width&1u)!=0u||(out->info.height&1u)!=0u)return ODM_STATUS_INVALID_DATA;
    pos=off+ODM_RENDER_IR_HEADER_BYTES;payload_end=off+wi.payload_bytes;
    for(i=0u;i<ODM_RENDER_IR_SECTION_COUNT;++i){uint32_t id,ver,count,rb;uint64_t bytes;if(pos>payload_end||payload_end-pos<ODM_RENDER_IR_SECTION_HEADER_BYTES)return ODM_STATUS_INVALID_DATA;id=odm_ir_read_u32(record+pos);ver=odm_ir_read_u32(record+pos+4u);count=odm_ir_read_u32(record+pos+8u);rb=odm_ir_read_u32(record+pos+12u);if(id!=ids[i]||ver!=1u||rb!=sizes[i])return ODM_STATUS_VERSION_MISMATCH;pos+=ODM_RENDER_IR_SECTION_HEADER_BYTES;bytes=(uint64_t)count*(uint64_t)rb;if(count!=0u&&bytes/(uint64_t)count!=(uint64_t)rb)return ODM_STATUS_OVERFLOW;if(bytes>payload_end-pos)return ODM_STATUS_INVALID_DATA;out->sections[i].records=record+pos;out->sections[i].count=count;out->sections[i].record_bytes=rb;pos+=bytes;}
    if(pos!=payload_end)return ODM_STATUS_INVALID_DATA;
    st=validate_caps(out);if(st!=ODM_STATUS_OK)return st;st=validate_cap_usage(out);if(st!=ODM_STATUS_OK)return st;st=validate_resources(out);if(st!=ODM_STATUS_OK)return st;st=validate_timeline(out,&acts,&scenes,&cues,&overlaps);if(st!=ODM_STATUS_OK)return st;(void)acts;(void)cues;
    st=validate_nodes(out,scenes);if(st!=ODM_STATUS_OK)return st;st=validate_modulators(out);if(st!=ODM_STATUS_OK)return st;st=validate_automation(out);if(st!=ODM_STATUS_OK)return st;st=validate_transitions(out,overlaps);if(st!=ODM_STATUS_OK)return st;st=validate_transfers(out);if(st!=ODM_STATUS_OK)return st;return validate_output(out);
}

odm_status odm_render_ir_validate(const uint8_t *record,uint64_t record_bytes,const odm_sha256_digest*expected_score,const odm_sha256_digest*expected_policy,odm_render_ir_info*out_info){odm_ir_parsed p;odm_status st;if(!out_info)return ODM_STATUS_INVALID_ARGUMENT;st=odm_ir_parse_internal(record,record_bytes,&p);if(st!=ODM_STATUS_OK)return st;if(expected_score&&!odm_sha256_equal(expected_score,&p.info.score_sha256))return ODM_STATUS_INTEGRITY_ERROR;if(expected_policy&&!odm_sha256_equal(expected_policy,&p.info.music_policy_sha256))return ODM_STATUS_INTEGRITY_ERROR;*out_info=p.info;return ODM_STATUS_OK;}

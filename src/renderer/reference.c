#include "renderer_internal.h"
#include "frame_state_internal.h"
#include "visual_internal.h"

#include "odm_fixed.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RENDER_ITEM_ALIGN UINT64_C(8)

typedef struct {
    odm_frame_node_state state;
    const odm_render_surface_frame *surface;
    int64_t node_start_sample;
    odm_visual_history history;
} odm_render_item;

static int bytes_zero_u32(const uint32_t *p, uint32_t n) {
    uint32_t i; for (i = 0u; i < n; ++i) if (p[i] != 0u) return 0; return 1;
}

static int digest_zero(const odm_sha256_digest *d) {
    uint32_t i; for (i = 0u; i < ODM_SHA256_DIGEST_BYTES; ++i) if (d->bytes[i] != 0u) return 0; return 1;
}

static void le32(uint8_t b[4], uint32_t v) {
    b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24);
}
static void le64(uint8_t b[8], uint64_t v) {
    uint32_t i; for(i=0u;i<8u;++i)b[i]=(uint8_t)(v>>(i*8u));
}
static odm_status hu32(odm_sha256_context *c, uint32_t v) { uint8_t b[4]; le32(b,v); return odm_sha256_update(c,b,4u); }
static odm_status hi64(odm_sha256_context *c, int64_t v) { uint8_t b[8]; le64(b,(uint64_t)v); return odm_sha256_update(c,b,8u); }
static odm_status hu64(odm_sha256_context *c, uint64_t v) { uint8_t b[8]; le64(b,v); return odm_sha256_update(c,b,8u); }

static odm_status checked_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (UINT64_MAX - a < b) return ODM_STATUS_OVERFLOW;
    *out = a + b; return ODM_STATUS_OK;
}
static odm_status checked_mul(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (a != 0u && b > UINT64_MAX / a) return ODM_STATUS_OVERFLOW;
    *out = a * b; return ODM_STATUS_OK;
}
static odm_status align8(uint64_t value, uint64_t *out) {
    uint64_t t;
    if (checked_add(value, RENDER_ITEM_ALIGN - 1u, &t) != ODM_STATUS_OK) return ODM_STATUS_OVERFLOW;
    *out = t & ~(RENDER_ITEM_ALIGN - 1u); return ODM_STATUS_OK;
}

static int ranges_overlap(const void *a, uint64_t a_bytes,
                          const void *b, uint64_t b_bytes) {
    uintptr_t as, bs, ae, be;
    if (a_bytes == 0u || b_bytes == 0u) return 0;
    if (!a || !b || a_bytes > (uint64_t)UINTPTR_MAX ||
        b_bytes > (uint64_t)UINTPTR_MAX) return 1;
    as = (uintptr_t)a; bs = (uintptr_t)b;
    if ((uintptr_t)a_bytes > UINTPTR_MAX - as ||
        (uintptr_t)b_bytes > UINTPTR_MAX - bs) return 1;
    ae = as + (uintptr_t)a_bytes; be = bs + (uintptr_t)b_bytes;
    return as < be && bs < ae;
}

static odm_status validate_surface(const odm_render_surface_frame *f, int video) {
    uint64_t pixels, bytes;
    if (!f || !f->pixels || f->width == 0u || f->height == 0u ||
        f->width > 16384u || f->height > 16384u ||
        f->pixel_format != ODM_RENDER_SURFACE_RGBA8 ||
        f->primaries != ODM_COLOR_PRIMARIES_BT709 ||
        (f->transfer != ODM_COLOR_TRANSFER_LINEAR && f->transfer != ODM_COLOR_TRANSFER_SRGB) ||
        f->alpha_mode != ODM_ALPHA_STRAIGHT || f->flags != 0u || f->reserved0 != 0u ||
        !bytes_zero_u32(f->reserved,4u)) return ODM_STATUS_INVALID_DATA;
    if (checked_mul(f->width, f->height, &pixels) != ODM_STATUS_OK ||
        pixels > ODM_RENDER_MAX_REFERENCE_PIXELS ||
        checked_mul(pixels, 4u, &bytes) != ODM_STATUS_OK || bytes != f->pixel_bytes) return ODM_STATUS_INVALID_DATA;
    if (video) {
        if (f->start_sample < 0 || f->end_sample <= f->start_sample) return ODM_STATUS_INVALID_DATA;
    } else if (f->start_sample != 0 || f->end_sample != 0) return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static odm_status validate_resource(const odm_render_resource_view *r) {
    uint32_t i;
    int video;
    if (!r || r->resource_id == 0u || r->flags != 0u || !bytes_zero_u32(r->reserved,4u) ||
        digest_zero(&r->source_sha256) || !r->frames || r->frame_count == 0u ||
        r->frame_count > ODM_RENDER_MAX_SURFACE_FRAMES ||
        (r->kind != ODM_SCORE_RESOURCE_IMAGE && r->kind != ODM_SCORE_RESOURCE_VIDEO)) return ODM_STATUS_INVALID_DATA;
    video = r->kind == ODM_SCORE_RESOURCE_VIDEO;
    if (!video && r->frame_count != 1u) return ODM_STATUS_INVALID_DATA;
    for (i=0u;i<r->frame_count;++i) {
        odm_status st=validate_surface(&r->frames[i],video); if(st!=ODM_STATUS_OK)return st;
        if (video) {
            if ((i==0u && r->frames[i].start_sample!=0) ||
                (i!=0u && r->frames[i].start_sample!=r->frames[i-1u].end_sample)) return ODM_STATUS_INVALID_DATA;
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_render_asset_set_sha256(const odm_render_asset_set *assets,
                                       odm_sha256_digest *out_sha256) {
    static const uint8_t domain[]={'O','D','P','A','R','_','R','E','N','D','E','R','_','A','S','S','E','T','S','_','V','1',0};
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER; uint32_t i,j,prev=0u; odm_status st;
    if(!assets||!out_sha256||assets->flags!=0u||!bytes_zero_u32(assets->reserved,4u)||
       (assets->resource_count!=0u&&!assets->resources))return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,domain,sizeof(domain));if(st!=ODM_STATUS_OK)return st;
    st=hu32(&c,assets->resource_count);if(st!=ODM_STATUS_OK)return st;
    for(i=0u;i<assets->resource_count;++i){const odm_render_resource_view*r=&assets->resources[i];
        st=validate_resource(r);if(st!=ODM_STATUS_OK)return st;if(r->resource_id<=prev)return ODM_STATUS_INVALID_DATA;prev=r->resource_id;
        if((st=hu32(&c,r->resource_id))!=ODM_STATUS_OK||(st=hu32(&c,r->kind))!=ODM_STATUS_OK||
           (st=hu32(&c,r->frame_count))!=ODM_STATUS_OK||
           (st=odm_sha256_update(&c,r->source_sha256.bytes,32u))!=ODM_STATUS_OK)return st;
        for(j=0u;j<r->frame_count;++j){const odm_render_surface_frame*f=&r->frames[j];odm_sha256_digest px;
            st=odm_sha256(f->pixels,f->pixel_bytes,&px);if(st!=ODM_STATUS_OK)return st;
            if((st=hu32(&c,f->width))!=ODM_STATUS_OK||(st=hu32(&c,f->height))!=ODM_STATUS_OK||
               (st=hu32(&c,f->pixel_format))!=ODM_STATUS_OK||(st=hu32(&c,f->primaries))!=ODM_STATUS_OK||
               (st=hu32(&c,f->transfer))!=ODM_STATUS_OK||(st=hu32(&c,f->alpha_mode))!=ODM_STATUS_OK||
               (st=hi64(&c,f->start_sample))!=ODM_STATUS_OK||(st=hi64(&c,f->end_sample))!=ODM_STATUS_OK||
               (st=hu64(&c,f->pixel_bytes))!=ODM_STATUS_OK||(st=odm_sha256_update(&c,px.bytes,32u))!=ODM_STATUS_OK)return st;
        }
    }
    return odm_sha256_final(&c,out_sha256);
}

static const odm_render_resource_view *find_asset(const odm_render_asset_set *assets,uint32_t id){
    uint32_t lo=0u,hi=assets->resource_count;while(lo<hi){uint32_t mid=lo+(hi-lo)/2u;uint32_t got=assets->resources[mid].resource_id;if(got==id)return &assets->resources[mid];if(got<id)lo=mid+1u;else hi=mid;}return NULL;
}
static const uint8_t *find_node_record(const odm_ir_parsed*p,uint32_t id){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_NODES-1u];uint32_t lo=0u,hi=s->count;while(lo<hi){uint32_t mid=lo+(hi-lo)/2u;const uint8_t*r=s->records+(uint64_t)mid*s->record_bytes;uint32_t got=odm_ir_read_u32(r);if(got==id)return r;if(got<id)lo=mid+1u;else hi=mid;}return NULL;
}

static odm_status assets_match_ir(const odm_ir_parsed *p,const odm_render_asset_set *assets){
    const odm_ir_section_view*s=&p->sections[ODM_IR_SECTION_RESOURCES-1u];uint32_t i;
    if(!assets||assets->resource_count!=s->count)return ODM_STATUS_INTEGRITY_ERROR;
    for(i=0u;i<s->count;++i){const uint8_t*r=s->records+(uint64_t)i*s->record_bytes;const odm_render_resource_view*a=&assets->resources[i];
        odm_status st=validate_resource(a);if(st!=ODM_STATUS_OK)return st;
        if(a->resource_id!=odm_ir_read_u32(r)||a->kind!=odm_ir_read_u32(r+4u)||
           memcmp(a->source_sha256.bytes,r+16u,32u)!=0)return ODM_STATUS_INTEGRITY_ERROR;
    }return ODM_STATUS_OK;
}

static const odm_render_surface_frame *select_surface(const odm_render_resource_view*r,int64_t local){
    uint32_t lo,hi;
    if(r->kind==ODM_SCORE_RESOURCE_IMAGE)return &r->frames[0];
    if(local<0)return NULL;
    lo=0u;hi=r->frame_count;
    while(lo<hi){uint32_t mid=lo+(hi-lo)/2u;const odm_render_surface_frame*f=&r->frames[mid];if(local<f->start_sample)hi=mid;else if(local>=f->end_sample)lo=mid+1u;else return f;}return NULL;
}

static int item_less(const odm_render_item*a,const odm_render_item*b){
    if(a->state.scene_id!=b->state.scene_id)return a->state.scene_id<b->state.scene_id;
    if(a->state.role==ODM_SCORE_NODE_ENVIRONMENT&&b->state.role!=ODM_SCORE_NODE_ENVIRONMENT)return 1;
    if(b->state.role==ODM_SCORE_NODE_ENVIRONMENT&&a->state.role!=ODM_SCORE_NODE_ENVIRONMENT)return 0;
    if(a->state.z_index!=b->state.z_index)return a->state.z_index<b->state.z_index;
    return a->state.node_id<b->state.node_id;
}
static void sort_items(odm_render_item*items,uint64_t n){
    uint64_t i;for(i=1u;i<n;++i){odm_render_item v=items[i];uint64_t j=i;while(j>0u&&item_less(&v,&items[j-1u])){items[j]=items[j-1u];--j;}items[j]=v;}
}

static odm_status frame_bytes_for(const odm_ir_parsed*p,uint64_t*out){uint64_t px;if(checked_mul(p->info.width,p->info.height,&px)!=ODM_STATUS_OK||px>ODM_RENDER_MAX_REFERENCE_PIXELS)return ODM_STATUS_BUDGET_EXCEEDED;return checked_mul(px,8u,out);}

static odm_status scratch_for_counts(uint64_t frame,uint64_t nr,uint64_t tr,uint64_t*out_scratch){
    uint64_t off,nbytes,tbytes,ibytes;odm_status st;
    if(!out_scratch)return ODM_STATUS_INVALID_ARGUMENT;
    off=frame;st=align8(off,&off);if(st!=ODM_STATUS_OK)return st;
    st=checked_mul(nr,sizeof(odm_frame_node_state),&nbytes);if(st!=ODM_STATUS_OK)return st;
    st=checked_add(off,nbytes,&off);if(st!=ODM_STATUS_OK)return st;st=align8(off,&off);if(st!=ODM_STATUS_OK)return st;
    st=checked_mul(tr,sizeof(odm_frame_transfer_event),&tbytes);if(st!=ODM_STATUS_OK)return st;
    st=checked_add(off,tbytes,&off);if(st!=ODM_STATUS_OK)return st;st=align8(off,&off);if(st!=ODM_STATUS_OK)return st;
    st=checked_mul(nr,sizeof(odm_render_item),&ibytes);if(st!=ODM_STATUS_OK)return st;
    st=checked_add(off,ibytes,&off);if(st!=ODM_STATUS_OK)return st;
    *out_scratch=off;return ODM_STATUS_OK;
}

static odm_status requirements_parsed(const odm_ir_parsed*p,const uint8_t*record,uint64_t record_bytes,int64_t frame_index,const odm_music_analysis_tick*tick,uint64_t*out_frame,uint64_t*out_scratch,uint64_t*out_nodes,uint64_t*out_transfers){
    uint64_t frame,nr,tr;odm_status st;
    st=frame_bytes_for(p,&frame);if(st!=ODM_STATUS_OK)return st;
    st=odm_frame_state_requirements(record,record_bytes,frame_index,tick,&nr,&tr);if(st!=ODM_STATUS_OK)return st;
    st=scratch_for_counts(frame,nr,tr,out_scratch);if(st!=ODM_STATUS_OK)return st;
    *out_frame=frame;if(out_nodes)*out_nodes=nr;if(out_transfers)*out_transfers=tr;return ODM_STATUS_OK;
}

odm_status odm_reference_render_requirements(const uint8_t *render_ir,uint64_t render_ir_bytes,int64_t frame_index,const odm_music_analysis_tick *music_tick,uint64_t *out_frame_bytes,uint64_t *out_scratch_bytes){
    odm_ir_parsed p;uint64_t nr,tr;odm_status st;if(!out_frame_bytes||!out_scratch_bytes)return ODM_STATUS_INVALID_ARGUMENT;st=odm_ir_parse_internal(render_ir,render_ir_bytes,&p);if(st!=ODM_STATUS_OK)return st;return requirements_parsed(&p,render_ir,render_ir_bytes,frame_index,music_tick,out_frame_bytes,out_scratch_bytes,&nr,&tr);
}

odm_status odm_reference_render_max_requirements(const uint8_t *render_ir,uint64_t render_ir_bytes,uint64_t *out_frame_bytes,uint64_t *out_scratch_bytes){
    odm_ir_parsed p;uint64_t frame,nr,tr;odm_status st;
    if(!render_ir||!out_frame_bytes||!out_scratch_bytes)return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_ir_parse_internal(render_ir,render_ir_bytes,&p);if(st!=ODM_STATUS_OK)return st;
    st=frame_bytes_for(&p,&frame);if(st!=ODM_STATUS_OK)return st;
    nr=(uint64_t)p.sections[ODM_IR_SECTION_NODES-1u].count;
    tr=(uint64_t)p.sections[ODM_IR_SECTION_STATE_TRANSFER-1u].count;
    st=scratch_for_counts(frame,nr,tr,out_scratch_bytes);if(st!=ODM_STATUS_OK)return st;
    *out_frame_bytes=frame;return ODM_STATUS_OK;
}

static odm_status cancel_check(const odm_job_ticket*ticket){int cancelled=0;odm_status st;if(!ticket)return ODM_STATUS_OK;st=odm_job_should_cancel(*ticket,&cancelled);if(st!=ODM_STATUS_OK)return st;return cancelled?ODM_STATUS_CANCELLED:ODM_STATUS_OK;}

static int procedural_cap(uint32_t cap) {
    return cap == ODM_CAP_GRID_PROOF || cap == ODM_CAP_HALO ||
           cap == ODM_CAP_RESIDUAL_EVENT_FIELD ||
           cap == ODM_CAP_DETERMINISTIC_PARTICLES;
}

static odm_status scene_pixel(const odm_render_item*items,uint64_t n,uint32_t scene,int64_t sample,uint32_t x,uint32_t y,uint32_t w,uint32_t h,odm_linear_pixel*out){
    uint64_t i;odm_status st;out->r=0;out->g=0;out->b=0;out->a=0;
    for(i=0u;i<n;++i){const odm_frame_node_state*ns=&items[i].state;odm_linear_pixel src;if(ns->scene_id!=scene)continue;
        if(ns->capability_id==ODM_CAP_BLACK_VOID){src.r=0;src.g=0;src.b=0;src.a=ns->opacity_q31;odm_renderer_blend_over(out,&src);continue;}
        if(ns->capability_id==ODM_CAP_PRIMARY_EMPTY||ns->capability_id==ODM_CAP_LOOK_IDENTITY)continue;
        if(procedural_cap(ns->capability_id)){odm_visual_pixel_q31 vp;const odm_visual_history*vh=ns->state_slot!=0u?&items[i].history:NULL;st=odm_visual_procedural_pixel(ns,vh,sample,x,y,w,h,&vp);if(st!=ODM_STATUS_OK)return st;src.r=(odm_render_q4_27)(vp.r/16);src.g=(odm_render_q4_27)(vp.g/16);src.b=(odm_render_q4_27)(vp.b/16);src.a=vp.a;odm_renderer_blend_over(out,&src);continue;}
        if(ns->capability_id!=ODM_CAP_PRIMARY_IMAGE&&ns->capability_id!=ODM_CAP_SUBJECT_IMAGE&&ns->capability_id!=ODM_CAP_PRIMARY_VIDEO&&ns->capability_id!=ODM_CAP_SUBJECT_VIDEO)return ODM_STATUS_UNSUPPORTED;
        if(!items[i].surface)return ODM_STATUS_INVALID_DATA;
        st=odm_renderer_sample_node(ns,items[i].surface,x,y,w,h,&src);if(st!=ODM_STATUS_OK)return st;odm_renderer_blend_over(out,&src);
    }return ODM_STATUS_OK;
}

static void put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}

static odm_status frame_hash(const odm_ir_parsed*p,const odm_reference_frame_info*info,const uint8_t*pixels,odm_sha256_digest*out){
    static const uint8_t domain[]={'O','D','P','A','R','_','R','E','F','_','F','R','A','M','E','_','V','1',0};odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;odm_status st;
    st=odm_sha256_init(&c);if(st!=ODM_STATUS_OK)return st;st=odm_sha256_update(&c,domain,sizeof(domain));if(st!=ODM_STATUS_OK)return st;
    if((st=odm_sha256_update(&c,p->info.record_sha256.bytes,32u))!=ODM_STATUS_OK||
       (st=odm_sha256_update(&c,info->assets_sha256.bytes,32u))!=ODM_STATUS_OK||
       (st=hu32(&c,info->renderer_version_major))!=ODM_STATUS_OK||
       (st=hu32(&c,info->renderer_version_minor))!=ODM_STATUS_OK||
       (st=hu32(&c,(uint32_t)info->fps))!=ODM_STATUS_OK||
       (st=hi64(&c,info->frame_index))!=ODM_STATUS_OK||
       (st=hi64(&c,info->sample))!=ODM_STATUS_OK||
       (st=hu32(&c,info->width))!=ODM_STATUS_OK||
       (st=hu32(&c,info->height))!=ODM_STATUS_OK||
       (st=hu32(&c,info->pixel_format))!=ODM_STATUS_OK||
       (st=hu64(&c,info->pixel_bytes))!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,pixels,info->pixel_bytes);if(st!=ODM_STATUS_OK)return st;
    return odm_sha256_final(&c,out);
}

odm_status odm_reference_render_frame(const uint8_t *render_ir,uint64_t render_ir_bytes,int64_t frame_index,const odm_music_analysis_tick *music_tick,const odm_render_asset_set *assets,const odm_job_ticket *job_ticket,void *scratch,uint64_t scratch_bytes,uint8_t *frame,uint64_t frame_capacity,uint64_t *out_required_frame,uint64_t *out_required_scratch,odm_reference_frame_info *out_info){
    odm_ir_parsed p;uint64_t fb,sb,nr,tr,off,nbytes,tbytes,i;odm_frame_node_state*nodes;odm_frame_transfer_event*transfers;odm_render_item*items;odm_frame_state fs;odm_sha256_digest assets_sha,pixel_sha;uint8_t*work;odm_status st;uint32_t y,x;
    if(!out_required_frame||!out_required_scratch||!out_info)return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_ir_parse_internal(render_ir,render_ir_bytes,&p);if(st!=ODM_STATUS_OK)return st;
    st=requirements_parsed(&p,render_ir,render_ir_bytes,frame_index,music_tick,&fb,&sb,&nr,&tr);if(st!=ODM_STATUS_OK)return st;*out_required_frame=fb;*out_required_scratch=sb;
    if(!scratch||scratch_bytes<sb||!frame||frame_capacity<fb)return ODM_STATUS_BUFFER_TOO_SMALL;
    if((((uintptr_t)scratch)&(uintptr_t)(ODM_REFERENCE_SCRATCH_ALIGNMENT-1u))!=0u)return ODM_STATUS_INVALID_ARGUMENT;
    if(ranges_overlap(scratch,sb,frame,fb))return ODM_STATUS_INVALID_ARGUMENT;
    if(fb>(uint64_t)SIZE_MAX||sb>(uint64_t)SIZE_MAX)return ODM_STATUS_OVERFLOW;
    st=assets_match_ir(&p,assets);if(st!=ODM_STATUS_OK)return st;st=odm_render_asset_set_sha256(assets,&assets_sha);if(st!=ODM_STATUS_OK)return st;st=cancel_check(job_ticket);if(st!=ODM_STATUS_OK)return st;
    work=(uint8_t*)scratch;off=fb;st=align8(off,&off);if(st!=ODM_STATUS_OK)return st;nodes=(odm_frame_node_state*)((uint8_t*)scratch+off);nbytes=nr*sizeof(*nodes);off+=nbytes;st=align8(off,&off);if(st!=ODM_STATUS_OK)return st;transfers=(odm_frame_transfer_event*)((uint8_t*)scratch+off);tbytes=tr*sizeof(*transfers);off+=tbytes;st=align8(off,&off);if(st!=ODM_STATUS_OK)return st;items=(odm_render_item*)((uint8_t*)scratch+off);
    st=odm_frame_state_resolve(render_ir,render_ir_bytes,frame_index,music_tick,nodes,nr,transfers,tr,&nr,&tr,&fs);if(st!=ODM_STATUS_OK)return st;
    for(i=0u;i<nr;++i){const uint8_t*r=find_node_record(&p,nodes[i].node_id);const odm_render_resource_view*res=NULL;const odm_render_surface_frame*surf=NULL;int synthetic=(uint64_t)fs.sample>=p.info.canonical_music_frames;if(!r)return ODM_STATUS_INVARIANT_BROKEN;
        st=odm_frame_node_compute_internal(&p,r,fs.sample,fs.scene_a_id,fs.scene_b_id,fs.transition_progress_q31,music_tick,synthetic,0,&items[i].state);if(st!=ODM_STATUS_OK)return st;items[i].node_start_sample=odm_ir_read_i64(r+40u);items[i].surface=NULL;memset(&items[i].history,0,sizeof(items[i].history));
        if(items[i].state.state_slot!=0u){st=odm_visual_history_resolve_parsed(&p,r,fs.sample,&items[i].history);if(st!=ODM_STATUS_OK)return st;}
        if(items[i].state.resource_id!=0u){res=find_asset(assets,items[i].state.resource_id);if(!res)return ODM_STATUS_INTEGRITY_ERROR;surf=select_surface(res,fs.sample-items[i].node_start_sample);if(!surf)return ODM_STATUS_INVALID_DATA;items[i].surface=surf;}
    }
    sort_items(items,nr);
    for(y=0u;y<p.info.height;++y){st=cancel_check(job_ticket);if(st!=ODM_STATUS_OK)return st;for(x=0u;x<p.info.width;++x){odm_linear_pixel a,b,o;uint8_t*dst=work+((uint64_t)y*p.info.width+x)*8u;st=scene_pixel(items,nr,fs.scene_a_id,fs.sample,x,y,p.info.width,p.info.height,&a);if(st!=ODM_STATUS_OK)return st;if(fs.scene_b_id!=0u){st=scene_pixel(items,nr,fs.scene_b_id,fs.sample,x,y,p.info.width,p.info.height,&b);if(st!=ODM_STATUS_OK)return st;st=odm_renderer_q27_lerp(a.r,b.r,fs.transition_progress_q31,&o.r);if(st!=ODM_STATUS_OK)return st;st=odm_renderer_q27_lerp(a.g,b.g,fs.transition_progress_q31,&o.g);if(st!=ODM_STATUS_OK)return st;st=odm_renderer_q27_lerp(a.b,b.b,fs.transition_progress_q31,&o.b);if(st!=ODM_STATUS_OK)return st;st=odm_renderer_q31_lerp(a.a,b.a,fs.transition_progress_q31,&o.a);if(st!=ODM_STATUS_OK)return st;}else o=a;put16(dst,odm_renderer_q27_to_u16(o.r));put16(dst+2u,odm_renderer_q27_to_u16(o.g));put16(dst+4u,odm_renderer_q27_to_u16(o.b));put16(dst+6u,odm_renderer_q31_to_u16(o.a));}}
    st=odm_sha256(work,fb,&pixel_sha);if(st!=ODM_STATUS_OK)return st;
    {odm_reference_frame_info info;memset(&info,0,sizeof(info));info.renderer_version_major=ODM_REFERENCE_RENDERER_VERSION_MAJOR;info.renderer_version_minor=ODM_REFERENCE_RENDERER_VERSION_MINOR;info.pixel_format=ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL;info.width=p.info.width;info.height=p.info.height;info.fps=p.info.fps;info.frame_index=frame_index;info.sample=fs.sample;info.pixel_bytes=fb;info.assets_sha256=assets_sha;info.pixel_sha256=pixel_sha;st=frame_hash(&p,&info,work,&info.frame_sha256);if(st!=ODM_STATUS_OK)return st;memcpy(frame,work,(size_t)fb);*out_info=info;}
    return ODM_STATUS_OK;
}

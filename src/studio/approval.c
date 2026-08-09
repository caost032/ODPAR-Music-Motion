#include "odm_studio.h"
#include "odm_time.h"

#include <stddef.h>
#include <string.h>

static const uint8_t MAGIC[8]={'O','D','M','S','T','U','A','1'};
static const uint8_t DOMAIN[]="ODPAR_STUDIO_PREVIEW_APPROVAL_V1";
static int dnz(const odm_sha256_digest*d){uint32_t i;if(!d)return 0;for(i=0u;i<32u;++i)if(d->bytes[i]!=0u)return 1;return 0;}
static void le32(uint8_t*b,uint32_t v){b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);b[2]=(uint8_t)(v>>16);b[3]=(uint8_t)(v>>24);}
static void le64(uint8_t*b,uint64_t v){uint32_t i;for(i=0u;i<8u;++i)b[i]=(uint8_t)(v>>(8u*i));}
static odm_status h32(odm_sha256_context*c,uint32_t v){uint8_t b[4];le32(b,v);return odm_sha256_update(c,b,4u);}
static odm_status h64(odm_sha256_context*c,uint64_t v){uint8_t b[8];le64(b,v);return odm_sha256_update(c,b,8u);}
static uint32_t div_for(uint32_t r){switch(r){case ODM_PREVIEW_RASTER_EXACT:return 1u;case ODM_PREVIEW_RASTER_HALF:return 2u;case ODM_PREVIEW_RASTER_QUARTER:return 4u;case ODM_PREVIEW_RASTER_EIGHTH:return 8u;default:return 0u;}}
static uint32_t ceil_div(uint32_t a,uint32_t b){return a/b+((a%b)!=0u?1u:0u);}

static odm_status approval_id_compute(const odm_studio_preview_approval_info*i,odm_sha256_digest*out){
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;odm_status st;uint8_t b[8];
    if (!i || !out) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,DOMAIN,sizeof(DOMAIN));if(st!=ODM_STATUS_OK)return st;
#define H32(x) do{st=h32(&c,(uint32_t)(x));if(st!=ODM_STATUS_OK)return st;}while(0)
#define H64(x) do{st=h64(&c,(uint64_t)(x));if(st!=ODM_STATUS_OK)return st;}while(0)
#define HD(x) do{st=odm_sha256_update(&c,(x).bytes,32u);if(st!=ODM_STATUS_OK)return st;}while(0)
    H32(i->schema_version_major);H32(i->schema_version_minor);H32(i->raster_class);H32(i->pixel_format);H32(i->semantics_class);H32(i->flags);
    le64(b,(uint64_t)i->frame_index);st=odm_sha256_update(&c,b,8u);if(st!=ODM_STATUS_OK)return st;
    le64(b,(uint64_t)i->sample);st=odm_sha256_update(&c,b,8u);if(st!=ODM_STATUS_OK)return st;
    H32(i->source_width);H32(i->source_height);H32(i->output_width);H32(i->output_height);H32((uint32_t)i->fps);H32(i->reserved0);
    le64(b,(uint64_t)i->project_end_sample);st=odm_sha256_update(&c,b,8u);if(st!=ODM_STATUS_OK)return st;
    H64(i->revision_number);HD(i->revision_id);HD(i->render_ir_sha256);HD(i->canonical_frame_sha256);HD(i->preview_pixel_sha256);
#undef H32
#undef H64
#undef HD
    return odm_sha256_final(&c,out);
}
static odm_status semantic_validate(const odm_studio_preview_approval_info*i){
    odm_timeline tl;odm_sha256_digest id;int64_t sample;uint32_t d;odm_status st;
    if (!i) return ODM_STATUS_INVALID_ARGUMENT;
    d=div_for(i->raster_class);
    if(i->schema_version_major!=1u||i->schema_version_minor!=0u||d==0u||i->pixel_format!=ODM_PREVIEW_PIXEL_RGBA8_SRGB_STRAIGHT||
       i->semantics_class!=ODM_STUDIO_APPROVAL_SEMANTICS_EXACT||i->flags!=0u||i->reserved0!=0u||i->frame_index<0||
       i->source_width<2u||i->source_height<2u||i->output_width!=ceil_div(i->source_width,d)||i->output_height!=ceil_div(i->source_height,d)||
       !odm_time_fps_supported(i->fps)||i->project_end_sample<=0||!dnz(&i->revision_id)||!dnz(&i->render_ir_sha256)||
       !dnz(&i->canonical_frame_sha256)||!dnz(&i->preview_pixel_sha256)||!dnz(&i->approval_id))return ODM_STATUS_INVALID_DATA;
    st=odm_time_resolve_timeline(i->project_end_sample,i->fps,&tl);if(st!=ODM_STATUS_OK||i->frame_index>=tl.frame_count)return ODM_STATUS_INVALID_DATA;
    st=odm_time_frame_start(i->frame_index,i->fps,&sample);if(st!=ODM_STATUS_OK||sample!=i->sample)return ODM_STATUS_INVALID_DATA;
    st=approval_id_compute(i,&id);if(st!=ODM_STATUS_OK)return st;return odm_sha256_equal(&id,&i->approval_id)?ODM_STATUS_OK:ODM_STATUS_INTEGRITY_ERROR;
}
static odm_status payload_write(const odm_studio_preview_approval_info*i,uint8_t p[ODM_STUDIO_APPROVAL_PAYLOAD_BYTES]){
    odm_wire_writer w=ODM_WIRE_WRITER_INITIALIZER;uint8_t rz[8]={0};uint64_t n=0u;odm_status st=odm_wire_writer_init(&w,p,ODM_STUDIO_APPROVAL_PAYLOAD_BYTES);if(st!=ODM_STATUS_OK)return st;
#define W(x) do{st=(x);if(st!=ODM_STATUS_OK)return st;}while(0)
    W(odm_wire_write_bytes(&w,MAGIC,8u));W(odm_wire_write_u32(&w,i->schema_version_major));W(odm_wire_write_u32(&w,i->schema_version_minor));
    W(odm_wire_write_u32(&w,i->raster_class));W(odm_wire_write_u32(&w,i->pixel_format));W(odm_wire_write_u32(&w,i->semantics_class));W(odm_wire_write_u32(&w,i->flags));
    W(odm_wire_write_i64(&w,i->frame_index));W(odm_wire_write_i64(&w,i->sample));W(odm_wire_write_u32(&w,i->source_width));W(odm_wire_write_u32(&w,i->source_height));
    W(odm_wire_write_u32(&w,i->output_width));W(odm_wire_write_u32(&w,i->output_height));W(odm_wire_write_i32(&w,i->fps));W(odm_wire_write_u32(&w,i->reserved0));
    W(odm_wire_write_i64(&w,i->project_end_sample));W(odm_wire_write_u64(&w,i->revision_number));W(odm_wire_write_bytes(&w,i->revision_id.bytes,32u));
    W(odm_wire_write_bytes(&w,i->render_ir_sha256.bytes,32u));W(odm_wire_write_bytes(&w,i->canonical_frame_sha256.bytes,32u));W(odm_wire_write_bytes(&w,i->preview_pixel_sha256.bytes,32u));
    W(odm_wire_write_bytes(&w,i->approval_id.bytes,32u));W(odm_wire_write_bytes(&w,rz,8u));
#undef W
    st=odm_wire_writer_finish(&w,&n);if(st!=ODM_STATUS_OK)return st;return n==ODM_STUDIO_APPROVAL_PAYLOAD_BYTES?ODM_STATUS_OK:ODM_STATUS_INVARIANT_BROKEN;
}

odm_status odm_studio_preview_approval_validate(const uint8_t*record,uint64_t bytes,const odm_sha256_digest*expected,odm_studio_preview_approval_info*out){
    odm_wire_record_info wi;odm_wire_reader r=ODM_WIRE_READER_INITIALIZER;odm_studio_preview_approval_info i;odm_sha256_digest sha;uint64_t off=0u;uint8_t magic[8],rz[8];uint32_t j;odm_status st;
    if (!record || !out) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_wire_record_read(record,bytes,&wi,&off,&sha);if(st!=ODM_STATUS_OK)return st;
    if(wi.kind!=ODM_STUDIO_APPROVAL_KIND||wi.schema_version_major!=1u||wi.schema_version_minor!=0u||wi.payload_bytes!=ODM_STUDIO_APPROVAL_PAYLOAD_BYTES||bytes!=ODM_STUDIO_APPROVAL_RECORD_BYTES)return ODM_STATUS_VERSION_MISMATCH;
    memset(&i,0,sizeof(i));st=odm_wire_reader_init(&r,record+off,wi.payload_bytes);if(st!=ODM_STATUS_OK)return st;
#define R(x) do{st=(x);if(st!=ODM_STATUS_OK)return st;}while(0)
    R(odm_wire_read_bytes(&r,magic,8u));if(memcmp(magic,MAGIC,8u)!=0)return ODM_STATUS_INVALID_DATA;
    R(odm_wire_read_u32(&r,&i.schema_version_major));R(odm_wire_read_u32(&r,&i.schema_version_minor));R(odm_wire_read_u32(&r,&i.raster_class));R(odm_wire_read_u32(&r,&i.pixel_format));
    R(odm_wire_read_u32(&r,&i.semantics_class));R(odm_wire_read_u32(&r,&i.flags));R(odm_wire_read_i64(&r,&i.frame_index));R(odm_wire_read_i64(&r,&i.sample));
    R(odm_wire_read_u32(&r,&i.source_width));R(odm_wire_read_u32(&r,&i.source_height));R(odm_wire_read_u32(&r,&i.output_width));R(odm_wire_read_u32(&r,&i.output_height));
    R(odm_wire_read_i32(&r,&i.fps));R(odm_wire_read_u32(&r,&i.reserved0));R(odm_wire_read_i64(&r,&i.project_end_sample));R(odm_wire_read_u64(&r,&i.revision_number));
    R(odm_wire_read_bytes(&r,i.revision_id.bytes,32u));R(odm_wire_read_bytes(&r,i.render_ir_sha256.bytes,32u));R(odm_wire_read_bytes(&r,i.canonical_frame_sha256.bytes,32u));
    R(odm_wire_read_bytes(&r,i.preview_pixel_sha256.bytes,32u));R(odm_wire_read_bytes(&r,i.approval_id.bytes,32u));R(odm_wire_read_bytes(&r,rz,8u));
#undef R
    st=odm_wire_reader_finish(&r);if(st!=ODM_STATUS_OK)return st;for(j=0u;j<8u;++j)if(rz[j]!=0u)return ODM_STATUS_INVALID_DATA;
    st=semantic_validate(&i);if(st!=ODM_STATUS_OK)return st;if(expected&&!odm_sha256_equal(expected,&i.revision_id))return ODM_STATUS_INTEGRITY_ERROR;
    i.record_sha256=sha;*out=i;return ODM_STATUS_OK;
}

odm_status odm_studio_preview_approval_write(const uint8_t*revision,uint64_t revision_bytes,const odm_preview_plan*plan,const odm_preview_frame_info*preview,
                                             uint8_t*record,uint64_t capacity,uint64_t*out_required,odm_studio_preview_approval_info*out){
    odm_studio_revision_info rev;odm_studio_preview_approval_info i;odm_sha256_digest id,sha;uint8_t payload[ODM_STUDIO_APPROVAL_PAYLOAD_BYTES];odm_status st;
    if (!revision || !plan || !preview || !out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_studio_revision_validate(revision,revision_bytes,&rev);if(st!=ODM_STATUS_OK)return st;
    if(plan->schema_version!=ODM_PREVIEW_PLAN_SCHEMA_VERSION||preview->schema_version!=ODM_PREVIEW_FRAME_INFO_SCHEMA_VERSION||plan->raster_class!=preview->raster_class||
       plan->pixel_format!=preview->pixel_format||plan->semantics_class!=preview->semantics_class||plan->source_width!=preview->source_width||plan->source_height!=preview->source_height||
       plan->output_width!=preview->output_width||plan->output_height!=preview->output_height||plan->fps!=preview->fps||plan->frame_index!=preview->frame_index||plan->sample!=preview->sample||
       !odm_sha256_equal(&plan->render_ir_sha256,&rev.render_ir_sha256)||plan->source_width!=rev.width||plan->source_height!=rev.height||plan->fps!=rev.fps||
       preview->semantics_class!=ODM_PREVIEW_SEMANTICS_EXACT||preview->flags!=0u)return ODM_STATUS_INTEGRITY_ERROR;
    memset(&i,0,sizeof(i));i.schema_version_major=1u;i.raster_class=preview->raster_class;i.pixel_format=preview->pixel_format;i.semantics_class=preview->semantics_class;
    i.frame_index=preview->frame_index;i.sample=preview->sample;i.source_width=preview->source_width;i.source_height=preview->source_height;i.output_width=preview->output_width;i.output_height=preview->output_height;
    i.fps=preview->fps;i.project_end_sample=rev.project_end_sample;i.revision_number=rev.revision_number;i.revision_id=rev.revision_id;i.render_ir_sha256=rev.render_ir_sha256;
    i.canonical_frame_sha256=preview->canonical_frame_sha256;i.preview_pixel_sha256=preview->preview_pixel_sha256;
    st=approval_id_compute(&i,&id);if(st!=ODM_STATUS_OK)return st;i.approval_id=id;st=semantic_validate(&i);if(st!=ODM_STATUS_OK)return st;
    st=payload_write(&i,payload);if(st!=ODM_STATUS_OK)return st;st=odm_wire_record_write(ODM_STUDIO_APPROVAL_KIND,1u,0u,payload,sizeof(payload),record,capacity,out_required,&sha);if(st!=ODM_STATUS_OK)return st;
    i.record_sha256=sha;if(out)*out=i;return ODM_STATUS_OK;
}

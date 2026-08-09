#include "odm_studio.h"
#include "odm_ir.h"
#include "odm_time.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static const uint8_t ASSET_DOMAIN[] = "ODPAR_STUDIO_ASSET_INDEX_V1";
static const uint8_t REV_DOMAIN[] = "ODPAR_STUDIO_REVISION_ID_V1";
static const uint8_t PROJECT_DOMAIN[] = "ODPAR_STUDIO_PROJECT_ID_V1";
static const uint8_t REV_MAGIC[8] = {'O','D','M','S','T','U','R','1'};

static int digest_nonzero(const odm_sha256_digest *d) {
    uint32_t i;
    if (!d) return 0;
    for (i = 0u; i < ODM_SHA256_DIGEST_BYTES; ++i) if (d->bytes[i] != 0u) return 1;
    return 0;
}
static int digest_zero(const odm_sha256_digest *d) { return !digest_nonzero(d); }
static int bytes_nonzero(const uint8_t *p, uint32_t n) {
    uint32_t i;
    if (!p) return 0;
    for (i = 0u; i < n; ++i) if (p[i] != 0u) return 1;
    return 0;
}
static void le32(uint8_t b[4], uint32_t v) {
    b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24);
}
static void le64(uint8_t b[8], uint64_t v) {
    uint32_t i; for (i=0u;i<8u;++i) b[i]=(uint8_t)(v>>(8u*i));
}
static odm_status h32(odm_sha256_context *c, uint32_t v) { uint8_t b[4]; le32(b,v); return odm_sha256_update(c,b,4u); }
static odm_status h64(odm_sha256_context *c, uint64_t v) { uint8_t b[8]; le64(b,v); return odm_sha256_update(c,b,8u); }

odm_status odm_studio_asset_index_sha256(const odm_score_view *score,
                                          odm_sha256_digest *out_sha256) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status st; uint32_t i;
    if (!out_sha256) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_score_validate(score); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_init(&c); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,ASSET_DOMAIN,sizeof(ASSET_DOMAIN)); if(st!=ODM_STATUS_OK) return st;
    st=h32(&c,score->header->resource_count); if(st!=ODM_STATUS_OK) return st;
    for(i=0u;i<score->header->resource_count;++i){
        const odm_score_resource *r=&score->resources[i];
        st=h32(&c,r->resource_id); if(st!=ODM_STATUS_OK) return st;
        st=h32(&c,r->kind); if(st!=ODM_STATUS_OK) return st;
        st=h32(&c,r->flags); if(st!=ODM_STATUS_OK) return st;
        st=odm_sha256_update(&c,r->content_sha256.bytes,32u); if(st!=ODM_STATUS_OK) return st;
    }
    return odm_sha256_final(&c,out_sha256);
}

static odm_status revision_id_compute(const odm_studio_revision_info *i,
                                      odm_sha256_digest *out) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status st; uint8_t b[8];
    if(!i||!out) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,REV_DOMAIN,sizeof(REV_DOMAIN)); if(st!=ODM_STATUS_OK) return st;
#define H32(v) do{st=h32(&c,(uint32_t)(v));if(st!=ODM_STATUS_OK)return st;}while(0)
#define H64(v) do{st=h64(&c,(uint64_t)(v));if(st!=ODM_STATUS_OK)return st;}while(0)
#define HD(v) do{st=odm_sha256_update(&c,(v).bytes,32u);if(st!=ODM_STATUS_OK)return st;}while(0)
    H32(i->schema_version_major); H32(i->schema_version_minor); H64(i->revision_number);
    H32(i->asset_count); H32(i->flags); H32(i->width); H32(i->height); H32((uint32_t)i->fps); H32(i->reserved0);
    le64(b,(uint64_t)i->project_end_sample); st=odm_sha256_update(&c,b,8u); if(st!=ODM_STATUS_OK)return st;
    H64(i->project_seed); HD(i->parent_revision_id); HD(i->package_sha256); HD(i->score_sha256);
    HD(i->capability_sha256); HD(i->music_map_sha256); HD(i->music_policy_sha256);
    HD(i->render_ir_sha256); HD(i->asset_index_sha256); HD(i->package_content_sha256); HD(i->project_id);
    st=odm_sha256_update(&c,i->package_signer_public_key,ODM_ODPARMS_PUBLIC_KEY_BYTES); if(st!=ODM_STATUS_OK)return st;
#undef H32
#undef H64
#undef HD
    return odm_sha256_final(&c,out);
}

static odm_status project_id_compute(const odm_studio_revision_info *i,
                                     odm_sha256_digest *out) {
    odm_sha256_context c = ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status st;
    if (!i || !out) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_sha256_init(&c);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&c, PROJECT_DOMAIN, sizeof(PROJECT_DOMAIN));
    if (st == ODM_STATUS_OK) st = h64(&c, i->project_seed);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&c, i->score_sha256.bytes, 32u);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&c, i->package_content_sha256.bytes, 32u);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&c, i->package_signer_public_key, 32u);
    if (st == ODM_STATUS_OK) st = odm_sha256_final(&c, out);
    return st;
}

static odm_status revision_payload_write(const odm_studio_revision_info *i,
                                         uint8_t payload[ODM_STUDIO_REVISION_PAYLOAD_BYTES]) {
    odm_wire_writer w=ODM_WIRE_WRITER_INITIALIZER; uint8_t reserved[64]={0}; uint64_t n=0u; odm_status st;
    st=odm_wire_writer_init(&w,payload,ODM_STUDIO_REVISION_PAYLOAD_BYTES); if(st!=ODM_STATUS_OK)return st;
#define W(x) do{st=(x);if(st!=ODM_STATUS_OK)return st;}while(0)
    W(odm_wire_write_bytes(&w,REV_MAGIC,8u)); W(odm_wire_write_u32(&w,i->schema_version_major)); W(odm_wire_write_u32(&w,i->schema_version_minor));
    W(odm_wire_write_u64(&w,i->revision_number)); W(odm_wire_write_u32(&w,i->asset_count)); W(odm_wire_write_u32(&w,i->flags));
    W(odm_wire_write_u32(&w,i->width)); W(odm_wire_write_u32(&w,i->height)); W(odm_wire_write_i32(&w,i->fps)); W(odm_wire_write_u32(&w,i->reserved0));
    W(odm_wire_write_i64(&w,i->project_end_sample)); W(odm_wire_write_u64(&w,i->project_seed));
    W(odm_wire_write_bytes(&w,i->parent_revision_id.bytes,32u)); W(odm_wire_write_bytes(&w,i->package_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->score_sha256.bytes,32u));
    W(odm_wire_write_bytes(&w,i->capability_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->music_map_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->music_policy_sha256.bytes,32u));
    W(odm_wire_write_bytes(&w,i->render_ir_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->asset_index_sha256.bytes,32u));
    W(odm_wire_write_bytes(&w,i->package_content_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->project_id.bytes,32u));
    W(odm_wire_write_bytes(&w,i->package_signer_public_key,32u));
    W(odm_wire_write_bytes(&w,i->revision_id.bytes,32u)); W(odm_wire_write_bytes(&w,reserved,sizeof(reserved)));
#undef W
    st=odm_wire_writer_finish(&w,&n); if(st!=ODM_STATUS_OK)return st;
    return n==ODM_STUDIO_REVISION_PAYLOAD_BYTES?ODM_STATUS_OK:ODM_STATUS_INVARIANT_BROKEN;
}

static odm_status revision_semantic_validate(const odm_studio_revision_info *i) {
    odm_sha256_digest id; odm_timeline tl; odm_status st;
    if(!i) return ODM_STATUS_INVALID_ARGUMENT;
    if(i->schema_version_major!=ODM_STUDIO_REVISION_SCHEMA_MAJOR || i->schema_version_minor!=ODM_STUDIO_REVISION_SCHEMA_MINOR ||
       i->flags!=0u || i->reserved0!=0u || i->width<2u || i->height<2u || (i->width&1u)!=0u || (i->height&1u)!=0u ||
       !odm_time_fps_supported(i->fps) || i->project_end_sample<=0) return ODM_STATUS_INVALID_DATA;
    if((i->revision_number==0u&&!digest_zero(&i->parent_revision_id)) ||
       (i->revision_number!=0u&&digest_zero(&i->parent_revision_id))) return ODM_STATUS_INVALID_DATA;
    if(!digest_nonzero(&i->package_sha256)||!digest_nonzero(&i->score_sha256)||!digest_nonzero(&i->capability_sha256)||
       !digest_nonzero(&i->music_map_sha256)||!digest_nonzero(&i->music_policy_sha256)||!digest_nonzero(&i->render_ir_sha256)||
       !digest_nonzero(&i->asset_index_sha256)||!digest_nonzero(&i->package_content_sha256)||!digest_nonzero(&i->project_id)||
       !bytes_nonzero(i->package_signer_public_key,32u)||!digest_nonzero(&i->revision_id)) return ODM_STATUS_INVALID_DATA;
    st=odm_time_resolve_timeline(i->project_end_sample,i->fps,&tl); if(st!=ODM_STATUS_OK) return ODM_STATUS_INVALID_DATA;
    st=revision_id_compute(i,&id); if(st!=ODM_STATUS_OK)return st;
    return odm_sha256_equal(&id,&i->revision_id)?ODM_STATUS_OK:ODM_STATUS_INTEGRITY_ERROR;
}

odm_status odm_studio_revision_validate(const uint8_t *record, uint64_t bytes,
                                        odm_studio_revision_info *out) {
    odm_wire_record_info wi; odm_wire_reader r=ODM_WIRE_READER_INITIALIZER; odm_studio_revision_info i;
    odm_sha256_digest record_sha; uint64_t off=0u; uint8_t magic[8],reserved[64]; uint32_t j; odm_status st;
    if(!record||!out) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_wire_record_read(record,bytes,&wi,&off,&record_sha); if(st!=ODM_STATUS_OK)return st;
    if(wi.kind!=ODM_STUDIO_REVISION_KIND||wi.schema_version_major!=ODM_STUDIO_REVISION_SCHEMA_MAJOR||
       wi.schema_version_minor!=ODM_STUDIO_REVISION_SCHEMA_MINOR||wi.payload_bytes!=ODM_STUDIO_REVISION_PAYLOAD_BYTES||
       bytes!=ODM_STUDIO_REVISION_RECORD_BYTES) return ODM_STATUS_VERSION_MISMATCH;
    memset(&i,0,sizeof(i)); st=odm_wire_reader_init(&r,record+off,wi.payload_bytes); if(st!=ODM_STATUS_OK)return st;
#define R(x) do{st=(x);if(st!=ODM_STATUS_OK)return st;}while(0)
    R(odm_wire_read_bytes(&r,magic,8u)); if(memcmp(magic,REV_MAGIC,8u)!=0)return ODM_STATUS_INVALID_DATA;
    R(odm_wire_read_u32(&r,&i.schema_version_major)); R(odm_wire_read_u32(&r,&i.schema_version_minor)); R(odm_wire_read_u64(&r,&i.revision_number));
    R(odm_wire_read_u32(&r,&i.asset_count)); R(odm_wire_read_u32(&r,&i.flags)); R(odm_wire_read_u32(&r,&i.width)); R(odm_wire_read_u32(&r,&i.height));
    R(odm_wire_read_i32(&r,&i.fps)); R(odm_wire_read_u32(&r,&i.reserved0)); R(odm_wire_read_i64(&r,&i.project_end_sample)); R(odm_wire_read_u64(&r,&i.project_seed));
    R(odm_wire_read_bytes(&r,i.parent_revision_id.bytes,32u)); R(odm_wire_read_bytes(&r,i.package_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.score_sha256.bytes,32u));
    R(odm_wire_read_bytes(&r,i.capability_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.music_map_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.music_policy_sha256.bytes,32u));
    R(odm_wire_read_bytes(&r,i.render_ir_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.asset_index_sha256.bytes,32u));
    R(odm_wire_read_bytes(&r,i.package_content_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.project_id.bytes,32u));
    R(odm_wire_read_bytes(&r,i.package_signer_public_key,32u));
    R(odm_wire_read_bytes(&r,i.revision_id.bytes,32u)); R(odm_wire_read_bytes(&r,reserved,sizeof(reserved)));
#undef R
    st=odm_wire_reader_finish(&r); if(st!=ODM_STATUS_OK)return st;
    for(j=0u;j<(uint32_t)sizeof(reserved);++j) if(reserved[j]!=0u)return ODM_STATUS_INVALID_DATA;
    st=revision_semantic_validate(&i); if(st!=ODM_STATUS_OK)return st;
    i.record_sha256=record_sha; *out=i; return ODM_STATUS_OK;
}

static int same_semantics(const odm_studio_revision_info *a,const odm_studio_revision_info *b) {
    return a&&b&&a->asset_count==b->asset_count&&a->width==b->width&&a->height==b->height&&a->fps==b->fps&&
           a->project_end_sample==b->project_end_sample&&a->project_seed==b->project_seed&&
           odm_sha256_equal(&a->score_sha256,&b->score_sha256)&&odm_sha256_equal(&a->capability_sha256,&b->capability_sha256)&&
           odm_sha256_equal(&a->music_map_sha256,&b->music_map_sha256)&&odm_sha256_equal(&a->music_policy_sha256,&b->music_policy_sha256)&&
           odm_sha256_equal(&a->render_ir_sha256,&b->render_ir_sha256)&&odm_sha256_equal(&a->asset_index_sha256,&b->asset_index_sha256)&&
           odm_sha256_equal(&a->package_content_sha256,&b->package_content_sha256);
}

odm_status odm_studio_revision_freeze(const uint8_t *package_bytes,uint64_t package_size,const uint8_t expected_key[32],
                                      const odm_score_view *score,const uint8_t *parent,uint64_t parent_bytes,
                                      uint8_t *record,uint64_t capacity,uint64_t *out_required,odm_studio_revision_info *out_info) {
    odm_odparms_info pkg; odm_odparms_core_views views; odm_render_ir_info ir; odm_sha256_digest score_sha,cap_sha,asset_sha,content_sha,id,record_sha;
    odm_studio_revision_info i,parent_i; uint8_t payload[ODM_STUDIO_REVISION_PAYLOAD_BYTES]; odm_status st;
    if(!package_bytes||!expected_key||!score||!out_required||((parent==NULL)!=(parent_bytes==0u)))return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_odparms_verified_core_views(package_bytes,package_size,expected_key,&pkg,&views); if(st!=ODM_STATUS_OK)return st;
    st=odm_score_validate(score); if(st!=ODM_STATUS_OK)return st;
    st=odm_score_hash(score,&score_sha); if(st!=ODM_STATUS_OK)return st;
    st=odm_score_capability_hash(score,&cap_sha); if(st!=ODM_STATUS_OK)return st;
    st=odm_studio_asset_index_sha256(score,&asset_sha); if(st!=ODM_STATUS_OK)return st;
    st=odm_odparms_content_sha256(package_bytes,package_size,expected_key,&content_sha); if(st!=ODM_STATUS_OK)return st;
    if(!odm_sha256_equal(&score_sha,&pkg.score_sha256)||!odm_sha256_equal(&cap_sha,&pkg.capability_sha256))return ODM_STATUS_INTEGRITY_ERROR;
    st=odm_render_ir_validate(views.render_ir,views.render_ir_bytes,&pkg.score_sha256,&pkg.music_policy_sha256,&ir); if(st!=ODM_STATUS_OK)return st;
    if(!odm_sha256_equal(&ir.record_sha256,&pkg.render_ir_sha256)||!odm_sha256_equal(&ir.capability_sha256,&pkg.capability_sha256)||
       !odm_sha256_equal(&ir.music_map_sha256,&pkg.music_map_sha256)||!odm_sha256_equal(&ir.music_policy_sha256,&pkg.music_policy_sha256))return ODM_STATUS_INTEGRITY_ERROR;
    memset(&i,0,sizeof(i)); i.schema_version_major=1u; i.asset_count=score->header->resource_count; i.width=ir.width; i.height=ir.height; i.fps=ir.fps;
    i.project_end_sample=ir.project_end_sample; i.project_seed=ir.project_seed; i.package_sha256=pkg.package_sha256; i.score_sha256=pkg.score_sha256;
    i.capability_sha256=pkg.capability_sha256; i.music_map_sha256=pkg.music_map_sha256; i.music_policy_sha256=pkg.music_policy_sha256;
    i.render_ir_sha256=pkg.render_ir_sha256; i.asset_index_sha256=asset_sha; i.package_content_sha256=content_sha;
    memcpy(i.package_signer_public_key,pkg.signer_public_key,32u);
    if(parent){
        st=odm_studio_revision_validate(parent,parent_bytes,&parent_i); if(st!=ODM_STATUS_OK)return st;
        if(parent_i.revision_number==UINT64_MAX)return ODM_STATUS_OVERFLOW;
        if(memcmp(parent_i.package_signer_public_key,i.package_signer_public_key,32u)!=0)return ODM_STATUS_INTEGRITY_ERROR;
        i.revision_number=parent_i.revision_number+1u; i.parent_revision_id=parent_i.revision_id; i.project_id=parent_i.project_id;
        if(same_semantics(&i,&parent_i))return ODM_STATUS_INVALID_STATE;
    } else {
        st=project_id_compute(&i,&i.project_id); if(st!=ODM_STATUS_OK)return st;
    }
    st=revision_id_compute(&i,&id); if(st!=ODM_STATUS_OK)return st; i.revision_id=id;
    st=revision_payload_write(&i,payload); if(st!=ODM_STATUS_OK)return st;
    st=odm_wire_record_write(ODM_STUDIO_REVISION_KIND,1u,0u,payload,sizeof(payload),record,capacity,out_required,&record_sha); if(st!=ODM_STATUS_OK)return st;
    i.record_sha256=record_sha; if(out_info)*out_info=i; return ODM_STATUS_OK;
}

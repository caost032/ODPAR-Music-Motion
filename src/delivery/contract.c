#include "odm_delivery.h"
#include "odm_time.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static const uint8_t DESC_DOMAIN[] = "ODPAR_DELIVERY_DESCRIPTOR_V1";
static const uint8_t CONTRACT_DOMAIN[] = "ODPAR_DELIVERY_CONTRACT_V1";
static const uint8_t ARTIFACT_DOMAIN[] = "ODPAR_DELIVERY_ARTIFACT_V1";
static const uint8_t CONTRACT_MAGIC[8] = {'O','D','M','D','L','V','R','1'};
static const uint8_t ARTIFACT_MAGIC[8] = {'O','D','M','D','L','V','A','1'};

static void put_le32(uint8_t out[4], uint32_t v) {
    out[0]=(uint8_t)v; out[1]=(uint8_t)(v>>8u); out[2]=(uint8_t)(v>>16u); out[3]=(uint8_t)(v>>24u);
}
static void put_le64(uint8_t out[8], uint64_t v) {
    uint32_t i; for (i=0u;i<8u;++i) out[i]=(uint8_t)(v>>(8u*i));
}
static odm_status hash_u32(odm_sha256_context *c, uint32_t v) {
    uint8_t b[4]; put_le32(b,v); return odm_sha256_update(c,b,sizeof(b));
}
static odm_status hash_u64(odm_sha256_context *c, uint64_t v) {
    uint8_t b[8]; put_le64(b,v); return odm_sha256_update(c,b,sizeof(b));
}
static int digest_nonzero(const odm_sha256_digest *d) {
    uint32_t i; if (d==NULL) return 0; for(i=0u;i<32u;++i) if(d->bytes[i]!=0u) return 1; return 0;
}

odm_status odm_delivery_descriptor_sha256(uint32_t kind, const uint8_t *descriptor,
                                          uint32_t descriptor_bytes,
                                          odm_sha256_digest *out_sha256) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER; odm_status st; uint32_t i;
    if (descriptor==NULL || out_sha256==NULL || descriptor_bytes==0u ||
        descriptor_bytes>ODM_DELIVERY_DESCRIPTOR_MAX_BYTES ||
        kind<ODM_DELIVERY_DESCRIPTOR_VIDEO_ENCODER || kind>ODM_DELIVERY_DESCRIPTOR_SETTINGS)
        return ODM_STATUS_INVALID_ARGUMENT;
    for(i=0u;i<descriptor_bytes;++i)
        if(descriptor[i]<UINT8_C(0x20) || descriptor[i]>UINT8_C(0x7e)) return ODM_STATUS_INVALID_DATA;
    st=odm_sha256_init(&c); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,DESC_DOMAIN,sizeof(DESC_DOMAIN)); if(st!=ODM_STATUS_OK) return st;
    st=hash_u32(&c,kind); if(st!=ODM_STATUS_OK) return st;
    st=hash_u32(&c,descriptor_bytes); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,descriptor,descriptor_bytes); if(st!=ODM_STATUS_OK) return st;
    return odm_sha256_final(&c,out_sha256);
}

static odm_status contract_id_compute(const odm_delivery_contract_info *i, odm_sha256_digest *out) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER; odm_status st;
    if(i==NULL || out==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,CONTRACT_DOMAIN,sizeof(CONTRACT_DOMAIN)); if(st!=ODM_STATUS_OK) return st;
#define H32(v) do { st=hash_u32(&c,(uint32_t)(v)); if(st!=ODM_STATUS_OK) return st; } while(0)
#define H64(v) do { st=hash_u64(&c,(uint64_t)(v)); if(st!=ODM_STATUS_OK) return st; } while(0)
#define HD(v) do { st=odm_sha256_update(&c,(v).bytes,sizeof((v).bytes)); if(st!=ODM_STATUS_OK) return st; } while(0)
    H32(i->schema_version_major); H32(i->schema_version_minor); H32(i->container); H32(i->video_codec);
    H32(i->audio_codec); H32(i->color_profile); H32(i->pixel_format); H32(i->rate_control);
    H32(i->width); H32(i->height); H32((uint32_t)i->fps); H32(i->audio_sample_rate); H32(i->flags); H32(i->reserved0);
    H64(i->frame_count); H64((uint64_t)i->output_end_sample);
    HD(i->render_id); HD(i->render_receipt_sha256); HD(i->video_encoder_identity_sha256);
    HD(i->audio_encoder_identity_sha256); HD(i->encoder_settings_sha256);
#undef H32
#undef H64
#undef HD
    return odm_sha256_final(&c,out);
}

static odm_status contract_semantic_validate(const odm_delivery_contract_info *i) {
    odm_sha256_digest id; int64_t spf=0; odm_status st;
    if(i==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    if(i->schema_version_major!=ODM_DELIVERY_CONTRACT_SCHEMA_MAJOR || i->schema_version_minor!=ODM_DELIVERY_CONTRACT_SCHEMA_MINOR ||
       i->container!=ODM_DELIVERY_CONTAINER_MP4 || i->video_codec!=ODM_DELIVERY_VIDEO_H264 || i->audio_codec!=ODM_DELIVERY_AUDIO_AAC ||
       i->color_profile!=ODM_DELIVERY_COLOR_SDR_BT709 || i->pixel_format!=ODM_DELIVERY_PIXEL_YUV420P || i->rate_control!=ODM_DELIVERY_RATE_CONTROL_CRF ||
       i->width<2u || i->height<2u || i->width>ODM_DELIVERY_MAX_DIMENSION || i->height>ODM_DELIVERY_MAX_DIMENSION ||
       (i->width&1u)!=0u || (i->height&1u)!=0u || !odm_time_fps_supported(i->fps) || i->audio_sample_rate!=ODM_DELIVERY_AUDIO_SAMPLE_RATE ||
       i->flags!=0u || i->reserved0!=0u || i->frame_count==0u || i->output_end_sample<=0 ||
       !digest_nonzero(&i->render_id) || !digest_nonzero(&i->render_receipt_sha256) ||
       !digest_nonzero(&i->video_encoder_identity_sha256) || !digest_nonzero(&i->audio_encoder_identity_sha256) ||
       !digest_nonzero(&i->encoder_settings_sha256) || !digest_nonzero(&i->delivery_contract_id)) return ODM_STATUS_INVALID_DATA;
    st=odm_time_samples_per_frame(i->fps,&spf); if(st!=ODM_STATUS_OK || spf<=0) return ODM_STATUS_INVALID_DATA;
    if(i->frame_count>(uint64_t)INT64_MAX/(uint64_t)spf) return ODM_STATUS_INVALID_DATA;
    if((int64_t)(i->frame_count*(uint64_t)spf)!=i->output_end_sample) return ODM_STATUS_INVALID_DATA;
    st=contract_id_compute(i,&id); if(st!=ODM_STATUS_OK) return st;
    return odm_sha256_equal(&id,&i->delivery_contract_id)?ODM_STATUS_OK:ODM_STATUS_INTEGRITY_ERROR;
}

static odm_status contract_payload_write(const odm_delivery_contract_info *i,
                                         uint8_t p[ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES]) {
    odm_wire_writer w=ODM_WIRE_WRITER_INITIALIZER; uint8_t rz[48]={0}; uint64_t n=0u; odm_status st;
    st=odm_wire_writer_init(&w,p,ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES); if(st!=ODM_STATUS_OK) return st;
#define W(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    W(odm_wire_write_bytes(&w,CONTRACT_MAGIC,sizeof(CONTRACT_MAGIC)));
    W(odm_wire_write_u32(&w,i->schema_version_major)); W(odm_wire_write_u32(&w,i->schema_version_minor));
    W(odm_wire_write_u32(&w,i->container)); W(odm_wire_write_u32(&w,i->video_codec)); W(odm_wire_write_u32(&w,i->audio_codec));
    W(odm_wire_write_u32(&w,i->color_profile)); W(odm_wire_write_u32(&w,i->pixel_format)); W(odm_wire_write_u32(&w,i->rate_control));
    W(odm_wire_write_u32(&w,i->width)); W(odm_wire_write_u32(&w,i->height)); W(odm_wire_write_i32(&w,i->fps));
    W(odm_wire_write_u32(&w,i->audio_sample_rate)); W(odm_wire_write_u32(&w,i->flags)); W(odm_wire_write_u32(&w,i->reserved0));
    W(odm_wire_write_u64(&w,i->frame_count)); W(odm_wire_write_i64(&w,i->output_end_sample));
    W(odm_wire_write_bytes(&w,i->render_id.bytes,32u)); W(odm_wire_write_bytes(&w,i->render_receipt_sha256.bytes,32u));
    W(odm_wire_write_bytes(&w,i->video_encoder_identity_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->audio_encoder_identity_sha256.bytes,32u));
    W(odm_wire_write_bytes(&w,i->encoder_settings_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i->delivery_contract_id.bytes,32u));
    W(odm_wire_write_bytes(&w,rz,sizeof(rz)));
#undef W
    st=odm_wire_writer_finish(&w,&n); if(st!=ODM_STATUS_OK) return st;
    return n==ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES?ODM_STATUS_OK:ODM_STATUS_INVARIANT_BROKEN;
}

odm_status odm_delivery_contract_write(const uint8_t *receipt, uint64_t receipt_bytes,
                                       const odm_sha256_digest *video_id, const odm_sha256_digest *audio_id,
                                       const odm_sha256_digest *settings_id, uint8_t *record, uint64_t cap,
                                       uint64_t *out_required, odm_delivery_contract_info *out) {
    odm_render_receipt_info ri; odm_delivery_contract_info i; uint8_t payload[ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES];
    odm_sha256_digest id,sha; odm_status st;
    if(receipt==NULL || video_id==NULL || audio_id==NULL || settings_id==NULL || out_required==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_render_receipt_validate(receipt,receipt_bytes,NULL,&ri); if(st!=ODM_STATUS_OK) return st;
    if(!digest_nonzero(video_id)||!digest_nonzero(audio_id)||!digest_nonzero(settings_id)) return ODM_STATUS_INVALID_DATA;
    memset(&i,0,sizeof(i)); i.schema_version_major=1u; i.container=ODM_DELIVERY_CONTAINER_MP4; i.video_codec=ODM_DELIVERY_VIDEO_H264;
    i.audio_codec=ODM_DELIVERY_AUDIO_AAC; i.color_profile=ODM_DELIVERY_COLOR_SDR_BT709; i.pixel_format=ODM_DELIVERY_PIXEL_YUV420P;
    i.rate_control=ODM_DELIVERY_RATE_CONTROL_CRF; i.width=ri.width; i.height=ri.height; i.fps=ri.fps; i.audio_sample_rate=ODM_DELIVERY_AUDIO_SAMPLE_RATE;
    i.frame_count=ri.frame_count; i.output_end_sample=ri.output_end_sample; i.render_id=ri.render_id; i.render_receipt_sha256=ri.receipt_sha256;
    i.video_encoder_identity_sha256=*video_id; i.audio_encoder_identity_sha256=*audio_id; i.encoder_settings_sha256=*settings_id;
    st=contract_id_compute(&i,&id); if(st!=ODM_STATUS_OK) return st; i.delivery_contract_id=id;
    st=contract_semantic_validate(&i); if(st!=ODM_STATUS_OK) return st;
    st=contract_payload_write(&i,payload); if(st!=ODM_STATUS_OK) return st;
    st=odm_wire_record_write(ODM_DELIVERY_CONTRACT_KIND,1u,0u,payload,sizeof(payload),record,cap,out_required,&sha); if(st!=ODM_STATUS_OK) return st;
    i.record_sha256=sha; if(out!=NULL) *out=i; return ODM_STATUS_OK;
}

odm_status odm_delivery_contract_validate(const uint8_t *record, uint64_t bytes,
                                          const uint8_t *receipt, uint64_t receipt_bytes,
                                          odm_delivery_contract_info *out) {
    odm_wire_record_info wi; odm_wire_reader r=ODM_WIRE_READER_INITIALIZER; odm_delivery_contract_info i;
    odm_sha256_digest sha; odm_render_receipt_info ri; uint64_t off=0u; uint8_t magic[8],rz[48]; uint32_t j; odm_status st;
    if(record==NULL || out==NULL || ((receipt==NULL)!=(receipt_bytes==0u))) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_wire_record_read(record,bytes,&wi,&off,&sha); if(st!=ODM_STATUS_OK) return st;
    if(wi.kind!=ODM_DELIVERY_CONTRACT_KIND || wi.schema_version_major!=1u || wi.schema_version_minor!=0u ||
       wi.payload_bytes!=ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES || bytes!=ODM_DELIVERY_CONTRACT_RECORD_BYTES) return ODM_STATUS_VERSION_MISMATCH;
    memset(&i,0,sizeof(i)); st=odm_wire_reader_init(&r,record+off,wi.payload_bytes); if(st!=ODM_STATUS_OK) return st;
#define R(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    R(odm_wire_read_bytes(&r,magic,sizeof(magic))); if(memcmp(magic,CONTRACT_MAGIC,sizeof(magic))!=0) return ODM_STATUS_INVALID_DATA;
    R(odm_wire_read_u32(&r,&i.schema_version_major)); R(odm_wire_read_u32(&r,&i.schema_version_minor)); R(odm_wire_read_u32(&r,&i.container));
    R(odm_wire_read_u32(&r,&i.video_codec)); R(odm_wire_read_u32(&r,&i.audio_codec)); R(odm_wire_read_u32(&r,&i.color_profile));
    R(odm_wire_read_u32(&r,&i.pixel_format)); R(odm_wire_read_u32(&r,&i.rate_control)); R(odm_wire_read_u32(&r,&i.width)); R(odm_wire_read_u32(&r,&i.height));
    R(odm_wire_read_i32(&r,&i.fps)); R(odm_wire_read_u32(&r,&i.audio_sample_rate)); R(odm_wire_read_u32(&r,&i.flags)); R(odm_wire_read_u32(&r,&i.reserved0));
    R(odm_wire_read_u64(&r,&i.frame_count)); R(odm_wire_read_i64(&r,&i.output_end_sample)); R(odm_wire_read_bytes(&r,i.render_id.bytes,32u));
    R(odm_wire_read_bytes(&r,i.render_receipt_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.video_encoder_identity_sha256.bytes,32u));
    R(odm_wire_read_bytes(&r,i.audio_encoder_identity_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.encoder_settings_sha256.bytes,32u));
    R(odm_wire_read_bytes(&r,i.delivery_contract_id.bytes,32u)); R(odm_wire_read_bytes(&r,rz,sizeof(rz)));
#undef R
    st=odm_wire_reader_finish(&r); if(st!=ODM_STATUS_OK) return st;
    for(j=0u;j<(uint32_t)sizeof(rz);++j) if(rz[j]!=0u) return ODM_STATUS_INVALID_DATA;
    st=contract_semantic_validate(&i); if(st!=ODM_STATUS_OK) return st;
    if(receipt!=NULL) {
        st=odm_render_receipt_validate(receipt,receipt_bytes,&i.render_id,&ri); if(st!=ODM_STATUS_OK) return st;
        if(!odm_sha256_equal(&ri.receipt_sha256,&i.render_receipt_sha256) || ri.width!=i.width || ri.height!=i.height ||
           ri.fps!=i.fps || ri.frame_count!=i.frame_count || ri.output_end_sample!=i.output_end_sample) return ODM_STATUS_INTEGRITY_ERROR;
    }
    i.record_sha256=sha; *out=i; return ODM_STATUS_OK;
}

static odm_status artifact_id_compute(const odm_delivery_artifact_info *i, odm_sha256_digest *out) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER; odm_status st;
    if(i==NULL||out==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,ARTIFACT_DOMAIN,sizeof(ARTIFACT_DOMAIN)); if(st!=ODM_STATUS_OK) return st;
    st=hash_u32(&c,i->schema_version_major); if(st!=ODM_STATUS_OK) return st; st=hash_u32(&c,i->schema_version_minor); if(st!=ODM_STATUS_OK) return st;
    st=hash_u32(&c,i->flags); if(st!=ODM_STATUS_OK) return st; st=hash_u32(&c,i->reserved0); if(st!=ODM_STATUS_OK) return st;
    st=hash_u64(&c,i->file_bytes); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,i->delivery_contract_id.bytes,32u); if(st!=ODM_STATUS_OK) return st;
    st=odm_sha256_update(&c,i->file_sha256.bytes,32u); if(st!=ODM_STATUS_OK) return st;
    return odm_sha256_final(&c,out);
}
static odm_status artifact_semantic_validate(const odm_delivery_artifact_info *i) {
    odm_sha256_digest id; odm_status st; if(i==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    if(i->schema_version_major!=1u || i->schema_version_minor!=0u || i->flags!=0u || i->reserved0!=0u || i->file_bytes==0u ||
       !digest_nonzero(&i->delivery_contract_id)||!digest_nonzero(&i->file_sha256)||!digest_nonzero(&i->artifact_id)) return ODM_STATUS_INVALID_DATA;
    st=artifact_id_compute(i,&id); if(st!=ODM_STATUS_OK) return st;
    return odm_sha256_equal(&id,&i->artifact_id)?ODM_STATUS_OK:ODM_STATUS_INTEGRITY_ERROR;
}

odm_status odm_delivery_artifact_write(const uint8_t *contract, uint64_t contract_bytes,
                                       const odm_sha256_digest *file_sha, uint64_t file_bytes,
                                       uint8_t *record, uint64_t cap, uint64_t *out_required,
                                       odm_delivery_artifact_info *out) {
    odm_delivery_contract_info ci; odm_delivery_artifact_info i; odm_wire_writer w=ODM_WIRE_WRITER_INITIALIZER;
    uint8_t p[ODM_DELIVERY_ARTIFACT_PAYLOAD_BYTES],rz[64]={0}; uint64_t n=0u; odm_sha256_digest id,sha; odm_status st;
    if(contract==NULL||file_sha==NULL||out_required==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_delivery_contract_validate(contract,contract_bytes,NULL,0u,&ci); if(st!=ODM_STATUS_OK) return st;
    if(!digest_nonzero(file_sha)||file_bytes==0u) return ODM_STATUS_INVALID_DATA;
    memset(&i,0,sizeof(i)); i.schema_version_major=1u; i.file_bytes=file_bytes; i.delivery_contract_id=ci.delivery_contract_id; i.file_sha256=*file_sha;
    st=artifact_id_compute(&i,&id); if(st!=ODM_STATUS_OK) return st; i.artifact_id=id;
    st=artifact_semantic_validate(&i); if(st!=ODM_STATUS_OK) return st;
    st=odm_wire_writer_init(&w,p,sizeof(p)); if(st!=ODM_STATUS_OK) return st;
#define W(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    W(odm_wire_write_bytes(&w,ARTIFACT_MAGIC,sizeof(ARTIFACT_MAGIC))); W(odm_wire_write_u32(&w,i.schema_version_major));
    W(odm_wire_write_u32(&w,i.schema_version_minor)); W(odm_wire_write_u32(&w,i.flags)); W(odm_wire_write_u32(&w,i.reserved0));
    W(odm_wire_write_u64(&w,i.file_bytes)); W(odm_wire_write_bytes(&w,i.delivery_contract_id.bytes,32u));
    W(odm_wire_write_bytes(&w,i.file_sha256.bytes,32u)); W(odm_wire_write_bytes(&w,i.artifact_id.bytes,32u)); W(odm_wire_write_bytes(&w,rz,sizeof(rz)));
#undef W
    st=odm_wire_writer_finish(&w,&n); if(st!=ODM_STATUS_OK) return st; if(n!=sizeof(p)) return ODM_STATUS_INVARIANT_BROKEN;
    st=odm_wire_record_write(ODM_DELIVERY_ARTIFACT_KIND,1u,0u,p,sizeof(p),record,cap,out_required,&sha); if(st!=ODM_STATUS_OK) return st;
    i.record_sha256=sha; if(out!=NULL) *out=i; return ODM_STATUS_OK;
}

odm_status odm_delivery_artifact_validate(const uint8_t *record, uint64_t bytes,
                                          const odm_sha256_digest *expected,
                                          odm_delivery_artifact_info *out) {
    odm_wire_record_info wi; odm_wire_reader r=ODM_WIRE_READER_INITIALIZER; odm_delivery_artifact_info i; odm_sha256_digest sha;
    uint64_t off=0u; uint8_t magic[8],rz[64]; uint32_t j; odm_status st;
    if(record==NULL||out==NULL) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_wire_record_read(record,bytes,&wi,&off,&sha); if(st!=ODM_STATUS_OK) return st;
    if(wi.kind!=ODM_DELIVERY_ARTIFACT_KIND || wi.schema_version_major!=1u || wi.schema_version_minor!=0u ||
       wi.payload_bytes!=ODM_DELIVERY_ARTIFACT_PAYLOAD_BYTES || bytes!=ODM_DELIVERY_ARTIFACT_RECORD_BYTES) return ODM_STATUS_VERSION_MISMATCH;
    memset(&i,0,sizeof(i)); st=odm_wire_reader_init(&r,record+off,wi.payload_bytes); if(st!=ODM_STATUS_OK) return st;
#define R(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    R(odm_wire_read_bytes(&r,magic,sizeof(magic))); if(memcmp(magic,ARTIFACT_MAGIC,sizeof(magic))!=0) return ODM_STATUS_INVALID_DATA;
    R(odm_wire_read_u32(&r,&i.schema_version_major)); R(odm_wire_read_u32(&r,&i.schema_version_minor)); R(odm_wire_read_u32(&r,&i.flags));
    R(odm_wire_read_u32(&r,&i.reserved0)); R(odm_wire_read_u64(&r,&i.file_bytes)); R(odm_wire_read_bytes(&r,i.delivery_contract_id.bytes,32u));
    R(odm_wire_read_bytes(&r,i.file_sha256.bytes,32u)); R(odm_wire_read_bytes(&r,i.artifact_id.bytes,32u)); R(odm_wire_read_bytes(&r,rz,sizeof(rz)));
#undef R
    st=odm_wire_reader_finish(&r); if(st!=ODM_STATUS_OK) return st;
    for(j=0u;j<(uint32_t)sizeof(rz);++j) if(rz[j]!=0u) return ODM_STATUS_INVALID_DATA;
    st=artifact_semantic_validate(&i); if(st!=ODM_STATUS_OK) return st;
    if(expected!=NULL && !odm_sha256_equal(expected,&i.delivery_contract_id)) return ODM_STATUS_INTEGRITY_ERROR;
    i.record_sha256=sha; *out=i; return ODM_STATUS_OK;
}

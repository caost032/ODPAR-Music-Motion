#include "test_harness.h"
#include "master_internal.h"
#include "odm_delivery.h"
#include "odm_renderer.h"
#include "odm_wire.h"
#include <stdint.h>
#include <string.h>

static odm_status td_hash(const char *s, odm_sha256_digest *out) {
    return odm_sha256((const uint8_t *)s,(uint64_t)strlen(s),out);
}
static odm_status td_rewrap(uint32_t kind,uint32_t payload_bytes,uint8_t *r,uint64_t bytes) {
    uint8_t tmp[ODM_DELIVERY_CONTRACT_RECORD_BYTES]; uint64_t need=0u; odm_status st;
    if(r==NULL || bytes>sizeof(tmp) || bytes!=(uint64_t)ODM_WIRE_RECORD_HEADER_BYTES+payload_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_wire_record_write(kind,1u,0u,r+ODM_WIRE_RECORD_HEADER_BYTES,payload_bytes,tmp,sizeof(tmp),&need,NULL);
    if (st != ODM_STATUS_OK) return st;
    if (need != bytes) return ODM_STATUS_INVARIANT_BROKEN;
    memcpy(r, tmp, (size_t)bytes);
    return ODM_STATUS_OK;
}
static odm_status td_receipt(uint8_t r[ODM_RENDER_RECEIPT_RECORD_BYTES],odm_render_receipt_info *out) {
    odm_master_plan p; odm_render_receipt_info i; uint64_t need=0u; odm_sha256_digest sha; odm_status st;
    memset(&p,0,sizeof(p)); p.schema_version_major=1u; p.output_profile=ODM_MASTER_PROFILE_REFERENCE_V1;
    p.pixel_format=ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL; p.audio_format=ODM_MASTER_AUDIO_PCM_Q31_STEREO_LE;
    p.work_schema=ODM_MASTER_WORK_SCHEMA_OUTPUT_VOLUME_V1; p.renderer_version_major=ODM_REFERENCE_RENDERER_VERSION_MAJOR;
    p.renderer_version_minor=ODM_REFERENCE_RENDERER_VERSION_MINOR; p.width=4u; p.height=4u; p.fps=30; p.frame_count=2u;
    p.samples_per_frame=1600; p.project_end_sample=3200; p.output_end_sample=3200; p.canonical_music_frames=1600u; p.project_seed=UINT64_C(0x1122334455667788);
#define D(f,s) do{st=td_hash((s),&p.f);if(st!=ODM_STATUS_OK)return st;}while(0)
    D(package_sha256,"g11-package"); D(score_sha256,"g11-score"); D(capability_sha256,"g11-cap"); D(music_map_sha256,"g11-map");
    D(music_policy_sha256,"g11-policy"); D(render_ir_sha256,"g11-ir"); D(assets_sha256,"g11-assets"); D(canonical_pcm_sha256,"g11-pcm");
#undef D
    st=odm_master_render_id_internal(&p,&p.render_id); if(st!=ODM_STATUS_OK)return st;
    memset(&i,0,sizeof(i)); i.schema_version_major=1u; i.output_profile=p.output_profile; i.pixel_format=p.pixel_format; i.audio_format=p.audio_format;
    i.work_schema=p.work_schema; i.renderer_version_major=p.renderer_version_major; i.renderer_version_minor=p.renderer_version_minor;
    i.width=p.width; i.height=p.height; i.fps=p.fps; i.frame_count=p.frame_count; i.samples_per_frame=p.samples_per_frame;
    i.project_end_sample=p.project_end_sample; i.output_end_sample=p.output_end_sample; i.canonical_music_frames=p.canonical_music_frames;
    i.quoted_work_units=3232u; i.observed_work_units=3232u; i.project_seed=p.project_seed; i.package_sha256=p.package_sha256;
    i.score_sha256=p.score_sha256; i.capability_sha256=p.capability_sha256; i.music_map_sha256=p.music_map_sha256; i.music_policy_sha256=p.music_policy_sha256;
    i.render_ir_sha256=p.render_ir_sha256; i.assets_sha256=p.assets_sha256; i.canonical_pcm_sha256=p.canonical_pcm_sha256; i.render_id=p.render_id;
    st=td_hash("g11-soundtrack",&i.soundtrack_sha256); if(st!=ODM_STATUS_OK)return st; st=td_hash("g11-root",&i.frame_root_sha256); if(st!=ODM_STATUS_OK)return st;
    st=odm_render_receipt_write_internal(&i,r,ODM_RENDER_RECEIPT_RECORD_BYTES,&need,&sha); if(st!=ODM_STATUS_OK)return st;
    if (need != ODM_RENDER_RECEIPT_RECORD_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    i.receipt_sha256 = sha;
    *out = i;
    return ODM_STATUS_OK;
}

void odm_test_delivery(odm_test_context *c) {
    static const uint8_t vd[]="ffmpeg/libx264", ad[]="ffmpeg/aac";
    static const uint8_t sd[]="mp4;h264;yuv420p;bt709;crf=18;aac=192k;ar=48000";
    static const uint8_t sd2[]="mp4;h264;yuv420p;bt709;crf=19;aac=192k;ar=48000";
    uint8_t receipt[ODM_RENDER_RECEIPT_RECORD_BYTES],contract[ODM_DELIVERY_CONTRACT_RECORD_BYTES],contract2[ODM_DELIVERY_CONTRACT_RECORD_BYTES];
    uint8_t bad[ODM_DELIVERY_CONTRACT_RECORD_BYTES],artifact[ODM_DELIVERY_ARTIFACT_RECORD_BYTES],abad[ODM_DELIVERY_ARTIFACT_RECORD_BYTES];
    odm_render_receipt_info ri; odm_delivery_contract_info ci,ci2; odm_delivery_artifact_info ai; odm_sha256_digest v,a,s,s2,file; uint64_t need=0u; odm_status st;
    memset(&ri,0,sizeof(ri)); memset(&ci,0,sizeof(ci)); memset(&ci2,0,sizeof(ci2)); memset(&ai,0,sizeof(ai));
    ODM_TEST_CHECK(c,td_receipt(receipt,&ri)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_render_receipt_validate(receipt,sizeof(receipt),NULL,&ri)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(1u,vd,(uint32_t)(sizeof(vd)-1u),&v)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(2u,ad,(uint32_t)(sizeof(ad)-1u),&a)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(3u,sd,(uint32_t)(sizeof(sd)-1u),&s)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(3u,sd2,(uint32_t)(sizeof(sd2)-1u),&s2)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(0u,vd,(uint32_t)(sizeof(vd)-1u),&file)==ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(1u,vd,0u,&file)==ODM_STATUS_INVALID_ARGUMENT);
    { const uint8_t x[3]={'a',0u,'b'}; ODM_TEST_CHECK(c,odm_delivery_descriptor_sha256(3u,x,3u,&file)==ODM_STATUS_INVALID_DATA); }
    st=odm_delivery_contract_write(receipt,sizeof(receipt),&v,&a,&s,NULL,0u,&need,NULL); ODM_TEST_CHECK(c,st==ODM_STATUS_BUFFER_TOO_SMALL); ODM_TEST_CHECK(c,need==sizeof(contract));
    ODM_TEST_CHECK(c,odm_delivery_contract_write(receipt,sizeof(receipt),&v,&a,&s,contract,sizeof(contract),&need,&ci)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(contract,sizeof(contract),receipt,sizeof(receipt),&ci2)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(contract,sizeof(contract)-1u,NULL,0u,&ci2)==ODM_STATUS_INVALID_DATA);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(contract,sizeof(contract),receipt,sizeof(receipt)-1u,&ci2)==ODM_STATUS_INVALID_DATA);
    ODM_TEST_CHECK(c,ci2.width==4u&&ci2.height==4u&&ci2.fps==30&&ci2.frame_count==2u&&ci2.output_end_sample==3200);
    ODM_TEST_CHECK(c,odm_sha256_equal(&ci2.render_id,&ri.render_id)); ODM_TEST_CHECK(c,odm_sha256_equal(&ci2.render_receipt_sha256,&ri.receipt_sha256));
    ODM_TEST_CHECK(c,odm_delivery_contract_write(receipt,sizeof(receipt),&v,&a,&s2,contract2,sizeof(contract2),&need,&ci2)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,!odm_sha256_equal(&ci.delivery_contract_id,&ci2.delivery_contract_id)); ODM_TEST_CHECK(c,odm_sha256_equal(&ci.render_id,&ci2.render_id));
    memcpy(bad,contract,sizeof(bad)); bad[ODM_WIRE_RECORD_HEADER_BYTES+40u]=5u; ODM_TEST_CHECK(c,td_rewrap(ODM_DELIVERY_CONTRACT_KIND,ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES,bad,sizeof(bad))==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(bad,sizeof(bad),NULL,0u,&ci2)==ODM_STATUS_INVALID_DATA);
    memcpy(bad,contract,sizeof(bad)); bad[ODM_WIRE_RECORD_HEADER_BYTES+272u]=1u; ODM_TEST_CHECK(c,td_rewrap(ODM_DELIVERY_CONTRACT_KIND,ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES,bad,sizeof(bad))==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(bad,sizeof(bad),NULL,0u,&ci2)==ODM_STATUS_INVALID_DATA);
    memcpy(bad,contract,sizeof(bad)); bad[ODM_WIRE_RECORD_HEADER_BYTES+52u]=0x44u; bad[ODM_WIRE_RECORD_HEADER_BYTES+53u]=0xacu; bad[ODM_WIRE_RECORD_HEADER_BYTES+54u]=0u; bad[ODM_WIRE_RECORD_HEADER_BYTES+55u]=0u; ODM_TEST_CHECK(c,td_rewrap(ODM_DELIVERY_CONTRACT_KIND,ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES,bad,sizeof(bad))==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(bad,sizeof(bad),NULL,0u,&ci2)==ODM_STATUS_INVALID_DATA);
    memcpy(bad,contract,sizeof(bad)); bad[ODM_WIRE_RECORD_HEADER_BYTES+240u]^=1u; ODM_TEST_CHECK(c,td_rewrap(ODM_DELIVERY_CONTRACT_KIND,ODM_DELIVERY_CONTRACT_PAYLOAD_BYTES,bad,sizeof(bad))==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_contract_validate(bad,sizeof(bad),NULL,0u,&ci2)==ODM_STATUS_INTEGRITY_ERROR);
    ODM_TEST_CHECK(c,td_hash("g11-file",&file)==ODM_STATUS_OK); st=odm_delivery_artifact_write(contract,sizeof(contract),&file,12345u,NULL,0u,&need,NULL);
    ODM_TEST_CHECK(c,st==ODM_STATUS_BUFFER_TOO_SMALL&&need==sizeof(artifact));
    ODM_TEST_CHECK(c,odm_delivery_artifact_write(contract,sizeof(contract),&file,12345u,artifact,sizeof(artifact),&need,&ai)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_artifact_validate(artifact,sizeof(artifact),&ci.delivery_contract_id,&ai)==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,ai.file_bytes==12345u&&odm_sha256_equal(&ai.file_sha256,&file));
    ODM_TEST_CHECK(c,odm_delivery_artifact_validate(artifact,sizeof(artifact),&s,&ai)==ODM_STATUS_INTEGRITY_ERROR);
    memcpy(abad,artifact,sizeof(abad)); abad[ODM_WIRE_RECORD_HEADER_BYTES+24u]^=1u; ODM_TEST_CHECK(c,td_rewrap(ODM_DELIVERY_ARTIFACT_KIND,ODM_DELIVERY_ARTIFACT_PAYLOAD_BYTES,abad,sizeof(abad))==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_artifact_validate(abad,sizeof(abad),NULL,&ai)==ODM_STATUS_INTEGRITY_ERROR);
    memcpy(abad,artifact,sizeof(abad)); abad[ODM_WIRE_RECORD_HEADER_BYTES+128u]=1u; ODM_TEST_CHECK(c,td_rewrap(ODM_DELIVERY_ARTIFACT_KIND,ODM_DELIVERY_ARTIFACT_PAYLOAD_BYTES,abad,sizeof(abad))==ODM_STATUS_OK);
    ODM_TEST_CHECK(c,odm_delivery_artifact_validate(abad,sizeof(abad),NULL,&ai)==ODM_STATUS_INVALID_DATA);
    ODM_TEST_CHECK(c,odm_delivery_artifact_validate(artifact,sizeof(artifact)-1u,NULL,&ai)==ODM_STATUS_INVALID_DATA);
}

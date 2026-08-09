#include "test_harness.h"

#include "odm_renderer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void node_init(odm_score_node*n,uint32_t id,uint32_t scene,uint32_t role,uint32_t cap,uint32_t res,int64_t start,int64_t end,int32_t z){
    memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1u;n->resource_id=res;n->state_model=ODM_STATELESS;n->z_index=z;n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=id;
}
static uint16_t rd16(const uint8_t*p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}

void odm_test_renderer(odm_test_context *context){
    odm_score_header h={0};odm_score_resource rs[2];odm_score_act act={0};odm_score_scene scenes[2];odm_score_node nodes[5];odm_score_transition tr={0};odm_score_view score={0};odm_music_map_binding music={0};uint64_t ir_bytes=0u;uint8_t*ir=NULL;odm_render_ir_info iri;
    static const uint8_t ring[4u*4u*4u]={
        0,0,0,0, 255,220,120,255, 255,220,120,255, 0,0,0,0,
        255,220,120,255, 0,0,0,0, 0,0,0,0, 255,220,120,255,
        255,220,120,255, 0,0,0,0, 0,0,0,0, 255,220,120,255,
        0,0,0,0, 255,220,120,255, 255,220,120,255, 0,0,0,0
    };
    static const uint8_t vred[2u*2u*4u]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    static const uint8_t vblue[2u*2u*4u]={0,0,255,255,0,0,255,255,0,0,255,255,0,0,255,255};
    odm_render_surface_frame image_frame={0},video_frames[2];odm_render_resource_view assets_v[2];odm_render_asset_set assets={0};odm_sha256_digest assets_sha;
    uint64_t frame_bytes=0u,scratch_bytes=0u,required_scratch[6]={0};uint8_t*frame=NULL,*frame2=NULL,*scratch=NULL;odm_reference_frame_info fi[6],tmpinfo;odm_reference_frame_root_context root;odm_sha256_digest root_sha;uint32_t i;

    memset(rs,0,sizeof(rs));memset(scenes,0,sizeof(scenes));memset(nodes,0,sizeof(nodes));memset(video_frames,0,sizeof(video_frames));memset(assets_v,0,sizeof(assets_v));
    h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=8u;h.height=8u;h.project_end_sample=9600;h.project_seed=77u;h.resource_count=2u;h.act_count=1u;h.scene_count=2u;h.node_count=5u;h.transition_count=1u;
    rs[0].resource_id=1u;rs[0].kind=ODM_SCORE_RESOURCE_IMAGE;ODM_TEST_CHECK(context,odm_sha256("ring-source",11u,&rs[0].content_sha256)==ODM_STATUS_OK);
    rs[1].resource_id=2u;rs[1].kind=ODM_SCORE_RESOURCE_VIDEO;ODM_TEST_CHECK(context,odm_sha256("video-source",12u,&rs[1].content_sha256)==ODM_STATUS_OK);
    act.act_id=1u;act.end_sample=9600;
    scenes[0].scene_id=1u;scenes[0].act_id=1u;scenes[0].environment_node_id=1u;scenes[0].primary_node_id=2u;scenes[0].start_sample=0;scenes[0].end_sample=8000;
    scenes[1].scene_id=2u;scenes[1].act_id=1u;scenes[1].environment_node_id=4u;scenes[1].primary_node_id=5u;scenes[1].start_sample=4800;scenes[1].end_sample=9600;
    node_init(&nodes[0],1u,1u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0u,0,8000,-100);
    node_init(&nodes[1],2u,1u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1u,0,8000,0);
    node_init(&nodes[2],3u,1u,ODM_SCORE_NODE_SUBJECT,ODM_CAP_SUBJECT_IMAGE,1u,0,8000,10);nodes[2].scale_x_micro=500000;nodes[2].scale_y_micro=500000;nodes[2].opacity_micro=500000;nodes[2].phase_microturns=250000;
    node_init(&nodes[3],4u,2u,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0u,4800,9600,-100);
    node_init(&nodes[4],5u,2u,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_VIDEO,2u,4800,9600,0);
    tr.transition_id=1u;tr.from_scene_id=1u;tr.to_scene_id=2u;tr.kind=ODM_TRANSITION_CROSSFADE;tr.start_sample=4800;tr.end_sample=8000;
    score.header=&h;score.resources=rs;score.acts=&act;score.scenes=scenes;score.nodes=nodes;score.transitions=&tr;
    music.canonical_frames=9600u;ODM_TEST_CHECK(context,odm_sha256("render-map",10u,&music.music_map_sha256)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_music_policy_current_sha256(&music.music_policy_sha256)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_score_validate(&score)==ODM_STATUS_OK);
    ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&music,NULL,0u,&ir_bytes,NULL)==ODM_STATUS_BUFFER_TOO_SMALL);
    ir=(uint8_t*)malloc((size_t)ir_bytes);ODM_TEST_CHECK(context,ir!=NULL);if(!ir)return;
    ODM_TEST_CHECK(context,odm_score_compile_render_ir(&score,&music,ir,ir_bytes,&ir_bytes,NULL)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_render_ir_validate(ir,ir_bytes,NULL,&music.music_policy_sha256,&iri)==ODM_STATUS_OK);ODM_TEST_CHECK(context,iri.frame_count==6);

    image_frame.width=4u;image_frame.height=4u;image_frame.pixel_format=ODM_RENDER_SURFACE_RGBA8;image_frame.primaries=ODM_COLOR_PRIMARIES_BT709;image_frame.transfer=ODM_COLOR_TRANSFER_SRGB;image_frame.alpha_mode=ODM_ALPHA_STRAIGHT;image_frame.pixel_bytes=sizeof(ring);image_frame.pixels=ring;
    for(i=0u;i<2u;++i){video_frames[i].width=2u;video_frames[i].height=2u;video_frames[i].pixel_format=ODM_RENDER_SURFACE_RGBA8;video_frames[i].primaries=ODM_COLOR_PRIMARIES_BT709;video_frames[i].transfer=ODM_COLOR_TRANSFER_LINEAR;video_frames[i].alpha_mode=ODM_ALPHA_STRAIGHT;video_frames[i].start_sample=(int64_t)i*2400;video_frames[i].end_sample=(int64_t)(i+1u)*2400;video_frames[i].pixel_bytes=sizeof(vred);video_frames[i].pixels=i==0u?vred:vblue;}
    assets_v[0].resource_id=1u;assets_v[0].kind=ODM_SCORE_RESOURCE_IMAGE;assets_v[0].frame_count=1u;assets_v[0].source_sha256=rs[0].content_sha256;assets_v[0].frames=&image_frame;
    assets_v[1].resource_id=2u;assets_v[1].kind=ODM_SCORE_RESOURCE_VIDEO;assets_v[1].frame_count=2u;assets_v[1].source_sha256=rs[1].content_sha256;assets_v[1].frames=video_frames;
    assets.resource_count=2u;assets.resources=assets_v;ODM_TEST_CHECK(context,odm_render_asset_set_sha256(&assets,&assets_sha)==ODM_STATUS_OK);
    {odm_render_asset_set bad=assets;odm_render_resource_view br[2];memcpy(br,assets_v,sizeof(br));br[0]=assets_v[1];br[1]=assets_v[0];bad.resources=br;ODM_TEST_CHECK(context,odm_render_asset_set_sha256(&bad,&assets_sha)==ODM_STATUS_INVALID_DATA);}
    {odm_render_asset_set bad=assets;odm_render_resource_view br[2];odm_render_surface_frame bf=image_frame;memcpy(br,assets_v,sizeof(br));bf.transfer=99u;br[0].frames=&bf;bad.resources=br;ODM_TEST_CHECK(context,odm_render_asset_set_sha256(&bad,&assets_sha)==ODM_STATUS_INVALID_DATA);}
    {odm_render_asset_set bad=assets;odm_render_resource_view br[2];odm_render_surface_frame bf[2];memcpy(br,assets_v,sizeof(br));memcpy(bf,video_frames,sizeof(bf));bf[1].start_sample=2401;br[1].frames=bf;bad.resources=br;ODM_TEST_CHECK(context,odm_render_asset_set_sha256(&bad,&assets_sha)==ODM_STATUS_INVALID_DATA);}
    {odm_render_asset_set bad=assets;odm_render_resource_view br[2];odm_render_surface_frame bf=image_frame;memcpy(br,assets_v,sizeof(br));bf.pixel_bytes--;br[0].frames=&bf;bad.resources=br;ODM_TEST_CHECK(context,odm_render_asset_set_sha256(&bad,&assets_sha)==ODM_STATUS_INVALID_DATA);}
    ODM_TEST_CHECK(context,odm_render_asset_set_sha256(&assets,&assets_sha)==ODM_STATUS_OK);
    {uint64_t max_scratch=0u,max_frame=0u;for(i=0u;i<6u;++i){uint64_t fbi=0u,sbi=0u;ODM_TEST_CHECK(context,odm_reference_render_requirements(ir,ir_bytes,(int64_t)i,NULL,&fbi,&sbi)==ODM_STATUS_OK);ODM_TEST_CHECK(context,fbi==512u);required_scratch[i]=sbi;if(sbi>max_scratch)max_scratch=sbi;}ODM_TEST_CHECK(context,odm_reference_render_max_requirements(ir,ir_bytes,&max_frame,&scratch_bytes)==ODM_STATUS_OK);ODM_TEST_CHECK(context,max_frame==512u&&scratch_bytes>=max_scratch);frame_bytes=512u;ODM_TEST_CHECK(context,scratch_bytes>frame_bytes);}
    frame=(uint8_t*)malloc((size_t)frame_bytes);frame2=(uint8_t*)malloc((size_t)frame_bytes);scratch=(uint8_t*)malloc((size_t)scratch_bytes+ODM_REFERENCE_SCRATCH_ALIGNMENT);ODM_TEST_CHECK(context,frame&&frame2&&scratch);if(!frame||!frame2||!scratch)goto cleanup;
    for(i=0u;i<6u;++i){uint64_t fb=0u,sb=0u;ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,(int64_t)i,NULL,&assets,NULL,scratch,scratch_bytes,frame,frame_bytes,&fb,&sb,&fi[i])==ODM_STATUS_OK);ODM_TEST_CHECK(context,fb==frame_bytes&&sb==required_scratch[i]&&fi[i].frame_index==(int64_t)i&&fi[i].sample==(int64_t)i*1600);memset(frame2,0xa5,(size_t)frame_bytes);ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,(int64_t)i,NULL,&assets,NULL,scratch,scratch_bytes,frame2,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_OK);ODM_TEST_CHECK(context,memcmp(frame,frame2,(size_t)frame_bytes)==0&&odm_sha256_equal(&fi[i].frame_sha256,&tmpinfo.frame_sha256));if(i==5u){uint64_t center=((uint64_t)4u*8u+4u)*8u;ODM_TEST_CHECK(context,rd16(frame+center)==0u&&rd16(frame+center+4u)==UINT16_MAX&&rd16(frame+center+6u)==UINT16_MAX);}}
    ODM_TEST_CHECK(context,odm_reference_frame_root_init(&root,&iri,&assets_sha)==ODM_STATUS_OK);for(i=0u;i<6u;++i)ODM_TEST_CHECK(context,odm_reference_frame_root_update(&root,&fi[i])==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_reference_frame_root_final(&root,&root_sha)==ODM_STATUS_OK);{uint32_t nz=0u;for(i=0u;i<32u;++i)nz|=root_sha.bytes[i];ODM_TEST_CHECK(context,nz!=0u);}
    {odm_reference_frame_root_context bad;ODM_TEST_CHECK(context,odm_reference_frame_root_init(&bad,&iri,&assets_sha)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&fi[1])==ODM_STATUS_INTEGRITY_ERROR);ODM_TEST_CHECK(context,odm_reference_frame_root_final(&bad,&root_sha)==ODM_STATUS_INVALID_STATE);}
    {odm_reference_frame_root_context bad;odm_reference_frame_info forged=fi[0];ODM_TEST_CHECK(context,odm_reference_frame_root_init(&bad,&iri,&assets_sha)==ODM_STATUS_OK);forged.renderer_version_minor^=1u;ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);forged=fi[0];forged.assets_sha256.bytes[0]^=1u;ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);forged=fi[0];forged.sample+=1;ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);forged=fi[0];forged.pixel_bytes-=8u;ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);forged=fi[0];forged.reserved[2]=1u;ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&fi[0])==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_reference_frame_root_final(&bad,&root_sha)==ODM_STATUS_INVALID_STATE);}
    {odm_reference_frame_root_context bad;odm_reference_frame_info forged=fi[0];forged.sample++;ODM_TEST_CHECK(context,odm_reference_frame_root_init(&bad,&iri,&assets_sha)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);}
    {odm_reference_frame_root_context bad;odm_reference_frame_info forged=fi[0];forged.pixel_bytes--;ODM_TEST_CHECK(context,odm_reference_frame_root_init(&bad,&iri,&assets_sha)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_reference_frame_root_update(&bad,&forged)==ODM_STATUS_INTEGRITY_ERROR);}
    {odm_render_asset_set wrong=assets;odm_render_resource_view wr[2];uint64_t fb=0,sb=0;memcpy(wr,assets_v,sizeof(wr));wrong.resources=wr;wr[0].source_sha256.bytes[0]^=1u;memset(frame,0x5a,(size_t)frame_bytes);memset(&tmpinfo,0x6b,sizeof(tmpinfo));ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,0,NULL,&wrong,NULL,scratch,scratch_bytes,frame,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_INTEGRITY_ERROR);ODM_TEST_CHECK(context,frame[0]==0x5a&&((const uint8_t*)&tmpinfo)[0]==0x6b);}
    {odm_render_asset_set short_assets=assets;odm_render_resource_view wr[2];odm_render_surface_frame short_video[2];uint64_t fb=0,sb=0;memcpy(wr,assets_v,sizeof(wr));memcpy(short_video,video_frames,sizeof(short_video));short_video[1].end_sample=3000;wr[1].frames=short_video;short_assets.resources=wr;memset(frame,0x5c,(size_t)frame_bytes);memset(&tmpinfo,0x6d,sizeof(tmpinfo));ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,5,NULL,&short_assets,NULL,scratch,scratch_bytes,frame,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_INVALID_DATA);ODM_TEST_CHECK(context,frame[0]==0x5c&&((const uint8_t*)&tmpinfo)[0]==0x6d);}
    {uint64_t fb=0,sb=0;memset(frame,0x33,(size_t)frame_bytes);ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,0,NULL,&assets,NULL,scratch,required_scratch[0]-1u,frame,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_BUFFER_TOO_SMALL);ODM_TEST_CHECK(context,frame[0]==0x33);}
    {uint64_t fb=0,sb=0;memset(frame,0x44,(size_t)frame_bytes);ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,0,NULL,&assets,NULL,scratch+1u,scratch_bytes,frame,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_INVALID_ARGUMENT);ODM_TEST_CHECK(context,frame[0]==0x44);}
    {uint64_t fb=0,sb=0;memset(scratch,0x71,(size_t)scratch_bytes);memset(&tmpinfo,0x72,sizeof(tmpinfo));ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,0,NULL,&assets,NULL,scratch,scratch_bytes,scratch,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_INVALID_ARGUMENT);ODM_TEST_CHECK(context,scratch[0]==0x71&&((const uint8_t*)&tmpinfo)[0]==0x72);}
    {odm_job job;odm_job_ticket ticket;uint64_t fb=0,sb=0;ODM_TEST_CHECK(context,odm_job_init(&job)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_job_begin(&job,64u,&ticket)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_job_request_cancel(&job)==ODM_STATUS_OK);memset(frame,0x22,(size_t)frame_bytes);ODM_TEST_CHECK(context,odm_reference_render_frame(ir,ir_bytes,0,NULL,&assets,&ticket,scratch,scratch_bytes,frame,frame_bytes,&fb,&sb,&tmpinfo)==ODM_STATUS_CANCELLED);ODM_TEST_CHECK(context,frame[0]==0x22);ODM_TEST_CHECK(context,odm_job_finish(ticket,ODM_STATUS_CANCELLED)==ODM_STATUS_OK);ODM_TEST_CHECK(context,odm_job_destroy(&job)==ODM_STATUS_OK);}
cleanup:free(scratch);free(frame2);free(frame);free(ir);
}

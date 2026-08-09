#include "master_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t PCM_DOMAIN[] = {'O','D','P','A','R','_','P','C','M','_','Q','3','1','_','S','T','E','R','E','O','_','4','8','K','_','V','1',0};
static const uint8_t SOUNDTRACK_DOMAIN[] = {'O','D','P','A','R','_','M','A','S','T','E','R','_','S','O','U','N','D','T','R','A','C','K','_','V','1',0};

static void le32(uint8_t b[4], uint32_t v){b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);b[2]=(uint8_t)(v>>16);b[3]=(uint8_t)(v>>24);}
static void le64(uint8_t b[8], uint64_t v){uint32_t i;for(i=0u;i<8u;++i)b[i]=(uint8_t)(v>>(8u*i));}
static odm_status h64(odm_sha256_context*c,uint64_t v){uint8_t b[8];le64(b,v);return odm_sha256_update(c,b,8u);}
static int add64(uint64_t a,uint64_t b,uint64_t*out){if(!out||a>UINT64_MAX-b)return 0;*out=a+b;return 1;}
static int mul64(uint64_t a,uint64_t b,uint64_t*out){if(!out||(a!=0u&&b>UINT64_MAX/a))return 0;*out=a*b;return 1;}

odm_status odm_master_pcm_sha256(const odm_pcm_stereo_q31 *frames,uint64_t frame_count,odm_sha256_digest*out_sha256){
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;odm_status st;uint64_t i;uint8_t b[8];
    if(!out_sha256||(frame_count!=0u&&!frames))return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,PCM_DOMAIN,sizeof(PCM_DOMAIN));if(st!=ODM_STATUS_OK)return st;
    st=h64(&c,frame_count);if(st!=ODM_STATUS_OK)return st;
    for(i=0u;i<frame_count;++i){le32(b,(uint32_t)frames[i].left);le32(b+4u,(uint32_t)frames[i].right);st=odm_sha256_update(&c,b,8u);if(st!=ODM_STATUS_OK)return st;}
    return odm_sha256_final(&c,out_sha256);
}

static odm_status get_tick_for_frame(const odm_music_map_view*view,const odm_render_ir_info*ir,uint64_t frame_index,odm_music_analysis_tick*tick,const odm_music_analysis_tick**out){
    uint64_t sample,tick_index;if(!view||!ir||!tick||!out)return ODM_STATUS_INVALID_ARGUMENT;
    if(frame_index>UINT64_MAX/(uint64_t)ir->samples_per_frame)return ODM_STATUS_OVERFLOW;
    sample=frame_index*(uint64_t)ir->samples_per_frame;
    if(sample>=ir->canonical_music_frames){*out=NULL;return ODM_STATUS_OK;}tick_index=sample/(uint64_t)ODM_MUSIC_TICK_SAMPLES;
    if(tick_index>=view->tick_count)return ODM_STATUS_INVARIANT_BROKEN;
    {odm_status st=odm_music_map_view_read_tick(view,tick_index,tick);if(st!=ODM_STATUS_OK)return st;}*out=tick;return ODM_STATUS_OK;
}

odm_status odm_master_plan_build(const odm_master_request *request,odm_master_plan *out_plan){
    odm_odparms_info pkg;odm_odparms_core_views views;odm_music_map_view map;odm_render_ir_info ir;odm_music_map_binding binding;
    odm_sha256_digest score_sha,cap_sha,assets_sha,pcm_sha,compiled_sha;odm_master_plan plan;uint8_t*compiled=NULL;uint64_t compiled_bytes=0u;
    uint64_t frame_pixels,frame_bytes,render_bytes,audio_bytes,output_bytes,work,max_scratch=0u,max_frame_bytes=0u;odm_status st;
    if(!request||!out_plan||!request->package_bytes||!request->expected_package_public_key||!request->score||!request->assets||request->flags!=0u||request->output_profile!=ODM_MASTER_PROFILE_REFERENCE_V1||(request->canonical_pcm_frames!=0u&&!request->canonical_pcm))return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_odparms_verified_core_views(request->package_bytes,request->package_size,request->expected_package_public_key,&pkg,&views);if(st!=ODM_STATUS_OK)return st;
    st=odm_score_validate(request->score);if(st!=ODM_STATUS_OK)return st;st=odm_score_hash(request->score,&score_sha);if(st!=ODM_STATUS_OK)return st;st=odm_score_capability_hash(request->score,&cap_sha);if(st!=ODM_STATUS_OK)return st;
    if(!odm_sha256_equal(&score_sha,&pkg.score_sha256)||!odm_sha256_equal(&cap_sha,&pkg.capability_sha256))return ODM_STATUS_INTEGRITY_ERROR;
    st=odm_music_map_view_open(views.analysis_bin,views.analysis_bin_bytes,&pkg.music_policy_sha256,&map);if(st!=ODM_STATUS_OK)return st;
    if(!odm_sha256_equal(&map.info.record_sha256,&pkg.music_map_sha256))return ODM_STATUS_INTEGRITY_ERROR;
    st=odm_render_ir_validate(views.render_ir,views.render_ir_bytes,&pkg.score_sha256,&pkg.music_policy_sha256,&ir);if(st!=ODM_STATUS_OK)return st;
    if(!odm_sha256_equal(&ir.record_sha256,&pkg.render_ir_sha256)||!odm_sha256_equal(&ir.music_map_sha256,&pkg.music_map_sha256)||!odm_sha256_equal(&ir.capability_sha256,&pkg.capability_sha256)||ir.canonical_music_frames!=map.info.canonical_frames)return ODM_STATUS_INTEGRITY_ERROR;
    if(request->canonical_pcm_frames!=map.info.canonical_frames)return ODM_STATUS_INTEGRITY_ERROR;
    st=odm_master_pcm_sha256(request->canonical_pcm,request->canonical_pcm_frames,&pcm_sha);if(st!=ODM_STATUS_OK)return st;if(!odm_sha256_equal(&pcm_sha,&map.info.canonical_pcm_sha256))return ODM_STATUS_INTEGRITY_ERROR;
    binding.canonical_frames=map.info.canonical_frames;binding.music_map_sha256=map.info.record_sha256;binding.music_policy_sha256=map.info.policy_sha256;
    st=odm_score_compile_render_ir(request->score,&binding,NULL,0u,&compiled_bytes,NULL);if(st!=ODM_STATUS_BUFFER_TOO_SMALL)return st;if(compiled_bytes!=views.render_ir_bytes||compiled_bytes>(uint64_t)SIZE_MAX)return ODM_STATUS_INTEGRITY_ERROR;
    compiled=(uint8_t*)malloc((size_t)compiled_bytes);if(!compiled)return ODM_STATUS_OUT_OF_MEMORY;st=odm_score_compile_render_ir(request->score,&binding,compiled,compiled_bytes,&compiled_bytes,&compiled_sha);if(st!=ODM_STATUS_OK){free(compiled);return st;}
    if(memcmp(compiled,views.render_ir,(size_t)compiled_bytes)!=0||!odm_sha256_equal(&compiled_sha,&pkg.render_ir_sha256)){free(compiled);return ODM_STATUS_INTEGRITY_ERROR;}free(compiled);
    st=odm_render_asset_set_sha256(request->assets,&assets_sha);if(st!=ODM_STATUS_OK)return st;
    if(ir.frame_count<=0||ir.output_end_sample<=0||ir.samples_per_frame<=0)return ODM_STATUS_INVALID_DATA;
    if(!mul64((uint64_t)ir.width,(uint64_t)ir.height,&frame_pixels)||!mul64(frame_pixels,8u,&frame_bytes)||!mul64(frame_bytes,(uint64_t)ir.frame_count,&render_bytes)||!mul64((uint64_t)ir.output_end_sample,8u,&audio_bytes)||!add64(render_bytes,audio_bytes,&output_bytes)||!add64(output_bytes,ODM_RENDER_RECEIPT_RECORD_BYTES,&output_bytes)||!mul64(frame_pixels,(uint64_t)ir.frame_count,&work)||!add64(work,(uint64_t)ir.output_end_sample,&work))return ODM_STATUS_OVERFLOW;
    st=odm_reference_render_max_requirements(views.render_ir,views.render_ir_bytes,&max_frame_bytes,&max_scratch);if(st!=ODM_STATUS_OK)return st;if(max_frame_bytes!=frame_bytes)return ODM_STATUS_INVARIANT_BROKEN;
    memset(&plan,0,sizeof(plan));plan.schema_version_major=ODM_MASTER_SCHEMA_MAJOR;plan.schema_version_minor=ODM_MASTER_SCHEMA_MINOR;plan.output_profile=ODM_MASTER_PROFILE_REFERENCE_V1;plan.pixel_format=ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL;plan.audio_format=ODM_MASTER_AUDIO_PCM_Q31_STEREO_LE;plan.work_schema=ODM_MASTER_WORK_SCHEMA_OUTPUT_VOLUME_V1;plan.renderer_version_major=ODM_REFERENCE_RENDERER_VERSION_MAJOR;plan.renderer_version_minor=ODM_REFERENCE_RENDERER_VERSION_MINOR;plan.width=ir.width;plan.height=ir.height;plan.fps=ir.fps;plan.frame_count=(uint64_t)ir.frame_count;plan.samples_per_frame=ir.samples_per_frame;plan.project_end_sample=ir.project_end_sample;plan.output_end_sample=ir.output_end_sample;plan.canonical_music_frames=ir.canonical_music_frames;plan.frame_bytes=frame_bytes;plan.renderer_scratch_bytes=max_scratch;plan.quoted_work_units=work;plan.quoted_output_bytes=output_bytes;plan.project_seed=ir.project_seed;plan.package_sha256=pkg.package_sha256;plan.score_sha256=pkg.score_sha256;plan.capability_sha256=pkg.capability_sha256;plan.music_map_sha256=pkg.music_map_sha256;plan.music_policy_sha256=pkg.music_policy_sha256;plan.render_ir_sha256=pkg.render_ir_sha256;plan.assets_sha256=assets_sha;plan.canonical_pcm_sha256=pcm_sha;st=odm_master_render_id_internal(&plan,&plan.render_id);if(st!=ODM_STATUS_OK)return st;*out_plan=plan;return ODM_STATUS_OK;
}

static odm_status check_cancel(const odm_job_ticket*ticket){int c=0;odm_status st;if(!ticket)return ODM_STATUS_OK;st=odm_job_should_cancel(*ticket,&c);if(st!=ODM_STATUS_OK)return st;return c?ODM_STATUS_CANCELLED:ODM_STATUS_OK;}
static odm_status publish(const odm_job_ticket*ticket,odm_job_phase phase,uint64_t done){if(!ticket)return ODM_STATUS_OK;return odm_job_publish(*ticket,phase,done);}
static void abort_sink(const odm_master_sink*sink,int active,odm_status st){if(active&&sink&&sink->abort)sink->abort(sink->user,st);}

odm_status odm_master_run(const odm_master_request *request,const odm_master_sink *sink,uint8_t *frame_buffer,uint64_t frame_capacity,void *renderer_scratch,uint64_t renderer_scratch_capacity,uint8_t *receipt_buffer,uint64_t receipt_capacity,uint64_t *out_receipt_required,odm_render_receipt_info *out_receipt_info){
    odm_master_plan plan;odm_odparms_info pkg;odm_odparms_core_views views;odm_music_map_view map;odm_render_ir_info ir;odm_reference_frame_root_context root;odm_sha256_context audio_hash=ODM_SHA256_CONTEXT_INITIALIZER;odm_render_receipt_info receipt;odm_sha256_digest frame_root,soundtrack,receipt_sha;uint8_t receipt_local[ODM_RENDER_RECEIPT_RECORD_BYTES];uint8_t audio_chunk[ODM_MASTER_AUDIO_CHUNK_FRAMES*8u];uint64_t receipt_required=ODM_RENDER_RECEIPT_RECORD_BYTES,work_done=0u,pixels,fi,audio_pos=0u;odm_status st;int active=0;
    if(!out_receipt_required)return ODM_STATUS_INVALID_ARGUMENT;
    *out_receipt_required=receipt_required;
    if(!sink||!sink->begin||!sink->write_frame||!sink->write_audio||!sink->commit||!sink->abort)return ODM_STATUS_INVALID_ARGUMENT;
    if(!receipt_buffer||receipt_capacity<receipt_required)return ODM_STATUS_BUFFER_TOO_SMALL;
    st=odm_master_plan_build(request,&plan);if(st!=ODM_STATUS_OK)return st;if(!frame_buffer||frame_capacity<plan.frame_bytes||!renderer_scratch||renderer_scratch_capacity<plan.renderer_scratch_bytes)return ODM_STATUS_BUFFER_TOO_SMALL;
    if(request->job_ticket){odm_progress_snapshot snap;st=odm_job_snapshot(request->job_ticket->job,&snap);if(st!=ODM_STATUS_OK)return st;if(snap.generation!=request->job_ticket->generation||snap.state!=ODM_JOB_RUNNING||snap.work_total!=plan.quoted_work_units)return ODM_STATUS_INVALID_STATE;}
    st=check_cancel(request->job_ticket);if(st!=ODM_STATUS_OK)return st;st=publish(request->job_ticket,ODM_PHASE_VALIDATE,0u);if(st!=ODM_STATUS_OK)return st;
    st=odm_odparms_verified_core_views(request->package_bytes,request->package_size,request->expected_package_public_key,&pkg,&views);if(st!=ODM_STATUS_OK)return st;st=odm_music_map_view_open(views.analysis_bin,views.analysis_bin_bytes,&pkg.music_policy_sha256,&map);if(st!=ODM_STATUS_OK)return st;st=odm_render_ir_validate(views.render_ir,views.render_ir_bytes,&pkg.score_sha256,&pkg.music_policy_sha256,&ir);if(st!=ODM_STATUS_OK)return st;
    st=odm_reference_frame_root_init(&root,&ir,&plan.assets_sha256);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_init(&audio_hash);if(st!=ODM_STATUS_OK)return st;st=odm_sha256_update(&audio_hash,SOUNDTRACK_DOMAIN,sizeof(SOUNDTRACK_DOMAIN));if(st!=ODM_STATUS_OK)return st;st=h64(&audio_hash,(uint64_t)plan.output_end_sample);if(st!=ODM_STATUS_OK)return st;
    st=sink->begin(sink->user,&plan);if(st!=ODM_STATUS_OK)return st;active=1;st=publish(request->job_ticket,ODM_PHASE_EXECUTE,0u);if(st!=ODM_STATUS_OK){abort_sink(sink,active,st);return st;}
    if(!mul64((uint64_t)plan.width,(uint64_t)plan.height,&pixels)){st=ODM_STATUS_OVERFLOW;goto fail;}
    for(fi=0u;fi<plan.frame_count;++fi){odm_music_analysis_tick tick;const odm_music_analysis_tick*tp=NULL;odm_reference_frame_info frame_info;uint64_t rf=0u,rs=0u;
        st=check_cancel(request->job_ticket);if(st!=ODM_STATUS_OK)goto fail;st=get_tick_for_frame(&map,&ir,fi,&tick,&tp);if(st!=ODM_STATUS_OK)goto fail;
        st=odm_reference_render_frame(views.render_ir,views.render_ir_bytes,(int64_t)fi,tp,request->assets,request->job_ticket,renderer_scratch,renderer_scratch_capacity,frame_buffer,frame_capacity,&rf,&rs,&frame_info);if(st!=ODM_STATUS_OK)goto fail;if(rf!=plan.frame_bytes||rs>plan.renderer_scratch_bytes){st=ODM_STATUS_INVARIANT_BROKEN;goto fail;}
        st=odm_reference_frame_root_update(&root,&frame_info);if(st!=ODM_STATUS_OK)goto fail;st=sink->write_frame(sink->user,&frame_info,frame_buffer,plan.frame_bytes);if(st!=ODM_STATUS_OK)goto fail;
        if(!add64(work_done,pixels,&work_done)){st=ODM_STATUS_OVERFLOW;goto fail;}st=publish(request->job_ticket,ODM_PHASE_EXECUTE,work_done);if(st!=ODM_STATUS_OK)goto fail;
    }
    st=odm_reference_frame_root_final(&root,&frame_root);if(st!=ODM_STATUS_OK)goto fail;
    while(audio_pos<(uint64_t)plan.output_end_sample){uint64_t remain=(uint64_t)plan.output_end_sample-audio_pos;uint64_t n=remain<ODM_MASTER_AUDIO_CHUNK_FRAMES?remain:ODM_MASTER_AUDIO_CHUNK_FRAMES;uint64_t j;
        st=check_cancel(request->job_ticket);if(st!=ODM_STATUS_OK)goto fail;
        for(j=0u;j<n;++j){uint64_t idx=audio_pos+j;int32_t l=0,r=0;uint8_t*b=audio_chunk+j*8u;if(idx<request->canonical_pcm_frames){l=request->canonical_pcm[idx].left;r=request->canonical_pcm[idx].right;}le32(b,(uint32_t)l);le32(b+4u,(uint32_t)r);}
        st=odm_sha256_update(&audio_hash,audio_chunk,n*8u);if(st!=ODM_STATUS_OK)goto fail;st=sink->write_audio(sink->user,audio_pos,audio_chunk,n);if(st!=ODM_STATUS_OK)goto fail;audio_pos+=n;if(!add64(work_done,n,&work_done)){st=ODM_STATUS_OVERFLOW;goto fail;}st=publish(request->job_ticket,ODM_PHASE_EXECUTE,work_done);if(st!=ODM_STATUS_OK)goto fail;
    }
    st=odm_sha256_final(&audio_hash,&soundtrack);if(st!=ODM_STATUS_OK)goto fail;if(work_done!=plan.quoted_work_units){st=ODM_STATUS_INVARIANT_BROKEN;goto fail;}
    memset(&receipt,0,sizeof(receipt));receipt.schema_version_major=ODM_RENDER_RECEIPT_SCHEMA_MAJOR;receipt.schema_version_minor=ODM_RENDER_RECEIPT_SCHEMA_MINOR;receipt.output_profile=plan.output_profile;receipt.pixel_format=plan.pixel_format;receipt.audio_format=plan.audio_format;receipt.work_schema=plan.work_schema;receipt.renderer_version_major=plan.renderer_version_major;receipt.renderer_version_minor=plan.renderer_version_minor;receipt.width=plan.width;receipt.height=plan.height;receipt.fps=plan.fps;receipt.frame_count=plan.frame_count;receipt.samples_per_frame=plan.samples_per_frame;receipt.project_end_sample=plan.project_end_sample;receipt.output_end_sample=plan.output_end_sample;receipt.canonical_music_frames=plan.canonical_music_frames;receipt.quoted_work_units=plan.quoted_work_units;receipt.observed_work_units=work_done;receipt.project_seed=plan.project_seed;receipt.package_sha256=plan.package_sha256;receipt.score_sha256=plan.score_sha256;receipt.capability_sha256=plan.capability_sha256;receipt.music_map_sha256=plan.music_map_sha256;receipt.music_policy_sha256=plan.music_policy_sha256;receipt.render_ir_sha256=plan.render_ir_sha256;receipt.assets_sha256=plan.assets_sha256;receipt.canonical_pcm_sha256=plan.canonical_pcm_sha256;receipt.soundtrack_sha256=soundtrack;receipt.frame_root_sha256=frame_root;receipt.render_id=plan.render_id;
    st=odm_render_receipt_write_internal(&receipt,receipt_local,sizeof(receipt_local),&receipt_required,&receipt_sha);if(st!=ODM_STATUS_OK)goto fail;receipt.receipt_sha256=receipt_sha;st=odm_render_receipt_validate(receipt_local,receipt_required,&plan.render_id,&receipt);if(st!=ODM_STATUS_OK)goto fail;
    st=publish(request->job_ticket,ODM_PHASE_FINALIZE,work_done);if(st!=ODM_STATUS_OK)goto fail;st=check_cancel(request->job_ticket);if(st!=ODM_STATUS_OK)goto fail;
    st=sink->commit(sink->user,receipt_local,receipt_required);if(st!=ODM_STATUS_OK)goto fail;active=0;memcpy(receipt_buffer,receipt_local,(size_t)receipt_required);*out_receipt_required=receipt_required;if(out_receipt_info)*out_receipt_info=receipt;return ODM_STATUS_OK;
fail:
    abort_sink(sink,active,st);return st;
}

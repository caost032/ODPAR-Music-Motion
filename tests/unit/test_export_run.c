#include "test_harness.h"
#include "odm_export.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t pixels[4u*4u*4u];
    int fail_frame;
    int fail_core_frame;
    int bad_tick;
    int mutate_frame;
    odm_layered_config *mutate_config;
    odm_export_recipe *mutate_recipe;
    odm_export_sink *mutate_sink;
    odm_pcm_stereo_q31 *mutate_pcm;
    int mutate_pcm_frame;
} run_provider_ctx;

typedef struct {
    uint64_t video_frames;
    uint64_t video_bytes;
    uint64_t audio_frames;
    uint64_t audio_bytes;
    int begun;
    int committed;
    int aborted;
    int fail_begin;
    int fail_commit;
    int fail_video_frame;
    int64_t fail_audio_start;
    int cancel_video_frame;
    int64_t cancel_audio_start;
    odm_job *cancel_job;
    odm_export_sink *mutate_sink;
    int mutate_sink_on_begin;
    odm_status last_abort_reason;
    char transcript[128];
    uint32_t transcript_len;
} run_sink_ctx;

static uint32_t qratio(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void run_config(odm_layered_config*c){
    memset(c,0,sizeof(*c));c->schema_version=1;c->canvas.schema_version=1;c->canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c->canvas.width=32;c->canvas.height=32;c->canvas.fps=30;
    c->background.schema_version=1;c->background.style=ODM_BACKGROUND_GRID;c->background.solid_color.a=65535;c->background.grid_color.r=65535;c->background.grid_color.g=65535;c->background.grid_color.b=65535;c->background.grid_color.a=65535;c->background.grid_spacing_q16=8u<<16;c->background.grid_line_q16=1u<<16;c->background.grid_feather_q16=1u<<16;c->background.opacity_q31=INT32_MAX;
    c->core.schema_version=1;c->core.shape=ODM_CORE_SHAPE_CIRCLE;c->core.fit=ODM_CORE_FIT_COVER;c->core.center_x_q31=qratio(1,2);c->core.center_y_q31=qratio(1,2);c->core.width_q31=qratio(1,2);c->core.height_q31=qratio(1,2);c->core.feather_q16=1u<<16;c->core.opacity_q31=INT32_MAX;
    c->field.schema_version=1;c->field.flags=ODM_FIELD_RADIAL_BARS|ODM_FIELD_PARTICLES;c->field.radial_segments=48;c->field.particle_count=8;c->field.ring_gap_q16=2u<<16;c->field.bar_min_q16=1u<<16;c->field.bar_max_q16=4u<<16;c->field.bar_width_q16=1u<<16;c->field.particle_radius_q16=1u<<16;c->field.field_opacity_q31=INT32_MAX;c->field.primary_color.r=65535;c->field.primary_color.g=65535;c->field.primary_color.b=65535;c->field.primary_color.a=65535;c->field.seed=7u;
    c->hud.schema_version=1;c->hud.flags=ODM_HUD_PROGRESS_BAR;c->hud.margin_q16=2u<<16;c->hud.progress_height_q16=1u<<16;c->hud.progress_width_q31=qratio(3,4);c->hud.text_scale_q16=1u<<16;c->hud.opacity_q31=INT32_MAX;c->hud.foreground_color.r=65535;c->hud.foreground_color.g=65535;c->hud.foreground_color.b=65535;c->hud.foreground_color.a=65535;
}
static void run_profile(odm_export_profile*p){memset(p,0,sizeof(*p));p->schema_version=1;p->adapter=1;p->container=1;p->video_codec=1;p->audio_codec=1;p->source_video_format=1;p->source_audio_format=1;p->output_pixel_format=1;p->color_primaries=1;p->color_transfer=1;p->color_matrix=1;p->color_range=1;p->rate_control=1;p->crf=18;p->preset=ODM_EXPORT_PRESET_VERYFAST;p->video_threads=1;p->audio_bitrate_kbps=128;p->flags=ODM_EXPORT_FLAG_FASTSTART;}

static odm_status state_provider(void*user,uint64_t frame_index,int64_t sample,odm_composition_frame_state*c,odm_director_frame_state*d){
    run_provider_ctx*x=(run_provider_ctx*)user;uint64_t ti=(uint64_t)sample/(uint64_t)ODM_MUSIC_TICK_SAMPLES;uint32_t i;
    if(x->fail_frame>=0 && frame_index==(uint64_t)x->fail_frame)return ODM_STATUS_INVALID_DATA;
    if(x->mutate_pcm && x->mutate_pcm_frame>=0 &&
       frame_index==(uint64_t)x->mutate_pcm_frame){
        x->mutate_pcm[0].left ^= INT32_C(0x01010101);
        x->mutate_pcm_frame=-1;
    }
    if(x->mutate_frame>=0 && frame_index==(uint64_t)x->mutate_frame){
        if(x->mutate_config)x->mutate_config->hud.flags=0u;
        if(x->mutate_recipe)x->mutate_recipe->frame_count=1u;
        if(x->mutate_sink)x->mutate_sink->write_video=NULL;
        x->mutate_frame=-1;
    }
    memset(c,0,sizeof(*c));memset(d,0,sizeof(*d));c->schema_version=ODM_COMPOSITION_SCHEMA_VERSION;c->mode=ODM_COMPOSITION_MODE_FLOW;c->tick_index=ti+(x->bad_tick?1u:0u);c->center_sample=c->tick_index*(uint64_t)ODM_MUSIC_TICK_SAMPLES;c->core_scale_q31=INT32_MAX;c->core_breath_q31=qratio(1,10);c->radial_gain_q31=qratio(1,2);c->grid_q31=qratio(1,2);c->particles_q31=qratio(1,2);c->ring_phase=(uint32_t)ti;for(i=0u;i<ODM_COMPOSITION_RADIAL_SEGMENTS;++i)c->radial_q31[i]=qratio(1,2);
    d->schema_version=ODM_DIRECTOR_SCHEMA_VERSION;d->layout=ODM_DIRECTOR_LAYOUT_ORBIT;d->previous_layout=ODM_DIRECTOR_LAYOUT_MONOLITH;d->tick_index=c->tick_index;d->depth_q31=qratio(1,3);d->edge_activity_q31=qratio(1,4);d->orbit_phase=(uint32_t)(ti*17u);return ODM_STATUS_OK;
}
static odm_status core_provider(void*user,uint64_t frame_index,int64_t sample,odm_render_surface_frame*s){
    run_provider_ctx*x=(run_provider_ctx*)user;if(x->fail_core_frame>=0&&frame_index==(uint64_t)x->fail_core_frame)return ODM_STATUS_INVALID_DATA;memset(s,0,sizeof(*s));s->width=4u;s->height=4u;s->pixel_format=ODM_RENDER_SURFACE_RGBA8;s->primaries=ODM_COLOR_PRIMARIES_BT709;s->transfer=ODM_COLOR_TRANSFER_SRGB;s->alpha_mode=ODM_ALPHA_STRAIGHT;s->start_sample=sample;s->end_sample=sample+1;s->pixel_bytes=sizeof(x->pixels);s->pixels=x->pixels;return ODM_STATUS_OK;
}
static odm_status transcript_add(run_sink_ctx *s, char event) {
    if (!s || s->transcript_len + 1u >= (uint32_t)sizeof(s->transcript))
        return ODM_STATUS_BUDGET_EXCEEDED;
    s->transcript[s->transcript_len++] = event;
    s->transcript[s->transcript_len] = '\0';
    return ODM_STATUS_OK;
}
static odm_status sbegin(void*u,const odm_export_recipe*r){run_sink_ctx*s=(run_sink_ctx*)u;odm_status ts;(void)r;ts=transcript_add(s,'B');if(ts!=ODM_STATUS_OK)return ts;s->begun++;if(s->mutate_sink_on_begin&&s->mutate_sink){s->mutate_sink->write_video=NULL;s->mutate_sink->commit=NULL;}return s->fail_begin?ODM_STATUS_INTERNAL_ERROR:ODM_STATUS_OK;}
static odm_status svideo(void*u,uint64_t fi,int64_t sample,const uint8_t*p,uint64_t n){run_sink_ctx*s=(run_sink_ctx*)u;odm_status st;(void)sample;st=transcript_add(s,'V');if(st!=ODM_STATUS_OK)return st;if(!p||fi!=s->video_frames)return ODM_STATUS_INVALID_DATA;if(s->fail_video_frame>=0&&fi==(uint64_t)s->fail_video_frame)return ODM_STATUS_INTERNAL_ERROR;s->video_frames++;s->video_bytes+=n;if(s->cancel_job&&s->cancel_video_frame>=0&&fi==(uint64_t)s->cancel_video_frame){st=odm_job_request_cancel(s->cancel_job);if(st!=ODM_STATUS_OK)return st;}return ODM_STATUS_OK;}
static odm_status saudio(void*u,uint64_t start,const uint8_t*p,uint64_t frames){run_sink_ctx*s=(run_sink_ctx*)u;odm_status st;st=transcript_add(s,'A');if(st!=ODM_STATUS_OK)return st;if(!p||start!=s->audio_frames)return ODM_STATUS_INVALID_DATA;if(s->fail_audio_start>=0&&start==(uint64_t)s->fail_audio_start)return ODM_STATUS_INTERNAL_ERROR;s->audio_frames+=frames;s->audio_bytes+=frames*8u;if(s->cancel_job&&s->cancel_audio_start>=0&&start==(uint64_t)s->cancel_audio_start){st=odm_job_request_cancel(s->cancel_job);if(st!=ODM_STATUS_OK)return st;}return ODM_STATUS_OK;}
static odm_status scommit(void*u,const uint8_t*r,uint64_t n){run_sink_ctx*s=(run_sink_ctx*)u;odm_status st=transcript_add(s,'C');if(st!=ODM_STATUS_OK)return st;if(!r||n!=ODM_EXPORT_RUN_RECEIPT_BYTES)return ODM_STATUS_INVALID_DATA;s->committed++;return s->fail_commit?ODM_STATUS_INTERNAL_ERROR:ODM_STATUS_OK;}
static void sabort(void*u,odm_status reason){run_sink_ctx*s=(run_sink_ctx*)u;(void)transcript_add(s,'X');s->last_abort_reason=reason;s->aborted++;}

static void sink_reset(run_sink_ctx*s){memset(s,0,sizeof(*s));s->fail_video_frame=-1;s->fail_audio_start=-1;s->cancel_video_frame=-1;s->cancel_audio_start=-1;s->last_abort_reason=ODM_STATUS_OK;}

void odm_test_export_run(odm_test_context*ctx){
    odm_layered_config c;odm_export_plan lp;odm_export_profile ep;odm_export_recipe er;odm_export_run_request rq;odm_export_sink sink;run_provider_ctx pc;run_sink_ctx sc;odm_pcm_stereo_q31*pcm=NULL;uint64_t fb=0,sb=0,work=0,rrn=0;uint8_t*frame=NULL,*scratch=NULL,receipt[ODM_EXPORT_RUN_RECEIPT_BYTES];odm_export_run_receipt rr,parsed;uint64_t i;
    run_config(&c);run_profile(&ep);ODM_TEST_CHECK(ctx,odm_export_plan_build(&c,4801,4801,ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,&lp)==ODM_STATUS_OK);ODM_TEST_CHECK(ctx,odm_export_recipe_build(&lp,&ep,&er)==ODM_STATUS_OK);
    memset(&pc,0,sizeof(pc));pc.fail_frame=-1;pc.fail_core_frame=-1;pc.mutate_frame=-1;pc.mutate_pcm_frame=-1;for(i=0;i<sizeof(pc.pixels);i+=4u){pc.pixels[i]=64u;pc.pixels[i+1u]=128u;pc.pixels[i+2u]=192u;pc.pixels[i+3u]=255u;}
    pcm=(odm_pcm_stereo_q31*)calloc((size_t)er.audio_source_frames,sizeof(*pcm));ODM_TEST_CHECK(ctx,pcm!=NULL);if(!pcm)return;for(i=0u;i<er.audio_source_frames;++i){pcm[i].left=(int32_t)i;pcm[i].right=-(int32_t)i;}
    memset(&rq,0,sizeof(rq));rq.schema_version=ODM_EXPORT_RUN_SCHEMA_VERSION;rq.config=&c;rq.recipe=&er;rq.state_provider=state_provider;rq.state_user=&pc;rq.core_provider=core_provider;rq.core_user=&pc;rq.canonical_pcm=pcm;rq.canonical_pcm_frames=er.audio_source_frames;
    memset(&sink,0,sizeof(sink));sink.user=&sc;sink.begin=sbegin;sink.write_video=svideo;sink.write_audio=saudio;sink.commit=scommit;sink.abort=sabort;
    ODM_TEST_CHECK(ctx,odm_export_run_requirements(&rq,&fb,&sb,&work)==ODM_STATUS_OK);ODM_TEST_CHECK(ctx,fb==4096u&&sb>0u&&work==er.frame_count+er.source_audio_total_frames);
    {
        odm_export_runtime_plan runtime_plan, before_plan;
        odm_layered_config rc = c;
        odm_export_plan rlp;
        odm_export_recipe rer;
        odm_export_run_request rrq = rq;
        memset(&runtime_plan,0xa5,sizeof(runtime_plan));
        before_plan=runtime_plan;
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rq,0u,&runtime_plan)==ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(ctx,memcmp(&runtime_plan,&before_plan,sizeof(runtime_plan))==0);
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rq,5u,&runtime_plan)==ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(ctx,memcmp(&runtime_plan,&before_plan,sizeof(runtime_plan))==0);
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rq,1u,&runtime_plan)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,runtime_plan.schema_version==ODM_EXPORT_RUNTIME_PLAN_SCHEMA_VERSION&&
                           runtime_plan.worker_budget==1u&&runtime_plan.selected_workers==1u&&
                           runtime_plan.recommended_flags==ODM_EXPORT_RUN_FLAG_DIRECT_STAGING&&
                           runtime_plan.frame_pixels==UINT64_C(1024)&&
                           runtime_plan.frame_bytes==fb&&runtime_plan.scratch_bytes==sb&&
                           runtime_plan.work_units==work);
        rq.flags=ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH;
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rq,4u,&runtime_plan)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,runtime_plan.selected_workers==1u&&
                           runtime_plan.recommended_flags==(ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH|ODM_EXPORT_RUN_FLAG_DIRECT_STAGING));
        rq.flags=0u;

        rc.canvas.width=128u;rc.canvas.height=128u;
        ODM_TEST_CHECK(ctx,odm_export_plan_build(&rc,4801,4801,ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,&rlp)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,odm_export_recipe_build(&rlp,&ep,&rer)==ODM_STATUS_OK);
        rrq.config=&rc;rrq.recipe=&rer;rrq.flags=0u;
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rrq,2u,&runtime_plan)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,runtime_plan.frame_pixels==UINT64_C(16384)&&runtime_plan.selected_workers==2u&&
                           runtime_plan.recommended_flags==(ODM_EXPORT_RUN_FLAG_DIRECT_STAGING|ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2));

        rc.canvas.width=256u;rc.canvas.height=256u;
        ODM_TEST_CHECK(ctx,odm_export_plan_build(&rc,4801,4801,ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,&rlp)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,odm_export_recipe_build(&rlp,&ep,&rer)==ODM_STATUS_OK);
        rrq.config=&rc;rrq.recipe=&rer;
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rrq,3u,&runtime_plan)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,runtime_plan.selected_workers==2u&&
                           runtime_plan.recommended_flags==(ODM_EXPORT_RUN_FLAG_DIRECT_STAGING|ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2));
        ODM_TEST_CHECK(ctx,odm_export_runtime_plan_recommend(&rrq,4u,&runtime_plan)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,runtime_plan.frame_pixels==UINT64_C(65536)&&runtime_plan.selected_workers==4u&&
                           runtime_plan.recommended_flags==(ODM_EXPORT_RUN_FLAG_DIRECT_STAGING|ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4));
    }
    frame=(uint8_t*)malloc((size_t)fb);scratch=(uint8_t*)malloc((size_t)sb);ODM_TEST_CHECK(ctx,frame!=NULL&&scratch!=NULL);
    if(frame&&scratch){
        odm_sha256_digest wrong_recipe;
        uint8_t tampered[ODM_EXPORT_RUN_RECEIPT_BYTES];
        uint8_t before[ODM_EXPORT_RUN_RECEIPT_BYTES];
        odm_export_run_receipt before_rr;
        sink_reset(&sc);
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,rrn==ODM_EXPORT_RUN_RECEIPT_BYTES&&sc.begun==1&&sc.committed==1&&sc.aborted==0);
        ODM_TEST_CHECK(ctx,sc.video_frames==er.frame_count&&sc.video_bytes==er.source_video_total_bytes);
        ODM_TEST_CHECK(ctx,sc.audio_frames==er.source_audio_total_frames&&sc.audio_bytes==er.source_audio_total_bytes);
        ODM_TEST_CHECK(ctx,strcmp(sc.transcript,"BVVVVAAAAAAAC")==0);
        ODM_TEST_CHECK(ctx,sc.committed+sc.aborted==1);
        ODM_TEST_CHECK(ctx,odm_export_run_receipt_validate(receipt,rrn,&er.export_recipe_sha256,&parsed)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,odm_sha256_equal(&rr.export_run_sha256,&parsed.export_run_sha256));
        ODM_TEST_CHECK(ctx,odm_sha256_equal(&rr.export_policy_sha256,&er.export_policy_sha256));
        ODM_TEST_CHECK(ctx,!odm_sha256_equal(&rr.canonical_pcm_sha256,&rr.audio_stream_sha256));
        {
            odm_export_run_receipt changed_rr;
            uint8_t changed_receipt[ODM_EXPORT_RUN_RECEIPT_BYTES];
            uint64_t changed_required=0u;
            int32_t saved_sample=pcm[17].left;
            pcm[17].left ^= INT32_C(0x00010000);
            sink_reset(&sc);
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,changed_receipt,sizeof(changed_receipt),&changed_required,&changed_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,changed_required==ODM_EXPORT_RUN_RECEIPT_BYTES);
            ODM_TEST_CHECK(ctx,odm_sha256_equal(&changed_rr.export_recipe_sha256,&rr.export_recipe_sha256));
            ODM_TEST_CHECK(ctx,odm_sha256_equal(&changed_rr.video_stream_sha256,&rr.video_stream_sha256));
            ODM_TEST_CHECK(ctx,!odm_sha256_equal(&changed_rr.canonical_pcm_sha256,&rr.canonical_pcm_sha256));
            ODM_TEST_CHECK(ctx,!odm_sha256_equal(&changed_rr.audio_stream_sha256,&rr.audio_stream_sha256));
            ODM_TEST_CHECK(ctx,!odm_sha256_equal(&changed_rr.export_run_sha256,&rr.export_run_sha256));
            pcm[17].left=saved_sample;
        }

        {
            /* Hash-provider choice is runtime-only and MUST be non-semantic.
             * Force the portable implementation and require an identical
             * canonical Receipt byte-for-byte to the default accelerated/fallback
             * run above. Unknown runtime flags fail closed before begin(). */
            uint8_t portable_receipt[ODM_EXPORT_RUN_RECEIPT_BYTES];
            odm_export_run_receipt portable_rr;
            uint64_t portable_required=0u;
            sink_reset(&sc);
            rq.flags=ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,portable_required==ODM_EXPORT_RUN_RECEIPT_BYTES);
            ODM_TEST_CHECK(ctx,memcmp(portable_receipt,receipt,ODM_EXPORT_RUN_RECEIPT_BYTES)==0);
            ODM_TEST_CHECK(ctx,memcmp(&portable_rr,&parsed,sizeof(portable_rr))==0);
            ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==1&&sc.aborted==0);

            sink_reset(&sc);
            rq.flags=ODM_EXPORT_RUN_FLAG_DIRECT_STAGING;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,NULL,0u,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,memcmp(portable_receipt,receipt,ODM_EXPORT_RUN_RECEIPT_BYTES)==0);
            ODM_TEST_CHECK(ctx,memcmp(&portable_rr,&parsed,sizeof(portable_rr))==0);
            ODM_TEST_CHECK(ctx,sc.video_frames==er.frame_count&&sc.committed==1&&sc.aborted==0);

            sink_reset(&sc);
            rq.flags=ODM_EXPORT_RUN_FLAG_DIRECT_STAGING |
                     ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,NULL,0u,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,memcmp(portable_receipt,receipt,ODM_EXPORT_RUN_RECEIPT_BYTES)==0);
            ODM_TEST_CHECK(ctx,memcmp(&portable_rr,&parsed,sizeof(portable_rr))==0);

            sink_reset(&sc);
            rq.flags=ODM_EXPORT_RUN_FLAG_DIRECT_STAGING |
                     ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,NULL,0u,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,memcmp(portable_receipt,receipt,ODM_EXPORT_RUN_RECEIPT_BYTES)==0);

            sink_reset(&sc);
            rq.flags=ODM_EXPORT_RUN_FLAG_DIRECT_STAGING |
                     ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4 |
                     ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,NULL,0u,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,memcmp(portable_receipt,receipt,ODM_EXPORT_RUN_RECEIPT_BYTES)==0);

            rq.flags=ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2 |
                     ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4;
            sink_reset(&sc);
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_INVALID_DATA);
            ODM_TEST_CHECK(ctx,sc.begun==0&&sc.committed==0&&sc.aborted==0);

            rq.flags=UINT32_C(0x80000000);
            sink_reset(&sc);
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,
                portable_receipt,sizeof(portable_receipt),&portable_required,
                &portable_rr)==ODM_STATUS_INVALID_DATA);
            ODM_TEST_CHECK(ctx,sc.begun==0&&sc.committed==0&&sc.aborted==0);
            rq.flags=0u;
        }

        /* Callback-side mutation of caller-owned control-plane structs must not
         * alter an admitted export. odm_export_run() snapshots config/recipe/
         * sink before begin(), so all four frames and the original Recipe
         * identity still commit. */
        {
            odm_layered_config saved_c=c;
            odm_export_recipe saved_er=er;
            odm_export_sink saved_sink=sink;
            uint64_t expected_frames=er.frame_count;
            odm_sha256_digest expected_recipe=er.export_recipe_sha256;
            sink_reset(&sc);pc.mutate_frame=0;pc.mutate_config=&c;pc.mutate_recipe=&er;pc.mutate_sink=&sink;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,sc.video_frames==expected_frames&&sc.committed==1&&sc.aborted==0);
            ODM_TEST_CHECK(ctx,odm_sha256_equal(&rr.export_recipe_sha256,&expected_recipe));
            c=saved_c;er=saved_er;sink=saved_sink;pc.mutate_config=NULL;pc.mutate_recipe=NULL;pc.mutate_sink=NULL;pc.mutate_frame=-1;
        }

        wrong_recipe=er.export_recipe_sha256;wrong_recipe.bytes[0]^=1u;
        ODM_TEST_CHECK(ctx,odm_export_run_receipt_validate(receipt,rrn,&wrong_recipe,NULL)==ODM_STATUS_INTEGRITY_ERROR);
        memcpy(tampered,receipt,sizeof(tampered));tampered[80]^=1u;
        ODM_TEST_CHECK(ctx,odm_export_run_receipt_validate(tampered,sizeof(tampered),&er.export_recipe_sha256,NULL)!=ODM_STATUS_OK);

        memset(receipt,0x5au,sizeof(receipt));memcpy(before,receipt,sizeof(before));
        sink_reset(&sc);pc.fail_frame=1;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.video_frames==1u);
        ODM_TEST_CHECK(ctx,memcmp(receipt,before,sizeof(receipt))==0);pc.fail_frame=-1;

        memset(&rr,0xa5,sizeof(rr));before_rr=rr;sink_reset(&sc);sc.fail_video_frame=2;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INTERNAL_ERROR);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.video_frames==2u);
        ODM_TEST_CHECK(ctx,strcmp(sc.transcript,"BVVVX")==0&&sc.committed+sc.aborted==1);
        ODM_TEST_CHECK(ctx,memcmp(&rr,&before_rr,sizeof(rr))==0);

        memset(&rr,0xa5,sizeof(rr));before_rr=rr;sink_reset(&sc);sc.fail_audio_start=(int64_t)ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INTERNAL_ERROR);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.video_frames==er.frame_count);
        ODM_TEST_CHECK(ctx,sc.audio_frames==ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES);
        ODM_TEST_CHECK(ctx,strcmp(sc.transcript,"BVVVVAAX")==0);
        ODM_TEST_CHECK(ctx,sc.committed+sc.aborted==1);
        ODM_TEST_CHECK(ctx,memcmp(&rr,&before_rr,sizeof(rr))==0);

        sink_reset(&sc);pc.bad_tick=1;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.video_frames==0u);pc.bad_tick=0;

        sink_reset(&sc);sc.fail_begin=1;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INTERNAL_ERROR);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==0&&sc.video_frames==0u);
        /* Persistent row-worker pool is created before begin() and must be
         * destroyed even when begin fails.  A subsequent identical pool run
         * must succeed and reproduce the canonical Receipt. */
        rq.flags=ODM_EXPORT_RUN_FLAG_DIRECT_STAGING | ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4;
        sink_reset(&sc);sc.fail_begin=1;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,NULL,0u,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INTERNAL_ERROR);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==0);
        sink_reset(&sc);
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,NULL,0u,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,sc.committed==1&&sc.aborted==0&&
                           odm_sha256_equal(&rr.export_run_sha256,&parsed.export_run_sha256));
        rq.flags=0u;

        memset(receipt,0x3cu,sizeof(receipt));memcpy(before,receipt,sizeof(before));memset(&rr,0xa5,sizeof(rr));before_rr=rr;
        sink_reset(&sc);sc.fail_commit=1;
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INTERNAL_ERROR);
        ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==1&&sc.aborted==1);
        ODM_TEST_CHECK(ctx,strcmp(sc.transcript,"BVVVVAAAAAAACX")==0);
        ODM_TEST_CHECK(ctx,memcmp(receipt,before,sizeof(receipt))==0&&memcmp(&rr,&before_rr,sizeof(rr))==0);

        {
            odm_job job;odm_job_ticket ticket;odm_progress_snapshot snap;
            ODM_TEST_CHECK(ctx,odm_job_init(&job)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,odm_job_begin(&job,work,&ticket)==ODM_STATUS_OK);
            rq.job_ticket=&ticket;sink_reset(&sc);sc.cancel_job=&job;sc.cancel_video_frame=0;
            memset(receipt,0x69u,sizeof(receipt));memcpy(before,receipt,sizeof(before));memset(&rr,0xa5,sizeof(rr));before_rr=rr;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_CANCELLED);
            ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.video_frames==1u);
            ODM_TEST_CHECK(ctx,memcmp(receipt,before,sizeof(receipt))==0&&memcmp(&rr,&before_rr,sizeof(rr))==0);
            ODM_TEST_CHECK(ctx,odm_job_finish(ticket,ODM_STATUS_CANCELLED)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,odm_job_snapshot(&job,&snap)==ODM_STATUS_OK&&snap.state==ODM_JOB_CANCELLED);
            ODM_TEST_CHECK(ctx,odm_job_destroy(&job)==ODM_STATUS_OK);rq.job_ticket=NULL;
        }

        sink_reset(&sc);
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,(uint8_t*)pcm,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(ctx,sc.begun==0&&sc.aborted==0);
        sink_reset(&sc);
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,(void*)pcm,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(ctx,sc.begun==0&&sc.aborted==0);
        {
            /* Core-provider failure is also post-begin and must abort exactly once
             * without publishing either form of receipt. */
            uint8_t receipt_before[ODM_EXPORT_RUN_RECEIPT_BYTES];
            odm_export_run_receipt rr_before;
            memset(receipt,0x44,sizeof(receipt));memcpy(receipt_before,receipt,sizeof(receipt_before));
            memset(&rr,0x71,sizeof(rr));rr_before=rr;sink_reset(&sc);pc.fail_core_frame=1;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INVALID_DATA);
            ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.video_frames==1u&&sc.last_abort_reason==ODM_STATUS_INVALID_DATA);
            ODM_TEST_CHECK(ctx,memcmp(receipt,receipt_before,sizeof(receipt_before))==0&&memcmp(&rr,&rr_before,sizeof(rr))==0);
            pc.fail_core_frame=-1;
        }

        {
            /* A cancellation requested by the audio sink is observed before the
             * next audio chunk/final commit; staged output is aborted. */
            odm_job job;odm_job_ticket ticket;odm_progress_snapshot snap;
            uint8_t receipt_before[ODM_EXPORT_RUN_RECEIPT_BYTES];
            odm_export_run_receipt rr_before;
            ODM_TEST_CHECK(ctx,odm_job_init(&job)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,odm_job_begin(&job,work,&ticket)==ODM_STATUS_OK);
            rq.job_ticket=&ticket;sink_reset(&sc);sc.cancel_job=&job;sc.cancel_audio_start=(int64_t)ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES;
            memset(receipt,0x45,sizeof(receipt));memcpy(receipt_before,receipt,sizeof(receipt_before));memset(&rr,0x72,sizeof(rr));rr_before=rr;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_CANCELLED);
            ODM_TEST_CHECK(ctx,sc.begun==1&&sc.committed==0&&sc.aborted==1&&sc.last_abort_reason==ODM_STATUS_CANCELLED);
            ODM_TEST_CHECK(ctx,sc.video_frames==er.frame_count&&sc.audio_frames==2u*(uint64_t)ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES);
            ODM_TEST_CHECK(ctx,memcmp(receipt,receipt_before,sizeof(receipt_before))==0&&memcmp(&rr,&rr_before,sizeof(rr))==0);
            ODM_TEST_CHECK(ctx,odm_job_finish(ticket,ODM_STATUS_CANCELLED)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,odm_job_snapshot(&job,&snap)==ODM_STATUS_OK&&snap.state==ODM_JOB_CANCELLED);
            ODM_TEST_CHECK(ctx,odm_job_destroy(&job)==ODM_STATUS_OK);rq.job_ticket=NULL;
        }

        {
            /* Control-plane snapshot: callbacks deliberately corrupt the original
             * config/recipe/sink after admission.  The in-flight export must use
             * the frozen copies and produce exactly the clean receipt. */
            odm_layered_config saved_config=c;
            odm_export_recipe saved_recipe=er;
            odm_export_sink saved_sink=sink;
            odm_export_run_receipt clean_rr=parsed, mutated_rr;
            uint8_t mutated_receipt[ODM_EXPORT_RUN_RECEIPT_BYTES];
            uint64_t mutated_required=0u;
            pc.mutate_config=&c;pc.mutate_recipe=&er;pc.mutate_sink=&sink;pc.mutate_frame=0;
            sink_reset(&sc);sc.mutate_sink=&sink;sc.mutate_sink_on_begin=1;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,mutated_receipt,sizeof(mutated_receipt),&mutated_required,&mutated_rr)==ODM_STATUS_OK);
            ODM_TEST_CHECK(ctx,pc.mutate_frame==-1&&sc.begun==1&&sc.committed==1&&sc.aborted==0);
            ODM_TEST_CHECK(ctx,mutated_required==ODM_EXPORT_RUN_RECEIPT_BYTES&&odm_sha256_equal(&clean_rr.export_run_sha256,&mutated_rr.export_run_sha256));
            c=saved_config;er=saved_recipe;sink=saved_sink;rq.config=&c;rq.recipe=&er;
            pc.mutate_config=NULL;pc.mutate_recipe=NULL;pc.mutate_sink=NULL;pc.mutate_frame=-1;
        }

        {
            /* PCM admission identity: a callback mutating canonical PCM after
             * begin() must be detected before commit. The source buffer is
             * restored after the test; neither receipt representation may publish. */
            odm_pcm_stereo_q31 saved0=pcm[0];
            uint8_t receipt_before[ODM_EXPORT_RUN_RECEIPT_BYTES];
            odm_export_run_receipt rr_before;
            memset(receipt,0x53,sizeof(receipt));memcpy(receipt_before,receipt,sizeof(receipt_before));
            memset(&rr,0x73,sizeof(rr));rr_before=rr;sink_reset(&sc);
            pc.mutate_pcm=pcm;pc.mutate_pcm_frame=0;
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INTEGRITY_ERROR);
            ODM_TEST_CHECK(ctx,pc.mutate_pcm_frame==-1&&sc.begun==1&&sc.committed==0&&sc.aborted==1);
            ODM_TEST_CHECK(ctx,sc.last_abort_reason==ODM_STATUS_INTEGRITY_ERROR);
            ODM_TEST_CHECK(ctx,memcmp(receipt,receipt_before,sizeof(receipt_before))==0&&memcmp(&rr,&rr_before,sizeof(rr))==0);
            pcm[0]=saved0;pc.mutate_pcm=NULL;pc.mutate_pcm_frame=-1;
        }

        {
            /* Receipt capacity failure is pre-begin, reports the canonical required
             * size and does not publish/abort a transaction. */
            uint64_t small_required=0u;
            sink_reset(&sc);
            ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,receipt,ODM_EXPORT_RUN_RECEIPT_BYTES-1u,&small_required,&rr)==ODM_STATUS_BUFFER_TOO_SMALL);
            ODM_TEST_CHECK(ctx,small_required==ODM_EXPORT_RUN_RECEIPT_BYTES&&sc.begun==0&&sc.committed==0&&sc.aborted==0);
        }

        sink_reset(&sc);
        ODM_TEST_CHECK(ctx,odm_export_run(&rq,&sink,scratch,sb,frame,fb,(uint8_t*)pcm,sizeof(receipt),&rrn,&rr)==ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(ctx,sc.begun==0&&sc.aborted==0);
    }
    free(frame);free(scratch);free(pcm);
}

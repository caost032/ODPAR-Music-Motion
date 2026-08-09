#include "test_harness.h"
#include "odm_export.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint32_t qr(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void tiny_config(odm_layered_config*c){
    memset(c,0,sizeof(*c));c->schema_version=1;c->canvas.schema_version=1;c->canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c->canvas.width=32;c->canvas.height=32;c->canvas.fps=30;
    c->background.schema_version=1;c->background.style=ODM_BACKGROUND_SOLID;c->background.solid_color.a=65535;c->background.opacity_q31=INT32_MAX;
    c->core.schema_version=1;c->core.shape=ODM_CORE_SHAPE_CIRCLE;c->core.fit=ODM_CORE_FIT_COVER;c->core.center_x_q31=qr(1,2);c->core.center_y_q31=qr(1,2);c->core.width_q31=qr(1,2);c->core.height_q31=qr(1,2);c->core.feather_q16=1u<<16;c->core.opacity_q31=INT32_MAX;
    c->field.schema_version=1;c->field.radial_segments=48;
    c->hud.schema_version=1;c->hud.text_scale_q16=1u<<16;c->hud.title[0]='\0';c->hud.artist[0]='\0';
}
static void profile(odm_export_profile*p){memset(p,0,sizeof(*p));p->schema_version=1;p->adapter=1;p->container=1;p->video_codec=1;p->audio_codec=1;p->source_video_format=1;p->source_audio_format=1;p->output_pixel_format=1;p->color_primaries=1;p->color_transfer=1;p->color_matrix=1;p->color_range=1;p->rate_control=1;p->crf=18;p->preset=ODM_EXPORT_PRESET_VERYFAST;p->video_threads=4;p->audio_bitrate_kbps=256;p->flags=ODM_EXPORT_FLAG_FASTSTART;}
void odm_test_export_engine(odm_test_context*ctx){
    odm_layered_config c;odm_export_plan lp;odm_export_profile pr;odm_export_recipe r,bad;odm_sha256_digest h,eph;uint8_t eb[ODM_EXPORT_POLICY_BYTES],pb[ODM_EXPORT_PROFILE_BYTES],rb[ODM_EXPORT_RECIPE_BYTES];uint64_t req=0;uint32_t tokens=0;char argvb[ODM_EXPORT_ARGV_MAX_BYTES];int64_t sample=0;odm_pcm_stereo_q31 pcm[3]={{1,-1},{INT32_MAX,INT32_MIN},{123,-456}};uint8_t audio[40];uint64_t i;
    tiny_config(&c);profile(&pr);
    {
        odm_export_profile built, before;
        memset(&built, 0xa5, sizeof(built));
        ODM_TEST_CHECK(ctx, odm_export_profile_init_h264_aac(
                               &built, 18u, ODM_EXPORT_PRESET_VERYFAST, 4u, 256u,
                               ODM_EXPORT_FLAG_FASTSTART) == ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx, memcmp(&built, &pr, sizeof(pr)) == 0);
        before = built;
        ODM_TEST_CHECK(ctx, odm_export_profile_init_h264_aac(
                               &built, 52u, ODM_EXPORT_PRESET_VERYFAST, 4u, 256u,
                               ODM_EXPORT_FLAG_FASTSTART) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(ctx, memcmp(&built, &before, sizeof(before)) == 0);
        ODM_TEST_CHECK(ctx, odm_export_profile_init_h264_aac(
                               &built, 18u, 99u, 4u, 256u,
                               ODM_EXPORT_FLAG_FASTSTART) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(ctx, memcmp(&built, &before, sizeof(before)) == 0);
        ODM_TEST_CHECK(ctx, odm_export_profile_init_h264_aac(
                               &built, 18u, ODM_EXPORT_PRESET_VERYFAST, 0u, 256u,
                               ODM_EXPORT_FLAG_FASTSTART) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(ctx, memcmp(&built, &before, sizeof(before)) == 0);
    }
    ODM_TEST_CHECK(ctx,odm_export_policy_bytes(eb,sizeof(eb),&req)==ODM_STATUS_OK&&req==ODM_EXPORT_POLICY_BYTES);
    ODM_TEST_CHECK(ctx,odm_export_policy_current_sha256(&eph)==ODM_STATUS_OK);
    {
        uint8_t small[64],before[64];uint64_t need=0u;
        memset(small,0x6bu,sizeof(small));memcpy(before,small,sizeof(small));
        ODM_TEST_CHECK(ctx,odm_export_policy_bytes(small,sizeof(small),&need)==ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(ctx,need==ODM_EXPORT_POLICY_BYTES&&memcmp(small,before,sizeof(small))==0);
    }
    ODM_TEST_CHECK(ctx,odm_export_profile_validate(&pr)==ODM_STATUS_OK);
    ODM_TEST_CHECK(ctx,odm_export_profile_bytes(&pr,pb,sizeof(pb),&req)==ODM_STATUS_OK&&req==ODM_EXPORT_PROFILE_BYTES);
    ODM_TEST_CHECK(ctx,odm_export_profile_sha256(&pr,&h)==ODM_STATUS_OK);
    ODM_TEST_CHECK(ctx,odm_export_plan_build(&c,48001,48001,ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,&lp)==ODM_STATUS_OK);
    ODM_TEST_CHECK(ctx,odm_export_recipe_build(&lp,&pr,&r)==ODM_STATUS_OK);
    ODM_TEST_CHECK(ctx,odm_export_recipe_validate(&r)==ODM_STATUS_OK);
    ODM_TEST_CHECK(ctx,odm_sha256_equal(&eph,&r.export_policy_sha256));
    ODM_TEST_CHECK(ctx,r.frame_count==31u&&r.output_end_sample==49600&&r.audio_pad_frames==1599u);
    ODM_TEST_CHECK(ctx,r.source_video_bytes_per_frame==4096u&&r.source_video_total_bytes==126976u);
    ODM_TEST_CHECK(ctx,r.source_audio_total_frames==49600u&&r.source_audio_total_bytes==396800u);
    ODM_TEST_CHECK(ctx,odm_export_recipe_bytes(&r,rb,sizeof(rb),&req)==ODM_STATUS_OK&&req==ODM_EXPORT_RECIPE_BYTES);
    ODM_TEST_CHECK(ctx,odm_export_frame_sample(&r,0u,&sample)==ODM_STATUS_OK&&sample==0);
    ODM_TEST_CHECK(ctx,odm_export_frame_sample(&r,30u,&sample)==ODM_STATUS_OK&&sample==48000);
    ODM_TEST_CHECK(ctx,odm_export_frame_sample(&r,31u,&sample)==ODM_STATUS_INVALID_ARGUMENT);
    /* Use a recipe-sized audio identity but tiny synthetic PCM by making a local
     * canonical recipe copy. */
    bad=r;bad.audio_source_frames=3u;bad.audio_pad_frames=(uint64_t)bad.output_end_sample-3u;bad.export_recipe_sha256=(odm_sha256_digest){{0}};
    /* Identity is intentionally invalid until rebuilt; the public writer must reject it. */
    ODM_TEST_CHECK(ctx,odm_export_audio_chunk_s32le(&bad,pcm,3u,0u,5u,audio,sizeof(audio),&req)==ODM_STATUS_INVALID_DATA);
    /* Real recipe tail is guaranteed silence without reading beyond PCM. */
    {
        odm_pcm_stereo_q31 *big=(odm_pcm_stereo_q31*)calloc((size_t)r.audio_source_frames,sizeof(*big));
        ODM_TEST_CHECK(ctx,big!=NULL);
        if(big){big[0]=pcm[0];big[1]=pcm[1];big[2]=pcm[2];ODM_TEST_CHECK(ctx,odm_export_audio_chunk_s32le(&r,big,r.audio_source_frames,r.audio_source_frames-2u,5u,audio,sizeof(audio),&req)==ODM_STATUS_OK&&req==40u);for(i=16u;i<40u;++i)if(audio[i]!=0u)break;ODM_TEST_CHECK(ctx,i==40u);free(big);}
    }
    ODM_TEST_CHECK(ctx,odm_export_ffmpeg_argv_template(&r,argvb,sizeof(argvb),&req,&tokens)==ODM_STATUS_OK);
    ODM_TEST_CHECK(ctx,tokens>40u&&req<sizeof(argvb));
    {
        char small[64]; char before[64]; uint64_t small_req=0u; uint32_t small_tokens=0u;
        memset(small,0x5au,sizeof(small)); memcpy(before,small,sizeof(small));
        ODM_TEST_CHECK(ctx,odm_export_ffmpeg_argv_template(&r,small,sizeof(small),&small_req,&small_tokens)==ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(ctx,small_req==req&&small_tokens==tokens&&memcmp(small,before,sizeof(small))==0);
    }
    {
        char bound[ODM_EXPORT_ARGV_MAX_BYTES],before[64];uint32_t offs[ODM_EXPORT_ARGV_MAX_TOKENS];uint64_t bn=0u;uint32_t bc=0u;uint32_t j;
        ODM_TEST_CHECK(ctx,odm_export_ffmpeg_argv_bind(&r,"/tmp/video input.rgba","/tmp/audio input.s32le","/tmp/output file.mp4",bound,sizeof(bound),&bn,&bc,offs,ODM_EXPORT_ARGV_MAX_TOKENS)==ODM_STATUS_OK);
        ODM_TEST_CHECK(ctx,bc==tokens&&bn<sizeof(bound));
        for(j=0u;j<bc;++j)ODM_TEST_CHECK(ctx,offs[j]<bn&&bound[offs[j]]!='\0');
        ODM_TEST_CHECK(ctx,strstr(bound+offs[13],"video input.rgba")!=NULL||strstr(bound,"ffmpeg")!=NULL);
        ODM_TEST_CHECK(ctx,odm_export_ffmpeg_argv_bind(&r,"same","same","out",bound,sizeof(bound),&bn,&bc,offs,ODM_EXPORT_ARGV_MAX_TOKENS)==ODM_STATUS_INVALID_DATA);
        memset(bound,0x77u,64u);memcpy(before,bound,64u);
        ODM_TEST_CHECK(ctx,odm_export_ffmpeg_argv_bind(&r,"/tmp/video input.rgba","/tmp/audio input.s32le","/tmp/output file.mp4",bound,64u,&bn,&bc,offs,ODM_EXPORT_ARGV_MAX_TOKENS)==ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(ctx,memcmp(bound,before,64u)==0);
    }
    ODM_TEST_CHECK(ctx,strstr(argvb,"ffmpeg")!=NULL); /* first token */
    {
        uint64_t pos=0u;int saw_video=0,saw_audio=0,saw_output=0,saw_shortest=0;
        while(pos<req){const char*t=argvb+pos;if(strcmp(t,ODM_EXPORT_PLACEHOLDER_VIDEO)==0)saw_video=1;if(strcmp(t,ODM_EXPORT_PLACEHOLDER_AUDIO)==0)saw_audio=1;if(strcmp(t,ODM_EXPORT_PLACEHOLDER_OUTPUT)==0)saw_output=1;if(strcmp(t,"-shortest")==0)saw_shortest=1;pos+=(uint64_t)strlen(t)+1u;}
        ODM_TEST_CHECK(ctx,saw_video&&saw_audio&&saw_output&&!saw_shortest);
    }
    bad=r;bad.frame_count++;
    ODM_TEST_CHECK(ctx,odm_export_recipe_validate(&bad)==ODM_STATUS_INVALID_DATA||odm_export_recipe_validate(&bad)==ODM_STATUS_INTEGRITY_ERROR);
    bad=r;bad.export_policy_sha256.bytes[0]^=1u;
    ODM_TEST_CHECK(ctx,odm_export_recipe_validate(&bad)==ODM_STATUS_VERSION_MISMATCH);
}

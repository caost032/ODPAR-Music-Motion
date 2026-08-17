#define _POSIX_C_SOURCE 200809L
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "odm_spine.h"
#include "odm_status.h"
#include "odm_version.h"
#include "odm_media.h"
#include "odm_resample.h"
#include "odm_music_map.h"
#include "odm_music_reaction.h"
#include "selftest.h"
#include "odpar_lab_scene.h"

static _Thread_local char g_last_error[512];

static jstring jstr(JNIEnv *env, const char *s) {
    return (*env)->NewStringUTF(env, s ? s : "");
}

static char *spine_json(odm_status (*fn)(char *, size_t, size_t *)) {
    size_t required = 0;
    odm_status st = fn(NULL, 0u, &required);
    char *buf;
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || required == 0u) return NULL;
    buf = (char *)malloc(required);
    if (!buf) return NULL;
    st = fn(buf, required, &required);
    if (st != ODM_STATUS_OK) { free(buf); return NULL; }
    return buf;
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeEngineInfo(JNIEnv *env, jobject thiz) {
    (void)thiz;
    char *summary = spine_json(odm_spine_summary_json);
    char out[4096];
    snprintf(out, sizeof(out),
             "ODPAR: Music Motion\nversion=%s\nabi=%u\nsource=%s\ncompiler=%s\n\n%s",
             odm_version_string(), odm_abi_version(), odm_source_id(), odm_compiler_id(),
             summary ? summary : "{\"spine\":\"unavailable\"}");
    free(summary);
    return jstr(env, out);
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeSelfTest(JNIEnv *env, jobject thiz) {
    (void)thiz;
    odm_selftest_result r;
    odm_status st = odm_selftest_run(&r);
    char out[1024];
    snprintf(out, sizeof(out),
             "{\"schema\":\"odm_selftest_v1\",\"checks\":%u,\"passed\":%u,"
             "\"failed\":%u,\"status\":\"%s\",\"failed_check\":%s,\"error\":\"%s\"}",
             r.checks, r.passed, r.checks-r.passed,
             st==ODM_STATUS_OK?"pass":"fail",
             r.failed_check ? "\"present\"" : "null",
             odm_status_name(st==ODM_STATUS_OK?ODM_STATUS_OK:r.failure_status));
    return jstr(env, out);
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeSpineReport(JNIEnv *env, jobject thiz) {
    (void)thiz;
    char *report = spine_json(odm_spine_report_json);
    jstring out;
    if (!report) return jstr(env, "{\"error\":\"spine report unavailable\"}");
    out = jstr(env, report);
    free(report);
    return out;
}

JNIEXPORT jintArray JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeRenderScene(
    JNIEnv *env, jobject thiz, jint width, jint height, jdouble yaw, jdouble pitch,
    jdouble distance, jdouble phase, jboolean shadows) {
    (void)thiz;
    uint64_t pixels;
    uint8_t *rgba;
    jint *argb;
    jintArray arr;
    uint64_t fragments=0;
    uint64_t i;
    if (width<=0 || height<=0 || width>2048 || height>2048) {
        snprintf(g_last_error,sizeof(g_last_error),"invalid dimensions");
        return NULL;
    }
    pixels=(uint64_t)(uint32_t)width*(uint64_t)(uint32_t)height;
    if (pixels > SIZE_MAX/4u) { snprintf(g_last_error,sizeof(g_last_error),"size overflow"); return NULL; }
    rgba=(uint8_t*)malloc((size_t)pixels*4u);
    argb=(jint*)malloc((size_t)pixels*sizeof(jint));
    if(!rgba||!argb){free(rgba);free(argb);snprintf(g_last_error,sizeof(g_last_error),"frame allocation failed");return NULL;}
    if(!odpar_lab_render_scene(rgba,(uint32_t)width,(uint32_t)height,yaw,pitch,distance,phase,
                               shadows?1:0,&fragments,g_last_error,sizeof(g_last_error))){
        free(rgba);free(argb);return NULL;
    }
    for(i=0;i<pixels;i++) {
        const uint8_t *p=rgba+i*4u;
        argb[i]=(jint)(((uint32_t)p[3]<<24)|((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|(uint32_t)p[2]);
    }
    free(rgba);
    arr=(*env)->NewIntArray(env,(jsize)pixels);
    if(!arr){free(argb);snprintf(g_last_error,sizeof(g_last_error),"JNI array allocation failed");return NULL;}
    (*env)->SetIntArrayRegion(env,arr,0,(jsize)pixels,argb);
    free(argb);
    snprintf(g_last_error,sizeof(g_last_error),"ok; fragments=%llu",(unsigned long long)fragments);
    return arr;
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeLastError(JNIEnv *env, jobject thiz) {
    (void)thiz; return jstr(env,g_last_error);
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeStressTest(
    JNIEnv *env, jobject thiz, jint frames, jint size, jboolean shadows) {
    (void)thiz;
    struct timespec a,b;
    uint8_t *rgba;
    int i, ok=0;
    uint64_t total_fragments=0;
    double seconds, fps;
    char err[512]={0};
    char out[1024];
    if (frames < 1) frames = 1;
    if (frames > 240) frames = 240;
    if (size < 128) size = 128;
    if (size > 768) size = 768;
    rgba=(uint8_t*)malloc((size_t)size*(size_t)size*4u);
    if(!rgba)return jstr(env,"{\"status\":\"fail\",\"error\":\"allocation\"}");
    clock_gettime(CLOCK_MONOTONIC,&a);
    for(i=0;i<frames;i++) {
        uint64_t f=0;
        double phase=(double)i/(double)(frames?frames:1);
        if(!odpar_lab_render_scene(rgba,(uint32_t)size,(uint32_t)size,25.0+phase*80.0,-10.0,
                                   5.2,phase,shadows?1:0,&f,err,sizeof(err))) break;
        total_fragments+=f; ok++;
    }
    clock_gettime(CLOCK_MONOTONIC,&b);
    free(rgba);
    seconds=(double)(b.tv_sec-a.tv_sec)+(double)(b.tv_nsec-a.tv_nsec)/1000000000.0;
    fps=seconds>0.0?(double)ok/seconds:0.0;
    snprintf(out,sizeof(out),
             "{\"status\":\"%s\",\"frames_requested\":%d,\"frames_rendered\":%d,"
             "\"size\":%d,\"shadows\":%s,\"seconds\":%.6f,\"fps\":%.3f,"
             "\"total_fragments\":%llu,\"error\":\"%s\"}",
             ok==frames?"pass":"fail",frames,ok,size,shadows?"true":"false",seconds,fps,
             (unsigned long long)total_fragments,ok==frames?"":err);
    return jstr(env,out);
}

static uint8_t *lab_read_file(const char *path, size_t *out_bytes, char *err, size_t errcap) {
    FILE *f = NULL;
    long end;
    size_t n;
    uint8_t *bytes = NULL;
    if (!path || !out_bytes) return NULL;
    *out_bytes = 0u;
    f = fopen(path, "rb");
    if (!f) { if (err && errcap) snprintf(err,errcap,"open failed"); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (err && errcap) snprintf(err,errcap,"size query failed");
        fclose(f);
        return NULL;
    }
    n = (size_t)end;
    bytes = (uint8_t *)malloc(n);
    if (!bytes) { if(err&&errcap)snprintf(err,errcap,"input allocation failed"); fclose(f); return NULL; }
    if (fread(bytes,1u,n,f) != n) { if(err&&errcap)snprintf(err,errcap,"read failed"); free(bytes); fclose(f); return NULL; }
    fclose(f); *out_bytes=n; return bytes;
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotionlab_MainActivity_nativeAnalyzeAudioFile(JNIEnv *env, jobject thiz, jstring jpath) {
    (void)thiz;
    const char *path = NULL;
    uint8_t *bytes = NULL;
    size_t bytes_n = 0u;
    odm_media_facts facts;
    odm_pcm_stereo_q31 *src = NULL, *canon = NULL;
    uint64_t src_need=0u, coeff_need=0u, out_need=0u, sat=0u, tick_count=0u;
    uint32_t taps=0u;
    int32_t *coeff=NULL, *window=NULL;
    odm_music_complex_q31 *tw=NULL;
    odm_resample_plan rs;
    odm_music_analysis_plan ap;
    odm_music_analysis_state as;
    odm_music_analysis_scratch *scratch=NULL;
    odm_music_reaction_tick *rt=NULL;
    odm_music_reaction_frame *rf=NULL;
    odm_music_reaction_profile prof;
    odm_music_reaction_state rstate;
    odm_status st=ODM_STATUS_OK;
    uint64_t i=0u,max_i=0u;
    uint32_t max_event=0u,max_broadband=0u;
    char err[256]={0};
    char out[1536];
#define FAIL_AT(label,status) do { st=(status); snprintf(err,sizeof(err),"%s: %s",(label),odm_status_name(st)); goto done; } while(0)
    if (!jpath) return jstr(env,"{\"status\":\"fail\",\"error\":\"null path\"}");
    path=(*env)->GetStringUTFChars(env,jpath,NULL);
    if(!path) return jstr(env,"{\"status\":\"fail\",\"error\":\"path decode\"}");
    bytes=lab_read_file(path,&bytes_n,err,sizeof(err));
    if(!bytes) goto done;
    memset(&facts,0,sizeof(facts));
    st=odm_media_decode_audio_q31(bytes,(uint64_t)bytes_n,NULL,0u,&src_need,&facts);
    if(st!=ODM_STATUS_BUFFER_TOO_SMALL) FAIL_AT("decode query",st);
    if(src_need==0u || src_need>(uint64_t)(SIZE_MAX/sizeof(*src))) { snprintf(err,sizeof(err),"source frame size overflow"); goto done; }
    src=(odm_pcm_stereo_q31*)calloc((size_t)src_need,sizeof(*src));
    if(!src){snprintf(err,sizeof(err),"source allocation failed");goto done;}
    st=odm_media_decode_audio_q31(bytes,(uint64_t)bytes_n,src,src_need,&src_need,&facts);
    if(st!=ODM_STATUS_OK) FAIL_AT("decode",st);
    st=odm_resample_plan_requirements(facts.sample_rate,&taps,&coeff_need);
    if(st!=ODM_STATUS_OK) FAIL_AT("resample requirements",st);
    if(coeff_need>(uint64_t)(SIZE_MAX/sizeof(*coeff))){snprintf(err,sizeof(err),"coefficient size overflow");goto done;}
    if(coeff_need){coeff=(int32_t*)calloc((size_t)coeff_need,sizeof(*coeff));if(!coeff){snprintf(err,sizeof(err),"coefficient allocation failed");goto done;}}
    st=odm_resample_plan_build(facts.sample_rate,coeff,coeff_need,&coeff_need,&rs);
    if(st!=ODM_STATUS_OK) FAIL_AT("resample plan",st);
    st=odm_resample_output_frames(src_need,facts.sample_rate,&out_need);
    if(st!=ODM_STATUS_OK) FAIL_AT("resample frame count",st);
    if(out_need==0u || out_need>(uint64_t)(SIZE_MAX/sizeof(*canon))){snprintf(err,sizeof(err),"canonical frame size overflow");goto done;}
    canon=(odm_pcm_stereo_q31*)calloc((size_t)out_need,sizeof(*canon));
    if(!canon){snprintf(err,sizeof(err),"canonical allocation failed");goto done;}
    st=odm_resample_q31(&rs,coeff,coeff_need,src,src_need,canon,out_need,&out_need,NULL,&sat);
    if(st!=ODM_STATUS_OK) FAIL_AT("resample",st);
    window=(int32_t*)calloc(ODM_MUSIC_WINDOW_SAMPLES,sizeof(*window));
    tw=(odm_music_complex_q31*)calloc(ODM_MUSIC_FFT_TWIDDLES,sizeof(*tw));
    scratch=(odm_music_analysis_scratch*)calloc(1u,sizeof(*scratch));
    if(!window||!tw||!scratch){snprintf(err,sizeof(err),"analysis allocation failed");goto done;}
    st=odm_music_analysis_plan_build(window,ODM_MUSIC_WINDOW_SAMPLES,tw,ODM_MUSIC_FFT_TWIDDLES,&ap);
    if(st!=ODM_STATUS_OK) FAIL_AT("analysis plan",st);
    st=odm_music_tick_count(out_need,&tick_count);
    if(st!=ODM_STATUS_OK) FAIL_AT("tick count",st);
    if(tick_count==0u || tick_count>(uint64_t)(SIZE_MAX/sizeof(*rt)) || tick_count>(uint64_t)(SIZE_MAX/sizeof(*rf))){snprintf(err,sizeof(err),"reaction timeline size overflow");goto done;}
    rt=(odm_music_reaction_tick*)calloc((size_t)tick_count,sizeof(*rt));
    rf=(odm_music_reaction_frame*)calloc((size_t)tick_count,sizeof(*rf));
    if(!rt||!rf){snprintf(err,sizeof(err),"reaction allocation failed");goto done;}
    odm_music_analysis_state_init(&as);
    for(i=0u;i<tick_count;i++){
        odm_music_analysis_tick tick;
        st=odm_music_analyze_tick(&ap,window,tw,canon,out_need,i,&as,scratch,&tick);
        if(st!=ODM_STATUS_OK) FAIL_AT("analyze tick",st);
        st=odm_music_reaction_extract_tick(&tick,scratch,&rt[i]);
        if(st!=ODM_STATUS_OK) FAIL_AT("reaction extract",st);
    }
    st=odm_music_reaction_profile_build(rt,tick_count,&prof);
    if(st!=ODM_STATUS_OK) FAIL_AT("reaction profile",st);
    odm_music_reaction_state_init(&rstate);
    for(i=0u;i<tick_count;i++){
        st=odm_music_reaction_resolve_tick(&rstate,&prof,&rt[i],&rf[i]);
        if(st!=ODM_STATUS_OK) FAIL_AT("reaction resolve",st);
        if(rf[i].event_strength_q31>max_event){max_event=rf[i].event_strength_q31;max_i=i;}
        if(rf[i].broadband_q31>max_broadband)max_broadband=rf[i].broadband_q31;
    }
    st=odm_music_reaction_refine_events_offline(rf,tick_count,rf,tick_count);
    if(st!=ODM_STATUS_OK) FAIL_AT("offline refine",st);
    snprintf(out,sizeof(out),
      "{\"status\":\"pass\",\"pipeline\":\"media->canonical48k->analysis->music_reaction\","
      "\"source_bytes\":%llu,\"source_rate\":%u,\"channels\":%u,\"source_frames\":%llu,"
      "\"canonical_frames\":%llu,\"resample_taps\":%u,\"saturated_samples\":%llu,"
      "\"reaction_ticks\":%llu,\"max_event_q31\":%u,\"max_event_tick\":%llu,"
      "\"max_event_center_sample\":%llu,\"max_broadband_q31\":%u,\"event_flags\":%u}",
      (unsigned long long)bytes_n,facts.sample_rate,facts.channels,(unsigned long long)facts.source_frames,
      (unsigned long long)out_need,taps,(unsigned long long)sat,(unsigned long long)tick_count,
      max_event,(unsigned long long)max_i,(unsigned long long)rf[max_i].center_sample,max_broadband,rf[max_i].event_flags);
    err[0]='\0';
done:
    free(bytes);free(src);free(canon);free(coeff);free(window);free(tw);free(scratch);free(rt);free(rf);
    if(path)(*env)->ReleaseStringUTFChars(env,jpath,path);
    if(err[0]){snprintf(out,sizeof(out),"{\"status\":\"fail\",\"error\":\"%s\"}",err);}
#undef FAIL_AT
    return jstr(env,out);
}

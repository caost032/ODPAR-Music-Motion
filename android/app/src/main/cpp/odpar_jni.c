#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "odpar_lab_core.h"

static char g_render_meta[1024] = "not rendered";

static jstring str_from_call(JNIEnv *env, int (*fn)(char*, size_t)) {
    int need = fn(NULL,0u);
    size_t cap = need > 0 ? (size_t)need + 1u : (size_t)(1u<<20);
    char *buf = (char*)calloc(cap,1u);
    jstring out;
    if(!buf) return (*env)->NewStringUTF(env,"out_of_memory");
    fn(buf,cap);
    out=(*env)->NewStringUTF(env,buf);
    free(buf); return out;
}

JNIEXPORT jstring JNICALL Java_com_odpar_musicmotionlab_NativeBridge_engineStatus(JNIEnv *env,jclass cls){
    (void)cls; char b[4096]; memset(b,0,sizeof b); odpar_lab_status(b,sizeof b); return (*env)->NewStringUTF(env,b);
}
JNIEXPORT jstring JNICALL Java_com_odpar_musicmotionlab_NativeBridge_selftest(JNIEnv *env,jclass cls){
    (void)cls; char b[4096]; memset(b,0,sizeof b); odpar_lab_selftest(b,sizeof b); return (*env)->NewStringUTF(env,b);
}
JNIEXPORT jstring JNICALL Java_com_odpar_musicmotionlab_NativeBridge_spineSummary(JNIEnv *env,jclass cls){
    (void)cls; return str_from_call(env,odpar_lab_spine_summary);
}
JNIEXPORT jstring JNICALL Java_com_odpar_musicmotionlab_NativeBridge_spineFull(JNIEnv *env,jclass cls){
    (void)cls; return str_from_call(env,odpar_lab_spine_full);
}
JNIEXPORT jstring JNICALL Java_com_odpar_musicmotionlab_NativeBridge_lastRenderMeta(JNIEnv *env,jclass cls){
    (void)cls; return (*env)->NewStringUTF(env,g_render_meta);
}
JNIEXPORT jintArray JNICALL Java_com_odpar_musicmotionlab_NativeBridge_renderDemo(JNIEnv *env,jclass cls,jint w,jint h,jint px,jint py,jint zoom){
    (void)cls;
    uint64_t count,bytes; uint8_t *rgba=NULL; jint *argb=NULL; jintArray out=NULL; uint64_t i;
    if(w<1||h<1) return NULL; count=(uint64_t)(uint32_t)w*(uint64_t)(uint32_t)h; if(count>UINT64_C(1048576))return NULL; bytes=count*4u;
    rgba=(uint8_t*)malloc((size_t)bytes); argb=(jint*)malloc((size_t)count*sizeof(jint)); if(!rgba||!argb)goto done;
    memset(g_render_meta,0,sizeof g_render_meta);
    if(odpar_lab_render_demo((uint32_t)w,(uint32_t)h,(int32_t)px,(int32_t)py,(int32_t)zoom,rgba,bytes,g_render_meta,sizeof g_render_meta)!=0)goto done;
    for(i=0u;i<count;i++){
        const uint8_t *p=rgba+i*4u;
        argb[i]=(jint)(UINT32_C(0xff000000)|((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|(uint32_t)p[2]);
    }
    out=(*env)->NewIntArray(env,(jsize)count); if(!out)goto done;
    (*env)->SetIntArrayRegion(env,out,0,(jsize)count,argb);
done:
    free(rgba); free(argb); return out;
}

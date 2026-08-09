#include "odm_export.h"
#include "odm_supersample.h"
#include "compositor_internal.h"

#include "odm_wire.h"
#include "odm_time.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>   /* snprintf for delivery argv construction */
#include <stdlib.h>
#include <string.h>

#if defined(ODM_SHA256_OPENSSL_ACCEL)
#include <openssl/evp.h>
#endif

#define EXPORT_DOMAIN "ODPAR_EXPORT_RECIPE_V1"
#define EXPORT_PROFILE_MAGIC "ODMEXPF1"
#define EXPORT_RECIPE_MAGIC "ODMEXPR1"
#define EXPORT_POLICY_MAGIC "ODMEXPO1"
#define EXPORT_PCM_SOURCE_DOMAIN "ODPAR_EXPORT_CANONICAL_PCM_Q31_SOURCE_V1"


typedef struct {
    int accelerated;
#if defined(ODM_SHA256_OPENSSL_ACCEL)
    EVP_MD_CTX *evp;
#endif
    odm_sha256_context portable;
} export_sha256_stream;

static int export_sha256_accel_selftest(void) {
#if defined(ODM_SHA256_OPENSSL_ACCEL)
    uint8_t probe[257];
    uint8_t accelerated[ODM_SHA256_DIGEST_BYTES];
    unsigned int accelerated_bytes = 0u;
    odm_sha256_digest expected;
    uint32_t i;
    for (i = 0u; i < (uint32_t)sizeof(probe); ++i)
        probe[i] = (uint8_t)((i * UINT32_C(73) + UINT32_C(19)) & UINT32_C(0xff));
    if (odm_sha256(probe, sizeof(probe), &expected) != ODM_STATUS_OK) return 0;
    if (EVP_Digest(probe, sizeof(probe), accelerated, &accelerated_bytes,
                   EVP_sha256(), NULL) != 1) return 0;
    return accelerated_bytes == ODM_SHA256_DIGEST_BYTES &&
           memcmp(accelerated, expected.bytes, ODM_SHA256_DIGEST_BYTES) == 0;
#else
    return 0;
#endif
}

static odm_status export_sha256_stream_init(export_sha256_stream *stream,
                                             int allow_acceleration) {
    if (!stream) return ODM_STATUS_INVALID_ARGUMENT;
    memset(stream, 0, sizeof(*stream));
#if defined(ODM_SHA256_OPENSSL_ACCEL)
    if (allow_acceleration) {
        stream->evp = EVP_MD_CTX_new();
        if (stream->evp && EVP_DigestInit_ex2(stream->evp, EVP_sha256(), NULL) == 1) {
            stream->accelerated = 1;
            return ODM_STATUS_OK;
        }
        EVP_MD_CTX_free(stream->evp);
        stream->evp = NULL;
    }
#else
    (void)allow_acceleration;
#endif
    return odm_sha256_init(&stream->portable);
}

static odm_status export_sha256_stream_update(export_sha256_stream *stream,
                                               const void *data, uint64_t bytes) {
    if (!stream || (!data && bytes != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
#if defined(ODM_SHA256_OPENSSL_ACCEL)
    if (stream->accelerated) {
        if (bytes > (uint64_t)SIZE_MAX) return ODM_STATUS_BUDGET_EXCEEDED;
        return EVP_DigestUpdate(stream->evp, data, (size_t)bytes) == 1
             ? ODM_STATUS_OK : ODM_STATUS_INTERNAL_ERROR;
    }
#endif
    return odm_sha256_update(&stream->portable, data, bytes);
}

static odm_status export_sha256_stream_final(export_sha256_stream *stream,
                                              odm_sha256_digest *out) {
    if (!stream || !out) return ODM_STATUS_INVALID_ARGUMENT;
#if defined(ODM_SHA256_OPENSSL_ACCEL)
    if (stream->accelerated) {
        unsigned int bytes = 0u;
        int ok = EVP_DigestFinal_ex(stream->evp, out->bytes, &bytes);
        EVP_MD_CTX_free(stream->evp);
        stream->evp = NULL;
        stream->accelerated = 0;
        if (ok != 1 || bytes != ODM_SHA256_DIGEST_BYTES)
            return ODM_STATUS_INTERNAL_ERROR;
        return ODM_STATUS_OK;
    }
#endif
    return odm_sha256_final(&stream->portable, out);
}

static void export_sha256_stream_cleanup(export_sha256_stream *stream) {
    if (!stream) return;
#if defined(ODM_SHA256_OPENSSL_ACCEL)
    EVP_MD_CTX_free(stream->evp);
    stream->evp = NULL;
#endif
    stream->accelerated = 0;
}

static int zero_u32(const uint32_t *p, uint32_t n) {
    uint32_t i;
    if (!p) return 0;
    for (i = 0u; i < n; ++i) if (p[i] != 0u) return 0;
    return 1;
}

static int digest_nonzero(const odm_sha256_digest *d) {
    uint32_t i;
    if (!d) return 0;
    for (i = 0u; i < 32u; ++i) if (d->bytes[i] != 0u) return 1;
    return 0;
}

static const char *preset_name(uint32_t preset) {
    switch (preset) {
        case ODM_EXPORT_PRESET_ULTRAFAST: return "ultrafast";
        case ODM_EXPORT_PRESET_VERYFAST: return "veryfast";
        case ODM_EXPORT_PRESET_MEDIUM: return "medium";
        case ODM_EXPORT_PRESET_SLOW: return "slow";
        default: return NULL;
    }
}

static odm_status export_policy_encode(uint8_t *buffer, uint64_t capacity,
                                       uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint64_t used = 0u;
    static const uint8_t argv_contract[] =
        "ffmpeg\0-hide_banner\0-loglevel\0error\0-nostdin\0-n\0-f\0rawvideo\0"
        "-pixel_format\0rgba\0-video_size\0<WIDTHxHEIGHT>\0"
        "-framerate\0<FPS>\0-i\0{ODPAR_VIDEO_RGBA8}\0"
        "-f\0s32le\0-ar\048000\0-ac\02\0-i\0{ODPAR_AUDIO_S32LE}\0"
        "-map\00:v:0\0-map\01:a:0\0-map_metadata\0-1\0-map_chapters\0-1\0"
        "-c:v\0libx264\0-preset\0<PRESET>\0-crf\0<CRF>\0"
        "-threads\0<THREADS>\0-pix_fmt\0yuv420p\0-colorspace\0bt709\0"
        "-color_primaries\0bt709\0-color_trc\0iec61966-2-1\0"
        "-color_range\0tv\0-c:a\0aac\0-b:a\0<ABR>\0"
        "-frames:v\0<FRAME_COUNT>\0[-movflags\0+faststart]\0"
        "{ODPAR_OUTPUT_MP4}\0";
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_EXPORT_POLICY_BYTES;
    if (!buffer || capacity < ODM_EXPORT_POLICY_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_EXPORT_POLICY_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_EXPORT_POLICY_BYTES);
    if (st != ODM_STATUS_OK) return st;
#define EPW32(v) do { st=odm_wire_write_u32(&w,(v)); if(st!=ODM_STATUS_OK)return st; } while(0)
#define EPWB(p,n) do { st=odm_wire_write_bytes(&w,(p),(n)); if(st!=ODM_STATUS_OK)return st; } while(0)
    EPWB(EXPORT_POLICY_MAGIC, 8u);
    EPW32(ODM_EXPORT_POLICY_VERSION);
    EPW32(ODM_EXPORT_SCHEMA_VERSION);
    EPW32(ODM_EXPORT_PROFILE_BYTES);
    EPW32(ODM_EXPORT_RECIPE_BYTES);
    EPW32(ODM_EXPORT_RUN_SCHEMA_VERSION);
    EPW32(ODM_EXPORT_RUN_RECEIPT_BYTES);
    EPW32(ODM_EXPORT_ARGV_MAX_BYTES);
    EPW32(ODM_EXPORT_ARGV_MAX_TOKENS);
    EPW32(ODM_EXPORT_ADAPTER_FFMPEG);
    EPW32(ODM_EXPORT_CONTAINER_MP4);
    EPW32(ODM_EXPORT_VIDEO_H264);
    EPW32(ODM_EXPORT_AUDIO_AAC);
    EPW32(ODM_EXPORT_SOURCE_RGBA8_SRGB);
    EPW32(ODM_EXPORT_SOURCE_PCM_S32LE_Q31_STEREO);
    EPW32(ODM_EXPORT_PIXEL_YUV420P);
    EPW32(ODM_EXPORT_PRIMARIES_BT709);
    EPW32(ODM_EXPORT_TRANSFER_SRGB);
    EPW32(ODM_EXPORT_MATRIX_BT709);
    EPW32(ODM_EXPORT_RANGE_TV);
    EPW32(ODM_EXPORT_RATE_CONTROL_CRF);
    EPW32(ODM_EXPORT_PRESET_ULTRAFAST);
    EPW32(ODM_EXPORT_PRESET_VERYFAST);
    EPW32(ODM_EXPORT_PRESET_MEDIUM);
    EPW32(ODM_EXPORT_PRESET_SLOW);
    EPW32(ODM_EXPORT_FLAG_FASTSTART);
    EPW32(48000u); /* canonical audio sample rate */
    EPW32(2u);     /* canonical channels */
    EPW32(8u);     /* S32LE stereo bytes/frame */
    EPW32(ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES);
    EPW32(1u); /* exact frame_count authority */
    EPW32(1u); /* output_end_sample = frame_count * samples_per_frame */
    EPW32(1u); /* audio exact-silence padding to output_end_sample */
    EPW32(1u); /* -shortest forbidden */
    EPW32(1u); /* argv is NUL-separated tokens, never shell text */
    EPW32(1u); /* commit is sole publication point */
    EPW32(1u); /* every post-begin failure aborts exactly once */
    EPW32(1u); /* receipts publish only after successful commit */
    EPW32(1u); /* config/recipe/sink/job ticket snapshot on admission */
    EPW32(1u); /* canonical PCM admission identity must equal streamed source */
    EPW32(1u); /* encoder stdin disabled */
    EPW32(1u); /* output overwrite forbidden; staging path must be unique */
    EPW32(1u); /* video/audio stream mapping explicit */
    EPW32(1u); /* input metadata and chapters discarded */
    EPWB(EXPORT_DOMAIN, sizeof(EXPORT_DOMAIN));
    EPWB("ODPAR_EXPORT_RGBA8_STREAM_V1", sizeof("ODPAR_EXPORT_RGBA8_STREAM_V1"));
    EPWB("ODPAR_EXPORT_S32LE_STREAM_V1", sizeof("ODPAR_EXPORT_S32LE_STREAM_V1"));
    EPWB(EXPORT_PCM_SOURCE_DOMAIN, sizeof(EXPORT_PCM_SOURCE_DOMAIN));
    EPWB("ODPAR_EXPORT_RUN_RECEIPT_V1", sizeof("ODPAR_EXPORT_RUN_RECEIPT_V1"));
    EPWB(argv_contract, sizeof(argv_contract));
#undef EPWB
#undef EPW32
    st = odm_wire_writer_finish(&w, &used);
    if (st != ODM_STATUS_OK) return st;
    if (used > ODM_EXPORT_POLICY_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    *out_required = ODM_EXPORT_POLICY_BYTES;
    return ODM_STATUS_OK;
}

odm_status odm_export_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required) {
    return export_policy_encode(buffer, capacity, out_required);
}

odm_status odm_export_policy_current_sha256(odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_EXPORT_POLICY_BYTES];
    uint64_t required = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = export_policy_encode(bytes, sizeof(bytes), &required);
    if (st != ODM_STATUS_OK) return st;
    if (required != sizeof(bytes)) return ODM_STATUS_INVARIANT_BROKEN;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}

odm_status odm_export_profile_init_h264_aac(odm_export_profile *out_profile,
                                                uint32_t crf,
                                                uint32_t preset,
                                                uint32_t video_threads,
                                                uint32_t audio_bitrate_kbps,
                                                uint32_t flags) {
    odm_export_profile p;
    odm_status st;
    if (!out_profile) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&p, 0, sizeof(p));
    p.schema_version = ODM_EXPORT_SCHEMA_VERSION;
    p.adapter = ODM_EXPORT_ADAPTER_FFMPEG;
    p.container = ODM_EXPORT_CONTAINER_MP4;
    p.video_codec = ODM_EXPORT_VIDEO_H264;
    p.audio_codec = ODM_EXPORT_AUDIO_AAC;
    p.source_video_format = ODM_EXPORT_SOURCE_RGBA8_SRGB;
    p.source_audio_format = ODM_EXPORT_SOURCE_PCM_S32LE_Q31_STEREO;
    p.output_pixel_format = ODM_EXPORT_PIXEL_YUV420P;
    p.color_primaries = ODM_EXPORT_PRIMARIES_BT709;
    p.color_transfer = ODM_EXPORT_TRANSFER_SRGB;
    p.color_matrix = ODM_EXPORT_MATRIX_BT709;
    p.color_range = ODM_EXPORT_RANGE_TV;
    p.rate_control = ODM_EXPORT_RATE_CONTROL_CRF;
    p.crf = crf;
    p.preset = preset;
    p.video_threads = video_threads;
    p.audio_bitrate_kbps = audio_bitrate_kbps;
    p.flags = flags;
    st = odm_export_profile_validate(&p);
    if (st != ODM_STATUS_OK) return st;
    *out_profile = p;
    return ODM_STATUS_OK;
}

odm_status odm_export_profile_validate(const odm_export_profile *p) {
    if (!p) return ODM_STATUS_INVALID_ARGUMENT;
    if (p->schema_version != ODM_EXPORT_SCHEMA_VERSION ||
        p->adapter != ODM_EXPORT_ADAPTER_FFMPEG ||
        p->container != ODM_EXPORT_CONTAINER_MP4 ||
        p->video_codec != ODM_EXPORT_VIDEO_H264 ||
        p->audio_codec != ODM_EXPORT_AUDIO_AAC ||
        p->source_video_format != ODM_EXPORT_SOURCE_RGBA8_SRGB ||
        p->source_audio_format != ODM_EXPORT_SOURCE_PCM_S32LE_Q31_STEREO ||
        p->output_pixel_format != ODM_EXPORT_PIXEL_YUV420P ||
        p->color_primaries != ODM_EXPORT_PRIMARIES_BT709 ||
        p->color_transfer != ODM_EXPORT_TRANSFER_SRGB ||
        p->color_matrix != ODM_EXPORT_MATRIX_BT709 ||
        p->color_range != ODM_EXPORT_RANGE_TV ||
        p->rate_control != ODM_EXPORT_RATE_CONTROL_CRF ||
        p->crf > 51u || preset_name(p->preset) == NULL ||
        p->video_threads == 0u || p->video_threads > 64u ||
        p->audio_bitrate_kbps < 64u || p->audio_bitrate_kbps > 512u ||
        (p->flags & ~ODM_EXPORT_FLAG_FASTSTART) != 0u ||
        !zero_u32(p->reserved, 10u)) return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static odm_status profile_encode(const odm_export_profile *p, uint8_t *buffer,
                                 uint64_t capacity, uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint32_t i;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_EXPORT_PROFILE_BYTES;
    st = odm_export_profile_validate(p); if (st != ODM_STATUS_OK) return st;
    if (!buffer || capacity < ODM_EXPORT_PROFILE_BYTES) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_EXPORT_PROFILE_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_EXPORT_PROFILE_BYTES); if (st != ODM_STATUS_OK) return st;
#define W32(v) do { st=odm_wire_write_u32(&w,(v)); if(st!=ODM_STATUS_OK)return st; } while(0)
    st = odm_wire_write_bytes(&w, EXPORT_PROFILE_MAGIC, 8u); if (st != ODM_STATUS_OK) return st;
    W32(p->schema_version); W32(p->adapter); W32(p->container); W32(p->video_codec);
    W32(p->audio_codec); W32(p->source_video_format); W32(p->source_audio_format);
    W32(p->output_pixel_format); W32(p->color_primaries); W32(p->color_transfer);
    W32(p->color_matrix); W32(p->color_range); W32(p->rate_control); W32(p->crf);
    W32(p->preset); W32(p->video_threads); W32(p->audio_bitrate_kbps); W32(p->flags);
    for (i=0u;i<10u;++i) W32(0u);
#undef W32
    {
        uint64_t used = 0u;
        st = odm_wire_writer_finish(&w, &used);
        if (st != ODM_STATUS_OK) return st;
        if (used > ODM_EXPORT_PROFILE_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
        *out_required = ODM_EXPORT_PROFILE_BYTES;
        return ODM_STATUS_OK;
    }
}

odm_status odm_export_profile_bytes(const odm_export_profile *profile,
                                    uint8_t *buffer, uint64_t capacity,
                                    uint64_t *out_required) {
    uint64_t used=0u; odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st=profile_encode(profile,buffer,capacity,&used); *out_required=ODM_EXPORT_PROFILE_BYTES; return st;
}

odm_status odm_export_profile_sha256(const odm_export_profile *profile,
                                     odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_EXPORT_PROFILE_BYTES]; uint64_t n=0u; odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st=profile_encode(profile,bytes,sizeof(bytes),&n); if(st!=ODM_STATUS_OK)return st;
    return odm_sha256(bytes,sizeof(bytes),out_hash);
}

static odm_status recipe_identity(const odm_export_recipe *r, odm_sha256_digest *out) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER; odm_status st;
    uint8_t bytes[ODM_EXPORT_RECIPE_BYTES]; uint64_t n=0u;
    odm_export_recipe tmp;
    if (!r || !out) return ODM_STATUS_INVALID_ARGUMENT;
    tmp=*r; memset(&tmp.export_recipe_sha256,0,sizeof(tmp.export_recipe_sha256));
    st=odm_export_recipe_bytes(&tmp,bytes,sizeof(bytes),&n); if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_init(&c); if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,EXPORT_DOMAIN,sizeof(EXPORT_DOMAIN)); if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256_update(&c,bytes,sizeof(bytes)); if(st!=ODM_STATUS_OK)return st;
    return odm_sha256_final(&c,out);
}

static odm_status recipe_semantic_validate(const odm_export_recipe *r, int check_id) {
    odm_sha256_digest id;
    uint64_t expected_video_total, expected_audio_bytes;
    if (!r) return ODM_STATUS_INVALID_ARGUMENT;
    if (r->schema_version!=ODM_EXPORT_SCHEMA_VERSION ||
        r->adapter!=ODM_EXPORT_ADAPTER_FFMPEG || r->container!=ODM_EXPORT_CONTAINER_MP4 ||
        r->video_codec!=ODM_EXPORT_VIDEO_H264 || r->audio_codec!=ODM_EXPORT_AUDIO_AAC ||
        r->source_video_format!=ODM_EXPORT_SOURCE_RGBA8_SRGB ||
        r->source_audio_format!=ODM_EXPORT_SOURCE_PCM_S32LE_Q31_STEREO ||
        r->output_pixel_format!=ODM_EXPORT_PIXEL_YUV420P ||
        r->width==0u || r->height==0u || r->width>16384u || r->height>16384u ||
        (r->width&1u)!=0u || (r->height&1u)!=0u ||
        r->audio_sample_rate!=48000u || r->audio_channels!=2u ||
        r->crf>51u || preset_name(r->preset)==NULL || r->video_threads==0u || r->video_threads>64u ||
        r->audio_bitrate_kbps<64u || r->audio_bitrate_kbps>512u ||
        (r->flags&~ODM_EXPORT_FLAG_FASTSTART)!=0u || r->frame_count==0u ||
        r->samples_per_frame<=0 || r->project_end_sample<0 || r->output_end_sample<=0 ||
        r->frame_count > (uint64_t)INT64_MAX / (uint64_t)r->samples_per_frame ||
        r->output_end_sample!=(int64_t)(r->frame_count*(uint64_t)r->samples_per_frame) ||
        r->audio_source_frames>(uint64_t)r->project_end_sample ||
        r->audio_pad_frames!=(uint64_t)r->output_end_sample-r->audio_source_frames ||
        r->source_audio_total_frames!=(uint64_t)r->output_end_sample ||
        r->source_video_bytes_per_frame!=(uint64_t)r->width*r->height*4u ||
        !digest_nonzero(&r->layered_policy_sha256) || !digest_nonzero(&r->layered_config_sha256) ||
        !digest_nonzero(&r->export_profile_sha256) || !digest_nonzero(&r->export_policy_sha256))
        return ODM_STATUS_INVALID_DATA;
    {
        odm_sha256_digest current_policy;
        odm_status pst = odm_export_policy_current_sha256(&current_policy);
        if (pst != ODM_STATUS_OK) return pst;
        if (!odm_sha256_equal(&current_policy, &r->export_policy_sha256))
            return ODM_STATUS_VERSION_MISMATCH;
    }
    if (r->source_video_bytes_per_frame!=0u && r->frame_count>UINT64_MAX/r->source_video_bytes_per_frame)
        return ODM_STATUS_INVALID_DATA;
    expected_video_total=r->source_video_bytes_per_frame*r->frame_count;
    if(r->source_video_total_bytes!=expected_video_total)return ODM_STATUS_INVALID_DATA;
    if(r->source_audio_total_frames>UINT64_MAX/8u)return ODM_STATUS_INVALID_DATA;
    expected_audio_bytes=r->source_audio_total_frames*8u;
    if(r->source_audio_total_bytes!=expected_audio_bytes)return ODM_STATUS_INVALID_DATA;
    if(check_id){
        if(!digest_nonzero(&r->export_recipe_sha256))return ODM_STATUS_INVALID_DATA;
        if(recipe_identity(r,&id)!=ODM_STATUS_OK)return ODM_STATUS_INVARIANT_BROKEN;
        if(!odm_sha256_equal(&id,&r->export_recipe_sha256))return ODM_STATUS_INTEGRITY_ERROR;
    }
    return ODM_STATUS_OK;
}

odm_status odm_export_recipe_bytes(const odm_export_recipe *r,
                                   uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required) {
    odm_wire_writer w=ODM_WIRE_WRITER_INITIALIZER; odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_EXPORT_RECIPE_BYTES;
    st=recipe_semantic_validate(r,0); if(st!=ODM_STATUS_OK)return st;
    if(!buffer||capacity<ODM_EXPORT_RECIPE_BYTES)return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer,0,ODM_EXPORT_RECIPE_BYTES); st=odm_wire_writer_init(&w,buffer,ODM_EXPORT_RECIPE_BYTES);if(st!=ODM_STATUS_OK)return st;
#define W32(v) do{st=odm_wire_write_u32(&w,(v));if(st!=ODM_STATUS_OK)return st;}while(0)
#define WI32(v) do{st=odm_wire_write_i32(&w,(v));if(st!=ODM_STATUS_OK)return st;}while(0)
#define W64(v) do{st=odm_wire_write_u64(&w,(v));if(st!=ODM_STATUS_OK)return st;}while(0)
#define WI64(v) do{st=odm_wire_write_i64(&w,(v));if(st!=ODM_STATUS_OK)return st;}while(0)
#define WD(v) do{st=odm_wire_write_bytes(&w,(v).bytes,32u);if(st!=ODM_STATUS_OK)return st;}while(0)
    st=odm_wire_write_bytes(&w,EXPORT_RECIPE_MAGIC,8u);if(st!=ODM_STATUS_OK)return st;
    W32(r->schema_version);W32(r->adapter);W32(r->container);W32(r->video_codec);W32(r->audio_codec);
    W32(r->source_video_format);W32(r->source_audio_format);W32(r->output_pixel_format);
    W32(r->width);W32(r->height);WI32(r->fps);W32(r->audio_sample_rate);W32(r->audio_channels);
    W32(r->crf);W32(r->preset);W32(r->video_threads);W32(r->audio_bitrate_kbps);W32(r->flags);
    W64(r->frame_count);WI64(r->samples_per_frame);WI64(r->project_end_sample);WI64(r->output_end_sample);
    W64(r->audio_source_frames);W64(r->audio_pad_frames);W64(r->source_video_bytes_per_frame);
    W64(r->source_video_total_bytes);W64(r->source_audio_total_frames);W64(r->source_audio_total_bytes);
    WD(r->layered_policy_sha256);WD(r->layered_config_sha256);WD(r->export_profile_sha256);WD(r->export_recipe_sha256);WD(r->export_policy_sha256);
#undef W32
#undef WI32
#undef W64
#undef WI64
#undef WD
    {
        uint64_t used = 0u;
        st = odm_wire_writer_finish(&w, &used);
        if (st != ODM_STATUS_OK) return st;
        if (used > ODM_EXPORT_RECIPE_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
        *out_required = ODM_EXPORT_RECIPE_BYTES;
        return ODM_STATUS_OK;
    }
}

odm_status odm_export_recipe_validate(const odm_export_recipe *recipe) {
    return recipe_semantic_validate(recipe,1);
}

odm_status odm_export_recipe_build(const odm_export_plan *p,
                                   const odm_export_profile *profile,
                                   odm_export_recipe *out) {
    odm_export_recipe r; odm_sha256_digest id; odm_status st;
    if(!p||!profile||!out)return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_export_profile_validate(profile);if(st!=ODM_STATUS_OK)return st;
    if(p->schema_version!=ODM_LAYERED_SCHEMA_VERSION ||
       p->output_pixel_format!=ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE ||
       p->width==0u||p->height==0u||(p->width&1u)||(p->height&1u)||p->frame_count==0u||
       p->audio_sample_rate!=48000u||p->audio_channels!=2u||p->samples_per_frame<=0||
       p->output_end_sample<=0||p->audio_source_frames>(uint64_t)p->project_end_sample||
       !digest_nonzero(&p->layered_policy_sha256)||!digest_nonzero(&p->config_sha256))return ODM_STATUS_INVALID_DATA;
    memset(&r,0,sizeof(r));r.schema_version=1;r.adapter=profile->adapter;r.container=profile->container;
    r.video_codec=profile->video_codec;r.audio_codec=profile->audio_codec;r.source_video_format=profile->source_video_format;
    r.source_audio_format=profile->source_audio_format;r.output_pixel_format=profile->output_pixel_format;
    r.width=p->width;r.height=p->height;r.fps=p->fps;r.audio_sample_rate=p->audio_sample_rate;r.audio_channels=p->audio_channels;
    r.crf=profile->crf;r.preset=profile->preset;r.video_threads=profile->video_threads;r.audio_bitrate_kbps=profile->audio_bitrate_kbps;r.flags=profile->flags;
    r.frame_count=p->frame_count;r.samples_per_frame=p->samples_per_frame;r.project_end_sample=p->project_end_sample;r.output_end_sample=p->output_end_sample;
    r.audio_source_frames=p->audio_source_frames;r.audio_pad_frames=p->audio_pad_frames;
    r.source_video_bytes_per_frame=(uint64_t)p->width*p->height*4u;
    if(r.frame_count>UINT64_MAX/r.source_video_bytes_per_frame)return ODM_STATUS_OVERFLOW;
    r.source_video_total_bytes=r.source_video_bytes_per_frame*r.frame_count;
    r.source_audio_total_frames=(uint64_t)r.output_end_sample;
    if(r.source_audio_total_frames>UINT64_MAX/8u)return ODM_STATUS_OVERFLOW;
    r.source_audio_total_bytes=r.source_audio_total_frames*8u;
    r.layered_policy_sha256=p->layered_policy_sha256;r.layered_config_sha256=p->config_sha256;
    st=odm_export_profile_sha256(profile,&r.export_profile_sha256);if(st!=ODM_STATUS_OK)return st;
    st=odm_export_policy_current_sha256(&r.export_policy_sha256);if(st!=ODM_STATUS_OK)return st;
    st=recipe_identity(&r,&id);if(st!=ODM_STATUS_OK)return st;r.export_recipe_sha256=id;
    st=recipe_semantic_validate(&r,1);if(st!=ODM_STATUS_OK)return st;*out=r;return ODM_STATUS_OK;
}

odm_status odm_export_frame_sample(const odm_export_recipe *r,
                                   uint64_t frame_index,
                                   int64_t *out_sample) {
    odm_status st;
    if (!out_sample) return ODM_STATUS_INVALID_ARGUMENT;
    *out_sample = 0;
    st=odm_export_recipe_validate(r);if(st!=ODM_STATUS_OK)return st;
    if(frame_index>=r->frame_count)return ODM_STATUS_INVALID_ARGUMENT;
    if(frame_index>(uint64_t)INT64_MAX/(uint64_t)r->samples_per_frame)return ODM_STATUS_OVERFLOW;
    *out_sample=(int64_t)(frame_index*(uint64_t)r->samples_per_frame);return ODM_STATUS_OK;
}

static void put_i32le(uint8_t out[4], int32_t v) {
    uint32_t u=(uint32_t)v;out[0]=(uint8_t)u;out[1]=(uint8_t)(u>>8u);out[2]=(uint8_t)(u>>16u);out[3]=(uint8_t)(u>>24u);
}

odm_status odm_export_audio_chunk_s32le(const odm_export_recipe *r,
    const odm_pcm_stereo_q31 *pcm,uint64_t pcm_frames,uint64_t start,uint64_t frames,
    uint8_t *out,uint64_t cap,uint64_t *req) {
    uint64_t i,bytes;odm_status st;
    if (!req) return ODM_STATUS_INVALID_ARGUMENT;
    *req = 0u;
    st = odm_export_recipe_validate(r);
    if (st != ODM_STATUS_OK) return st;
    if(pcm_frames!=r->audio_source_frames || (pcm_frames>0u&&!pcm) || start>r->source_audio_total_frames ||
       frames>r->source_audio_total_frames-start)return ODM_STATUS_INVALID_DATA;
    if (frames > UINT64_MAX / 8u) return ODM_STATUS_OVERFLOW;
    bytes = frames * 8u;
    *req = bytes;
    if((bytes>0u&&!out)||cap<bytes)return ODM_STATUS_BUFFER_TOO_SMALL;
    for(i=0u;i<frames;++i){uint64_t n=start+i;int32_t l=0,rr=0;if(n<pcm_frames){l=pcm[n].left;rr=pcm[n].right;}put_i32le(out+i*8u,l);put_i32le(out+i*8u+4u,rr);}return ODM_STATUS_OK;
}

static odm_status add_token(char *buffer,uint64_t cap,uint64_t *used,uint32_t *count,const char *token) {
    size_t n;
    if (!used || !count || !token) return ODM_STATUS_INVALID_ARGUMENT;
    n = strlen(token) + 1u;
    if((uint64_t)n>UINT64_MAX-*used)return ODM_STATUS_OVERFLOW;
    if(buffer&&*used+(uint64_t)n<=cap)memcpy(buffer+*used,token,n);
    *used+=(uint64_t)n;(*count)++;return ODM_STATUS_OK;
}

odm_status odm_export_ffmpeg_argv_template(const odm_export_recipe *r,
    char *buffer,uint64_t cap,uint64_t *out_required,uint32_t *out_count) {
    char size[48],fps[24],crf[24],threads[24],abr[24],frames[32];
    uint64_t used=0u;uint32_t count=0u;odm_status st;const char *preset;
    const char *tokens[ODM_EXPORT_ARGV_MAX_TOKENS]; uint32_t n=0u,i;
    if (!out_required || !out_count) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = 0u;
    *out_count = 0u;
    st=odm_export_recipe_validate(r);if(st!=ODM_STATUS_OK)return st;preset=preset_name(r->preset);if(!preset)return ODM_STATUS_INVALID_DATA;
    (void)snprintf(size,sizeof(size),"%ux%u",r->width,r->height);(void)snprintf(fps,sizeof(fps),"%d",r->fps);
    (void)snprintf(crf,sizeof(crf),"%u",r->crf);(void)snprintf(threads,sizeof(threads),"%u",r->video_threads);
    (void)snprintf(abr,sizeof(abr),"%uk",r->audio_bitrate_kbps);(void)snprintf(frames,sizeof(frames),"%llu",(unsigned long long)r->frame_count);
#define T(x) do{if(n>=ODM_EXPORT_ARGV_MAX_TOKENS)return ODM_STATUS_INVARIANT_BROKEN;tokens[n++]=(x);}while(0)
    T("ffmpeg");T("-hide_banner");T("-loglevel");T("error");T("-nostdin");T("-n");T("-f");T("rawvideo");T("-pixel_format");T("rgba");T("-video_size");T(size);T("-framerate");T(fps);T("-i");T(ODM_EXPORT_PLACEHOLDER_VIDEO);
    T("-f");T("s32le");T("-ar");T("48000");T("-ac");T("2");T("-i");T(ODM_EXPORT_PLACEHOLDER_AUDIO);
    T("-map");T("0:v:0");T("-map");T("1:a:0");T("-map_metadata");T("-1");T("-map_chapters");T("-1");
    T("-c:v");T("libx264");T("-preset");T(preset);T("-crf");T(crf);T("-threads");T(threads);
    T("-pix_fmt");T("yuv420p");T("-colorspace");T("bt709");T("-color_primaries");T("bt709");T("-color_trc");T("iec61966-2-1");T("-color_range");T("tv");
    T("-c:a");T("aac");T("-b:a");T(abr);T("-frames:v");T(frames);
    if((r->flags&ODM_EXPORT_FLAG_FASTSTART)!=0u){T("-movflags");T("+faststart");}
    T(ODM_EXPORT_PLACEHOLDER_OUTPUT);
#undef T
    if(n>ODM_EXPORT_ARGV_MAX_TOKENS)return ODM_STATUS_INVARIANT_BROKEN;
    /* Transactional publication: size pass must not touch caller memory. */
    for(i=0u;i<n;++i){st=add_token(NULL,0u,&used,&count,tokens[i]);if(st!=ODM_STATUS_OK)return st;}
    *out_required=used;*out_count=count;
    if (!buffer || cap < used) return ODM_STATUS_BUFFER_TOO_SMALL;
    used=0u;count=0u;
    for(i=0u;i<n;++i){st=add_token(buffer,cap,&used,&count,tokens[i]);if(st!=ODM_STATUS_OK)return st;}
    if(count!=n)return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

static odm_status export_path_length(const char *path, uint64_t *out_len) {
    uint64_t n;
    if (!path || !out_len) return ODM_STATUS_INVALID_ARGUMENT;
    for (n = 0u; n < ODM_EXPORT_PATH_MAX_BYTES; ++n) {
        if (path[n] == '\0') {
            if (n == 0u) return ODM_STATUS_INVALID_DATA;
            *out_len = n;
            return ODM_STATUS_OK;
        }
    }
    return ODM_STATUS_BUDGET_EXCEEDED;
}

odm_status odm_export_ffmpeg_argv_bind(
    const odm_export_recipe *recipe,
    const char *video_path, const char *audio_path, const char *output_path,
    char *buffer, uint64_t capacity,
    uint64_t *out_required, uint32_t *out_count,
    uint32_t *offsets, uint32_t offsets_capacity) {
    char tmpl[ODM_EXPORT_ARGV_MAX_BYTES];
    uint64_t tmpl_required = 0u, used = 0u;
    uint64_t vl = 0u, al = 0u, ol = 0u;
    uint32_t tmpl_count = 0u, count = 0u;
    uint64_t pos = 0u;
    odm_status st;

    if (!out_required || !out_count) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = 0u; *out_count = 0u;
    st = export_path_length(video_path, &vl); if (st != ODM_STATUS_OK) return st;
    st = export_path_length(audio_path, &al); if (st != ODM_STATUS_OK) return st;
    st = export_path_length(output_path, &ol); if (st != ODM_STATUS_OK) return st;
    (void)vl; (void)al; (void)ol;
    if (strcmp(video_path, audio_path) == 0 || strcmp(video_path, output_path) == 0 ||
        strcmp(audio_path, output_path) == 0) return ODM_STATUS_INVALID_DATA;
    st = odm_export_ffmpeg_argv_template(recipe, tmpl, sizeof(tmpl),
                                         &tmpl_required, &tmpl_count);
    if (st != ODM_STATUS_OK) return st;
    while (pos < tmpl_required && count < tmpl_count) {
        const char *token = tmpl + pos;
        const char *bound = token;
        size_t tn = strlen(token);
        if (strcmp(token, ODM_EXPORT_PLACEHOLDER_VIDEO) == 0) bound = video_path;
        else if (strcmp(token, ODM_EXPORT_PLACEHOLDER_AUDIO) == 0) bound = audio_path;
        else if (strcmp(token, ODM_EXPORT_PLACEHOLDER_OUTPUT) == 0) bound = output_path;
        st = add_token(NULL, 0u, &used, &count, bound);
        if (st != ODM_STATUS_OK) return st;
        pos += (uint64_t)tn + 1u;
    }
    if (pos != tmpl_required || count != tmpl_count || used > ODM_EXPORT_ARGV_MAX_BYTES)
        return ODM_STATUS_INVARIANT_BROKEN;
    *out_required = used; *out_count = count;
    if (!buffer || capacity < used || (offsets && offsets_capacity < count))
        return ODM_STATUS_BUFFER_TOO_SMALL;

    pos = 0u; used = 0u; count = 0u;
    while (pos < tmpl_required && count < tmpl_count) {
        const char *token = tmpl + pos;
        const char *bound = token;
        size_t tn = strlen(token);
        if (strcmp(token, ODM_EXPORT_PLACEHOLDER_VIDEO) == 0) bound = video_path;
        else if (strcmp(token, ODM_EXPORT_PLACEHOLDER_AUDIO) == 0) bound = audio_path;
        else if (strcmp(token, ODM_EXPORT_PLACEHOLDER_OUTPUT) == 0) bound = output_path;
        if (offsets) offsets[count] = (uint32_t)used;
        st = add_token(buffer, capacity, &used, &count, bound);
        if (st != ODM_STATUS_OK) return st;
        pos += (uint64_t)tn + 1u;
    }
    if (pos != tmpl_required || count != tmpl_count) return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

/* -------------------------------------------------------------------------
 * Native Export Session v1
 * ------------------------------------------------------------------------- */

#define EXPORT_RUN_MAGIC "ODMEXRN1"
static const uint8_t EXPORT_VIDEO_DOMAIN[] = "ODPAR_EXPORT_RGBA8_STREAM_V1";
static const uint8_t EXPORT_AUDIO_DOMAIN[] = "ODPAR_EXPORT_S32LE_STREAM_V1";
static const uint8_t EXPORT_RUN_RECEIPT_DOMAIN[] = "ODPAR_EXPORT_RUN_RECEIPT_V1";

static int export_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out || a > UINT64_MAX - b) return 0;
    *out = a + b;
    return 1;
}

static int export_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out || (a != 0u && b > UINT64_MAX / a)) return 0;
    *out = a * b;
    return 1;
}

static int export_ranges_overlap(const void *a, uint64_t an,
                                 const void *b, uint64_t bn) {
    uintptr_t ap, bp;
    if (!a || !b || an == 0u || bn == 0u) return 0;
    if (an > (uint64_t)SIZE_MAX || bn > (uint64_t)SIZE_MAX) return 1;
    ap = (uintptr_t)a;
    bp = (uintptr_t)b;
    if (ap > UINTPTR_MAX - (uintptr_t)an || bp > UINTPTR_MAX - (uintptr_t)bn)
        return 1;
    return ap < bp + (uintptr_t)bn && bp < ap + (uintptr_t)an;
}

static odm_status export_hash_stream_u64(export_sha256_stream *context, uint64_t value) {
    uint8_t bytes[8];
    uint32_t i;
    for (i = 0u; i < 8u; ++i) bytes[i] = (uint8_t)(value >> (8u * i));
    return export_sha256_stream_update(context, bytes, sizeof(bytes));
}

static odm_status export_check_cancel(const odm_job_ticket *ticket) {
    int cancelled = 0;
    odm_status st;
    if (!ticket) return ODM_STATUS_OK;
    st = odm_job_should_cancel(*ticket, &cancelled);
    if (st != ODM_STATUS_OK) return st;
    return cancelled ? ODM_STATUS_CANCELLED : ODM_STATUS_OK;
}

static odm_status export_publish(const odm_job_ticket *ticket,
                                 odm_job_phase phase, uint64_t done) {
    return ticket ? odm_job_publish(*ticket, phase, done) : ODM_STATUS_OK;
}

static void export_abort_sink(const odm_export_sink *sink, int active,
                              odm_status reason) {
    if (active && sink && sink->abort) sink->abort(sink->user, reason);
}

static odm_status export_run_receipt_encode(const odm_export_run_receipt *receipt,
                                            uint8_t *buffer, uint64_t capacity,
                                            uint64_t *out_required,
                                            int zero_identity) {
    odm_wire_writer writer = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    odm_sha256_digest zero = {{0}};
    const uint8_t *identity;
    uint32_t i;
    uint64_t used = 0u;

    if (!receipt || !out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_EXPORT_RUN_RECEIPT_BYTES;
    if (!buffer || capacity < ODM_EXPORT_RUN_RECEIPT_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;

    identity = zero_identity ? zero.bytes : receipt->export_run_sha256.bytes;
    memset(buffer, 0, ODM_EXPORT_RUN_RECEIPT_BYTES);
    st = odm_wire_writer_init(&writer, buffer, ODM_EXPORT_RUN_RECEIPT_BYTES);
    if (st != ODM_STATUS_OK) return st;
#define EW(expr) do { st = (expr); if (st != ODM_STATUS_OK) return st; } while (0)
    EW(odm_wire_write_bytes(&writer, EXPORT_RUN_MAGIC, 8u));
    EW(odm_wire_write_u32(&writer, receipt->schema_version));
    EW(odm_wire_write_u32(&writer, receipt->flags));
    EW(odm_wire_write_u32(&writer, receipt->width));
    EW(odm_wire_write_u32(&writer, receipt->height));
    EW(odm_wire_write_i32(&writer, receipt->fps));
    EW(odm_wire_write_u32(&writer, receipt->pixel_format));
    EW(odm_wire_write_u32(&writer, receipt->audio_sample_rate));
    EW(odm_wire_write_u32(&writer, receipt->audio_channels));
    EW(odm_wire_write_u64(&writer, receipt->frame_count));
    EW(odm_wire_write_i64(&writer, receipt->samples_per_frame));
    EW(odm_wire_write_i64(&writer, receipt->project_end_sample));
    EW(odm_wire_write_i64(&writer, receipt->output_end_sample));
    EW(odm_wire_write_u64(&writer, receipt->video_bytes));
    EW(odm_wire_write_u64(&writer, receipt->audio_frames));
    EW(odm_wire_write_u64(&writer, receipt->audio_bytes));
    EW(odm_wire_write_bytes(&writer, receipt->export_recipe_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, receipt->export_policy_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, receipt->layered_policy_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, receipt->layered_config_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, receipt->canonical_pcm_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, receipt->video_stream_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, receipt->audio_stream_sha256.bytes, 32u));
    EW(odm_wire_write_bytes(&writer, identity, 32u));
    for (i = 0u; i < 8u; ++i) EW(odm_wire_write_u32(&writer, 0u));
#undef EW
    st = odm_wire_writer_finish(&writer, &used);
    if (st != ODM_STATUS_OK) return st;
    if (used > ODM_EXPORT_RUN_RECEIPT_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    *out_required = ODM_EXPORT_RUN_RECEIPT_BYTES;
    return ODM_STATUS_OK;
}

static odm_status export_run_receipt_identity(const odm_export_run_receipt *receipt,
                                              odm_sha256_digest *out) {
    odm_sha256_context hash = ODM_SHA256_CONTEXT_INITIALIZER;
    uint8_t bytes[ODM_EXPORT_RUN_RECEIPT_BYTES];
    uint64_t required = 0u;
    odm_status st;
    if (!receipt || !out) return ODM_STATUS_INVALID_ARGUMENT;
    st = export_run_receipt_encode(receipt, bytes, sizeof(bytes), &required, 1);
    if (st != ODM_STATUS_OK) return st;
    if (required != sizeof(bytes)) return ODM_STATUS_INVARIANT_BROKEN;
    st = odm_sha256_init(&hash);
    if (st != ODM_STATUS_OK) return st;
    st = odm_sha256_update(&hash, EXPORT_RUN_RECEIPT_DOMAIN,
                           sizeof(EXPORT_RUN_RECEIPT_DOMAIN));
    if (st != ODM_STATUS_OK) return st;
    st = odm_sha256_update(&hash, bytes, sizeof(bytes));
    if (st != ODM_STATUS_OK) return st;
    return odm_sha256_final(&hash, out);
}

odm_status odm_export_run_receipt_bytes(const odm_export_run_receipt *receipt,
                                        uint8_t *buffer, uint64_t capacity,
                                        uint64_t *out_required) {
    return export_run_receipt_encode(receipt, buffer, capacity, out_required, 0);
}

static odm_status export_run_receipt_semantic_validate(
    const odm_export_run_receipt *receipt, int verify_identity) {
    odm_sha256_digest identity;
    uint64_t video_bytes, audio_bytes;

    if (!receipt) return ODM_STATUS_INVALID_ARGUMENT;
    if (receipt->schema_version != ODM_EXPORT_RUN_SCHEMA_VERSION ||
        receipt->flags != 0u ||
        receipt->width == 0u || receipt->height == 0u ||
        receipt->width > 16384u || receipt->height > 16384u ||
        (receipt->width & 1u) != 0u || (receipt->height & 1u) != 0u ||
        !odm_time_fps_supported(receipt->fps) ||
        receipt->pixel_format != ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE ||
        receipt->audio_sample_rate != 48000u || receipt->audio_channels != 2u ||
        receipt->frame_count == 0u || receipt->samples_per_frame <= 0 ||
        receipt->project_end_sample < 0 || receipt->output_end_sample <= 0 ||
        receipt->project_end_sample > receipt->output_end_sample ||
        receipt->frame_count > (uint64_t)INT64_MAX /
                               (uint64_t)receipt->samples_per_frame ||
        receipt->output_end_sample !=
            (int64_t)(receipt->frame_count * (uint64_t)receipt->samples_per_frame) ||
        receipt->audio_frames != (uint64_t)receipt->output_end_sample ||
        !digest_nonzero(&receipt->export_recipe_sha256) ||
        !digest_nonzero(&receipt->export_policy_sha256) ||
        !digest_nonzero(&receipt->layered_policy_sha256) ||
        !digest_nonzero(&receipt->layered_config_sha256) ||
        !digest_nonzero(&receipt->canonical_pcm_sha256) ||
        !digest_nonzero(&receipt->video_stream_sha256) ||
        !digest_nonzero(&receipt->audio_stream_sha256) ||
        !zero_u32(receipt->reserved, 8u))
        return ODM_STATUS_INVALID_DATA;

    if (!export_mul_u64((uint64_t)receipt->width, (uint64_t)receipt->height,
                        &video_bytes) ||
        !export_mul_u64(video_bytes, 4u, &video_bytes) ||
        !export_mul_u64(video_bytes, receipt->frame_count, &video_bytes) ||
        video_bytes != receipt->video_bytes)
        return ODM_STATUS_INVALID_DATA;
    if (!export_mul_u64(receipt->audio_frames, 8u, &audio_bytes) ||
        audio_bytes != receipt->audio_bytes)
        return ODM_STATUS_INVALID_DATA;

    if (verify_identity) {
        odm_status st;
        if (!digest_nonzero(&receipt->export_run_sha256)) return ODM_STATUS_INVALID_DATA;
        st = export_run_receipt_identity(receipt, &identity);
        if (st != ODM_STATUS_OK) return st;
        if (!odm_sha256_equal(&identity, &receipt->export_run_sha256))
            return ODM_STATUS_INTEGRITY_ERROR;
    }
    return ODM_STATUS_OK;
}

odm_status odm_export_run_receipt_validate(
    const uint8_t *record, uint64_t record_bytes,
    const odm_sha256_digest *expected_recipe_sha256,
    odm_export_run_receipt *out_receipt) {
    odm_wire_reader reader = ODM_WIRE_READER_INITIALIZER;
    odm_export_run_receipt receipt;
    uint8_t magic[8];
    uint32_t i;
    odm_status st;

    if (!record || record_bytes != ODM_EXPORT_RUN_RECEIPT_BYTES)
        return ODM_STATUS_INVALID_ARGUMENT;
    memset(&receipt, 0, sizeof(receipt));
    st = odm_wire_reader_init(&reader, record, record_bytes);
    if (st != ODM_STATUS_OK) return st;
#define ER(expr) do { st = (expr); if (st != ODM_STATUS_OK) return st; } while (0)
    ER(odm_wire_read_bytes(&reader, magic, 8u));
    if (memcmp(magic, EXPORT_RUN_MAGIC, 8u) != 0) return ODM_STATUS_INVALID_DATA;
    ER(odm_wire_read_u32(&reader, &receipt.schema_version));
    ER(odm_wire_read_u32(&reader, &receipt.flags));
    ER(odm_wire_read_u32(&reader, &receipt.width));
    ER(odm_wire_read_u32(&reader, &receipt.height));
    ER(odm_wire_read_i32(&reader, &receipt.fps));
    ER(odm_wire_read_u32(&reader, &receipt.pixel_format));
    ER(odm_wire_read_u32(&reader, &receipt.audio_sample_rate));
    ER(odm_wire_read_u32(&reader, &receipt.audio_channels));
    ER(odm_wire_read_u64(&reader, &receipt.frame_count));
    ER(odm_wire_read_i64(&reader, &receipt.samples_per_frame));
    ER(odm_wire_read_i64(&reader, &receipt.project_end_sample));
    ER(odm_wire_read_i64(&reader, &receipt.output_end_sample));
    ER(odm_wire_read_u64(&reader, &receipt.video_bytes));
    ER(odm_wire_read_u64(&reader, &receipt.audio_frames));
    ER(odm_wire_read_u64(&reader, &receipt.audio_bytes));
    ER(odm_wire_read_bytes(&reader, receipt.export_recipe_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.export_policy_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.layered_policy_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.layered_config_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.canonical_pcm_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.video_stream_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.audio_stream_sha256.bytes, 32u));
    ER(odm_wire_read_bytes(&reader, receipt.export_run_sha256.bytes, 32u));
    for (i = 0u; i < 8u; ++i) ER(odm_wire_read_u32(&reader, &receipt.reserved[i]));
#undef ER
    while (reader.offset < record_bytes) {
        uint8_t zero = 1u;
        st = odm_wire_read_u8(&reader, &zero);
        if (st != ODM_STATUS_OK) return st;
        if (zero != 0u) return ODM_STATUS_INVALID_DATA;
    }
    st = odm_wire_reader_finish(&reader);
    if (st != ODM_STATUS_OK) return st;
    st = export_run_receipt_semantic_validate(&receipt, 1);
    if (st != ODM_STATUS_OK) return st;
    {
        odm_sha256_digest current_export_policy;
        st = odm_export_policy_current_sha256(&current_export_policy);
        if (st != ODM_STATUS_OK) return st;
        if (!odm_sha256_equal(&current_export_policy, &receipt.export_policy_sha256))
            return ODM_STATUS_INTEGRITY_ERROR;
    }
    if (expected_recipe_sha256 &&
        !odm_sha256_equal(expected_recipe_sha256, &receipt.export_recipe_sha256))
        return ODM_STATUS_INTEGRITY_ERROR;
    if (out_receipt) *out_receipt = receipt;
    return ODM_STATUS_OK;
}

static odm_status export_request_validate(const odm_export_run_request *request) {
    odm_sha256_digest config_sha, policy_sha;
    odm_status st;
    if (!request || !request->config || !request->recipe ||
        !request->state_provider || !request->core_provider ||
        (request->canonical_pcm_frames > 0u && !request->canonical_pcm))
        return ODM_STATUS_INVALID_ARGUMENT;
    if (request->schema_version != ODM_EXPORT_RUN_SCHEMA_VERSION ||
        (request->flags & ~ODM_EXPORT_RUN_FLAGS_ALLOWED) != 0u ||
        ((request->flags & ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2) != 0u &&
         (request->flags & ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4) != 0u) ||
        request->quality_tier >= (uint32_t)ODM_QUALITY_COUNT ||
        !zero_u32(request->reserved, 3u))
        return ODM_STATUS_INVALID_DATA;
    st = odm_layered_config_validate(request->config);
    if (st != ODM_STATUS_OK) return st;
    st = odm_export_recipe_validate(request->recipe);
    if (st != ODM_STATUS_OK) return st;
    if (request->recipe->source_video_format != ODM_EXPORT_SOURCE_RGBA8_SRGB ||
        request->recipe->source_audio_format != ODM_EXPORT_SOURCE_PCM_S32LE_Q31_STEREO ||
        request->recipe->width != request->config->canvas.width ||
        request->recipe->height != request->config->canvas.height ||
        request->recipe->fps != request->config->canvas.fps ||
        request->canonical_pcm_frames != request->recipe->audio_source_frames)
        return ODM_STATUS_INVALID_DATA;
    st = odm_layered_config_sha256(request->config, &config_sha);
    if (st != ODM_STATUS_OK) return st;
    st = odm_layered_policy_current_sha256(&policy_sha);
    if (st != ODM_STATUS_OK) return st;
    if (!odm_sha256_equal(&config_sha, &request->recipe->layered_config_sha256) ||
        !odm_sha256_equal(&policy_sha, &request->recipe->layered_policy_sha256))
        return ODM_STATUS_INTEGRITY_ERROR;
    return ODM_STATUS_OK;
}

odm_status odm_export_run_requirements(const odm_export_run_request *request,
                                       uint64_t *out_frame_bytes,
                                       uint64_t *out_scratch_bytes,
                                       uint64_t *out_work_units) {
    odm_status st;
    uint64_t frame_bytes = 0u, scratch_bytes = 0u, work_units = 0u;
    if (!out_frame_bytes || !out_scratch_bytes || !out_work_units)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_frame_bytes = 0u;
    *out_scratch_bytes = 0u;
    *out_work_units = 0u;
    st = export_request_validate(request);
    if (st != ODM_STATUS_OK) return st;
    st = odm_layered_render_requirements(
        request->config, ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
        &frame_bytes, &scratch_bytes);
    if (st != ODM_STATUS_OK) return st;
    if (!export_add_u64(request->recipe->frame_count,
                        request->recipe->source_audio_total_frames,
                        &work_units))
        return ODM_STATUS_OVERFLOW;
    *out_frame_bytes = frame_bytes;
    *out_scratch_bytes = scratch_bytes;
    *out_work_units = work_units;
    return ODM_STATUS_OK;
}

odm_status odm_export_runtime_plan_recommend(
    const odm_export_run_request *request,
    uint32_t worker_budget,
    odm_export_runtime_plan *out_plan) {
    odm_export_runtime_plan plan;
    odm_status st;
    uint64_t frame_pixels = 0u;

    if (!out_plan) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&plan, 0, sizeof(plan));
    if (worker_budget == 0u || worker_budget > ODM_EXPORT_RUNTIME_MAX_WORKERS)
        return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_export_run_requirements(request, &plan.frame_bytes,
                                     &plan.scratch_bytes, &plan.work_units);
    if (st != ODM_STATUS_OK) return st;
    if (!export_mul_u64((uint64_t)request->recipe->width,
                        (uint64_t)request->recipe->height, &frame_pixels))
        return ODM_STATUS_OVERFLOW;

    plan.schema_version = ODM_EXPORT_RUNTIME_PLAN_SCHEMA_VERSION;
    plan.worker_budget = worker_budget;
    plan.selected_workers = 1u;
    plan.recommended_flags = request->flags & ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH;
    /* Direct staging removes one full-frame copy. Sink byte pointers are
     * borrowed for the duration of each callback only, so staging lifetime is
     * sufficient and canonical stream bytes are unchanged. */
    plan.recommended_flags |= ODM_EXPORT_RUN_FLAG_DIRECT_STAGING;
    plan.frame_pixels = frame_pixels;

    if (frame_pixels >= ODM_EXPORT_RUNTIME_PARALLEL4_MIN_PIXELS &&
        worker_budget >= 4u) {
        plan.selected_workers = 4u;
        plan.recommended_flags |= ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4;
    } else if (frame_pixels >= ODM_EXPORT_RUNTIME_PARALLEL2_MIN_PIXELS &&
               worker_budget >= 2u) {
        plan.selected_workers = 2u;
        plan.recommended_flags |= ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2;
    }
    *out_plan = plan;
    return ODM_STATUS_OK;
}

odm_status odm_export_run(
    const odm_export_run_request *request,
    const odm_export_sink *sink,
    void *scratch, uint64_t scratch_bytes,
    uint8_t *frame, uint64_t frame_capacity,
    uint8_t *receipt_buffer, uint64_t receipt_capacity,
    uint64_t *out_receipt_required,
    odm_export_run_receipt *out_receipt) {
    odm_status st;
    uint64_t need_frame = 0u, need_scratch = 0u, work_total = 0u;
    uint64_t work_done = 0u, frame_index = 0u, audio_pos = 0u;
    uint64_t video_bytes = 0u, audio_bytes = 0u, pcm_bytes = 0u;
    export_sha256_stream video_hash;
    export_sha256_stream audio_hash;
    export_sha256_stream source_pcm_hash;
    export_sha256_stream admitted_pcm_hash;
    int hash_acceleration = 0;
    int direct_staging = 0;
    uint32_t row_workers = 1u;
    odm_sha256_digest video_digest, audio_digest, source_pcm_digest, admitted_pcm_digest;
    odm_export_run_receipt receipt;
    uint8_t receipt_local[ODM_EXPORT_RUN_RECEIPT_BYTES];
    uint8_t audio_chunk[ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES * 8u];
    uint64_t receipt_required = ODM_EXPORT_RUN_RECEIPT_BYTES;
    odm_layered_config config_snapshot;
    odm_export_recipe recipe_snapshot;
    odm_export_run_request request_snapshot;
    odm_export_sink sink_snapshot;
    odm_job_ticket ticket_snapshot;
    odm_layered_render_admission render_admission;
    odm_layered_worker_pool render_pool;
    int render_pool_active = 0;
    /* Estado del supermuestreo. Reservado una sola vez para todo el export:
     * un raster de alta resolucion por fotograma seria una tormenta de
     * asignaciones en el bucle mas caliente del motor. */
    uint32_t ss_factor = 1u;
    odm_layered_config ss_config;
    odm_layered_render_admission ss_admission;
    odm_layered_worker_pool ss_pool;
    int ss_pool_active = 0;
    uint8_t *ss_frame = NULL, *ss_scratch = NULL;
    uint64_t ss_frame_bytes = 0u, ss_scratch_bytes = 0u;
    odm_sha256_digest ss_digest;
    const odm_export_run_request *run_request = request;
    const odm_export_sink *run_sink = sink;
    int active = 0;

    memset(&video_hash, 0, sizeof(video_hash));
    memset(&audio_hash, 0, sizeof(audio_hash));
    memset(&source_pcm_hash, 0, sizeof(source_pcm_hash));
    memset(&admitted_pcm_hash, 0, sizeof(admitted_pcm_hash));
    memset(&render_pool, 0, sizeof(render_pool));
    memset(&ss_config, 0, sizeof(ss_config));
    memset(&ss_admission, 0, sizeof(ss_admission));
    memset(&ss_pool, 0, sizeof(ss_pool));
    memset(&ss_digest, 0, sizeof(ss_digest));
    if (!out_receipt_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_receipt_required = ODM_EXPORT_RUN_RECEIPT_BYTES;
    if (!sink || !sink->begin || !sink->write_video || !sink->write_audio ||
        !sink->commit || !sink->abort)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (!receipt_buffer || receipt_capacity < ODM_EXPORT_RUN_RECEIPT_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;

    st = odm_export_run_requirements(request, &need_frame, &need_scratch, &work_total);
    if (st != ODM_STATUS_OK) return st;
    direct_staging = (request->flags & ODM_EXPORT_RUN_FLAG_DIRECT_STAGING) != 0u;
    if ((request->flags & ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4) != 0u) row_workers = 4u;
    else if ((request->flags & ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2) != 0u) row_workers = 2u;
    if (!scratch || scratch_bytes < need_scratch ||
        (!direct_staging && (!frame || frame_capacity < need_frame)))
        return ODM_STATUS_BUFFER_TOO_SMALL;
    if (((uintptr_t)scratch & (uintptr_t)7u) != 0u)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (!export_mul_u64(request->canonical_pcm_frames, 8u, &pcm_bytes))
        return ODM_STATUS_OVERFLOW;
    if (need_frame > (uint64_t)SIZE_MAX || need_scratch > (uint64_t)SIZE_MAX ||
        pcm_bytes > (uint64_t)SIZE_MAX)
        return ODM_STATUS_BUDGET_EXCEEDED;

    /* The streaming output buffers may never overwrite immutable input while
     * the session is in flight.  Reject before begin(), so failure is fully
     * transactional from the sink's point of view. */
    if (export_ranges_overlap(frame, need_frame, scratch, need_scratch) ||
        export_ranges_overlap(frame, need_frame, receipt_buffer,
                              ODM_EXPORT_RUN_RECEIPT_BYTES) ||
        export_ranges_overlap(scratch, need_scratch, receipt_buffer,
                              ODM_EXPORT_RUN_RECEIPT_BYTES) ||
        export_ranges_overlap(frame, need_frame, request->canonical_pcm, pcm_bytes) ||
        export_ranges_overlap(scratch, need_scratch, request->canonical_pcm, pcm_bytes) ||
        export_ranges_overlap(receipt_buffer, ODM_EXPORT_RUN_RECEIPT_BYTES,
                              request->canonical_pcm, pcm_bytes) ||
        export_ranges_overlap(frame, need_frame, request, sizeof(*request)) ||
        export_ranges_overlap(scratch, need_scratch, request, sizeof(*request)) ||
        export_ranges_overlap(receipt_buffer, ODM_EXPORT_RUN_RECEIPT_BYTES,
                              request, sizeof(*request)) ||
        export_ranges_overlap(frame, need_frame, request->config, sizeof(*request->config)) ||
        export_ranges_overlap(scratch, need_scratch, request->config, sizeof(*request->config)) ||
        export_ranges_overlap(receipt_buffer, ODM_EXPORT_RUN_RECEIPT_BYTES,
                              request->config, sizeof(*request->config)) ||
        export_ranges_overlap(frame, need_frame, request->recipe, sizeof(*request->recipe)) ||
        export_ranges_overlap(scratch, need_scratch, request->recipe, sizeof(*request->recipe)) ||
        export_ranges_overlap(receipt_buffer, ODM_EXPORT_RUN_RECEIPT_BYTES,
                              request->recipe, sizeof(*request->recipe)) ||
        export_ranges_overlap(frame, need_frame, sink, sizeof(*sink)) ||
        export_ranges_overlap(scratch, need_scratch, sink, sizeof(*sink)) ||
        export_ranges_overlap(receipt_buffer, ODM_EXPORT_RUN_RECEIPT_BYTES,
                              sink, sizeof(*sink)) ||
        (request->job_ticket &&
         (export_ranges_overlap(frame, need_frame, request->job_ticket,
                                sizeof(*request->job_ticket)) ||
          export_ranges_overlap(scratch, need_scratch, request->job_ticket,
                                sizeof(*request->job_ticket)) ||
          export_ranges_overlap(receipt_buffer, ODM_EXPORT_RUN_RECEIPT_BYTES,
                                request->job_ticket, sizeof(*request->job_ticket)))))
        return ODM_STATUS_INVALID_ARGUMENT;

    /* Freeze all small control-plane inputs before begin().  Callbacks are
     * allowed to interact with their own user state, but they must never be
     * able to change the semantics of an in-flight export by mutating the
     * caller-owned request/config/recipe/sink tables after admission. */
    config_snapshot = *request->config;
    recipe_snapshot = *request->recipe;
    request_snapshot = *request;
    sink_snapshot = *sink;
    request_snapshot.config = &config_snapshot;
    request_snapshot.recipe = &recipe_snapshot;
    if (request->job_ticket) {
        ticket_snapshot = *request->job_ticket;
        request_snapshot.job_ticket = &ticket_snapshot;
    } else {
        request_snapshot.job_ticket = NULL;
    }
    run_request = &request_snapshot;
    run_sink = &sink_snapshot;

    /* Revalidate the frozen snapshot, not the caller-owned objects.  This also
     * makes future changes to export_request_validate() automatically apply to
     * the exact state used for every frame/audio chunk below. */
    st = export_request_validate(run_request);
    if (st != ODM_STATUS_OK) return st;
    memset(&render_admission, 0, sizeof(render_admission));
    st = odm_layered_render_admit(run_request->config,
                                  ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                  &render_admission);
    if (st != ODM_STATUS_OK) return st;
    if (render_admission.frame_bytes != need_frame ||
        render_admission.scratch_bytes != need_scratch ||
        !odm_sha256_equal(&render_admission.config_sha256,
                          &run_request->recipe->layered_config_sha256))
        return ODM_STATUS_INVARIANT_BROKEN;

    if (run_request->job_ticket) {
        odm_progress_snapshot snapshot;
        st = odm_job_snapshot(run_request->job_ticket->job, &snapshot);
        if (st != ODM_STATUS_OK) return st;
        if (snapshot.generation != run_request->job_ticket->generation ||
            snapshot.state != ODM_JOB_RUNNING || snapshot.work_total != work_total)
            return ODM_STATUS_INVALID_STATE;
    }
    st = export_check_cancel(run_request->job_ticket);
    if (st != ODM_STATUS_OK) return st;
    st = export_publish(run_request->job_ticket, ODM_PHASE_VALIDATE, 0u);
    if (st != ODM_STATUS_OK) return st;

    hash_acceleration =
        (run_request->flags & ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH) == 0u &&
        export_sha256_accel_selftest();
    st = export_sha256_stream_init(&video_hash, hash_acceleration);
    if (st != ODM_STATUS_OK) return st;
    st = export_sha256_stream_update(&video_hash, EXPORT_VIDEO_DOMAIN,
                                     sizeof(EXPORT_VIDEO_DOMAIN));
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_sha256_stream_update(&video_hash,
                                     run_request->recipe->export_recipe_sha256.bytes, 32u);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_hash_stream_u64(&video_hash, run_request->recipe->frame_count);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;

    st = export_sha256_stream_init(&audio_hash, hash_acceleration);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_sha256_stream_update(&audio_hash, EXPORT_AUDIO_DOMAIN,
                                     sizeof(EXPORT_AUDIO_DOMAIN));
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_sha256_stream_update(&audio_hash,
                                     run_request->recipe->export_recipe_sha256.bytes, 32u);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_hash_stream_u64(&audio_hash,
                                run_request->recipe->source_audio_total_frames);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;

    st = export_sha256_stream_init(&source_pcm_hash, hash_acceleration);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_sha256_stream_update(&source_pcm_hash, EXPORT_PCM_SOURCE_DOMAIN,
                                     sizeof(EXPORT_PCM_SOURCE_DOMAIN));
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_hash_stream_u64(&source_pcm_hash, run_request->canonical_pcm_frames);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;

    /* Admission identity: freeze the exact canonical PCM byte identity before
     * begin(). The large PCM buffer is not copied; instead it is encoded with
     * the same canonical S32LE path used for delivery and hashed once up front.
     * A second hash is built from the source portion actually streamed later.
     * Any callback-time mutation therefore becomes INTEGRITY_ERROR before
     * commit, preventing a master assembled from mixed audio states. */
    st = export_sha256_stream_init(&admitted_pcm_hash, hash_acceleration);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_sha256_stream_update(&admitted_pcm_hash, EXPORT_PCM_SOURCE_DOMAIN,
                                     sizeof(EXPORT_PCM_SOURCE_DOMAIN));
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    st = export_hash_stream_u64(&admitted_pcm_hash, run_request->canonical_pcm_frames);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;
    {
        uint64_t admitted_pos = 0u;
        while (admitted_pos < run_request->canonical_pcm_frames) {
            uint64_t remain = run_request->canonical_pcm_frames - admitted_pos;
            uint64_t chunk_frames = remain < ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES
                                  ? remain : ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES;
            uint64_t required = 0u;
            st = odm_export_audio_chunk_s32le(
                run_request->recipe, run_request->canonical_pcm,
                run_request->canonical_pcm_frames, admitted_pos, chunk_frames,
                audio_chunk, sizeof(audio_chunk), &required);
            if (st != ODM_STATUS_OK) goto hash_setup_fail;
            if (required != chunk_frames * 8u) {
                st = ODM_STATUS_INVARIANT_BROKEN;
                goto hash_setup_fail;
            }
            st = export_sha256_stream_update(&admitted_pcm_hash, audio_chunk, required);
            if (st != ODM_STATUS_OK) goto hash_setup_fail;
            admitted_pos += chunk_frames;
        }
    }
    st = export_sha256_stream_final(&admitted_pcm_hash, &admitted_pcm_digest);
    if (st != ODM_STATUS_OK) goto hash_setup_fail;

    ss_factor = odm_supersample_factor(run_request->quality_tier);
    if (ss_factor == 0u) { st = ODM_STATUS_INVALID_ARGUMENT; goto hash_setup_fail; }
    if (ss_factor > 1u) {
        odm_layered_frame_plan probe_plan;
        memset(&probe_plan, 0, sizeof(probe_plan));
        st = odm_supersample_scale(run_request->config, &probe_plan, ss_factor,
                                   &ss_config, &probe_plan);
        if (st != ODM_STATUS_OK) goto hash_setup_fail;
        st = odm_layered_render_requirements(&ss_config,
                                             ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                             &ss_frame_bytes, &ss_scratch_bytes);
        if (st != ODM_STATUS_OK) goto hash_setup_fail;
        ss_frame = (uint8_t *)malloc((size_t)ss_frame_bytes);
        ss_scratch = ss_scratch_bytes ? (uint8_t *)malloc((size_t)ss_scratch_bytes) : NULL;
        if (!ss_frame || (ss_scratch_bytes && !ss_scratch)) {
            st = ODM_STATUS_OUT_OF_MEMORY;
            goto hash_setup_fail;
        }
        /* La rejilla fina cuesta factor^2 veces mas que la de entrega: es el
         * bucle mas caro del motor y tiene que repartirse entre los nucleos.
         * Sin esto, pedir calidad convertia el render en monohilo. */
        st = odm_layered_render_admit(&ss_config,
                                      ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                      &ss_admission);
        if (st != ODM_STATUS_OK) goto hash_setup_fail;
        if (row_workers > 1u) {
            st = odm_layered_worker_pool_init(&ss_pool, row_workers);
            if (st != ODM_STATUS_OK) goto hash_setup_fail;
            ss_pool_active = 1;
        }
    }

    if (row_workers > 1u) {
        st = odm_layered_worker_pool_init(&render_pool, row_workers);
        if (st != ODM_STATUS_OK) goto hash_setup_fail;
        render_pool_active = 1;
    }
    st = run_sink->begin(run_sink->user, run_request->recipe);
    if (st != ODM_STATUS_OK) goto prebegin_fail;
    active = 1;
    st = export_publish(run_request->job_ticket, ODM_PHASE_EXECUTE, 0u);
    if (st != ODM_STATUS_OK) goto fail;

    for (frame_index = 0u; frame_index < run_request->recipe->frame_count; ++frame_index) {
        int64_t sample = 0;
        uint64_t tick_index;
        odm_composition_frame_state composition;
        odm_director_frame_state director;
        odm_render_surface_frame surface;
        odm_layered_frame_plan plan;
        uint64_t required_frame = 0u, required_scratch = 0u;

        st = export_check_cancel(run_request->job_ticket);
        if (st != ODM_STATUS_OK) goto fail;
        st = odm_export_frame_sample(run_request->recipe, frame_index, &sample);
        if (st != ODM_STATUS_OK) goto fail;
        tick_index = (uint64_t)sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
        memset(&composition, 0, sizeof(composition));
        memset(&director, 0, sizeof(director));
        memset(&surface, 0, sizeof(surface));
        st = run_request->state_provider(run_request->state_user, frame_index, sample,
                                     &composition, &director);
        if (st != ODM_STATUS_OK) { goto fail; }
        if (composition.schema_version != ODM_COMPOSITION_SCHEMA_VERSION ||
            director.schema_version != ODM_DIRECTOR_SCHEMA_VERSION ||
            composition.tick_index != tick_index || director.tick_index != tick_index ||
            composition.center_sample != tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES) {
            st = ODM_STATUS_INVALID_DATA;
            goto fail;
        }
        st = run_request->core_provider(run_request->core_user, frame_index, sample, &surface);
        if (st != ODM_STATUS_OK) { goto fail; }
        st = odm_layered_resolve_frame_plan(run_request->config, &composition, &director,
                                            sample, run_request->recipe->project_end_sample,
                                            &plan);
        if (st != ODM_STATUS_OK) { goto fail; }
        {
            const uint8_t *stream_frame = frame;
            if (ss_factor > 1u) {
                /* Supermuestreo dentro del render: se rasteriza en una rejilla
                 * `ss_factor` veces mas fina y se resuelve por area a la
                 * resolucion de entrega. El recipe no cambia porque la entrega
                 * no cambia; solo cambia con cuanta finura se dibujo. */
                odm_layered_frame_plan hi_plan;
                uint64_t hi_required = 0u, hi_scratch_required = 0u, resolved = 0u;
                st = odm_supersample_scale(run_request->config, &plan, ss_factor,
                                           &ss_config, &hi_plan);
                if (st != ODM_STATUS_OK) goto fail;
                {
                    const uint8_t *hi_stream = ss_frame;
                    if (ss_pool_active) {
                        st = odm_layered_render_frame_stream_stage_admitted_pool(
                            &ss_config, &ss_admission, &hi_plan, &surface,
                            run_request->job_ticket, ss_scratch, ss_scratch_bytes,
                            &ss_pool, &hi_stream, &hi_required, &hi_scratch_required);
                    } else {
                        st = odm_layered_render_frame_stream_stage_admitted_workers(
                            &ss_config, &ss_admission, &hi_plan, &surface,
                            run_request->job_ticket, ss_scratch, ss_scratch_bytes,
                            row_workers, &hi_stream, &hi_required, &hi_scratch_required);
                    }
                    if (st != ODM_STATUS_OK) goto fail;
                    if (hi_stream != ss_frame) {
                        if (hi_required > ss_frame_bytes) { st = ODM_STATUS_INVARIANT_BROKEN; goto fail; }
                        memcpy(ss_frame, hi_stream, (size_t)hi_required);
                    }
                }
                st = odm_supersample_resolve_rgba8_srgb(
                    ss_frame, hi_required, ss_config.canvas.width, ss_config.canvas.height,
                    ss_factor, frame, need_frame,
                    run_request->config->canvas.width, run_request->config->canvas.height,
                    &resolved);
                if (st != ODM_STATUS_OK) goto fail;
                stream_frame = frame;
                required_frame = resolved;
                required_scratch = 0u;
            } else if (render_pool_active) {
                st = odm_layered_render_frame_stream_stage_admitted_pool(
                    run_request->config, &render_admission, &plan, &surface,
                    run_request->job_ticket, scratch, scratch_bytes, &render_pool,
                    &stream_frame, &required_frame, &required_scratch);
            } else {
                st = odm_layered_render_frame_stream_stage_admitted_workers(
                    run_request->config, &render_admission, &plan, &surface,
                    run_request->job_ticket, scratch, scratch_bytes, row_workers,
                    &stream_frame, &required_frame, &required_scratch);
            }
            if (st != ODM_STATUS_OK) { goto fail; }
            if (!direct_staging && ss_factor == 1u) {
                if (export_ranges_overlap(frame, need_frame,
                                          surface.pixels, surface.pixel_bytes)) {
                    st = ODM_STATUS_INVALID_ARGUMENT;
                    goto fail;
                }
                st = export_check_cancel(run_request->job_ticket);
                if (st != ODM_STATUS_OK) goto fail;
                memcpy(frame, stream_frame, (size_t)required_frame);
                stream_frame = frame;
            }
            if (!stream_frame || required_frame != need_frame ||
                required_scratch > need_scratch) {
                st = ODM_STATUS_INVARIANT_BROKEN;
                goto fail;
            }
            st = export_sha256_stream_update(&video_hash, stream_frame, required_frame);
            if (st != ODM_STATUS_OK) goto fail;
            st = run_sink->write_video(run_sink->user, frame_index, sample,
                                       stream_frame, required_frame);
        }
        if (st != ODM_STATUS_OK) goto fail;
        if (!export_add_u64(video_bytes, required_frame, &video_bytes) ||
            !export_add_u64(work_done, 1u, &work_done)) {
            st = ODM_STATUS_OVERFLOW;
            goto fail;
        }
        st = export_publish(run_request->job_ticket, ODM_PHASE_EXECUTE, work_done);
        if (st != ODM_STATUS_OK) goto fail;
    }
    st = export_sha256_stream_final(&video_hash, &video_digest);
    if (st != ODM_STATUS_OK) goto fail;

    while (audio_pos < run_request->recipe->source_audio_total_frames) {
        uint64_t remain = run_request->recipe->source_audio_total_frames - audio_pos;
        uint64_t chunk_frames = remain < ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES
                              ? remain : ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES;
        uint64_t required = 0u;
        st = export_check_cancel(run_request->job_ticket);
        if (st != ODM_STATUS_OK) goto fail;
        st = odm_export_audio_chunk_s32le(
            run_request->recipe, run_request->canonical_pcm, run_request->canonical_pcm_frames,
            audio_pos, chunk_frames, audio_chunk, sizeof(audio_chunk), &required);
        if (st != ODM_STATUS_OK) goto fail;
        if (required != chunk_frames * 8u) {
            st = ODM_STATUS_INVARIANT_BROKEN;
            goto fail;
        }
        st = export_sha256_stream_update(&audio_hash, audio_chunk, required);
        if (st != ODM_STATUS_OK) goto fail;
        if (audio_pos < run_request->canonical_pcm_frames) {
            uint64_t source_left = run_request->canonical_pcm_frames - audio_pos;
            uint64_t source_frames = chunk_frames < source_left ? chunk_frames : source_left;
            st = export_sha256_stream_update(&source_pcm_hash, audio_chunk, source_frames * 8u);
            if (st != ODM_STATUS_OK) goto fail;
        }
        st = run_sink->write_audio(run_sink->user, audio_pos, audio_chunk, chunk_frames);
        if (st != ODM_STATUS_OK) goto fail;
        if (!export_add_u64(audio_bytes, required, &audio_bytes) ||
            !export_add_u64(work_done, chunk_frames, &work_done)) {
            st = ODM_STATUS_OVERFLOW;
            goto fail;
        }
        audio_pos += chunk_frames;
        st = export_publish(run_request->job_ticket, ODM_PHASE_EXECUTE, work_done);
        if (st != ODM_STATUS_OK) goto fail;
    }
    st = export_sha256_stream_final(&audio_hash, &audio_digest);
    if (st != ODM_STATUS_OK) goto fail;
    st = export_sha256_stream_final(&source_pcm_hash, &source_pcm_digest);
    if (st != ODM_STATUS_OK) goto fail;
    if (!odm_sha256_equal(&admitted_pcm_digest, &source_pcm_digest)) {
        st = ODM_STATUS_INTEGRITY_ERROR;
        goto fail;
    }
    if (work_done != work_total ||
        video_bytes != run_request->recipe->source_video_total_bytes ||
        audio_bytes != run_request->recipe->source_audio_total_bytes) {
        st = ODM_STATUS_INVARIANT_BROKEN;
        goto fail;
    }

    memset(&receipt, 0, sizeof(receipt));
    receipt.schema_version = ODM_EXPORT_RUN_SCHEMA_VERSION;
    receipt.width = run_request->recipe->width;
    receipt.height = run_request->recipe->height;
    receipt.fps = run_request->recipe->fps;
    receipt.pixel_format = ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE;
    receipt.audio_sample_rate = 48000u;
    receipt.audio_channels = 2u;
    receipt.frame_count = run_request->recipe->frame_count;
    receipt.samples_per_frame = run_request->recipe->samples_per_frame;
    receipt.project_end_sample = run_request->recipe->project_end_sample;
    receipt.output_end_sample = run_request->recipe->output_end_sample;
    receipt.video_bytes = video_bytes;
    receipt.audio_frames = run_request->recipe->source_audio_total_frames;
    receipt.audio_bytes = audio_bytes;
    receipt.export_recipe_sha256 = run_request->recipe->export_recipe_sha256;
    receipt.export_policy_sha256 = run_request->recipe->export_policy_sha256;
    receipt.layered_policy_sha256 = run_request->recipe->layered_policy_sha256;
    receipt.layered_config_sha256 = run_request->recipe->layered_config_sha256;
    receipt.canonical_pcm_sha256 = admitted_pcm_digest;
    receipt.video_stream_sha256 = video_digest;
    receipt.audio_stream_sha256 = audio_digest;
    st = export_run_receipt_identity(&receipt, &receipt.export_run_sha256);
    if (st != ODM_STATUS_OK) goto fail;
    st = export_run_receipt_semantic_validate(&receipt, 1);
    if (st != ODM_STATUS_OK) goto fail;
    st = odm_export_run_receipt_bytes(&receipt, receipt_local, sizeof(receipt_local),
                                      &receipt_required);
    if (st != ODM_STATUS_OK) goto fail;

    st = export_publish(run_request->job_ticket, ODM_PHASE_FINALIZE, work_done);
    if (st != ODM_STATUS_OK) goto fail;
    st = export_check_cancel(run_request->job_ticket);
    if (st != ODM_STATUS_OK) goto fail;
    st = run_sink->commit(run_sink->user, receipt_local, receipt_required);
    if (st != ODM_STATUS_OK) goto fail;
    active = 0;

    memcpy(receipt_buffer, receipt_local, (size_t)receipt_required);
    *out_receipt_required = receipt_required;
    if (out_receipt) *out_receipt = receipt;
    if (ss_pool_active) { odm_layered_worker_pool_destroy(&ss_pool); ss_pool_active = 0; }
    free(ss_frame); ss_frame = NULL;
    free(ss_scratch); ss_scratch = NULL;
    if (render_pool_active) {
        odm_layered_worker_pool_destroy(&render_pool);
        render_pool_active = 0;
    }
    export_sha256_stream_cleanup(&video_hash);
    export_sha256_stream_cleanup(&audio_hash);
    export_sha256_stream_cleanup(&source_pcm_hash);
    export_sha256_stream_cleanup(&admitted_pcm_hash);
    return ODM_STATUS_OK;

prebegin_fail:
    if (ss_pool_active) { odm_layered_worker_pool_destroy(&ss_pool); ss_pool_active = 0; }
    free(ss_frame); ss_frame = NULL;
    free(ss_scratch); ss_scratch = NULL;
    if (render_pool_active) {
        odm_layered_worker_pool_destroy(&render_pool);
        render_pool_active = 0;
    }
    export_sha256_stream_cleanup(&video_hash);
    export_sha256_stream_cleanup(&audio_hash);
    export_sha256_stream_cleanup(&source_pcm_hash);
    export_sha256_stream_cleanup(&admitted_pcm_hash);
    return st;

hash_setup_fail:
    if (ss_pool_active) { odm_layered_worker_pool_destroy(&ss_pool); ss_pool_active = 0; }
    free(ss_frame); ss_frame = NULL;
    free(ss_scratch); ss_scratch = NULL;
    if (render_pool_active) {
        odm_layered_worker_pool_destroy(&render_pool);
        render_pool_active = 0;
    }
    export_sha256_stream_cleanup(&video_hash);
    export_sha256_stream_cleanup(&audio_hash);
    export_sha256_stream_cleanup(&source_pcm_hash);
    export_sha256_stream_cleanup(&admitted_pcm_hash);
    return st;

fail:
    free(ss_frame); ss_frame = NULL;
    free(ss_scratch); ss_scratch = NULL;
    export_abort_sink(run_sink, active, st);
    if (render_pool_active) {
        odm_layered_worker_pool_destroy(&render_pool);
        render_pool_active = 0;
    }
    export_sha256_stream_cleanup(&video_hash);
    export_sha256_stream_cleanup(&audio_hash);
    export_sha256_stream_cleanup(&source_pcm_hash);
    export_sha256_stream_cleanup(&admitted_pcm_hash);
    return st;
}

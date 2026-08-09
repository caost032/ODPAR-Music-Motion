#include "master_internal.h"
#include "odm_time.h"
#include "odm_wire.h"

#include <limits.h>
#include <string.h>

static const uint8_t RECEIPT_MAGIC[8] = {'O','D','M','R','C','P','T','1'};
static const uint8_t RENDER_ID_DOMAIN[] = {'O','D','P','A','R','_','M','A','S','T','E','R','_','R','E','N','D','E','R','_','I','D','_','V','1',0};

static void rid_le32(uint8_t b[4], uint32_t v) {
    b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24);
}
static void rid_le64(uint8_t b[8], uint64_t v) {
    uint32_t i; for(i=0u;i<8u;++i)b[i]=(uint8_t)(v>>(8u*i));
}
static odm_status rid_h32(odm_sha256_context*c,uint32_t v){uint8_t b[4];rid_le32(b,v);return odm_sha256_update(c,b,4u);}
static odm_status rid_h64(odm_sha256_context*c,uint64_t v){uint8_t b[8];rid_le64(b,v);return odm_sha256_update(c,b,8u);}
static odm_status rid_hi64(odm_sha256_context*c,int64_t v){return rid_h64(c,(uint64_t)v);}

static int checked_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out || a > UINT64_MAX - b) return 0;
    *out = a + b; return 1;
}
static int checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out || (a != 0u && b > UINT64_MAX / a)) return 0;
    *out = a * b; return 1;
}

odm_status odm_master_render_id_internal(const odm_master_plan *p,
                                         odm_sha256_digest *out) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER;odm_status st;
    if(!p||!out)return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_sha256_init(&c);if(st!=ODM_STATUS_OK)return st;
#define H(x) do{st=(x);if(st!=ODM_STATUS_OK)return st;}while(0)
    H(odm_sha256_update(&c,RENDER_ID_DOMAIN,sizeof(RENDER_ID_DOMAIN)));
    H(odm_sha256_update(&c,p->package_sha256.bytes,32u));H(odm_sha256_update(&c,p->score_sha256.bytes,32u));
    H(odm_sha256_update(&c,p->capability_sha256.bytes,32u));H(odm_sha256_update(&c,p->music_map_sha256.bytes,32u));
    H(odm_sha256_update(&c,p->music_policy_sha256.bytes,32u));H(odm_sha256_update(&c,p->render_ir_sha256.bytes,32u));
    H(odm_sha256_update(&c,p->assets_sha256.bytes,32u));H(odm_sha256_update(&c,p->canonical_pcm_sha256.bytes,32u));
    H(rid_h32(&c,p->output_profile));H(rid_h32(&c,p->pixel_format));H(rid_h32(&c,p->audio_format));H(rid_h32(&c,p->work_schema));
    H(rid_h32(&c,p->renderer_version_major));H(rid_h32(&c,p->renderer_version_minor));H(rid_h32(&c,p->width));H(rid_h32(&c,p->height));H(rid_h32(&c,(uint32_t)p->fps));
    H(rid_h64(&c,p->frame_count));H(rid_hi64(&c,p->samples_per_frame));H(rid_hi64(&c,p->project_end_sample));H(rid_hi64(&c,p->output_end_sample));
    H(rid_h64(&c,p->canonical_music_frames));H(rid_h64(&c,p->project_seed));
#undef H
    return odm_sha256_final(&c,out);
}

static odm_status receipt_payload_write(const odm_render_receipt_info *info,
                                        uint8_t payload[ODM_RENDER_RECEIPT_PAYLOAD_BYTES]) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    uint64_t written = 0u;
    uint8_t reserved[40] = {0};
    odm_status st;
    if (!info || !payload) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_wire_writer_init(&w, payload, ODM_RENDER_RECEIPT_PAYLOAD_BYTES); if (st != ODM_STATUS_OK) return st;
#define W(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    W(odm_wire_write_bytes(&w, RECEIPT_MAGIC, sizeof(RECEIPT_MAGIC)));
    W(odm_wire_write_u32(&w, info->schema_version_major));
    W(odm_wire_write_u32(&w, info->schema_version_minor));
    W(odm_wire_write_u32(&w, info->output_profile));
    W(odm_wire_write_u32(&w, info->pixel_format));
    W(odm_wire_write_u32(&w, info->audio_format));
    W(odm_wire_write_u32(&w, info->work_schema));
    W(odm_wire_write_u32(&w, info->renderer_version_major));
    W(odm_wire_write_u32(&w, info->renderer_version_minor));
    W(odm_wire_write_u32(&w, info->width));
    W(odm_wire_write_u32(&w, info->height));
    W(odm_wire_write_i32(&w, info->fps));
    W(odm_wire_write_u32(&w, info->flags));
    W(odm_wire_write_u64(&w, info->frame_count));
    W(odm_wire_write_i64(&w, info->samples_per_frame));
    W(odm_wire_write_i64(&w, info->project_end_sample));
    W(odm_wire_write_i64(&w, info->output_end_sample));
    W(odm_wire_write_u64(&w, info->canonical_music_frames));
    W(odm_wire_write_u64(&w, info->quoted_work_units));
    W(odm_wire_write_u64(&w, info->observed_work_units));
    W(odm_wire_write_u64(&w, info->project_seed));
    W(odm_wire_write_bytes(&w, info->package_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->score_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->capability_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->music_map_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->music_policy_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->render_ir_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->assets_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->canonical_pcm_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->soundtrack_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->frame_root_sha256.bytes, 32u));
    W(odm_wire_write_bytes(&w, info->render_id.bytes, 32u));
    W(odm_wire_write_bytes(&w, reserved, sizeof(reserved)));
#undef W
    st = odm_wire_writer_finish(&w, &written);
    if (st != ODM_STATUS_OK) return st;
    return written == ODM_RENDER_RECEIPT_PAYLOAD_BYTES ? ODM_STATUS_OK : ODM_STATUS_INVARIANT_BROKEN;
}

odm_status odm_render_receipt_write_internal(const odm_render_receipt_info *info,
                                             uint8_t *record, uint64_t capacity,
                                             uint64_t *out_required,
                                             odm_sha256_digest *out_sha) {
    uint8_t payload[ODM_RENDER_RECEIPT_PAYLOAD_BYTES];
    odm_status st;
    if (!info || !out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st = receipt_payload_write(info, payload); if (st != ODM_STATUS_OK) return st;
    return odm_wire_record_write(ODM_RENDER_RECEIPT_KIND,
                                 ODM_RENDER_RECEIPT_SCHEMA_MAJOR,
                                 ODM_RENDER_RECEIPT_SCHEMA_MINOR,
                                 payload, sizeof(payload), record, capacity,
                                 out_required, out_sha);
}

static odm_status read_digest(odm_wire_reader *r, odm_sha256_digest *d) {
    return odm_wire_read_bytes(r, d->bytes, 32u);
}

static odm_status receipt_semantic_validate(const odm_render_receipt_info *info) {
    odm_timeline timeline;
    odm_master_plan plan;
    odm_sha256_digest recomputed_id;
    int64_t samples_per_frame = 0;
    uint64_t pixels = 0u, frame_work = 0u, expected_work = 0u;
    odm_status st;
    if (!info) return ODM_STATUS_INVALID_ARGUMENT;
    if (info->schema_version_major != ODM_RENDER_RECEIPT_SCHEMA_MAJOR || info->schema_version_minor != ODM_RENDER_RECEIPT_SCHEMA_MINOR ||
        info->output_profile != ODM_MASTER_PROFILE_REFERENCE_V1 || info->pixel_format != ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL ||
        info->audio_format != ODM_MASTER_AUDIO_PCM_Q31_STEREO_LE || info->work_schema != ODM_MASTER_WORK_SCHEMA_OUTPUT_VOLUME_V1 ||
        info->renderer_version_major != ODM_REFERENCE_RENDERER_VERSION_MAJOR || info->renderer_version_minor != ODM_REFERENCE_RENDERER_VERSION_MINOR ||
        info->width == 0u || info->height == 0u || info->fps <= 0 || info->flags != 0u || info->frame_count == 0u ||
        info->samples_per_frame <= 0 || info->project_end_sample <= 0 || info->output_end_sample <= 0 ||
        info->canonical_music_frames == 0u || info->canonical_music_frames > (uint64_t)info->project_end_sample ||
        info->observed_work_units != info->quoted_work_units) return ODM_STATUS_INVALID_DATA;

    st = odm_time_samples_per_frame(info->fps, &samples_per_frame);
    if (st != ODM_STATUS_OK || samples_per_frame != info->samples_per_frame) return ODM_STATUS_INVALID_DATA;
    st = odm_time_resolve_timeline(info->project_end_sample, info->fps, &timeline);
    if (st != ODM_STATUS_OK || timeline.frame_count <= 0 ||
        (uint64_t)timeline.frame_count != info->frame_count ||
        timeline.samples_per_frame != info->samples_per_frame ||
        timeline.output_end_sample != info->output_end_sample) return ODM_STATUS_INVALID_DATA;

    if (!checked_mul_u64((uint64_t)info->width, (uint64_t)info->height, &pixels) ||
        !checked_mul_u64(pixels, info->frame_count, &frame_work) ||
        !checked_add_u64(frame_work, (uint64_t)info->output_end_sample, &expected_work) ||
        expected_work != info->quoted_work_units) return ODM_STATUS_INVALID_DATA;

    memset(&plan, 0, sizeof(plan));
    plan.schema_version_major = ODM_MASTER_SCHEMA_MAJOR;
    plan.schema_version_minor = ODM_MASTER_SCHEMA_MINOR;
    plan.output_profile = info->output_profile;
    plan.pixel_format = info->pixel_format;
    plan.audio_format = info->audio_format;
    plan.work_schema = info->work_schema;
    plan.renderer_version_major = info->renderer_version_major;
    plan.renderer_version_minor = info->renderer_version_minor;
    plan.width = info->width; plan.height = info->height; plan.fps = info->fps;
    plan.frame_count = info->frame_count; plan.samples_per_frame = info->samples_per_frame;
    plan.project_end_sample = info->project_end_sample; plan.output_end_sample = info->output_end_sample;
    plan.canonical_music_frames = info->canonical_music_frames; plan.project_seed = info->project_seed;
    plan.package_sha256 = info->package_sha256; plan.score_sha256 = info->score_sha256;
    plan.capability_sha256 = info->capability_sha256; plan.music_map_sha256 = info->music_map_sha256;
    plan.music_policy_sha256 = info->music_policy_sha256; plan.render_ir_sha256 = info->render_ir_sha256;
    plan.assets_sha256 = info->assets_sha256; plan.canonical_pcm_sha256 = info->canonical_pcm_sha256;
    st = odm_master_render_id_internal(&plan, &recomputed_id);
    if (st != ODM_STATUS_OK) return st;
    if (!odm_sha256_equal(&recomputed_id, &info->render_id)) return ODM_STATUS_INTEGRITY_ERROR;
    return ODM_STATUS_OK;
}

odm_status odm_render_receipt_validate(const uint8_t *record, uint64_t record_bytes,
                                       const odm_sha256_digest *expected_render_id,
                                       odm_render_receipt_info *out_info) {
    odm_wire_record_info wi;
    odm_render_receipt_info info;
    odm_wire_reader r = ODM_WIRE_READER_INITIALIZER;
    odm_sha256_digest record_sha;
    uint64_t off = 0u;
    uint8_t magic[8], reserved[40];
    uint32_t i;
    odm_status st;
    if (!record || !out_info) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_wire_record_read(record, record_bytes, &wi, &off, &record_sha); if (st != ODM_STATUS_OK) return st;
    if (wi.kind != ODM_RENDER_RECEIPT_KIND || wi.schema_version_major != ODM_RENDER_RECEIPT_SCHEMA_MAJOR ||
        wi.schema_version_minor != ODM_RENDER_RECEIPT_SCHEMA_MINOR || wi.payload_bytes != ODM_RENDER_RECEIPT_PAYLOAD_BYTES ||
        record_bytes != ODM_RENDER_RECEIPT_RECORD_BYTES) return ODM_STATUS_VERSION_MISMATCH;
    memset(&info, 0, sizeof(info));
    st = odm_wire_reader_init(&r, record + off, wi.payload_bytes); if (st != ODM_STATUS_OK) return st;
#define R(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    R(odm_wire_read_bytes(&r, magic, sizeof(magic))); if (memcmp(magic, RECEIPT_MAGIC, sizeof(magic)) != 0) return ODM_STATUS_INVALID_DATA;
    R(odm_wire_read_u32(&r, &info.schema_version_major)); R(odm_wire_read_u32(&r, &info.schema_version_minor));
    R(odm_wire_read_u32(&r, &info.output_profile)); R(odm_wire_read_u32(&r, &info.pixel_format));
    R(odm_wire_read_u32(&r, &info.audio_format)); R(odm_wire_read_u32(&r, &info.work_schema));
    R(odm_wire_read_u32(&r, &info.renderer_version_major)); R(odm_wire_read_u32(&r, &info.renderer_version_minor));
    R(odm_wire_read_u32(&r, &info.width)); R(odm_wire_read_u32(&r, &info.height)); R(odm_wire_read_i32(&r, &info.fps)); R(odm_wire_read_u32(&r, &info.flags));
    R(odm_wire_read_u64(&r, &info.frame_count)); R(odm_wire_read_i64(&r, &info.samples_per_frame));
    R(odm_wire_read_i64(&r, &info.project_end_sample)); R(odm_wire_read_i64(&r, &info.output_end_sample));
    R(odm_wire_read_u64(&r, &info.canonical_music_frames)); R(odm_wire_read_u64(&r, &info.quoted_work_units));
    R(odm_wire_read_u64(&r, &info.observed_work_units)); R(odm_wire_read_u64(&r, &info.project_seed));
    R(read_digest(&r,&info.package_sha256)); R(read_digest(&r,&info.score_sha256)); R(read_digest(&r,&info.capability_sha256));
    R(read_digest(&r,&info.music_map_sha256)); R(read_digest(&r,&info.music_policy_sha256)); R(read_digest(&r,&info.render_ir_sha256));
    R(read_digest(&r,&info.assets_sha256)); R(read_digest(&r,&info.canonical_pcm_sha256)); R(read_digest(&r,&info.soundtrack_sha256));
    R(read_digest(&r,&info.frame_root_sha256)); R(read_digest(&r,&info.render_id));
    R(odm_wire_read_bytes(&r, reserved, sizeof(reserved)));
#undef R
    st = odm_wire_reader_finish(&r); if (st != ODM_STATUS_OK) return st;
    for (i=0u;i<sizeof(reserved);++i) if (reserved[i] != 0u) return ODM_STATUS_INVALID_DATA;
    st = receipt_semantic_validate(&info); if (st != ODM_STATUS_OK) return st;
    if (expected_render_id && !odm_sha256_equal(expected_render_id, &info.render_id)) return ODM_STATUS_INTEGRITY_ERROR;
    info.receipt_sha256 = record_sha;
    *out_info = info;
    return ODM_STATUS_OK;
}

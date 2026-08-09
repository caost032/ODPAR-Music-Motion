#include "odm_renderer.h"
#include "odm_fixed.h"

#include <limits.h>
#include <string.h>

static void le32(uint8_t b[4], uint32_t v) { b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);b[2]=(uint8_t)(v>>16);b[3]=(uint8_t)(v>>24); }
static void le64(uint8_t b[8], uint64_t v) { uint32_t i;for(i=0u;i<8u;++i)b[i]=(uint8_t)(v>>(8u*i)); }
static odm_status h32(odm_sha256_context*c,uint32_t v){uint8_t b[4];le32(b,v);return odm_sha256_update(c,b,4u);}
static odm_status h64(odm_sha256_context*c,uint64_t v){uint8_t b[8];le64(b,v);return odm_sha256_update(c,b,8u);}

odm_status odm_reference_frame_root_init(
    odm_reference_frame_root_context *context,
    const odm_render_ir_info *ir,
    const odm_sha256_digest *assets_sha256) {
    static const uint8_t domain[] = {
        'O','D','P','A','R','_','R','E','F','_','F','R','A','M','E','_',
        'R','O','O','T','_','V','1',0
    };
    odm_sha256_context h = ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status st;
    if (!context || !ir || !assets_sha256 || ir->frame_count <= 0 ||
        ir->samples_per_frame <= 0 || ir->output_end_sample <= 0 ||
        ir->width == 0u || ir->height == 0u || ir->fps <= 0) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    st = odm_sha256_init(&h); if (st != ODM_STATUS_OK) return st;
    if ((st = odm_sha256_update(&h, domain, sizeof(domain))) != ODM_STATUS_OK ||
        (st = odm_sha256_update(&h, ir->record_sha256.bytes, 32u)) != ODM_STATUS_OK ||
        (st = odm_sha256_update(&h, assets_sha256->bytes, 32u)) != ODM_STATUS_OK ||
        (st = h32(&h, ODM_REFERENCE_RENDERER_VERSION_MAJOR)) != ODM_STATUS_OK ||
        (st = h32(&h, ODM_REFERENCE_RENDERER_VERSION_MINOR)) != ODM_STATUS_OK ||
        (st = h32(&h, ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL)) != ODM_STATUS_OK ||
        (st = h32(&h, ir->width)) != ODM_STATUS_OK ||
        (st = h32(&h, ir->height)) != ODM_STATUS_OK ||
        (st = h32(&h, (uint32_t)ir->fps)) != ODM_STATUS_OK ||
        (st = h64(&h, (uint64_t)ir->frame_count)) != ODM_STATUS_OK ||
        (st = h64(&h, (uint64_t)ir->output_end_sample)) != ODM_STATUS_OK) return st;
    memset(context, 0, sizeof(*context));
    context->hash = h;
    context->ir_sha256 = ir->record_sha256;
    context->assets_sha256 = *assets_sha256;
    context->expected_frames = (uint64_t)ir->frame_count;
    context->width = ir->width;
    context->height = ir->height;
    context->fps = ir->fps;
    context->state = 1u;
    context->samples_per_frame = ir->samples_per_frame;
    context->output_end_sample = ir->output_end_sample;
    return ODM_STATUS_OK;
}

odm_status odm_reference_frame_root_update(
    odm_reference_frame_root_context *context,
    const odm_reference_frame_info *frame) {
    odm_sha256_context next;
    odm_status st;
    int64_t expected_sample;
    uint64_t expected_pixel_bytes;
    uint32_t i;
    if (!context || !frame || context->state != 1u) return ODM_STATUS_INVALID_STATE;
    if (context->next_frame >= context->expected_frames) return ODM_STATUS_INVALID_STATE;
    if (frame->renderer_version_major != ODM_REFERENCE_RENDERER_VERSION_MAJOR ||
        frame->renderer_version_minor != ODM_REFERENCE_RENDERER_VERSION_MINOR ||
        frame->pixel_format != ODM_REFERENCE_PIXEL_RGBA16LE_LINEAR_PREMUL ||
        frame->width != context->width || frame->height != context->height ||
        frame->fps != context->fps || frame->reserved0 != 0u ||
        frame->frame_index < 0 ||
        (uint64_t)frame->frame_index != context->next_frame ||
        !odm_sha256_equal(&frame->assets_sha256, &context->assets_sha256)) {
        return ODM_STATUS_INTEGRITY_ERROR;
    }
    for (i = 0u; i < 4u; ++i) if (frame->reserved[i] != 0u) return ODM_STATUS_INTEGRITY_ERROR;
    if (odm_i64_mul_checked(frame->frame_index, context->samples_per_frame,
                            &expected_sample) != ODM_STATUS_OK ||
        frame->sample != expected_sample || frame->sample < 0 ||
        frame->sample >= context->output_end_sample) return ODM_STATUS_INTEGRITY_ERROR;
    expected_pixel_bytes = (uint64_t)context->width * (uint64_t)context->height * UINT64_C(8);
    if (frame->pixel_bytes != expected_pixel_bytes) return ODM_STATUS_INTEGRITY_ERROR;
    next = context->hash;
    if ((st = h64(&next, context->next_frame)) != ODM_STATUS_OK ||
        (st = odm_sha256_update(&next, frame->frame_sha256.bytes, 32u)) != ODM_STATUS_OK) return st;
    context->hash = next;
    context->next_frame++;
    return ODM_STATUS_OK;
}

odm_status odm_reference_frame_root_final(odm_reference_frame_root_context *context,odm_sha256_digest *out_root){
    odm_sha256_context copy;odm_sha256_digest root;odm_status st;if(!context||!out_root||context->state!=1u)return ODM_STATUS_INVALID_STATE;if(context->next_frame!=context->expected_frames)return ODM_STATUS_INVALID_STATE;copy=context->hash;st=odm_sha256_final(&copy,&root);if(st!=ODM_STATUS_OK)return st;*out_root=root;context->state=2u;return ODM_STATUS_OK;
}

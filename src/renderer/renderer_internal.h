#ifndef ODM_RENDERER_INTERNAL_H
#define ODM_RENDERER_INTERNAL_H

#include "odm_renderer.h"
#include "ir_internal.h"

#include <stdint.h>

typedef int32_t odm_render_q4_27;

#define ODM_RENDER_Q27_ONE INT32_C(134217728)

typedef struct {
    odm_render_q4_27 r;
    odm_render_q4_27 g;
    odm_render_q4_27 b;
    odm_q1_31 a;
} odm_linear_pixel;

odm_render_q4_27 odm_renderer_decode_u8(uint8_t value, uint32_t transfer);
odm_q1_31 odm_renderer_alpha_u8(uint8_t value);
uint16_t odm_renderer_q27_to_u16(odm_render_q4_27 value);
uint16_t odm_renderer_q31_to_u16(odm_q1_31 value);
uint8_t odm_renderer_linear_u16_to_srgb8(uint16_t value);
odm_status odm_renderer_q27_mul_q31(odm_render_q4_27 value, odm_q1_31 factor,
                                    odm_render_q4_27 *out);
odm_status odm_renderer_q31_lerp(odm_q1_31 a, odm_q1_31 b, odm_q1_31 t,
                                 odm_q1_31 *out);
odm_status odm_renderer_q27_lerp(odm_render_q4_27 a, odm_render_q4_27 b,
                                 odm_q1_31 t, odm_render_q4_27 *out);
void odm_renderer_blend_over(odm_linear_pixel *dst, const odm_linear_pixel *src);

odm_status odm_renderer_sample_node(const odm_frame_node_state *node,
                                    const odm_render_surface_frame *surface,
                                    uint32_t out_x, uint32_t out_y,
                                    uint32_t out_width, uint32_t out_height,
                                    odm_linear_pixel *out_pixel);

#endif /* ODM_RENDERER_INTERNAL_H */

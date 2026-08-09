/* Supermuestreo y resolucion por area. Contrato en odm_supersample.h.
 *
 * Nada de lo que hay aqui puede cambiar lo que la musica significa. Solo cambia
 * con cuanta finura se dibuja.
 */

#include "odm_supersample.h"

#include "compositor_internal.h"
#include "odm_renderer.h"

#include <stdlib.h>
#include <string.h>

/* Techo de lienzo intermedio: 16384 es el limite del motor por lado, y el
 * raster de alta resolucion tambien lo respeta. */
#define SS_MAX_DIMENSION UINT32_C(16384)

uint32_t odm_supersample_factor(uint32_t quality_tier) {
    switch (quality_tier) {
        case ODM_QUALITY_PREVIEW_FAST: return 1u;
        case ODM_QUALITY_PREVIEW_HIGH: return 2u;
        case ODM_QUALITY_MASTER:       return 3u;
        case ODM_QUALITY_MASTER_ULTRA: return 4u;
        default:                       return 0u;
    }
}

/* Escalado con saturacion explicita. Una coordenada que desborda no se recorta
 * en silencio: hace fallar la operacion completa. */
static int ss_scale_i32(int32_t value, uint32_t factor, int32_t *out) {
    int64_t v = (int64_t)value * (int64_t)factor;
    if (v > (int64_t)INT32_MAX || v < (int64_t)INT32_MIN) return 0;
    *out = (int32_t)v;
    return 1;
}

static int ss_scale_u32(uint32_t value, uint32_t factor, uint32_t *out) {
    uint64_t v = (uint64_t)value * (uint64_t)factor;
    if (v > (uint64_t)UINT32_MAX) return 0;
    *out = (uint32_t)v;
    return 1;
}

#define SS_I32(field) do { if (!ss_scale_i32(p.field, factor, &p.field)) return ODM_STATUS_OVERFLOW; } while (0)
#define SS_U32(field) do { if (!ss_scale_u32(c.field, factor, &c.field)) return ODM_STATUS_OVERFLOW; } while (0)

odm_status odm_supersample_scale(const odm_layered_config *config,
                                 const odm_layered_frame_plan *plan,
                                 uint32_t factor,
                                 odm_layered_config *out_config,
                                 odm_layered_frame_plan *out_plan) {
    odm_layered_config c;
    odm_layered_frame_plan p;
    odm_status st;

    if (!config || !plan || !out_config || !out_plan) return ODM_STATUS_INVALID_ARGUMENT;
    if (factor == 0u || factor > ODM_SUPERSAMPLE_MAX_FACTOR) return ODM_STATUS_INVALID_ARGUMENT;

    c = *config;
    p = *plan;

    if (factor == 1u) { *out_config = c; *out_plan = p; return ODM_STATUS_OK; }

    if ((uint64_t)c.canvas.width * factor > SS_MAX_DIMENSION ||
        (uint64_t)c.canvas.height * factor > SS_MAX_DIMENSION)
        return ODM_STATUS_BUDGET_EXCEEDED;

    /* --- Configuracion: dimensiones y toda medida absoluta en pixeles ---- */
    SS_U32(canvas.width);
    SS_U32(canvas.height);
    SS_U32(background.grid_spacing_q16);
    SS_U32(background.grid_line_q16);
    SS_U32(background.grid_feather_q16);
    SS_U32(core.border_q16);
    SS_U32(core.feather_q16);
    SS_U32(field.ring_gap_q16);
    SS_U32(field.bar_min_q16);
    SS_U32(field.bar_max_q16);
    SS_U32(field.bar_width_q16);
    SS_U32(field.particle_radius_q16);
    SS_U32(hud.margin_q16);
    SS_U32(hud.progress_height_q16);
    SS_U32(hud.text_scale_q16);
    SS_U32(hud.line_gap_q16);
    /* Las razones normalizadas (centro, tamano relativo, areas seguras,
     * opacidades) son adimensionales: escalarlas seria un error. */

    st = odm_layered_config_validate(&c);
    if (st != ODM_STATUS_OK) return st;

    /* --- Plan: solo geometria ------------------------------------------- */
    if (!ss_scale_u32(p.width, factor, &p.width) ||
        !ss_scale_u32(p.height, factor, &p.height))
        return ODM_STATUS_OVERFLOW;
    SS_I32(center_x_q16);
    SS_I32(center_y_q16);
    SS_I32(core_half_w_q16);
    SS_I32(core_half_h_q16);
    SS_I32(core_radius_q16);
    SS_I32(core_corner_q16);
    SS_I32(ring_inner_radius_q16);
    SS_I32(grid_spacing_q16);
    SS_I32(grid_line_q16);
    SS_I32(grid_feather_q16);
    SS_I32(grid_offset_x_q16);
    SS_I32(grid_offset_y_q16);
    SS_I32(safe_left_q16);
    SS_I32(safe_top_q16);
    SS_I32(safe_right_q16);
    SS_I32(safe_bottom_q16);
    /* grid_warp_q16 y grid_depth_q16 son factores de deformacion, no
     * distancias: escalarlos deformaria la escena, no la afinaria. */

    /* El plan lleva el hash de la configuracion que lo autoriza. Al escalar la
     * configuracion hay que recalcularlo, o el render lo rechazaria -- con
     * razon, porque seria un plan que no corresponde a su configuracion. */
    st = odm_layered_config_sha256(&c, &p.config_sha256);
    if (st != ODM_STATUS_OK) return st;

    *out_config = c;
    *out_plan = p;
    return ODM_STATUS_OK;
}

#undef SS_I32
#undef SS_U32

odm_status odm_supersample_resolve_rgba16(const uint8_t *hi_frame,
                                          uint64_t hi_bytes,
                                          uint32_t hi_width, uint32_t hi_height,
                                          uint32_t factor,
                                          uint8_t *out_frame,
                                          uint64_t out_capacity,
                                          uint32_t out_width, uint32_t out_height,
                                          uint64_t *out_required_bytes) {
    uint64_t needed;
    uint32_t x, y;
    uint32_t divisor;

    if (!out_required_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    if (factor == 0u || factor > ODM_SUPERSAMPLE_MAX_FACTOR) return ODM_STATUS_INVALID_ARGUMENT;
    if (out_width == 0u || out_height == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)out_width * factor != (uint64_t)hi_width ||
        (uint64_t)out_height * factor != (uint64_t)hi_height)
        return ODM_STATUS_INVALID_ARGUMENT;

    needed = (uint64_t)out_width * (uint64_t)out_height * 8u;
    *out_required_bytes = needed;
    if (!out_frame) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (out_capacity < needed) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (!hi_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (hi_bytes < (uint64_t)hi_width * (uint64_t)hi_height * 8u)
        return ODM_STATUS_INVALID_ARGUMENT;

    if (factor == 1u) {
        memcpy(out_frame, hi_frame, (size_t)needed);
        return ODM_STATUS_OK;
    }

    divisor = factor * factor;
    for (y = 0u; y < out_height; ++y) {
        for (x = 0u; x < out_width; ++x) {
            uint64_t r = 0u, g = 0u, b = 0u, a = 0u;
            uint32_t sy, sx;
            uint8_t *dst;
            for (sy = 0u; sy < factor; ++sy) {
                const uint8_t *row = hi_frame +
                    ((uint64_t)(y * factor + sy) * (uint64_t)hi_width +
                     (uint64_t)(x * factor)) * 8u;
                for (sx = 0u; sx < factor; ++sx) {
                    const uint8_t *px = row + (uint64_t)sx * 8u;
                    /* RGBA16 little endian, lineal premultiplicado. */
                    r += (uint64_t)((uint32_t)px[0] | ((uint32_t)px[1] << 8));
                    g += (uint64_t)((uint32_t)px[2] | ((uint32_t)px[3] << 8));
                    b += (uint64_t)((uint32_t)px[4] | ((uint32_t)px[5] << 8));
                    a += (uint64_t)((uint32_t)px[6] | ((uint32_t)px[7] << 8));
                }
            }
            /* Media aritmetica exacta con redondeo al mas cercano. */
            r = (r + divisor / 2u) / divisor;
            g = (g + divisor / 2u) / divisor;
            b = (b + divisor / 2u) / divisor;
            a = (a + divisor / 2u) / divisor;
            dst = out_frame + ((uint64_t)y * (uint64_t)out_width + (uint64_t)x) * 8u;
            dst[0] = (uint8_t)(r & 0xffu);       dst[1] = (uint8_t)((r >> 8) & 0xffu);
            dst[2] = (uint8_t)(g & 0xffu);       dst[3] = (uint8_t)((g >> 8) & 0xffu);
            dst[4] = (uint8_t)(b & 0xffu);       dst[5] = (uint8_t)((b >> 8) & 0xffu);
            dst[6] = (uint8_t)(a & 0xffu);       dst[7] = (uint8_t)((a >> 8) & 0xffu);
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_supersample_resolve_rgba8_srgb(const uint8_t *hi_frame,
                                              uint64_t hi_bytes,
                                              uint32_t hi_width, uint32_t hi_height,
                                              uint32_t factor,
                                              uint8_t *out_frame,
                                              uint64_t out_capacity,
                                              uint32_t out_width, uint32_t out_height,
                                              uint64_t *out_required_bytes) {
    uint64_t needed;
    uint32_t x, y, divisor;

    if (!out_required_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    if (factor == 0u || factor > ODM_SUPERSAMPLE_MAX_FACTOR) return ODM_STATUS_INVALID_ARGUMENT;
    if (out_width == 0u || out_height == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)out_width * factor != (uint64_t)hi_width ||
        (uint64_t)out_height * factor != (uint64_t)hi_height)
        return ODM_STATUS_INVALID_ARGUMENT;

    needed = (uint64_t)out_width * (uint64_t)out_height * 4u;
    *out_required_bytes = needed;
    if (!out_frame) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (out_capacity < needed) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (!hi_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (hi_bytes < (uint64_t)hi_width * (uint64_t)hi_height * 4u)
        return ODM_STATUS_INVALID_ARGUMENT;

    if (factor == 1u) {
        memcpy(out_frame, hi_frame, (size_t)needed);
        return ODM_STATUS_OK;
    }

    divisor = factor * factor;
    for (y = 0u; y < out_height; ++y) {
        for (x = 0u; x < out_width; ++x) {
            int64_t lr = 0, lg = 0, lb = 0;
            uint64_t la = 0u;
            uint32_t sy, sx;
            uint8_t *dst;
            for (sy = 0u; sy < factor; ++sy) {
                const uint8_t *row = hi_frame +
                    ((uint64_t)(y * factor + sy) * (uint64_t)hi_width +
                     (uint64_t)(x * factor)) * 4u;
                for (sx = 0u; sx < factor; ++sx) {
                    const uint8_t *px = row + (uint64_t)sx * 4u;
                    /* Decodificacion a luz lineal con la EOTF congelada. */
                    lr += (int64_t)odm_renderer_decode_u8(px[0], ODM_COLOR_TRANSFER_SRGB);
                    lg += (int64_t)odm_renderer_decode_u8(px[1], ODM_COLOR_TRANSFER_SRGB);
                    lb += (int64_t)odm_renderer_decode_u8(px[2], ODM_COLOR_TRANSFER_SRGB);
                    la += px[3];
                }
            }
            lr = (lr + (int64_t)divisor / 2) / (int64_t)divisor;
            lg = (lg + (int64_t)divisor / 2) / (int64_t)divisor;
            lb = (lb + (int64_t)divisor / 2) / (int64_t)divisor;
            la = (la + divisor / 2u) / divisor;
            dst = out_frame + ((uint64_t)y * (uint64_t)out_width + (uint64_t)x) * 4u;
            dst[0] = odm_renderer_linear_u16_to_srgb8(odm_renderer_q27_to_u16((odm_render_q4_27)lr));
            dst[1] = odm_renderer_linear_u16_to_srgb8(odm_renderer_q27_to_u16((odm_render_q4_27)lg));
            dst[2] = odm_renderer_linear_u16_to_srgb8(odm_renderer_q27_to_u16((odm_render_q4_27)lb));
            dst[3] = (uint8_t)(la & 0xffu);
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_supersample_render_frame(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        const odm_render_surface_frame *core_surface,
                                        const odm_job_ticket *job_ticket,
                                        uint32_t quality_tier,
                                        uint8_t *frame, uint64_t frame_capacity,
                                        uint64_t *out_required_frame_bytes,
                                        odm_sha256_digest *out_pixel_sha256) {
    uint32_t factor;
    odm_layered_config hi_config;
    odm_layered_frame_plan hi_plan;
    uint64_t hi_frame_bytes = 0u, hi_scratch_bytes = 0u;
    uint64_t produced = 0u, required_scratch = 0u;
    uint8_t *hi_frame = NULL, *hi_scratch = NULL;
    uint64_t needed;
    odm_sha256_digest hi_digest;
    odm_status st;

    if (!config || !plan || !out_required_frame_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    factor = odm_supersample_factor(quality_tier);
    if (factor == 0u) return ODM_STATUS_INVALID_ARGUMENT;

    needed = (uint64_t)config->canvas.width * (uint64_t)config->canvas.height * 8u;
    *out_required_frame_bytes = needed;
    if (!frame) return ODM_STATUS_BUFFER_TOO_SMALL;
    if (frame_capacity < needed) return ODM_STATUS_BUFFER_TOO_SMALL;

    /* Un unico camino para todos los factores: con factor 1 la escala es la
     * identidad y el resolve es una copia, asi que no hay dos implementaciones
     * que puedan divergir. */
    st = odm_supersample_scale(config, plan, factor, &hi_config, &hi_plan);
    if (st != ODM_STATUS_OK) return st;

    st = odm_layered_render_requirements(&hi_config,
                                         ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,
                                         &hi_frame_bytes, &hi_scratch_bytes);
    if (st != ODM_STATUS_OK) return st;

    hi_frame = (uint8_t *)malloc((size_t)hi_frame_bytes);
    hi_scratch = hi_scratch_bytes ? (uint8_t *)malloc((size_t)hi_scratch_bytes) : NULL;
    if (!hi_frame || (hi_scratch_bytes && !hi_scratch)) {
        free(hi_frame); free(hi_scratch);
        return ODM_STATUS_OUT_OF_MEMORY;
    }

    st = odm_layered_render_frame(&hi_config, &hi_plan, core_surface, job_ticket,
                                  ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,
                                  hi_scratch, hi_scratch_bytes,
                                  hi_frame, hi_frame_bytes,
                                  &produced, &required_scratch, &hi_digest);
    if (st == ODM_STATUS_OK) {
        uint64_t resolved = 0u;
        st = odm_supersample_resolve_rgba16(hi_frame, hi_frame_bytes,
                                            hi_config.canvas.width, hi_config.canvas.height,
                                            factor, frame, frame_capacity,
                                            config->canvas.width, config->canvas.height,
                                            &resolved);
        if (st == ODM_STATUS_OK && out_pixel_sha256)
            st = odm_sha256(frame, resolved, out_pixel_sha256);
    }
    free(hi_frame); free(hi_scratch);
    return st;
}

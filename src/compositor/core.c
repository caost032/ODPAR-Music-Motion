#include "compositor_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static int64_t abs_i64(int64_t v) { return v < 0 ? -v : v; }

static const uint16_t core_srgb8_to_linear_u16[256] = {
    0,20,40,60,80,99,119,139,
    159,179,199,219,241,264,288,313,
    340,367,396,427,458,491,526,562,
    599,637,677,718,761,805,851,898,
    947,997,1048,1101,1156,1212,1270,1330,
    1391,1453,1517,1583,1651,1720,1790,1863,
    1937,2013,2090,2170,2250,2333,2418,2504,
    2592,2681,2773,2866,2961,3058,3157,3258,
    3360,3464,3570,3678,3788,3900,4014,4129,
    4247,4366,4488,4611,4736,4864,4993,5124,
    5257,5392,5530,5669,5810,5953,6099,6246,
    6395,6547,6700,6856,7014,7174,7335,7500,
    7666,7834,8004,8177,8352,8528,8708,8889,
    9072,9258,9445,9635,9828,10022,10219,10417,
    10619,10822,11028,11235,11446,11658,11873,12090,
    12309,12530,12754,12980,13209,13440,13673,13909,
    14146,14387,14629,14874,15122,15371,15623,15878,
    16135,16394,16656,16920,17187,17456,17727,18001,
    18277,18556,18837,19121,19407,19696,19987,20281,
    20577,20876,21177,21481,21787,22096,22407,22721,
    23038,23357,23678,24002,24329,24658,24990,25325,
    25662,26001,26344,26688,27036,27386,27739,28094,
    28452,28813,29176,29542,29911,30282,30656,31033,
    31412,31794,32179,32567,32957,33350,33745,34143,
    34544,34948,35355,35764,36176,36591,37008,37429,
    37852,38278,38706,39138,39572,40009,40449,40891,
    41337,41785,42236,42690,43147,43606,44069,44534,
    45002,45473,45947,46423,46903,47385,47871,48359,
    48850,49344,49841,50341,50844,51349,51858,52369,
    52884,53401,53921,54445,54971,55500,56032,56567,
    57105,57646,58190,58737,59287,59840,60396,60955,
    61517,62082,62650,63221,63795,64372,64952,65535
};

static uint16_t mul_u16_q31(uint16_t v, uint32_t q31) {
    uint64_t p = (uint64_t)v * q31 + (uint64_t)INT32_MAX / 2u;
    p /= (uint64_t)INT32_MAX;
    return (uint16_t)(p > UINT16_MAX ? UINT16_MAX : p);
}

static uint16_t mul_u16_u16(uint16_t a, uint16_t b) {
    return (uint16_t)(((uint64_t)a * b + UINT64_C(32767)) / UINT64_C(65535));
}

static uint32_t signed_coverage_q31(int64_t sdf_q16, int64_t feather_q16) {
    int64_t half, num;
    if (feather_q16 <= 0) return sdf_q16 <= 0 ? (uint32_t)INT32_MAX : 0u;
    half = feather_q16 / 2;
    if (sdf_q16 <= -half) return (uint32_t)INT32_MAX;
    if (sdf_q16 >= half) return 0u;
    num = (half - sdf_q16) * (int64_t)INT32_MAX + feather_q16 / 2;
    num /= feather_q16;
    if (num < 0) return 0u;
    if (num > INT32_MAX) return (uint32_t)INT32_MAX;
    return (uint32_t)num;
}

/* Exact fast classification for the canonical circle SDF.  shape_sdf_q16()
 * defines distance as floor(sqrt(dx^2+dy^2)).  Therefore:
 *   floor_sqrt(sum) <= k  <=> sum < (k+1)^2
 *   floor_sqrt(sum) >= k  <=> sum >= k^2
 * We can classify the full/zero regions with squared distances and call the
 * integer square root only inside the feather band. No coverage value changes. */
static uint32_t circle_coverage_from_sum(uint64_t sum_q32,
                                         int64_t radius_q16,
                                         int64_t feather_q16) {
    int64_t half, inner, outer;
    uint64_t threshold;
    if (radius_q16 < 0) return 0u;
    if (feather_q16 <= 0) {
        uint64_t rp1 = (uint64_t)radius_q16 + UINT64_C(1);
        return sum_q32 < rp1 * rp1 ? (uint32_t)INT32_MAX : 0u;
    }
    half = feather_q16 / 2;
    inner = radius_q16 - half;
    outer = radius_q16 + half;
    if (inner >= 0) {
        uint64_t ip1 = (uint64_t)inner + UINT64_C(1);
        threshold = ip1 * ip1;
        if (sum_q32 < threshold) return (uint32_t)INT32_MAX;
    }
    if (outer <= 0) return 0u;
    threshold = (uint64_t)outer * (uint64_t)outer;
    if (sum_q32 >= threshold) return 0u;
    return signed_coverage_q31((int64_t)odm_layered_isqrt_u64(sum_q32) - radius_q16,
                               feather_q16);
}

static int64_t shape_sdf_q16(uint32_t shape, int64_t dx, int64_t dy,
                             int64_t half_w, int64_t half_h, int64_t radius,
                             int64_t corner) {
    int64_t ax = abs_i64(dx), ay = abs_i64(dy);
    if (shape == ODM_CORE_SHAPE_CIRCLE) {
        uint64_t ux = (uint64_t)ax, uy = (uint64_t)ay;
        uint64_t sum = ux * ux + uy * uy;
        return (int64_t)odm_layered_isqrt_u64(sum) - radius;
    }
    if (shape == ODM_CORE_SHAPE_SQUARE || corner <= 0) {
        int64_t sx = ax - half_w, sy = ay - half_h;
        return sx > sy ? sx : sy;
    }
    {
        int64_t bx = half_w - corner, by = half_h - corner;
        int64_t qx = ax - bx, qy = ay - by;
        int64_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
        uint64_t uox = (uint64_t)ox, uoy = (uint64_t)oy;
        int64_t outside = (int64_t)odm_layered_isqrt_u64(uox * uox + uoy * uoy);
        int64_t m = qx > qy ? qx : qy;
        int64_t inside = m < 0 ? m : 0;
        return outside + inside - corner;
    }
}

static int floor_q16(int64_t q, int64_t *out_i, uint32_t *out_frac) {
    int64_t i = q / INT64_C(65536), r = q % INT64_C(65536);
    if (r < 0) { --i; r += INT64_C(65536); }
    *out_i = i; *out_frac = (uint32_t)r;
    return 1;
}

static uint16_t core_read_u16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void texel_premul(const odm_render_surface_frame *s, int64_t x, int64_t y,
                         odm_layered_pixel16 *out) {
    const uint8_t *p;
    uint16_t a, r, g, b;
    out->r = 0u; out->g = 0u; out->b = 0u; out->a = 0u;
    if (x < 0 || y < 0 || x >= (int64_t)s->width || y >= (int64_t)s->height) return;
    if (s->pixel_format == ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL) {
        p = s->pixels + ((uint64_t)y * s->width + (uint64_t)x) * 8u;
        out->r = core_read_u16le(p + 0u);
        out->g = core_read_u16le(p + 2u);
        out->b = core_read_u16le(p + 4u);
        out->a = core_read_u16le(p + 6u);
        return;
    }
    p = s->pixels + ((uint64_t)y * s->width + (uint64_t)x) * 4u;
    a = (uint16_t)((uint16_t)p[3] * UINT16_C(257));
    if (s->transfer == ODM_COLOR_TRANSFER_SRGB) {
        r = core_srgb8_to_linear_u16[p[0]];
        g = core_srgb8_to_linear_u16[p[1]];
        b = core_srgb8_to_linear_u16[p[2]];
    } else {
        /* The two renderer roundings for linear u8 -> Q4.27 -> u16 collapse
         * exactly to value * 257 for all 256 codes (exhaustively checked). */
        r = (uint16_t)((uint16_t)p[0] * UINT16_C(257));
        g = (uint16_t)((uint16_t)p[1] * UINT16_C(257));
        b = (uint16_t)((uint16_t)p[2] * UINT16_C(257));
    }
    if (a == UINT16_MAX) {
        out->r = r; out->g = g; out->b = b; out->a = a;
    } else {
        out->r = mul_u16_u16(r, a); out->g = mul_u16_u16(g, a);
        out->b = mul_u16_u16(b, a); out->a = a;
    }
}

static uint16_t lerp16(uint16_t a, uint16_t b, uint32_t t_q16) {
    uint64_t v = (uint64_t)a * (UINT64_C(65536) - t_q16) + (uint64_t)b * t_q16;
    return (uint16_t)((v + UINT64_C(32768)) >> 16);
}

static void sample_bilinear_parts(const odm_render_surface_frame *s,
                                  int64_t ix, int64_t iy,
                                  uint32_t fx, uint32_t fy,
                                  odm_layered_pixel16 *out) {
    odm_layered_pixel16 p00, p10, p01, p11, top, bot;
    texel_premul(s, ix, iy, &p00); texel_premul(s, ix + 1, iy, &p10);
    texel_premul(s, ix, iy + 1, &p01); texel_premul(s, ix + 1, iy + 1, &p11);
#define L(ch) do { top.ch=lerp16(p00.ch,p10.ch,fx); bot.ch=lerp16(p01.ch,p11.ch,fx); out->ch=lerp16(top.ch,bot.ch,fy); } while(0)
    L(r); L(g); L(b); L(a);
#undef L
}

/* Interior bilinear fast path. The y mapping is constant for a destination
 * row and the source representation is constant for a frame, so the generic
 * texel loader would otherwise repeat bounds checks, row-offset arithmetic and
 * format/transfer dispatch four times per destination pixel. This helper skips
 * only work already proven unnecessary for an interior 2x2 footprint. Edge
 * sampling continues through sample_bilinear_parts(), preserving canonical
 * transparent-outside semantics exactly. */
static void sample_bilinear_parts_interior(const odm_render_surface_frame *s,
                                           const uint8_t *row0,
                                           const uint8_t *row1,
                                           int64_t ix, uint32_t fx, uint32_t fy,
                                           odm_layered_pixel16 *out) {
    odm_layered_pixel16 p00, p10, p01, p11, top, bot;
    const uint8_t *a0, *a1, *b0, *b1;
    uint64_t ux = (uint64_t)ix;
    if (s->pixel_format == ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL) {
        a0 = row0 + ux * 8u; a1 = a0 + 8u;
        b0 = row1 + ux * 8u; b1 = b0 + 8u;
#define LOAD16(dst, ptr) do { \
            (dst).r = core_read_u16le((ptr) + 0u); \
            (dst).g = core_read_u16le((ptr) + 2u); \
            (dst).b = core_read_u16le((ptr) + 4u); \
            (dst).a = core_read_u16le((ptr) + 6u); \
        } while (0)
        LOAD16(p00, a0); LOAD16(p10, a1); LOAD16(p01, b0); LOAD16(p11, b1);
#undef LOAD16
    } else {
        a0 = row0 + ux * 4u; a1 = a0 + 4u;
        b0 = row1 + ux * 4u; b1 = b0 + 4u;
        if (s->transfer == ODM_COLOR_TRANSFER_SRGB) {
#define LOAD8_SRGB(dst, ptr) do { \
                uint16_t la = (uint16_t)((uint16_t)(ptr)[3] * UINT16_C(257)); \
                uint16_t lr = core_srgb8_to_linear_u16[(ptr)[0]]; \
                uint16_t lg = core_srgb8_to_linear_u16[(ptr)[1]]; \
                uint16_t lb = core_srgb8_to_linear_u16[(ptr)[2]]; \
                (dst).a = la; \
                if (la == UINT16_MAX) { (dst).r=lr; (dst).g=lg; (dst).b=lb; } \
                else { (dst).r=mul_u16_u16(lr,la); (dst).g=mul_u16_u16(lg,la); (dst).b=mul_u16_u16(lb,la); } \
            } while (0)
            LOAD8_SRGB(p00, a0); LOAD8_SRGB(p10, a1);
            LOAD8_SRGB(p01, b0); LOAD8_SRGB(p11, b1);
#undef LOAD8_SRGB
        } else {
#define LOAD8_LINEAR(dst, ptr) do { \
                uint16_t la = (uint16_t)((uint16_t)(ptr)[3] * UINT16_C(257)); \
                uint16_t lr = (uint16_t)((uint16_t)(ptr)[0] * UINT16_C(257)); \
                uint16_t lg = (uint16_t)((uint16_t)(ptr)[1] * UINT16_C(257)); \
                uint16_t lb = (uint16_t)((uint16_t)(ptr)[2] * UINT16_C(257)); \
                (dst).a = la; \
                if (la == UINT16_MAX) { (dst).r=lr; (dst).g=lg; (dst).b=lb; } \
                else { (dst).r=mul_u16_u16(lr,la); (dst).g=mul_u16_u16(lg,la); (dst).b=mul_u16_u16(lb,la); } \
            } while (0)
            LOAD8_LINEAR(p00, a0); LOAD8_LINEAR(p10, a1);
            LOAD8_LINEAR(p01, b0); LOAD8_LINEAR(p11, b1);
#undef LOAD8_LINEAR
        }
    }
#define LFAST(ch) do { top.ch=lerp16(p00.ch,p10.ch,fx); bot.ch=lerp16(p01.ch,p11.ch,fx); out->ch=lerp16(top.ch,bot.ch,fy); } while(0)
    LFAST(r); LFAST(g); LFAST(b); LFAST(a);
#undef LFAST
}

/* Multiplica por (1 + exceso), saturando. El exceso llega en Q1.31 donde el
 * maximo equivale a duplicar el brillo. */
static uint16_t gain_u16(uint16_t v, uint32_t excess_q31) {
    uint64_t p = (uint64_t)v +
                 ((uint64_t)v * (uint64_t)excess_q31) / (uint64_t)INT32_MAX;
    return p > UINT16_MAX ? UINT16_MAX : (uint16_t)p;
}

static void sample_bilinear(const odm_render_surface_frame *s,
                            int64_t sx_q16, int64_t sy_q16,
                            odm_layered_pixel16 *out) {
    int64_t ix, iy;
    uint32_t fx, fy;
    (void)floor_q16(sx_q16, &ix, &fx); (void)floor_q16(sy_q16, &iy, &fy);
    sample_bilinear_parts(s, ix, iy, fx, fy, out);
}

odm_status odm_layered_core_surface_validate(const odm_render_surface_frame *s) {
    uint64_t pixels;
    if (!s || !s->pixels || s->width == 0u || s->height == 0u ||
        s->width > 16384u || s->height > 16384u ||
        s->primaries != ODM_COLOR_PRIMARIES_BT709 ||
        s->flags != 0u || s->reserved0 != 0u)
        return ODM_STATUS_INVALID_DATA;
    pixels = (uint64_t)s->width * s->height;
    if (pixels > ODM_RENDER_MAX_REFERENCE_PIXELS) return ODM_STATUS_INVALID_DATA;
    if (s->pixel_format == ODM_RENDER_SURFACE_RGBA8) {
        if ((s->transfer != ODM_COLOR_TRANSFER_LINEAR &&
             s->transfer != ODM_COLOR_TRANSFER_SRGB) ||
            s->alpha_mode != ODM_ALPHA_STRAIGHT || s->pixel_bytes != pixels * 4u)
            return ODM_STATUS_INVALID_DATA;
    } else if (s->pixel_format == ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL) {
        if (s->transfer != ODM_COLOR_TRANSFER_LINEAR ||
            s->alpha_mode != ODM_ALPHA_PREMULTIPLIED || s->pixel_bytes != pixels * 8u)
            return ODM_STATUS_INVALID_DATA;
    } else {
        return ODM_STATUS_INVALID_DATA;
    }
    return ODM_STATUS_OK;
}

static int64_t map_coord_q16(int64_t local_q16, int64_t box_q16,
                             int64_t crop_start_q16, int64_t crop_size_q16) {
    if (box_q16 <= 0) return -INT64_C(32768);
    return crop_start_q16 + (local_q16 * crop_size_q16) / box_q16 - INT64_C(32768);
}

int64_t odm_layered_core_sdf_q16(const odm_layered_config *config,
                                  const odm_layered_frame_plan *plan,
                                  int64_t dx_q16, int64_t dy_q16) {
    if (!config || !plan) return INT64_MAX;
    return shape_sdf_q16(config->core.shape, dx_q16, dy_q16,
                         plan->core_half_w_q16, plan->core_half_h_q16,
                         plan->core_radius_q16, plan->core_corner_q16);
}

odm_status odm_layered_core_pixel_prevalidated(const odm_layered_config *config,
                                                const odm_layered_frame_plan *plan,
                                                const odm_render_surface_frame *surface,
                                                uint32_t x, uint32_t y,
                                                odm_layered_pixel16 *out) {
    int64_t xq, yq, dx, dy, sdf = 0, inner_sdf = 0, feather, border;
    uint32_t mask, inner_mask, border_mask;
    uint64_t circle_sum = 0u;
    int64_t box_w, box_h, local_x, local_y;
    int64_t src_w, src_h, crop_x, crop_y, crop_w, crop_h;
    int64_t disp_w, disp_h, disp_x, disp_y;
    int64_t sx, sy;
    odm_layered_pixel16 sample;
    uint32_t effective;
    if (!config || !plan || !surface || !out) return ODM_STATUS_INVALID_ARGUMENT;
    out->r = 0u; out->g = 0u; out->b = 0u; out->a = 0u;
    xq = ((int64_t)x << 16) + INT64_C(32768);
    yq = ((int64_t)y << 16) + INT64_C(32768);
    dx = xq - plan->center_x_q16; dy = yq - plan->center_y_q16;
    feather = config->core.feather_q16;
    /* Exact conservative rejection before SDF/sqrt. Any point farther than
     * half-extent + half-feather on either axis has mathematically zero mask
     * for circle, square and rounded-rect. This changes no boundary coverage. */
    {
        int64_t margin = feather > 0 ? feather / 2 : 0;
        if (abs_i64(dx) >= (int64_t)plan->core_half_w_q16 + margin ||
            abs_i64(dy) >= (int64_t)plan->core_half_h_q16 + margin)
            return ODM_STATUS_OK;
    }
    if (config->core.shape == ODM_CORE_SHAPE_CIRCLE) {
        uint64_t ux = (uint64_t)abs_i64(dx), uy = (uint64_t)abs_i64(dy);
        circle_sum = ux * ux + uy * uy;
        mask = circle_coverage_from_sum(circle_sum, plan->core_radius_q16, feather);
    } else {
        sdf = shape_sdf_q16(config->core.shape, dx, dy,
                            plan->core_half_w_q16, plan->core_half_h_q16,
                            plan->core_radius_q16, plan->core_corner_q16);
        mask = signed_coverage_q31(sdf, feather);
    }
    if (mask == 0u) return ODM_STATUS_OK;

    box_w = (int64_t)plan->core_half_w_q16 * 2;
    box_h = (int64_t)plan->core_half_h_q16 * 2;
    local_x = dx + plan->core_half_w_q16;
    local_y = dy + plan->core_half_h_q16;
    src_w = (int64_t)surface->width << 16; src_h = (int64_t)surface->height << 16;
    crop_x = 0; crop_y = 0; crop_w = src_w; crop_h = src_h;
    disp_x = 0; disp_y = 0; disp_w = box_w; disp_h = box_h;

    if (config->core.fit == ODM_CORE_FIT_COVER) {
        if ((uint64_t)surface->width * (uint64_t)box_h >
            (uint64_t)surface->height * (uint64_t)box_w) {
            crop_w = (src_h * box_w) / box_h; crop_x = (src_w - crop_w) / 2;
        } else {
            crop_h = (src_w * box_h) / box_w; crop_y = (src_h - crop_h) / 2;
        }
    } else if (config->core.fit == ODM_CORE_FIT_CONTAIN) {
        if ((uint64_t)surface->width * (uint64_t)box_h >
            (uint64_t)surface->height * (uint64_t)box_w) {
            disp_h = (box_w * src_h) / src_w; disp_y = (box_h - disp_h) / 2;
        } else {
            disp_w = (box_h * src_w) / src_h; disp_x = (box_w - disp_w) / 2;
        }
        if (local_x < disp_x || local_x > disp_x + disp_w ||
            local_y < disp_y || local_y > disp_y + disp_h) return ODM_STATUS_OK;
        local_x -= disp_x; local_y -= disp_y; box_w = disp_w; box_h = disp_h;
    }
    sx = map_coord_q16(local_x, box_w, crop_x, crop_w);
    sy = map_coord_q16(local_y, box_h, crop_y, crop_h);
    sample_bilinear(surface, sx, sy, &sample);
    effective = (uint32_t)(((uint64_t)mask * plan->core_opacity_q31 + (uint64_t)INT32_MAX / 2u) /
                           (uint64_t)INT32_MAX);
    /* Ganancia emisiva: solo color. Subir tambien el alfa cambiaria la
     * cobertura, es decir la forma del nucleo, y lo que se quiere es que la
     * imagen se encienda con el golpe, no que crezca por segunda vez. */
    if (plan->core_gain_q31 != 0u) {
        sample.r = gain_u16(sample.r, plan->core_gain_q31);
        sample.g = gain_u16(sample.g, plan->core_gain_q31);
        sample.b = gain_u16(sample.b, plan->core_gain_q31);
    }
    sample.r = mul_u16_q31(sample.r, effective);
    sample.g = mul_u16_q31(sample.g, effective);
    sample.b = mul_u16_q31(sample.b, effective);
    sample.a = mul_u16_q31(sample.a, effective);
    *out = sample;

    border = config->core.border_q16;
    if (border > 0) {
        int64_t ihw = plan->core_half_w_q16 - border;
        int64_t ihh = plan->core_half_h_q16 - border;
        int64_t ir = plan->core_radius_q16 - border;
        int64_t ic = plan->core_corner_q16 - border;
        if (ihw < 0) ihw = 0;
        if (ihh < 0) ihh = 0;
        if (ir < 0) ir = 0;
        if (ic < 0) ic = 0;
        if (config->core.shape == ODM_CORE_SHAPE_CIRCLE) {
            inner_mask = circle_coverage_from_sum(circle_sum, ir, feather);
        } else {
            inner_sdf = shape_sdf_q16(config->core.shape, dx, dy, ihw, ihh, ir, ic);
            inner_mask = signed_coverage_q31(inner_sdf, feather);
        }
        border_mask = mask > inner_mask ? mask - inner_mask : 0u;
        odm_layered_blend16(out, &config->core.border_color, border_mask,
                            plan->core_opacity_q31);
    }
    return ODM_STATUS_OK;
}


odm_status odm_layered_core_pixel(const odm_layered_config *config,
                                  const odm_layered_frame_plan *plan,
                                  const odm_render_surface_frame *surface,
                                  uint32_t x, uint32_t y,
                                  odm_layered_pixel16 *out) {
    odm_status st = odm_layered_core_surface_validate(surface);
    if (st != ODM_STATUS_OK) return st;
    return odm_layered_core_pixel_prevalidated(config, plan, surface, x, y, out);
}



static void core_raster_geometry(const odm_layered_config *config,
                                 const odm_layered_frame_plan *plan,
                                 const odm_render_surface_frame *surface,
                                 odm_layered_core_raster_plan *r) {
    int64_t src_w = (int64_t)surface->width << 16;
    int64_t src_h = (int64_t)surface->height << 16;
    int64_t margin = config->core.feather_q16 > 0u ?
                     (int64_t)config->core.feather_q16 / 2 : 0;
    int64_t extent_x = (int64_t)plan->core_half_w_q16 + margin + INT64_C(65536);
    int64_t extent_y = (int64_t)plan->core_half_h_q16 + margin + INT64_C(65536);
    memset(r, 0, sizeof(*r));
    r->box_w_q16 = (int64_t)plan->core_half_w_q16 * 2;
    r->box_h_q16 = (int64_t)plan->core_half_h_q16 * 2;
    r->crop_w_q16 = src_w; r->crop_h_q16 = src_h;
    r->disp_w_q16 = r->box_w_q16; r->disp_h_q16 = r->box_h_q16;
    if (config->core.fit == ODM_CORE_FIT_COVER) {
        if ((uint64_t)surface->width * (uint64_t)r->box_h_q16 >
            (uint64_t)surface->height * (uint64_t)r->box_w_q16) {
            r->crop_w_q16 = (src_h * r->box_w_q16) / r->box_h_q16;
            r->crop_x_q16 = (src_w - r->crop_w_q16) / 2;
        } else {
            r->crop_h_q16 = (src_w * r->box_h_q16) / r->box_w_q16;
            r->crop_y_q16 = (src_h - r->crop_h_q16) / 2;
        }
    } else if (config->core.fit == ODM_CORE_FIT_CONTAIN) {
        if ((uint64_t)surface->width * (uint64_t)r->box_h_q16 >
            (uint64_t)surface->height * (uint64_t)r->box_w_q16) {
            r->disp_h_q16 = (r->box_w_q16 * src_h) / src_w;
            r->disp_y_q16 = (r->box_h_q16 - r->disp_h_q16) / 2;
        } else {
            r->disp_w_q16 = (r->box_h_q16 * src_w) / src_h;
            r->disp_x_q16 = (r->box_w_q16 - r->disp_w_q16) / 2;
        }
    }
    r->min_x = ((int64_t)plan->center_x_q16 - extent_x) >> 16;
    r->max_x = ((int64_t)plan->center_x_q16 + extent_x) >> 16;
    r->min_y = ((int64_t)plan->center_y_q16 - extent_y) >> 16;
    r->max_y = ((int64_t)plan->center_y_q16 + extent_y) >> 16;
    if (r->min_x < 0) r->min_x = 0;
    if (r->min_y < 0) r->min_y = 0;
    if (r->max_x >= (int64_t)plan->width) r->max_x = (int64_t)plan->width - 1;
    if (r->max_y >= (int64_t)plan->height) r->max_y = (int64_t)plan->height - 1;
}

odm_status odm_layered_core_prepare_raster(const odm_layered_config *config,
                                           const odm_layered_frame_plan *plan,
                                           const odm_render_surface_frame *surface,
                                           odm_layered_core_raster_plan *out_raster,
                                           odm_layered_core_map_entry *xmap,
                                           uint32_t xmap_count) {
    uint32_t x;
    if (!config || !plan || !surface || !out_raster || !xmap)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (xmap_count < plan->width) return ODM_STATUS_BUFFER_TOO_SMALL;
    core_raster_geometry(config, plan, surface, out_raster);
    for (x = 0u; x < plan->width; ++x) {
        int64_t xq = ((int64_t)x << 16) + INT64_C(32768);
        int64_t local_x = xq - plan->center_x_q16 + plan->core_half_w_q16;
        int64_t mapped_x = local_x;
        int64_t map_box = out_raster->box_w_q16;
        int64_t sx;
        xmap[x].index = INT32_MIN;
        xmap[x].frac_q16 = 0u;
        if (config->core.fit == ODM_CORE_FIT_CONTAIN) {
            if (local_x < out_raster->disp_x_q16 ||
                local_x > out_raster->disp_x_q16 + out_raster->disp_w_q16)
                continue;
            mapped_x -= out_raster->disp_x_q16;
            map_box = out_raster->disp_w_q16;
        }
        sx = map_coord_q16(mapped_x, map_box,
                           out_raster->crop_x_q16, out_raster->crop_w_q16);
        {
            int64_t ix;
            uint32_t fx;
            (void)floor_q16(sx, &ix, &fx);
            if (ix < INT32_MIN || ix > INT32_MAX) return ODM_STATUS_OVERFLOW;
            xmap[x].index = (int32_t)ix;
            xmap[x].frac_q16 = fx;
        }
    }
    return ODM_STATUS_OK;
}

static int core_y_map(const odm_layered_config *config,
                      const odm_layered_frame_plan *plan,
                      const odm_layered_core_raster_plan *r,
                      uint32_t y, int64_t *out_iy, uint32_t *out_fy) {
    int64_t yq = ((int64_t)y << 16) + INT64_C(32768);
    int64_t local_y = yq - plan->center_y_q16 + plan->core_half_h_q16;
    int64_t mapped_y = local_y;
    int64_t map_box = r->box_h_q16;
    int64_t sy;
    if (config->core.fit == ODM_CORE_FIT_CONTAIN) {
        if (local_y < r->disp_y_q16 || local_y > r->disp_y_q16 + r->disp_h_q16)
            return 0;
        mapped_y -= r->disp_y_q16;
        map_box = r->disp_h_q16;
    }
    sy = map_coord_q16(mapped_y, map_box, r->crop_y_q16, r->crop_h_q16);
    return floor_q16(sy, out_iy, out_fy);
}

static odm_status core_pixel_cached(const odm_layered_config *config,
                                    const odm_layered_frame_plan *plan,
                                    const odm_render_surface_frame *surface,
                                    const odm_layered_core_map_entry *xm,
                                    const uint8_t *row0, const uint8_t *row1,
                                    int64_t iy, uint32_t fy,
                                    uint32_t x, uint32_t y,
                                    odm_layered_pixel16 *out) {
    int64_t xq, yq, dx, dy, sdf = 0, inner_sdf = 0, feather, border;
    uint32_t mask, inner_mask, border_mask, effective;
    uint64_t circle_sum = 0u;
    odm_layered_pixel16 sample;
    if (!config || !plan || !surface || !xm || !out) return ODM_STATUS_INVALID_ARGUMENT;
    out->r = 0u; out->g = 0u; out->b = 0u; out->a = 0u;
    if (xm->index == INT32_MIN) return ODM_STATUS_OK;
    xq = ((int64_t)x << 16) + INT64_C(32768);
    yq = ((int64_t)y << 16) + INT64_C(32768);
    dx = xq - plan->center_x_q16; dy = yq - plan->center_y_q16;
    feather = config->core.feather_q16;
    {
        int64_t margin = feather > 0 ? feather / 2 : 0;
        if (abs_i64(dx) >= (int64_t)plan->core_half_w_q16 + margin ||
            abs_i64(dy) >= (int64_t)plan->core_half_h_q16 + margin)
            return ODM_STATUS_OK;
    }
    if (config->core.shape == ODM_CORE_SHAPE_CIRCLE) {
        uint64_t ux = (uint64_t)abs_i64(dx), uy = (uint64_t)abs_i64(dy);
        circle_sum = ux * ux + uy * uy;
        mask = circle_coverage_from_sum(circle_sum, plan->core_radius_q16, feather);
    } else {
        sdf = shape_sdf_q16(config->core.shape, dx, dy,
                            plan->core_half_w_q16, plan->core_half_h_q16,
                            plan->core_radius_q16, plan->core_corner_q16);
        mask = signed_coverage_q31(sdf, feather);
    }
    if (mask == 0u) return ODM_STATUS_OK;
    if (row0 && row1 && xm->index >= 0 &&
        xm->index + 1 < (int64_t)surface->width)
        sample_bilinear_parts_interior(surface, row0, row1, xm->index,
                                       xm->frac_q16, fy, &sample);
    else
        sample_bilinear_parts(surface, xm->index, iy, xm->frac_q16, fy, &sample);
    effective = (uint32_t)(((uint64_t)mask * plan->core_opacity_q31 +
                            (uint64_t)INT32_MAX / 2u) / (uint64_t)INT32_MAX);
    sample.r = mul_u16_q31(sample.r, effective);
    sample.g = mul_u16_q31(sample.g, effective);
    sample.b = mul_u16_q31(sample.b, effective);
    sample.a = mul_u16_q31(sample.a, effective);
    *out = sample;
    border = config->core.border_q16;
    if (border > 0) {
        int64_t ihw = plan->core_half_w_q16 - border;
        int64_t ihh = plan->core_half_h_q16 - border;
        int64_t ir = plan->core_radius_q16 - border;
        int64_t ic = plan->core_corner_q16 - border;
        if (ihw < 0) ihw = 0;
        if (ihh < 0) ihh = 0;
        if (ir < 0) ir = 0;
        if (ic < 0) ic = 0;
        if (config->core.shape == ODM_CORE_SHAPE_CIRCLE)
            inner_mask = circle_coverage_from_sum(circle_sum, ir, feather);
        else {
            inner_sdf = shape_sdf_q16(config->core.shape, dx, dy, ihw, ihh, ir, ic);
            inner_mask = signed_coverage_q31(inner_sdf, feather);
        }
        border_mask = mask > inner_mask ? mask - inner_mask : 0u;
        odm_layered_blend16(out, &config->core.border_color, border_mask,
                            plan->core_opacity_q31);
    }
    return ODM_STATUS_OK;
}

odm_status odm_layered_core_render_rows_cached(const odm_layered_config *config,
                                               const odm_layered_frame_plan *plan,
                                               const odm_render_surface_frame *surface,
                                               const odm_layered_core_raster_plan *raster,
                                               const odm_layered_core_map_entry *xmap,
                                               uint32_t y_begin, uint32_t y_end,
                                               odm_layered_pixel16 *frame) {
    uint32_t y;
    if (!config || !plan || !surface || !raster || !xmap || !frame)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (y_begin >= y_end || y_begin >= plan->height || plan->core_opacity_q31 == 0u)
        return ODM_STATUS_OK;
    if (y_end > plan->height) y_end = plan->height;
    if ((int64_t)y_begin > raster->max_y || (int64_t)(y_end - 1u) < raster->min_y ||
        raster->min_x > raster->max_x) return ODM_STATUS_OK;
    if ((int64_t)y_begin < raster->min_y) y_begin = (uint32_t)raster->min_y;
    if ((int64_t)y_end - 1 > raster->max_y) y_end = (uint32_t)(raster->max_y + 1);
    for (y = y_begin; y < y_end; ++y) {
        int64_t iy;
        uint32_t fy;
        int64_t x;
        const uint8_t *row0 = NULL, *row1 = NULL;
        uint64_t row_bytes;
        if (!core_y_map(config, plan, raster, y, &iy, &fy)) continue;
        if (iy >= 0 && iy + 1 < (int64_t)surface->height) {
            row_bytes = (uint64_t)surface->width *
                        (surface->pixel_format == ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL ? 8u : 4u);
            row0 = surface->pixels + (uint64_t)iy * row_bytes;
            row1 = row0 + row_bytes;
        }
        for (x = raster->min_x; x <= raster->max_x; ++x) {
            odm_layered_pixel16 core;
            odm_status st = core_pixel_cached(config, plan, surface, &xmap[x], row0, row1, iy, fy,
                                               (uint32_t)x, y, &core);
            if (st != ODM_STATUS_OK) return st;
            odm_layered_blend_premul16(&frame[(uint64_t)y * plan->width + (uint64_t)x],
                                       &core, (uint32_t)INT32_MAX);
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_layered_core_render_rows(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        const odm_render_surface_frame *surface,
                                        uint32_t y_begin, uint32_t y_end,
                                        odm_layered_pixel16 *frame) {
    int64_t margin, extent_x, extent_y, minx, maxx, miny, maxy;
    uint32_t y;
    if (!config || !plan || !surface || !frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (y_begin >= y_end || y_begin >= plan->height || plan->core_opacity_q31 == 0u)
        return ODM_STATUS_OK;
    if (y_end > plan->height) y_end = plan->height;
    margin = config->core.feather_q16 > 0u ? (int64_t)config->core.feather_q16 / 2 : 0;
    /* One extra pixel is conservative; odm_layered_core_pixel_prevalidated()
     * remains the final canonical mask authority. */
    extent_x = (int64_t)plan->core_half_w_q16 + margin + INT64_C(65536);
    extent_y = (int64_t)plan->core_half_h_q16 + margin + INT64_C(65536);
    minx = ((int64_t)plan->center_x_q16 - extent_x) >> 16;
    maxx = ((int64_t)plan->center_x_q16 + extent_x) >> 16;
    miny = ((int64_t)plan->center_y_q16 - extent_y) >> 16;
    maxy = ((int64_t)plan->center_y_q16 + extent_y) >> 16;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int64_t)plan->width) maxx = (int64_t)plan->width - 1;
    if (maxy >= (int64_t)plan->height) maxy = (int64_t)plan->height - 1;
    if ((int64_t)y_begin > maxy || (int64_t)(y_end - 1u) < miny || minx > maxx)
        return ODM_STATUS_OK;
    if ((int64_t)y_begin < miny) y_begin = (uint32_t)miny;
    if ((int64_t)y_end - 1 > maxy) y_end = (uint32_t)(maxy + 1);
    for (y = y_begin; y < y_end; ++y) {
        int64_t x;
        for (x = minx; x <= maxx; ++x) {
            odm_layered_pixel16 core;
            odm_status st = odm_layered_core_pixel_prevalidated(config, plan, surface,
                                                                (uint32_t)x, y, &core);
            if (st != ODM_STATUS_OK) return st;
            odm_layered_blend_premul16(&frame[(uint64_t)y * plan->width + (uint64_t)x],
                                       &core, (uint32_t)INT32_MAX);
        }
    }
    return ODM_STATUS_OK;
}

#include "renderer_internal.h"

#include "odm_fixed.h"

#include <limits.h>
#include <stddef.h>

#define RENDER_CORDIC_K_Q31 INT64_C(1304065748)
#define RENDER_QUARTER_PHASE UINT32_C(0x40000000)

static const uint32_t render_cordic_atan_phase[31] = {
    536870912u, 316933406u, 167458907u, 85004756u, 42667331u,
    21354465u, 10679838u, 5340245u, 2670163u, 1335087u, 667544u,
    333772u, 166886u, 83443u, 41722u, 20861u, 10430u, 5215u, 2608u,
    1304u, 652u, 326u, 163u, 81u, 41u, 20u, 10u, 5u, 3u, 1u, 1u
};

static uint64_t mag_i64(int64_t value) {
    if (value >= 0) return (uint64_t)value;
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static int32_t cordic_sin_q31(uint32_t phase) {
    uint32_t quadrant = phase >> 30;
    uint32_t offset = phase & UINT32_C(0x3fffffff);
    int sign = quadrant >= 2u ? -1 : 1;
    int64_t z = (quadrant == 0u || quadrant == 2u)
                    ? (int64_t)offset
                    : (int64_t)(RENDER_QUARTER_PHASE - offset);
    int64_t x = RENDER_CORDIC_K_Q31, y = 0;
    unsigned i;
    if (phase == 0u || phase == UINT32_C(0x80000000)) return 0;
    if (phase == UINT32_C(0x40000000)) return INT32_MAX;
    if (phase == UINT32_C(0xc0000000)) return -INT32_MAX;
    for (i = 0u; i < 31u; ++i) {
        int64_t divisor = INT64_C(1) << i;
        int64_t xs = x / divisor, ys = y / divisor;
        int64_t nx, ny;
        if (z >= 0) {
            nx = x - ys; ny = y + xs; z -= (int64_t)render_cordic_atan_phase[i];
        } else {
            nx = x + ys; ny = y - xs; z += (int64_t)render_cordic_atan_phase[i];
        }
        x = nx; y = ny;
    }
    if (sign < 0) y = -y;
    if (y > INT32_MAX) y = INT32_MAX;
    if (y < -INT32_MAX) y = -INT32_MAX;
    return (int32_t)y;
}

static odm_status phase_sincos_q32(odm_phase_u32 phase,
                                   odm_q32_32 *out_sin,
                                   odm_q32_32 *out_cos) {
    int32_t s31, c31;
    int64_t s32, c32;
    if (!out_sin || !out_cos) return ODM_STATUS_INVALID_ARGUMENT;
    if (phase == 0u) { *out_sin = 0; *out_cos = ODM_Q32_ONE; return ODM_STATUS_OK; }
    if (phase == UINT32_C(0x40000000)) { *out_sin = ODM_Q32_ONE; *out_cos = 0; return ODM_STATUS_OK; }
    if (phase == UINT32_C(0x80000000)) { *out_sin = 0; *out_cos = -ODM_Q32_ONE; return ODM_STATUS_OK; }
    if (phase == UINT32_C(0xc0000000)) { *out_sin = -ODM_Q32_ONE; *out_cos = 0; return ODM_STATUS_OK; }
    s31 = cordic_sin_q31(phase);
    c31 = cordic_sin_q31(phase + RENDER_QUARTER_PHASE);
    if (odm_i64_mul_checked((int64_t)s31, INT64_C(2), &s32) != ODM_STATUS_OK ||
        odm_i64_mul_checked((int64_t)c31, INT64_C(2), &c32) != ODM_STATUS_OK) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_sin = (odm_q32_32)s32; *out_cos = (odm_q32_32)c32;
    return ODM_STATUS_OK;
}

/* |numerator| <= denominator and denominator > 0. Returns Q32.32 ratio using
 * long division, rounded to nearest with ties away from zero. */
static odm_status ratio_unit_q32(int64_t numerator, int64_t denominator,
                                 odm_q32_32 *out) {
    uint64_t n, d, whole, rem, frac = 0u, q;
    unsigned i;
    int neg;
    if (!out || denominator <= 0) return ODM_STATUS_INVALID_ARGUMENT;
    n = mag_i64(numerator); d = (uint64_t)denominator;
    if (n > d) return ODM_STATUS_INVALID_ARGUMENT;
    neg = numerator < 0;
    whole = n / d; rem = n % d;
    for (i = 0u; i < 32u; ++i) {
        rem <<= 1;
        frac <<= 1;
        if (rem >= d) { rem -= d; frac |= UINT64_C(1); }
    }
    q = (whole << 32) | frac;
    if (rem >= d - rem) ++q;
    if (q > (uint64_t)ODM_Q32_ONE) q = (uint64_t)ODM_Q32_ONE;
    *out = neg ? -(odm_q32_32)q : (odm_q32_32)q;
    return ODM_STATUS_OK;
}

static odm_q1_31 q32_fraction_to_q31(uint64_t fraction) {
    uint64_t product = fraction * (uint64_t)INT32_MAX;
    uint64_t q = product >> 32;
    uint64_t r = product & UINT64_C(0xffffffff);
    if (r >= UINT64_C(0x80000000)) ++q;
    if (q > (uint64_t)INT32_MAX) q = (uint64_t)INT32_MAX;
    return (odm_q1_31)q;
}

static void texel(const odm_render_surface_frame *surface, int64_t x, int64_t y,
                  odm_linear_pixel *out) {
    const uint8_t *p;
    odm_q1_31 alpha;
    odm_render_q4_27 r, g, b;
    out->r = 0; out->g = 0; out->b = 0; out->a = 0;
    if (x < 0 || y < 0 || x >= (int64_t)surface->width ||
        y >= (int64_t)surface->height) return;
    p = surface->pixels + ((uint64_t)y * (uint64_t)surface->width + (uint64_t)x) * 4u;
    alpha = odm_renderer_alpha_u8(p[3]);
    r = odm_renderer_decode_u8(p[0], surface->transfer);
    g = odm_renderer_decode_u8(p[1], surface->transfer);
    b = odm_renderer_decode_u8(p[2], surface->transfer);
    (void)odm_renderer_q27_mul_q31(r, alpha, &out->r);
    (void)odm_renderer_q27_mul_q31(g, alpha, &out->g);
    (void)odm_renderer_q27_mul_q31(b, alpha, &out->b);
    out->a = alpha;
}

static odm_status pixel_lerp(const odm_linear_pixel *a, const odm_linear_pixel *b,
                             odm_q1_31 t, odm_linear_pixel *out) {
    odm_status st;
    st = odm_renderer_q27_lerp(a->r, b->r, t, &out->r); if (st != ODM_STATUS_OK) return st;
    st = odm_renderer_q27_lerp(a->g, b->g, t, &out->g); if (st != ODM_STATUS_OK) return st;
    st = odm_renderer_q27_lerp(a->b, b->b, t, &out->b); if (st != ODM_STATUS_OK) return st;
    return odm_renderer_q31_lerp(a->a, b->a, t, &out->a);
}

odm_status odm_renderer_sample_node(const odm_frame_node_state *node,
                                    const odm_render_surface_frame *surface,
                                    uint32_t out_x, uint32_t out_y,
                                    uint32_t out_width, uint32_t out_height,
                                    odm_linear_pixel *out_pixel) {
    odm_q32_32 nx, ny, dx, dy, sinv, cosv, t0, t1, rx, ry, lx, ly;
    int64_t tx, ty, ix, iy, remx, remy;
    uint64_t fx, fy;
    odm_q1_31 wx, wy;
    odm_linear_pixel p00, p10, p01, p11, top, bottom, sample;
    odm_status st;
    if (!node || !surface || !out_pixel || out_width == 0u || out_height == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    out_pixel->r = 0; out_pixel->g = 0; out_pixel->b = 0; out_pixel->a = 0;
    if (node->scale_x_q32_32 <= 0 || node->scale_y_q32_32 <= 0 || node->opacity_q31 <= 0) return ODM_STATUS_OK;

    nx = (odm_q32_32)((((uint64_t)out_x * 2u + 1u) * (uint64_t)ODM_Q32_ONE) / out_width) - ODM_Q32_ONE;
    ny = (odm_q32_32)((((uint64_t)out_y * 2u + 1u) * (uint64_t)ODM_Q32_ONE) / out_height) - ODM_Q32_ONE;
    st = odm_q32_32_sub(nx, node->x_q32_32, &dx); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_sub(ny, node->y_q32_32, &dy); if (st != ODM_STATUS_OK) return st;
    st = phase_sincos_q32(node->phase, &sinv, &cosv); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_mul(dx, cosv, &t0); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_mul(dy, sinv, &t1); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_add(t0, t1, &rx); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_mul(dx, sinv, &t0); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_mul(dy, cosv, &t1); if (st != ODM_STATUS_OK) return st;
    st = odm_q32_32_sub(t1, t0, &ry); if (st != ODM_STATUS_OK) return st;
    if (mag_i64(rx) > (uint64_t)node->scale_x_q32_32 ||
        mag_i64(ry) > (uint64_t)node->scale_y_q32_32) return ODM_STATUS_OK;
    st = ratio_unit_q32(rx, node->scale_x_q32_32, &lx); if (st != ODM_STATUS_OK) return st;
    st = ratio_unit_q32(ry, node->scale_y_q32_32, &ly); if (st != ODM_STATUS_OK) return st;

    tx = ((lx + ODM_Q32_ONE) * (int64_t)surface->width) / 2 - ODM_Q32_ONE / 2;
    ty = ((ly + ODM_Q32_ONE) * (int64_t)surface->height) / 2 - ODM_Q32_ONE / 2;
    ix = tx / ODM_Q32_ONE; remx = tx % ODM_Q32_ONE;
    iy = ty / ODM_Q32_ONE; remy = ty % ODM_Q32_ONE;
    if (remx < 0) { --ix; remx += ODM_Q32_ONE; }
    if (remy < 0) { --iy; remy += ODM_Q32_ONE; }
    fx = (uint64_t)remx; fy = (uint64_t)remy;
    wx = q32_fraction_to_q31(fx); wy = q32_fraction_to_q31(fy);
    texel(surface, ix, iy, &p00); texel(surface, ix + 1, iy, &p10);
    texel(surface, ix, iy + 1, &p01); texel(surface, ix + 1, iy + 1, &p11);
    st = pixel_lerp(&p00, &p10, wx, &top); if (st != ODM_STATUS_OK) return st;
    st = pixel_lerp(&p01, &p11, wx, &bottom); if (st != ODM_STATUS_OK) return st;
    st = pixel_lerp(&top, &bottom, wy, &sample); if (st != ODM_STATUS_OK) return st;
    st = odm_renderer_q27_mul_q31(sample.r, node->opacity_q31, &out_pixel->r); if (st != ODM_STATUS_OK) return st;
    st = odm_renderer_q27_mul_q31(sample.g, node->opacity_q31, &out_pixel->g); if (st != ODM_STATUS_OK) return st;
    st = odm_renderer_q27_mul_q31(sample.b, node->opacity_q31, &out_pixel->b); if (st != ODM_STATUS_OK) return st;
    st = odm_q1_31_mul(sample.a, node->opacity_q31, &out_pixel->a); if (st != ODM_STATUS_OK) return st;
    return ODM_STATUS_OK;
}

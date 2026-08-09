#include "compositor_internal.h"

#include "odm_time.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define LAYER_Q31_ONE ((uint32_t)INT32_MAX)
#define LAYER_POLICY_MAGIC "ODMLAYR3"

static int bytes_zero_u32(const uint32_t *p, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) if (p[i] != 0u) return 0;
    return 1;
}

static int text_canonical(const char *p, size_t n) {
    size_t i, z = n;
    if (!p || n == 0u) return 0;
    for (i = 0u; i < n; ++i) {
        unsigned char ch = (unsigned char)p[i];
        if (ch == 0u) { z = i; break; }
        if (ch < 32u || ch > 126u) return 0;
    }
    if (z == n) return 0;
    for (i = z + 1u; i < n; ++i) if (p[i] != '\0') return 0;
    return 1;
}

static int q31u(uint32_t v) { return v <= (uint32_t)INT32_MAX; }

static int aspect_ok(uint32_t aspect, uint32_t w, uint32_t h) {
    uint64_t a = w, b = h;
    switch (aspect) {
        case ODM_CANVAS_ASPECT_CUSTOM: return 1;
        case ODM_CANVAS_ASPECT_HORIZONTAL_16_9: return a * 9u == b * 16u;
        case ODM_CANVAS_ASPECT_SQUARE_1_1: return w == h;
        case ODM_CANVAS_ASPECT_VERTICAL_9_16: return a * 16u == b * 9u;
        case ODM_CANVAS_ASPECT_HORIZONTAL_4_3: return a * 3u == b * 4u;
        case ODM_CANVAS_ASPECT_VERTICAL_4_5: return a * 5u == b * 4u;
        default: return 0;
    }
}

static int rgba_valid(const odm_rgba16 *c) {
    (void)c;
    return 1; /* all uint16 channel combinations are canonical */
}

static uint32_t q31_ratio_u32(uint32_t n, uint32_t d) {
    uint64_t v;
    if (d == 0u || n > d) return 0u;
    v = (uint64_t)(uint32_t)INT32_MAX * (uint64_t)n + (uint64_t)d / 2u;
    v /= (uint64_t)d;
    return (uint32_t)v;
}

static uint32_t scaled_q16(uint32_t min_dimension, uint32_t divisor,
                           uint32_t min_px, uint32_t max_px) {
    uint64_t v;
    uint64_t lo = (uint64_t)min_px << 16;
    uint64_t hi = (uint64_t)max_px << 16;
    if (divisor == 0u) return (uint32_t)lo;
    v = ((uint64_t)min_dimension << 16) / (uint64_t)divisor;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (uint32_t)v;
}

static int copy_metadata_ascii(char *dst, size_t cap, const char *src) {
    size_t n = 0u, i;
    if (!dst || cap == 0u) return 0;
    memset(dst, 0, cap);
    if (!src) return 1;
    while (src[n] != '\0') {
        unsigned char ch = (unsigned char)src[n];
        if (n + 1u >= cap || ch < 32u || ch > 126u) return 0;
        ++n;
    }
    for (i = 0u; i < n; ++i) dst[i] = src[i];
    return 1;
}

odm_status odm_layered_config_init_default(odm_layered_config *out_config,
                                            uint32_t aspect,
                                            uint32_t width,
                                            uint32_t height,
                                            int32_t fps) {
    odm_layered_config c;
    uint32_t min_dim, text_scale_px, particles;
    if (!out_config || width == 0u || height == 0u ||
        width > 16384u || height > 16384u ||
        !odm_time_fps_supported(fps) || !aspect_ok(aspect, width, height))
        return ODM_STATUS_INVALID_ARGUMENT;
    memset(&c, 0, sizeof(c));
    min_dim = width < height ? width : height;
    c.schema_version = ODM_LAYERED_SCHEMA_VERSION;

    c.canvas.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c.canvas.aspect = aspect; c.canvas.width = width; c.canvas.height = height;
    c.canvas.fps = fps;
    c.canvas.safe_left_q31 = q31_ratio_u32(1u, 20u);
    c.canvas.safe_top_q31 = q31_ratio_u32(1u, 20u);
    c.canvas.safe_right_q31 = q31_ratio_u32(1u, 20u);
    c.canvas.safe_bottom_q31 = q31_ratio_u32(1u, 20u);

    c.background.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c.background.style = ODM_BACKGROUND_PERSPECTIVE_GRID;
    c.background.solid_color.a = UINT16_MAX;
    c.background.grid_color.r = UINT16_MAX;
    c.background.grid_color.g = UINT16_MAX;
    c.background.grid_color.b = UINT16_MAX;
    c.background.grid_color.a = UINT16_C(28672);
    c.background.grid_spacing_q16 = scaled_q16(min_dim, 16u, 8u, 1024u);
    c.background.grid_line_q16 = scaled_q16(min_dim, 720u, 1u, 8u);
    c.background.grid_feather_q16 = UINT32_C(1) << 16;
    c.background.zoom_reactivity_q31 = q31_ratio_u32(1u, 4u);
    c.background.warp_reactivity_q31 = q31_ratio_u32(1u, 8u);
    c.background.depth_reactivity_q31 = q31_ratio_u32(1u, 8u);
    c.background.opacity_q31 = (uint32_t)INT32_MAX;

    c.core.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c.core.shape = ODM_CORE_SHAPE_CIRCLE; c.core.fit = ODM_CORE_FIT_COVER;
    c.core.center_x_q31 = q31_ratio_u32(1u, 2u);
    c.core.center_y_q31 = q31_ratio_u32(1u, 2u);
    c.core.width_q31 = q31_ratio_u32(9u, 20u);
    c.core.height_q31 = q31_ratio_u32(9u, 20u);
    c.core.corner_radius_q31 = q31_ratio_u32(1u, 8u);
    c.core.border_q16 = scaled_q16(min_dim, 720u, 1u, 8u);
    c.core.feather_q16 = UINT32_C(1) << 16;
    c.core.scale_reactivity_q31 = q31_ratio_u32(1u, 16u);
    c.core.opacity_q31 = (uint32_t)INT32_MAX;
    c.core.border_color.r = UINT16_MAX; c.core.border_color.g = UINT16_MAX;
    c.core.border_color.b = UINT16_MAX; c.core.border_color.a = UINT16_MAX;

    c.field.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c.field.flags = ODM_FIELD_RADIAL_BARS | ODM_FIELD_PARTICLES | ODM_FIELD_ORBIT_RING;
    c.field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS;
    particles = min_dim / 16u;
    if (particles < 16u) particles = 16u;
    if (particles > 128u) particles = 128u;
    c.field.particle_count = particles;
    c.field.ring_gap_q16 = scaled_q16(min_dim, 96u, 2u, 128u);
    c.field.bar_min_q16 = scaled_q16(min_dim, 240u, 1u, 64u);
    c.field.bar_max_q16 = scaled_q16(min_dim, 40u, 4u, 512u);
    c.field.bar_width_q16 = scaled_q16(min_dim, 720u, 1u, 16u);
    c.field.particle_radius_q16 = scaled_q16(min_dim, 720u, 1u, 16u);
    c.field.field_opacity_q31 = q31_ratio_u32(3u, 4u);
    c.field.primary_color.r = UINT16_MAX; c.field.primary_color.g = UINT16_MAX;
    c.field.primary_color.b = UINT16_MAX; c.field.primary_color.a = UINT16_MAX;
    c.field.secondary_color.r = UINT16_C(40960); c.field.secondary_color.g = UINT16_C(40960);
    c.field.secondary_color.b = UINT16_C(40960); c.field.secondary_color.a = UINT16_MAX;
    c.field.seed = UINT64_C(0x0d130d130d130d13);

    c.hud.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c.hud.flags = ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE;
    c.hud.margin_q16 = scaled_q16(min_dim, 25u, 8u, 512u);
    c.hud.progress_height_q16 = scaled_q16(min_dim, 360u, 1u, 32u);
    c.hud.progress_width_q31 = q31_ratio_u32(3u, 4u);
    text_scale_px = min_dim / 540u;
    if (text_scale_px < 1u) text_scale_px = 1u;
    if (text_scale_px > 32u) text_scale_px = 32u;
    c.hud.text_scale_q16 = text_scale_px << 16;
    c.hud.line_gap_q16 = UINT32_C(1) << 16;
    c.hud.opacity_q31 = (uint32_t)INT32_MAX;
    c.hud.foreground_color.r = UINT16_MAX; c.hud.foreground_color.g = UINT16_MAX;
    c.hud.foreground_color.b = UINT16_MAX; c.hud.foreground_color.a = UINT16_MAX;
    c.hud.background_color.a = UINT16_C(32768);
    c.hud.metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_CENTER;
    c.hud.progress_style = ODM_HUD_PROGRESS_HAIRLINE;
    c.hud.time_mode = ODM_HUD_TIME_ELAPSED_TOTAL;
    /* Empty strings are canonical and metadata flags are disabled by default. */
    c.hud.title[0] = '\0'; c.hud.artist[0] = '\0';

    if (odm_layered_reaction_init_default(&c.reaction) != ODM_STATUS_OK)
        return ODM_STATUS_INVALID_DATA;
    if (odm_layered_config_validate(&c) != ODM_STATUS_OK) return ODM_STATUS_INVALID_DATA;
    *out_config = c;
    return ODM_STATUS_OK;
}

odm_status odm_layered_config_set_core_shape(odm_layered_config *config,
                                              uint32_t shape) {
    odm_layered_config c;
    if (!config) return ODM_STATUS_INVALID_ARGUMENT;
    if (shape < ODM_CORE_SHAPE_CIRCLE || shape > ODM_CORE_SHAPE_ROUNDED_RECT)
        return ODM_STATUS_INVALID_ARGUMENT;
    c = *config;
    c.core.shape = shape;
    if (shape == ODM_CORE_SHAPE_ROUNDED_RECT && c.core.corner_radius_q31 == 0u)
        c.core.corner_radius_q31 = q31_ratio_u32(1u, 8u);
    if (odm_layered_config_validate(&c) != ODM_STATUS_OK) return ODM_STATUS_INVALID_DATA;
    *config = c;
    return ODM_STATUS_OK;
}

odm_status odm_layered_config_set_metadata(odm_layered_config *config,
                                           const char *title,
                                           const char *artist) {
    odm_layered_config c;
    if (!config) return ODM_STATUS_INVALID_ARGUMENT;
    c = *config;
    if (!copy_metadata_ascii(c.hud.title, sizeof(c.hud.title), title) ||
        !copy_metadata_ascii(c.hud.artist, sizeof(c.hud.artist), artist))
        return ODM_STATUS_INVALID_DATA;
    c.hud.flags &= ~(ODM_HUD_TITLE | ODM_HUD_ARTIST);
    if (c.hud.title[0] != '\0') c.hud.flags |= ODM_HUD_TITLE;
    if (c.hud.artist[0] != '\0') c.hud.flags |= ODM_HUD_ARTIST;
    if (odm_layered_config_validate(&c) != ODM_STATUS_OK) return ODM_STATUS_INVALID_DATA;
    *config = c;
    return ODM_STATUS_OK;
}

odm_status odm_layered_config_validate(const odm_layered_config *c) {
    uint64_t pixels;
    if (!c) return ODM_STATUS_INVALID_ARGUMENT;
    if (c->schema_version != ODM_LAYERED_SCHEMA_VERSION || c->flags != 0u)
        return ODM_STATUS_VERSION_MISMATCH;
    if (!odm_layered_reaction_validate(&c->reaction)) return ODM_STATUS_INVALID_DATA;
    if (c->canvas.schema_version != ODM_LAYERED_SCHEMA_VERSION ||
        c->canvas.width == 0u || c->canvas.height == 0u ||
        c->canvas.width > 16384u || c->canvas.height > 16384u ||
        !odm_time_fps_supported(c->canvas.fps) || c->canvas.flags != 0u ||
        !q31u(c->canvas.safe_left_q31) || !q31u(c->canvas.safe_top_q31) ||
        !q31u(c->canvas.safe_right_q31) || !q31u(c->canvas.safe_bottom_q31) ||
        (uint64_t)c->canvas.safe_left_q31 + c->canvas.safe_right_q31 >= (uint64_t)INT32_MAX ||
        (uint64_t)c->canvas.safe_top_q31 + c->canvas.safe_bottom_q31 >= (uint64_t)INT32_MAX ||
        !bytes_zero_u32(c->canvas.reserved, 2u) ||
        !aspect_ok(c->canvas.aspect, c->canvas.width, c->canvas.height))
        return ODM_STATUS_INVALID_DATA;
    pixels = (uint64_t)c->canvas.width * (uint64_t)c->canvas.height;
    if (pixels > ODM_LAYERED_MAX_PIXELS) return ODM_STATUS_BUDGET_EXCEEDED;

    if (c->background.schema_version != ODM_LAYERED_SCHEMA_VERSION ||
        c->background.style > ODM_BACKGROUND_PERSPECTIVE_GRID ||
        !rgba_valid(&c->background.solid_color) || !rgba_valid(&c->background.grid_color) ||
        !q31u(c->background.zoom_reactivity_q31) ||
        !q31u(c->background.warp_reactivity_q31) ||
        !q31u(c->background.depth_reactivity_q31) ||
        !q31u(c->background.opacity_q31) ||
        !bytes_zero_u32(c->background.reserved, 6u)) return ODM_STATUS_INVALID_DATA;
    if ((c->background.style == ODM_BACKGROUND_GRID ||
         c->background.style == ODM_BACKGROUND_PERSPECTIVE_GRID) &&
        (c->background.grid_spacing_q16 < (4u << 16) ||
         c->background.grid_spacing_q16 > (4096u << 16) ||
         c->background.grid_line_q16 == 0u ||
         c->background.grid_line_q16 > c->background.grid_spacing_q16 / 2u ||
         c->background.grid_feather_q16 > (4u << 16))) return ODM_STATUS_INVALID_DATA;

    if (c->core.schema_version != ODM_LAYERED_SCHEMA_VERSION ||
        c->core.shape < ODM_CORE_SHAPE_CIRCLE || c->core.shape > ODM_CORE_SHAPE_ROUNDED_RECT ||
        c->core.fit < ODM_CORE_FIT_COVER || c->core.fit > ODM_CORE_FIT_STRETCH ||
        !q31u(c->core.center_x_q31) || !q31u(c->core.center_y_q31) ||
        c->core.width_q31 == 0u || c->core.height_q31 == 0u ||
        !q31u(c->core.width_q31) || !q31u(c->core.height_q31) ||
        !q31u(c->core.corner_radius_q31) || !q31u(c->core.scale_reactivity_q31) ||
        !q31u(c->core.opacity_q31) || c->core.border_q16 > (64u << 16) ||
        c->core.feather_q16 == 0u || c->core.feather_q16 > (8u << 16) ||
        !rgba_valid(&c->core.border_color) || !bytes_zero_u32(c->core.reserved, 6u))
        return ODM_STATUS_INVALID_DATA;
    if (c->core.shape == ODM_CORE_SHAPE_CIRCLE && c->core.width_q31 != c->core.height_q31)
        return ODM_STATUS_INVALID_DATA;

    if (c->field.schema_version != ODM_LAYERED_SCHEMA_VERSION ||
        (c->field.flags & ~(ODM_FIELD_RADIAL_BARS | ODM_FIELD_PARTICLES | ODM_FIELD_ORBIT_RING)) != 0u ||
        (c->field.radial_segments != ODM_COMPOSITION_RADIAL_SEGMENTS &&
         c->field.radial_segments != ODM_COMPOSITION_RADIAL_SEGMENTS_MAX) ||
        c->field.particle_count > ODM_LAYERED_MAX_PARTICLES ||
        c->field.ring_gap_q16 > (512u << 16) ||
        c->field.bar_min_q16 > c->field.bar_max_q16 ||
        c->field.bar_max_q16 > (2048u << 16) || c->field.bar_width_q16 > (32u << 16) ||
        c->field.particle_radius_q16 > (32u << 16) || !q31u(c->field.field_opacity_q31) ||
        !rgba_valid(&c->field.primary_color) || !rgba_valid(&c->field.secondary_color) ||
        !bytes_zero_u32(c->field.reserved, 6u)) return ODM_STATUS_INVALID_DATA;

    if (c->hud.schema_version != ODM_LAYERED_SCHEMA_VERSION ||
        (c->hud.flags & ~(ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE | ODM_HUD_TITLE | ODM_HUD_ARTIST)) != 0u ||
        c->hud.margin_q16 > (1024u << 16) || c->hud.progress_height_q16 > (128u << 16) ||
        !q31u(c->hud.progress_width_q31) || c->hud.text_scale_q16 == 0u ||
        c->hud.text_scale_q16 > (32u << 16) || c->hud.line_gap_q16 > (128u << 16) ||
        !q31u(c->hud.opacity_q31) ||
        !rgba_valid(&c->hud.foreground_color) || !rgba_valid(&c->hud.background_color) ||
        !text_canonical(c->hud.title, sizeof(c->hud.title)) ||
        !text_canonical(c->hud.artist, sizeof(c->hud.artist)) ||
        c->hud.metadata_anchor > ODM_HUD_ANCHOR_BOTTOM_RIGHT ||
        c->hud.progress_style > ODM_HUD_PROGRESS_HAIRLINE ||
        c->hud.time_mode > ODM_HUD_TIME_REMAINING ||
        c->hud.reserved0 != 0u) return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static int32_t ratio_px_q16(uint32_t dimension, uint32_t q31) {
    uint64_t num = (uint64_t)dimension * UINT64_C(65536) * (uint64_t)q31;
    num += (uint64_t)INT32_MAX / 2u;
    num /= (uint64_t)INT32_MAX;
    if (num > (uint64_t)INT32_MAX) return INT32_MAX;
    return (int32_t)num;
}

static uint32_t mul_q31u(uint32_t a, uint32_t b) {
    uint64_t p = (uint64_t)a * (uint64_t)b + (uint64_t)INT32_MAX / 2u;
    p /= (uint64_t)INT32_MAX;
    return p > (uint64_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)p;
}

/* Exact rounded (n * m) / d for n < d <= INT64_MAX and 32-bit m, without
 * ever materializing the potentially 95-bit product.  The quotient/remainder
 * recurrence keeps remainder < d; overflow-safe modular doubling/addition use
 * subtraction forms instead of 2*r or r+n when those could exceed uint64_t. */
static uint64_t muldiv_round_u64_u32_u64(uint64_t n, uint32_t m, uint64_t d) {
    uint64_t q = 0u, r = 0u;
    int bit;
    if (d == 0u || n == 0u || m == 0u) return 0u;
    if (n >= d) {
        /* Current caller only admits n<=d.  Handle equality exactly and keep
         * this helper fail-safe for accidental larger input. */
        if (n == d) return (uint64_t)m;
        return UINT64_MAX;
    }
    for (bit = 31; bit >= 0; --bit) {
        q <<= 1;
        if (r >= d - r) { r = r - (d - r); ++q; }
        else r += r;
        if (((m >> (unsigned)bit) & 1u) != 0u) {
            if (r >= d - n) { r = r - (d - n); ++q; }
            else r += n;
        }
    }
    /* nearest integer, exact half rounds upward; d-r avoids 2*r overflow. */
    if (r >= d - r) ++q;
    return q;
}

static uint32_t progress_q31_exact(int64_t sample, int64_t duration) {
    uint64_t q;
    if (sample <= 0) return 0u;
    if (sample >= duration) return (uint32_t)INT32_MAX;
    q = muldiv_round_u64_u32_u64((uint64_t)sample, (uint32_t)INT32_MAX,
                                 (uint64_t)duration);
    return q > (uint64_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)q;
}

int32_t odm_layered_mul_q16_q31(int32_t value_q16, uint32_t factor_q31) {
    int64_t p = (int64_t)value_q16 * (int64_t)factor_q31;
    if (p >= 0) p += INT32_MAX / 2; else p -= INT32_MAX / 2;
    p /= INT32_MAX;
    if (p > INT32_MAX) p = INT32_MAX;
    if (p < INT32_MIN) p = INT32_MIN;
    return (int32_t)p;
}

odm_status odm_layered_resolve_frame_plan(const odm_layered_config *c,
                                           const odm_composition_frame_state *comp,
                                           const odm_director_frame_state *dir,
                                           int64_t sample, int64_t duration,
                                           odm_layered_frame_plan *out) {
    odm_layered_frame_plan p;
    uint32_t react, particle_gain, signal;
    odm_reaction_config reaction;
    int32_t base_w, base_h, add_w, add_h, min_half, spacing, spacing_add;
    uint64_t prog;
    uint32_t i;
    odm_status st = odm_layered_config_validate(c);
    if (st != ODM_STATUS_OK) return st;
    if (!comp || !dir || !out) return ODM_STATUS_INVALID_ARGUMENT;
    if (comp->schema_version != ODM_COMPOSITION_SCHEMA_VERSION ||
        dir->schema_version != ODM_DIRECTOR_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;
    if (comp->tick_index != dir->tick_index || dir->layout > ODM_DIRECTOR_LAYOUT_FRACTURE ||
        sample < 0 || duration <= 0 || sample > duration) return ODM_STATUS_INVALID_DATA;
    memset(&p, 0, sizeof(p));
    odm_layered_reaction_effective(c, &reaction);
    p.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    p.width = c->canvas.width; p.height = c->canvas.height; p.fps = c->canvas.fps;
    p.center_x_q16 = ratio_px_q16(c->canvas.width, c->core.center_x_q31);
    p.center_y_q16 = ratio_px_q16(c->canvas.height, c->core.center_y_q31);
    base_w = ratio_px_q16(c->canvas.width, c->core.width_q31);
    base_h = ratio_px_q16(c->canvas.height, c->core.height_q31);
    st = odm_layered_reaction_eval(reaction.core_scale_route, comp, dir, &signal);
    if (st != ODM_STATUS_OK) return st;
    react = mul_q31u(c->core.scale_reactivity_q31, signal);
    if ((comp->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u) {
        /* In strict reaction mode the scale signal is already a bounded music
         * envelope. Preserve its full configured reactivity so a genuine
         * low-frequency punch is visible, then let the envelope decay itself. */
        add_w = odm_layered_mul_q16_q31(base_w, react);
        add_h = odm_layered_mul_q16_q31(base_h, react);
    } else {
        add_w = odm_layered_mul_q16_q31(base_w, react) / 2;
        add_h = odm_layered_mul_q16_q31(base_h, react) / 2;
    }
    if (base_w > INT32_MAX - add_w || base_h > INT32_MAX - add_h) return ODM_STATUS_OVERFLOW;
    p.core_half_w_q16 = (base_w + add_w) / 2;
    p.core_half_h_q16 = (base_h + add_h) / 2;
    min_half = p.core_half_w_q16 < p.core_half_h_q16 ? p.core_half_w_q16 : p.core_half_h_q16;
    p.core_radius_q16 = min_half;
    p.core_corner_q16 = odm_layered_mul_q16_q31(min_half, c->core.corner_radius_q31);
    if ((int64_t)p.core_radius_q16 + (int64_t)c->field.ring_gap_q16 > INT32_MAX)
        return ODM_STATUS_OVERFLOW;
    p.ring_inner_radius_q16 = p.core_radius_q16 + (int32_t)c->field.ring_gap_q16;

    spacing = (int32_t)c->background.grid_spacing_q16;
    st = odm_layered_reaction_eval(reaction.background_zoom_route, comp, dir, &signal);
    if (st != ODM_STATUS_OK) return st;
    react = mul_q31u(c->background.zoom_reactivity_q31, signal);
    spacing_add = odm_layered_mul_q16_q31(spacing, react) / 2;
    if (spacing > INT32_MAX - spacing_add) return ODM_STATUS_OVERFLOW;
    p.grid_spacing_q16 = spacing + spacing_add;
    p.grid_line_q16 = (int32_t)c->background.grid_line_q16;
    p.grid_feather_q16 = (int32_t)c->background.grid_feather_q16;
    if (p.grid_spacing_q16 > 0 &&
        (comp->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) == 0u) {
        uint64_t phase = ((uint64_t)comp->phase * (uint64_t)(uint32_t)p.grid_spacing_q16) >> 32;
        p.grid_offset_x_q16 = (int32_t)phase;
        phase = ((uint64_t)(comp->phase + UINT32_C(0x40000000)) *
                 (uint64_t)(uint32_t)p.grid_spacing_q16) >> 32;
        p.grid_offset_y_q16 = (int32_t)phase;
    }
    st = odm_layered_reaction_eval(reaction.background_warp_route, comp, dir, &signal);
    if (st != ODM_STATUS_OK) return st;
    react = mul_q31u(c->background.warp_reactivity_q31, signal);
    p.grid_warp_q16 = odm_layered_mul_q16_q31(p.grid_spacing_q16, react);
    st = odm_layered_reaction_eval(reaction.background_depth_route, comp, dir, &signal);
    if (st != ODM_STATUS_OK) return st;
    react = mul_q31u(c->background.depth_reactivity_q31, signal);
    p.grid_depth_q16 = odm_layered_mul_q16_q31(p.grid_spacing_q16, react);
    p.safe_left_q16 = ratio_px_q16(c->canvas.width, c->canvas.safe_left_q31);
    p.safe_top_q16 = ratio_px_q16(c->canvas.height, c->canvas.safe_top_q31);
    p.safe_right_q16 = ((int32_t)c->canvas.width << 16) - ratio_px_q16(c->canvas.width, c->canvas.safe_right_q31);
    p.safe_bottom_q16 = ((int32_t)c->canvas.height << 16) - ratio_px_q16(c->canvas.height, c->canvas.safe_bottom_q31);
    p.tick_index = comp->tick_index; p.sample = sample; p.duration_samples = duration;
    /* Propagate only semantic render-path flags that the Layered renderer
     * understands. This lets strict Music-Reaction rendering fail closed
     * instead of inferring causality from incidental values such as phase=0. */
    p.flags = comp->flags & (ODM_COMPOSITION_FLAG_RADIAL_HIRES |
                             ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                             ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE |
                             ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE);
    p.core_opacity_q31 = c->core.opacity_q31;
    st = odm_layered_reaction_eval(reaction.field_gain_route, comp, dir, &signal);
    if (st != ODM_STATUS_OK) return st;
    p.field_opacity_q31 = mul_q31u(c->field.field_opacity_q31, signal);
    prog = (uint64_t)progress_q31_exact(sample, duration);
    p.progress_q31 = (uint32_t)prog;
    st = odm_layered_reaction_eval(reaction.particle_density_route, comp, dir, &signal);
    if (st != ODM_STATUS_OK) return st;
    particle_gain = (uint32_t)INT32_MAX / 4u +
                    mul_q31u(signal,
                             (uint32_t)(((uint64_t)INT32_MAX * UINT64_C(3)) / UINT64_C(4)));
    p.particle_count = (uint32_t)(((uint64_t)c->field.particle_count * particle_gain + (uint64_t)INT32_MAX / 2u) / (uint64_t)INT32_MAX);
    if (p.particle_count > c->field.particle_count) p.particle_count = c->field.particle_count;
    if ((comp->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u)
        p.ring_phase = 0u;
    else if ((comp->flags & ODM_COMPOSITION_FLAG_RADIAL_HIRES) != 0u)
        p.ring_phase = comp->ring_phase;
    else
        p.ring_phase = comp->ring_phase + dir->orbit_phase;
    p.layout = dir->layout;

    /* Radial resolution + provenance bridge. When transient provenance is
     * available, reduction selects one causal source lane as a coherent tuple
     * (final, held body, attack); it never combines a body from one lane with
     * an attack from its neighbor. */
    if ((comp->flags & ODM_COMPOSITION_FLAG_RADIAL_HIRES) != 0u) {
        if (c->field.radial_segments == ODM_COMPOSITION_RADIAL_SEGMENTS_MAX) {
            for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
                p.radial_q31[i] = comp->radial_q31[i];
                if ((comp->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u) {
                    p.radial_body_q31[i] = comp->radial_body_q31[i];
                    p.radial_release_q31[i] = comp->radial_release_q31[i];
                    p.radial_attack_q31[i] = comp->radial_attack_q31[i];
                }
            }
        } else {
            for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i) {
                uint32_t ai = i * 2u;
                uint32_t bi = ai + 1u;
                uint32_t a = comp->radial_q31[ai];
                uint32_t b = comp->radial_q31[bi];
                if ((comp->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u) {
                    uint32_t winner = a >= b ? ai : bi;
                    p.radial_q31[i] = comp->radial_q31[winner];
                    if ((comp->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u) {
                        p.radial_body_q31[i] = comp->radial_body_q31[winner];
                        p.radial_release_q31[i] = comp->radial_release_q31[winner];
                        p.radial_attack_q31[i] = comp->radial_attack_q31[winner];
                    }
                } else {
                    uint64_t pair = (uint64_t)a + (uint64_t)b;
                    p.radial_q31[i] = (uint32_t)((pair + 1u) / 2u);
                }
            }
        }
    } else if (c->field.radial_segments == ODM_COMPOSITION_RADIAL_SEGMENTS) {
        for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i)
            p.radial_q31[i] = comp->radial_q31[i];
    } else {
        /* Explicit deterministic legacy upsample: duplicate each old radial
         * into the adjacent high-resolution pair. No interpolation invents a
         * new spectral cause. */
        for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i)
            p.radial_q31[i] = comp->radial_q31[i / 2u];
    }
    st = odm_layered_config_sha256(c, &p.config_sha256);
    if (st != ODM_STATUS_OK) return st;
    *out = p;
    return ODM_STATUS_OK;
}

odm_status odm_layered_trace_radial(const odm_layered_config *c,
                                          const odm_composition_frame_state *comp,
                                          const odm_layered_frame_plan *plan,
                                          uint32_t radial_index,
                                          odm_layered_radial_trace *out_trace) {
    const uint32_t required_flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES |
                                    ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                                    ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE |
                                    ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
    const uint32_t release_weight_q31 = UINT32_C(751619277); /* exact 0.35 */
    odm_layered_radial_trace t;
    odm_sha256_digest config_sha;
    uint32_t a, b, winner;
    uint32_t body, release, attack, release_mix, after_release, final;
    odm_status st;

    if (!c || !comp || !plan || !out_trace) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_layered_config_validate(c);
    if (st != ODM_STATUS_OK) return st;
    if (comp->schema_version != ODM_COMPOSITION_SCHEMA_VERSION ||
        plan->schema_version != ODM_LAYERED_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (comp->tick_index != plan->tick_index ||
        (comp->flags & required_flags) != required_flags ||
        (plan->flags & required_flags) != required_flags)
        return ODM_STATUS_INVALID_DATA;
    if (radial_index >= c->field.radial_segments) return ODM_STATUS_INVALID_ARGUMENT;

    st = odm_layered_config_sha256(c, &config_sha);
    if (st != ODM_STATUS_OK) return st;
    if (memcmp(config_sha.bytes, plan->config_sha256.bytes,
               sizeof(config_sha.bytes)) != 0)
        return ODM_STATUS_INVALID_DATA;

    memset(&t, 0, sizeof(t));
    t.schema_version = ODM_LAYERED_RADIAL_TRACE_SCHEMA_VERSION;
    t.radial_index = radial_index;
    t.output_radial_count = c->field.radial_segments;
    t.flags = ODM_LAYERED_RADIAL_TRACE_FLAG_STRICT_CAUSAL |
              ODM_LAYERED_RADIAL_TRACE_FLAG_TIMESCALE;

    if (c->field.radial_segments == ODM_COMPOSITION_RADIAL_SEGMENTS_MAX) {
        a = radial_index;
        b = radial_index;
        winner = radial_index;
        t.flags |= ODM_LAYERED_RADIAL_TRACE_FLAG_NATIVE_96;
    } else if (c->field.radial_segments == ODM_COMPOSITION_RADIAL_SEGMENTS) {
        a = radial_index * 2u;
        b = a + 1u;
        winner = comp->radial_q31[a] >= comp->radial_q31[b] ? a : b;
        t.flags |= ODM_LAYERED_RADIAL_TRACE_FLAG_REDUCED_48;
    } else {
        return ODM_STATUS_INVALID_DATA;
    }

    t.candidate_lane_a = a;
    t.candidate_lane_b = b;
    t.source_lane = winner;
    t.final_q31 = comp->radial_q31[winner];
    t.body_q31 = comp->radial_body_q31[winner];
    t.release_q31 = comp->radial_release_q31[winner];
    t.attack_q31 = comp->radial_attack_q31[winner];

    if (plan->radial_q31[radial_index] != t.final_q31 ||
        plan->radial_body_q31[radial_index] != t.body_q31 ||
        plan->radial_release_q31[radial_index] != t.release_q31 ||
        plan->radial_attack_q31[radial_index] != t.attack_q31)
        return ODM_STATUS_INVALID_DATA;

    body = t.body_q31;
    release = t.release_q31;
    attack = t.attack_q31;
    if (body > (uint32_t)INT32_MAX || release > (uint32_t)INT32_MAX ||
        attack > (uint32_t)INT32_MAX || t.final_q31 > (uint32_t)INT32_MAX)
        return ODM_STATUS_INVALID_DATA;
    release_mix = mul_q31u(release, release_weight_q31);
    after_release = body + mul_q31u((uint32_t)INT32_MAX - body, release_mix);
    final = after_release + mul_q31u((uint32_t)INT32_MAX - after_release, attack);
    if (final != t.final_q31) return ODM_STATUS_INVALID_DATA;

    *out_trace = t;
    return ODM_STATUS_OK;
}

odm_status odm_export_plan_build(const odm_layered_config *c,
                                 int64_t project_end_sample,
                                 uint64_t audio_source_frames,
                                 uint32_t pixel_format,
                                 odm_export_plan *out) {
    odm_export_plan p;
    odm_timeline t;
    odm_sha256_digest policy;
    uint64_t pixels, bpp, bytes;
    odm_status st = odm_layered_config_validate(c);
    if (st != ODM_STATUS_OK) return st;
    if (!out || project_end_sample < 0 || audio_source_frames > (uint64_t)INT64_MAX)
        return ODM_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)project_end_sample < audio_source_frames) return ODM_STATUS_INVALID_DATA;
    if (pixel_format == ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL) bpp = 8u;
    else if (pixel_format == ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE) bpp = 4u;
    else return ODM_STATUS_UNSUPPORTED;
    st = odm_time_resolve_timeline(project_end_sample, c->canvas.fps, &t);
    if (st != ODM_STATUS_OK) return st;
    pixels = (uint64_t)c->canvas.width * (uint64_t)c->canvas.height;
    if (pixels != 0u && bpp > UINT64_MAX / pixels) return ODM_STATUS_OVERFLOW;
    bytes = pixels * bpp;
    if ((uint64_t)t.frame_count != 0u && bytes > UINT64_MAX / (uint64_t)t.frame_count)
        return ODM_STATUS_OVERFLOW;
    st = odm_layered_policy_current_sha256(&policy); if (st != ODM_STATUS_OK) return st;
    memset(&p, 0, sizeof(p));
    p.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    p.output_pixel_format = pixel_format;
    p.width = c->canvas.width; p.height = c->canvas.height; p.fps = c->canvas.fps;
    p.audio_sample_rate = (uint32_t)ODM_TIMELINE_RATE; p.audio_channels = 2u;
    p.project_end_sample = project_end_sample; p.samples_per_frame = t.samples_per_frame;
    p.frame_count = (uint64_t)t.frame_count; p.output_end_sample = t.output_end_sample;
    p.audio_source_frames = audio_source_frames;
    p.audio_pad_frames = (uint64_t)t.output_end_sample - audio_source_frames;
    p.video_bytes_per_frame = bytes; p.video_total_bytes = bytes * (uint64_t)t.frame_count;
    p.layered_policy_sha256 = policy;
    st = odm_layered_config_sha256(c, &p.config_sha256);
    if (st != ODM_STATUS_OK) return st;
    *out = p;
    return ODM_STATUS_OK;
}


static odm_status write_rgba16(odm_wire_writer *w, const odm_rgba16 *c) {
    odm_status st;
    st = odm_wire_write_u16(w, c->r); if (st != ODM_STATUS_OK) return st;
    st = odm_wire_write_u16(w, c->g); if (st != ODM_STATUS_OK) return st;
    st = odm_wire_write_u16(w, c->b); if (st != ODM_STATUS_OK) return st;
    return odm_wire_write_u16(w, c->a);
}

static odm_status layer_config_encode(const odm_layered_config *c, uint8_t *buffer,
                                      uint64_t cap, uint64_t *req) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    if (!c || !req) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_layered_config_validate(c); if (st != ODM_STATUS_OK) return st;
    *req = ODM_LAYERED_CONFIG_BYTES;
    if (!buffer || cap < ODM_LAYERED_CONFIG_BYTES) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_LAYERED_CONFIG_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_LAYERED_CONFIG_BYTES); if (st != ODM_STATUS_OK) return st;
#define LC(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    LC(odm_wire_write_bytes(&w, "ODMCFG14", 8u));
    LC(odm_wire_write_u32(&w,c->schema_version)); LC(odm_wire_write_u32(&w,c->flags));
    LC(odm_wire_write_u32(&w,c->canvas.schema_version)); LC(odm_wire_write_u32(&w,c->canvas.aspect));
    LC(odm_wire_write_u32(&w,c->canvas.width)); LC(odm_wire_write_u32(&w,c->canvas.height));
    LC(odm_wire_write_i32(&w,c->canvas.fps)); LC(odm_wire_write_u32(&w,c->canvas.flags));
    LC(odm_wire_write_u32(&w,c->canvas.safe_left_q31)); LC(odm_wire_write_u32(&w,c->canvas.safe_top_q31));
    LC(odm_wire_write_u32(&w,c->canvas.safe_right_q31)); LC(odm_wire_write_u32(&w,c->canvas.safe_bottom_q31));
    LC(odm_wire_write_u32(&w,c->background.schema_version)); LC(odm_wire_write_u32(&w,c->background.style));
    LC(write_rgba16(&w,&c->background.solid_color)); LC(write_rgba16(&w,&c->background.grid_color));
    LC(odm_wire_write_u32(&w,c->background.grid_spacing_q16)); LC(odm_wire_write_u32(&w,c->background.grid_line_q16));
    LC(odm_wire_write_u32(&w,c->background.grid_feather_q16)); LC(odm_wire_write_u32(&w,c->background.zoom_reactivity_q31));
    LC(odm_wire_write_u32(&w,c->background.warp_reactivity_q31)); LC(odm_wire_write_u32(&w,c->background.depth_reactivity_q31));
    LC(odm_wire_write_u32(&w,c->background.opacity_q31));
    LC(odm_wire_write_u32(&w,c->core.schema_version)); LC(odm_wire_write_u32(&w,c->core.shape)); LC(odm_wire_write_u32(&w,c->core.fit));
    LC(odm_wire_write_u32(&w,c->core.center_x_q31)); LC(odm_wire_write_u32(&w,c->core.center_y_q31));
    LC(odm_wire_write_u32(&w,c->core.width_q31)); LC(odm_wire_write_u32(&w,c->core.height_q31)); LC(odm_wire_write_u32(&w,c->core.corner_radius_q31));
    LC(odm_wire_write_u32(&w,c->core.border_q16)); LC(odm_wire_write_u32(&w,c->core.feather_q16));
    LC(odm_wire_write_u32(&w,c->core.scale_reactivity_q31)); LC(odm_wire_write_u32(&w,c->core.opacity_q31)); LC(write_rgba16(&w,&c->core.border_color));
    LC(odm_wire_write_u32(&w,c->field.schema_version)); LC(odm_wire_write_u32(&w,c->field.flags)); LC(odm_wire_write_u32(&w,c->field.radial_segments));
    LC(odm_wire_write_u32(&w,c->field.particle_count)); LC(odm_wire_write_u32(&w,c->field.ring_gap_q16)); LC(odm_wire_write_u32(&w,c->field.bar_min_q16));
    LC(odm_wire_write_u32(&w,c->field.bar_max_q16)); LC(odm_wire_write_u32(&w,c->field.bar_width_q16)); LC(odm_wire_write_u32(&w,c->field.particle_radius_q16));
    LC(odm_wire_write_u32(&w,c->field.field_opacity_q31)); LC(write_rgba16(&w,&c->field.primary_color)); LC(write_rgba16(&w,&c->field.secondary_color)); LC(odm_wire_write_u64(&w,c->field.seed));
    LC(odm_wire_write_u32(&w,c->hud.schema_version)); LC(odm_wire_write_u32(&w,c->hud.flags)); LC(odm_wire_write_u32(&w,c->hud.margin_q16));
    LC(odm_wire_write_u32(&w,c->hud.progress_height_q16)); LC(odm_wire_write_u32(&w,c->hud.progress_width_q31)); LC(odm_wire_write_u32(&w,c->hud.text_scale_q16));
    LC(odm_wire_write_u32(&w,c->hud.line_gap_q16)); LC(odm_wire_write_u32(&w,c->hud.opacity_q31)); LC(write_rgba16(&w,&c->hud.foreground_color)); LC(write_rgba16(&w,&c->hud.background_color));
    LC(odm_wire_write_bytes(&w,c->hud.title,ODM_LAYERED_METADATA_BYTES)); LC(odm_wire_write_bytes(&w,c->hud.artist,ODM_LAYERED_METADATA_BYTES));
    LC(odm_wire_write_u32(&w,c->hud.metadata_anchor));
    LC(odm_wire_write_u32(&w,c->hud.progress_style));
    LC(odm_wire_write_u32(&w,c->hud.time_mode));
    LC(odm_wire_write_u32(&w,c->hud.reserved0));
    LC(odm_wire_write_u32(&w,c->reaction.schema_version));
    LC(odm_wire_write_u32(&w,c->reaction.flags));
    LC(odm_wire_write_u32(&w,c->reaction.background_zoom_route));
    LC(odm_wire_write_u32(&w,c->reaction.background_warp_route));
    LC(odm_wire_write_u32(&w,c->reaction.background_depth_route));
    LC(odm_wire_write_u32(&w,c->reaction.core_scale_route));
    LC(odm_wire_write_u32(&w,c->reaction.field_gain_route));
    LC(odm_wire_write_u32(&w,c->reaction.particle_density_route));
#undef LC
    return odm_wire_writer_finish(&w, req);
}

odm_status odm_layered_config_bytes(const odm_layered_config *config,
                                    uint8_t *buffer, uint64_t capacity,
                                    uint64_t *out_required) {
    uint64_t used = 0u;
    odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st = layer_config_encode(config, buffer, capacity, &used);
    *out_required = ODM_LAYERED_CONFIG_BYTES;
    return st;
}

odm_status odm_layered_config_sha256(const odm_layered_config *config,
                                    odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_LAYERED_CONFIG_BYTES];
    uint64_t req = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = layer_config_encode(config, bytes, sizeof(bytes), &req); if (st != ODM_STATUS_OK) return st;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}

static odm_status layer_policy_encode(uint8_t *buffer, uint64_t cap, uint64_t *req) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    if (!req) return ODM_STATUS_INVALID_ARGUMENT;
    *req = ODM_LAYERED_POLICY_BYTES;
    if (!buffer || cap < ODM_LAYERED_POLICY_BYTES) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_LAYERED_POLICY_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_LAYERED_POLICY_BYTES); if (st != ODM_STATUS_OK) return st;
#define LP(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    LP(odm_wire_write_bytes(&w, LAYER_POLICY_MAGIC, 8u));
    LP(odm_wire_write_u32(&w, ODM_LAYERED_POLICY_VERSION));
    LP(odm_wire_write_u32(&w, ODM_LAYERED_SCHEMA_VERSION));
    LP(odm_wire_write_u32(&w, ODM_COMPOSITION_SCHEMA_VERSION));
    LP(odm_wire_write_u32(&w, ODM_DIRECTOR_SCHEMA_VERSION));
    LP(odm_wire_write_u32(&w, 6u)); /* layer authorities */
    LP(odm_wire_write_u32(&w, 6u)); /* aspect modes incl custom */
    LP(odm_wire_write_u32(&w, 3u)); /* core shapes */
    LP(odm_wire_write_u32(&w, 3u)); /* core fit modes */
    LP(odm_wire_write_u32(&w, ODM_COMPOSITION_RADIAL_SEGMENTS)); /* legacy radial count */
    LP(odm_wire_write_u32(&w, ODM_COMPOSITION_RADIAL_SEGMENTS_MAX)); /* high-resolution radial count */
    LP(odm_wire_write_u32(&w, 1u)); /* hires radial phase excludes director orbit */
    LP(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_STRICT_CAUSAL)); /* explicit strict authority */
    LP(odm_wire_write_u32(&w, 1u)); /* strict grid has no autonomous phase offset */
    LP(odm_wire_write_u32(&w, 1u)); /* strict particles never advance from sample/tick clock */
    LP(odm_wire_write_u32(&w, 1u)); /* strict particle config seed is not a reaction authority */
    LP(odm_wire_write_u64(&w, UINT64_C(0x51a7c0de9e3779b9))); /* fixed strict topology seed */
    LP(odm_wire_write_u32(&w, 73u)); /* strict particle sector permutation multiplier */
    LP(odm_wire_write_u32(&w, UINT32_C(536870912))); /* strict radius base = 1/4 Q31 */
    LP(odm_wire_write_u32(&w, 4u)); /* strict static scatter divisor */
    LP(odm_wire_write_u32(&w, 2u)); /* strict music displacement divisor */
    LP(odm_wire_write_u32(&w, 1u)); /* strict ring phase is zero */
    LP(odm_wire_write_u32(&w, ODM_LAYERED_MAX_PARTICLES));
    LP(odm_wire_write_u32(&w, ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL));
    LP(odm_wire_write_u32(&w, ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE));
    LP(odm_wire_write_u32(&w, UINT32_C(65536))); /* Q16.16 pixel unit */
    LP(odm_wire_write_u32(&w, 1u)); /* exact integer sqrt circle/roundrect mask */
    LP(odm_wire_write_u32(&w, 1u)); /* one-pixel linear AA model */
    LP(odm_wire_write_u32(&w, 1u)); /* ring derives from core edge */
    LP(odm_wire_write_u32(&w, 1u)); /* grid independent layer */
    LP(odm_wire_write_u32(&w, 1u)); /* HUD independent layer */
    LP(odm_wire_write_u32(&w, 1u)); /* export plan uses exact timeline */
    LP(odm_wire_write_u32(&w, 1u)); /* transactional staging */
    LP(odm_wire_write_u32(&w, 1u)); /* bilinear core sampling */
    LP(odm_wire_write_u32(&w, 1u)); /* deterministic indexed particles */
    LP(odm_wire_write_u32(&w, 1u)); /* radial DDA splat model */
    LP(odm_wire_write_u32(&w, 1u)); /* progress sample/duration exact */
    LP(odm_wire_write_u32(&w, 1u)); /* safe-area geometry is canvas-authoritative */
    LP(odm_wire_write_u32(&w, 1u)); /* field contour follows exact core SDF */
    LP(odm_wire_write_u32(&w, 4u)); /* background styles incl perspective grid */
    LP(odm_wire_write_u32(&w, 1u)); /* perspective grid uses bounded row-projective integer mapping */
    LP(odm_wire_write_u32(&w, 1u)); /* perspective horizon suppresses sub-two-pixel projected cells */
    LP(odm_wire_write_u32(&w, 1u)); /* perspective world uses Q17 half-pixel-exact mirror geometry */
    LP(odm_wire_write_u32(&w, 6u)); /* HUD metadata anchors */
    LP(odm_wire_write_u32(&w, 3u)); /* HUD progress geometries */
    LP(odm_wire_write_u32(&w, 3u)); /* HUD time modes */
    LP(odm_wire_write_u32(&w, 1u)); /* capsule progress uses canonical integer SDF + 1px AA */
    LP(odm_wire_write_u32(&w, 1u)); /* built-in 5x7 is fallback typography, not layout authority */
    LP(odm_wire_write_u32(&w, ODM_REACTION_SCHEMA_VERSION));
    LP(odm_wire_write_u32(&w, ODM_REACTION_SOURCE_MAX + 1u)); /* selectable sources incl NONE */
    LP(odm_wire_write_u32(&w, 6u)); /* route targets */
    LP(odm_wire_write_u32(&w, 6u)); /* combine operators */
    LP(odm_wire_write_u32(&w, 1u)); /* all-zero reaction record = exact legacy routing */
    LP(odm_wire_write_u32(&w, 1u)); /* explicit routing is inside config SHA authority */
    LP(odm_wire_write_u32(&w, 1u)); /* strict 96->48 radial reduction preserves pair max */
    LP(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE));
    LP(odm_wire_write_u32(&w, 1u)); /* pair reduction selects one coherent provenance tuple */
    LP(odm_wire_write_u32(&w, 1u)); /* transient tip begins at held-body endpoint */
    LP(odm_wire_write_u32(&w, 1u)); /* transient tip ends at exact final radial endpoint */
    LP(odm_wire_write_u32(&w, 1u)); /* transient tip opacity is exact same-lane attack */
    LP(odm_wire_write_u32(&w, 1u)); /* no provenance flag => historical bar raster */
    LP(odm_wire_write_u32(&w, 1u)); /* provenance bars have zero autonomous minimum length */
    LP(odm_wire_write_u32(&w, 1u)); /* full-scale radial maps exactly to bar_max_q16 */
    LP(odm_wire_write_u32(&w, 1u)); /* strict render boundary rejects nonzero ring phase */
    LP(odm_wire_write_u32(&w, 1u)); /* strict render boundary rejects nonzero grid offsets */
    LP(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE));
    LP(odm_wire_write_u32(&w, 1u)); /* release provenance = exact slow-fast same-lane excess */
    LP(odm_wire_write_u32(&w, 1u)); /* 96->48 preserves final/body/release/attack tuple */
    LP(odm_wire_write_u32(&w, 1u)); /* release raster lies between body and attack tip */
#undef LP
    return odm_wire_writer_finish(&w, req);
}

odm_status odm_layered_policy_bytes(uint8_t *buffer, uint64_t capacity, uint64_t *out_required) {
    uint64_t used = 0u;
    odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st = layer_policy_encode(buffer, capacity, &used);
    *out_required = ODM_LAYERED_POLICY_BYTES;
    return st;
}

odm_status odm_layered_policy_current_sha256(odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_LAYERED_POLICY_BYTES];
    uint64_t req = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = layer_policy_encode(bytes, sizeof(bytes), &req); if (st != ODM_STATUS_OK) return st;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}

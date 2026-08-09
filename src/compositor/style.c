#include "odm_compositor.h"

#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define STYLE_Q16_ONE UINT32_C(65536)
#define STYLE_SCALE_MIN_Q16 UINT32_C(8192)    /* 1/8 */
#define STYLE_SCALE_MAX_Q16 UINT32_C(262144)  /* 4x */
#define STYLE_MAGIC "ODMSTY14"

static int zero_u32(const uint32_t *p, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) if (p[i] != 0u) return 0;
    return 1;
}

static int q31_ok(uint32_t v) { return v <= (uint32_t)INT32_MAX; }
static int scale_ok(uint32_t v) {
    return v >= STYLE_SCALE_MIN_Q16 && v <= STYLE_SCALE_MAX_Q16;
}

static odm_rgba16 rgba(uint16_t r, uint16_t g, uint16_t b, uint16_t a) {
    odm_rgba16 c;
    c.r = r; c.g = g; c.b = b; c.a = a;
    return c;
}

static void style_common(odm_layered_style_profile *s) {
    s->schema_version = ODM_LAYERED_STYLE_SCHEMA_VERSION;
    s->layer_mask = ODM_LAYERED_STYLE_LAYER_ALL;
    s->background_opacity_q31 = (uint32_t)INT32_MAX;
    s->field_opacity_q31 = (uint32_t)INT32_MAX;
    s->hud_opacity_q31 = (uint32_t)INT32_MAX;
    s->grid_spacing_scale_q16 = STYLE_Q16_ONE;
    s->grid_line_scale_q16 = STYLE_Q16_ONE;
    s->grid_feather_scale_q16 = STYLE_Q16_ONE;
    s->background_zoom_scale_q16 = STYLE_Q16_ONE;
    s->background_warp_scale_q16 = STYLE_Q16_ONE;
    s->background_depth_scale_q16 = STYLE_Q16_ONE;
    s->core_size_scale_q16 = STYLE_Q16_ONE;
    s->core_border_scale_q16 = STYLE_Q16_ONE;
    s->core_feather_scale_q16 = STYLE_Q16_ONE;
    s->core_reactivity_scale_q16 = STYLE_Q16_ONE;
    s->ring_gap_scale_q16 = STYLE_Q16_ONE;
    s->bar_min_scale_q16 = STYLE_Q16_ONE;
    s->bar_max_scale_q16 = STYLE_Q16_ONE;
    s->bar_width_scale_q16 = STYLE_Q16_ONE;
    s->particle_count_scale_q16 = STYLE_Q16_ONE;
    s->particle_radius_scale_q16 = STYLE_Q16_ONE;
    s->hud_margin_scale_q16 = STYLE_Q16_ONE;
    s->hud_progress_scale_q16 = STYLE_Q16_ONE;
    s->hud_text_scale_q16 = STYLE_Q16_ONE;
    s->hud_line_gap_scale_q16 = STYLE_Q16_ONE;
    s->progress_style = ODM_HUD_PROGRESS_HAIRLINE;
    s->metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_CENTER;
    s->time_mode = ODM_HUD_TIME_ELAPSED_TOTAL;
}

odm_status odm_layered_style_init(odm_layered_style_profile *out_style,
                                  uint32_t preset_id) {
    odm_layered_style_profile s;
    if (!out_style) return ODM_STATUS_INVALID_ARGUMENT;
    if (preset_id < ODM_LAYERED_STYLE_PRESET_MONO_PRECISE ||
        preset_id > ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL)
        return ODM_STATUS_INVALID_ARGUMENT;
    memset(&s, 0, sizeof(s));
    style_common(&s);
    s.preset_id = preset_id;
    s.background_solid = rgba(0u, 0u, 0u, UINT16_MAX);
    s.background_grid = rgba(UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_C(16384));
    s.core_border = rgba(UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX);
    s.field_primary = rgba(UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX);
    s.field_secondary = rgba(UINT16_C(32768), UINT16_C(32768), UINT16_C(32768), UINT16_MAX);
    s.hud_foreground = rgba(UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX);
    s.hud_background = rgba(0u, 0u, 0u, UINT16_C(24576));

    switch (preset_id) {
        case ODM_LAYERED_STYLE_PRESET_MONO_PRECISE:
            s.background_grid.a = UINT16_C(24576);
            s.field_opacity_q31 = UINT32_C(1610612735); /* 3/4 */
            break;
        case ODM_LAYERED_STYLE_PRESET_VOID_CINEMATIC:
            s.background_grid.a = UINT16_C(9216);
            s.field_secondary = rgba(UINT16_C(16384), UINT16_C(16384), UINT16_C(16384), UINT16_MAX);
            s.field_opacity_q31 = UINT32_C(1342177279); /* 5/8 */
            s.hud_opacity_q31 = UINT32_C(1879048191);   /* 7/8 */
            s.grid_spacing_scale_q16 = UINT32_C(81920); /* 1.25 */
            s.grid_line_scale_q16 = UINT32_C(49152);    /* .75 */
            s.background_warp_scale_q16 = UINT32_C(49152);
            s.background_depth_scale_q16 = UINT32_C(81920);
            s.core_size_scale_q16 = UINT32_C(58982);    /* ~.90 */
            s.core_border_scale_q16 = UINT32_C(49152);
            s.ring_gap_scale_q16 = UINT32_C(81920);
            s.bar_min_scale_q16 = UINT32_C(49152);
            s.bar_max_scale_q16 = UINT32_C(81920);
            s.bar_width_scale_q16 = UINT32_C(49152);
            s.particle_count_scale_q16 = UINT32_C(32768);
            s.particle_radius_scale_q16 = UINT32_C(49152);
            s.hud_progress_scale_q16 = UINT32_C(49152);
            s.progress_style = ODM_HUD_PROGRESS_HAIRLINE;
            break;
        case ODM_LAYERED_STYLE_PRESET_DEEP_GRID:
            s.background_grid.a = UINT16_C(20480);
            s.grid_spacing_scale_q16 = UINT32_C(73728);
            s.background_zoom_scale_q16 = UINT32_C(81920);
            s.background_warp_scale_q16 = UINT32_C(98304);
            s.background_depth_scale_q16 = UINT32_C(98304);
            s.field_opacity_q31 = UINT32_C(1503238553); /* ~.70 */
            s.ring_gap_scale_q16 = UINT32_C(73728);
            s.particle_count_scale_q16 = UINT32_C(49152);
            break;
        case ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL:
            s.background_grid.a = UINT16_C(6144);
            s.field_opacity_q31 = UINT32_C(1073741823); /* 1/2 */
            s.hud_opacity_q31 = UINT32_C(1610612735);   /* 3/4 */
            s.grid_spacing_scale_q16 = UINT32_C(98304);
            s.grid_line_scale_q16 = UINT32_C(32768);
            s.grid_feather_scale_q16 = UINT32_C(49152);
            s.background_warp_scale_q16 = UINT32_C(32768);
            s.background_depth_scale_q16 = UINT32_C(65536);
            s.core_border_scale_q16 = UINT32_C(32768);
            s.ring_gap_scale_q16 = UINT32_C(98304);
            s.bar_min_scale_q16 = UINT32_C(32768);
            s.bar_max_scale_q16 = UINT32_C(65536);
            s.bar_width_scale_q16 = UINT32_C(32768);
            s.particle_count_scale_q16 = UINT32_C(24576);
            s.particle_radius_scale_q16 = UINT32_C(32768);
            s.hud_progress_scale_q16 = UINT32_C(32768);
            break;
        default:
            return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (odm_layered_style_validate(&s) != ODM_STATUS_OK) return ODM_STATUS_INVALID_DATA;
    *out_style = s;
    return ODM_STATUS_OK;
}

odm_status odm_layered_style_validate(const odm_layered_style_profile *s) {
    if (!s) return ODM_STATUS_INVALID_ARGUMENT;
    if (s->schema_version != ODM_LAYERED_STYLE_SCHEMA_VERSION ||
        s->preset_id < ODM_LAYERED_STYLE_PRESET_MONO_PRECISE ||
        s->preset_id > ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL ||
        s->layer_mask == 0u ||
        (s->layer_mask & ~ODM_LAYERED_STYLE_LAYER_ALL) != 0u || s->flags != 0u ||
        !q31_ok(s->background_opacity_q31) || !q31_ok(s->field_opacity_q31) ||
        !q31_ok(s->hud_opacity_q31) ||
        !scale_ok(s->grid_spacing_scale_q16) || !scale_ok(s->grid_line_scale_q16) ||
        !scale_ok(s->grid_feather_scale_q16) || !scale_ok(s->background_zoom_scale_q16) ||
        !scale_ok(s->background_warp_scale_q16) || !scale_ok(s->background_depth_scale_q16) ||
        !scale_ok(s->core_size_scale_q16) || !scale_ok(s->core_border_scale_q16) ||
        !scale_ok(s->core_feather_scale_q16) || !scale_ok(s->core_reactivity_scale_q16) ||
        !scale_ok(s->ring_gap_scale_q16) || !scale_ok(s->bar_min_scale_q16) ||
        !scale_ok(s->bar_max_scale_q16) || !scale_ok(s->bar_width_scale_q16) ||
        !scale_ok(s->particle_count_scale_q16) || !scale_ok(s->particle_radius_scale_q16) ||
        !scale_ok(s->hud_margin_scale_q16) || !scale_ok(s->hud_progress_scale_q16) ||
        !scale_ok(s->hud_text_scale_q16) || !scale_ok(s->hud_line_gap_scale_q16) ||
        s->progress_style > ODM_HUD_PROGRESS_HAIRLINE ||
        s->metadata_anchor > ODM_HUD_ANCHOR_BOTTOM_RIGHT ||
        s->time_mode > ODM_HUD_TIME_REMAINING ||
        !zero_u32(s->reserved, 20u)) return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static uint32_t scale_u32_q16(uint32_t v, uint32_t scale, uint32_t maxv) {
    uint64_t p = (uint64_t)v * (uint64_t)scale + UINT64_C(32768);
    p >>= 16;
    if (p > maxv) p = maxv;
    return (uint32_t)p;
}

static uint32_t scale_q31_q16(uint32_t v, uint32_t scale) {
    return scale_u32_q16(v, scale, (uint32_t)INT32_MAX);
}

odm_status odm_layered_style_apply(const odm_layered_config *base,
                                   const odm_layered_style_profile *s,
                                   odm_layered_config *out) {
    odm_layered_config c;
    odm_status st;
    if (!base || !s || !out) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_layered_config_validate(base); if (st != ODM_STATUS_OK) return st;
    st = odm_layered_style_validate(s); if (st != ODM_STATUS_OK) return st;
    c = *base;
    if ((s->layer_mask & ODM_LAYERED_STYLE_LAYER_BACKGROUND) != 0u) {
        c.background.solid_color = s->background_solid;
        c.background.grid_color = s->background_grid;
        c.background.opacity_q31 = s->background_opacity_q31;
        c.background.grid_spacing_q16 = scale_u32_q16(c.background.grid_spacing_q16, s->grid_spacing_scale_q16, UINT32_MAX);
        c.background.grid_line_q16 = scale_u32_q16(c.background.grid_line_q16, s->grid_line_scale_q16, UINT32_MAX);
        c.background.grid_feather_q16 = scale_u32_q16(c.background.grid_feather_q16, s->grid_feather_scale_q16, UINT32_MAX);
        c.background.zoom_reactivity_q31 = scale_q31_q16(c.background.zoom_reactivity_q31, s->background_zoom_scale_q16);
        c.background.warp_reactivity_q31 = scale_q31_q16(c.background.warp_reactivity_q31, s->background_warp_scale_q16);
        c.background.depth_reactivity_q31 = scale_q31_q16(c.background.depth_reactivity_q31, s->background_depth_scale_q16);
    }
    if ((s->layer_mask & ODM_LAYERED_STYLE_LAYER_CORE) != 0u) {
        c.core.border_color = s->core_border;
        c.core.width_q31 = scale_q31_q16(c.core.width_q31, s->core_size_scale_q16);
        c.core.height_q31 = scale_q31_q16(c.core.height_q31, s->core_size_scale_q16);
        c.core.border_q16 = scale_u32_q16(c.core.border_q16, s->core_border_scale_q16, UINT32_MAX);
        c.core.feather_q16 = scale_u32_q16(c.core.feather_q16, s->core_feather_scale_q16, UINT32_MAX);
        c.core.scale_reactivity_q31 = scale_q31_q16(c.core.scale_reactivity_q31, s->core_reactivity_scale_q16);
    }
    if ((s->layer_mask & ODM_LAYERED_STYLE_LAYER_FIELD) != 0u) {
        c.field.primary_color = s->field_primary;
        c.field.secondary_color = s->field_secondary;
        c.field.field_opacity_q31 = s->field_opacity_q31;
        c.field.ring_gap_q16 = scale_u32_q16(c.field.ring_gap_q16, s->ring_gap_scale_q16, UINT32_MAX);
        c.field.bar_min_q16 = scale_u32_q16(c.field.bar_min_q16, s->bar_min_scale_q16, UINT32_MAX);
        c.field.bar_max_q16 = scale_u32_q16(c.field.bar_max_q16, s->bar_max_scale_q16, UINT32_MAX);
        c.field.bar_width_q16 = scale_u32_q16(c.field.bar_width_q16, s->bar_width_scale_q16, UINT32_MAX);
        c.field.particle_count = scale_u32_q16(c.field.particle_count, s->particle_count_scale_q16, ODM_LAYERED_MAX_PARTICLES);
        c.field.particle_radius_q16 = scale_u32_q16(c.field.particle_radius_q16, s->particle_radius_scale_q16, UINT32_MAX);
    }
    if ((s->layer_mask & ODM_LAYERED_STYLE_LAYER_HUD) != 0u) {
        c.hud.foreground_color = s->hud_foreground;
        c.hud.background_color = s->hud_background;
        c.hud.opacity_q31 = s->hud_opacity_q31;
        c.hud.margin_q16 = scale_u32_q16(c.hud.margin_q16, s->hud_margin_scale_q16, UINT32_MAX);
        c.hud.progress_height_q16 = scale_u32_q16(c.hud.progress_height_q16, s->hud_progress_scale_q16, UINT32_MAX);
        c.hud.text_scale_q16 = scale_u32_q16(c.hud.text_scale_q16, s->hud_text_scale_q16, UINT32_MAX);
        c.hud.line_gap_q16 = scale_u32_q16(c.hud.line_gap_q16, s->hud_line_gap_scale_q16, UINT32_MAX);
        c.hud.progress_style = s->progress_style;
        c.hud.metadata_anchor = s->metadata_anchor;
        c.hud.time_mode = s->time_mode;
    }
    st = odm_layered_config_validate(&c);
    if (st != ODM_STATUS_OK) return st;
    *out = c;
    return ODM_STATUS_OK;
}

static odm_status write_rgba(odm_wire_writer *w, const odm_rgba16 *c) {
    odm_status st;
    st = odm_wire_write_u16(w, c->r); if (st != ODM_STATUS_OK) return st;
    st = odm_wire_write_u16(w, c->g); if (st != ODM_STATUS_OK) return st;
    st = odm_wire_write_u16(w, c->b); if (st != ODM_STATUS_OK) return st;
    return odm_wire_write_u16(w, c->a);
}

static odm_status style_encode(const odm_layered_style_profile *s,
                               uint8_t *buffer, uint64_t capacity,
                               uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    if (!s || !out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_layered_style_validate(s); if (st != ODM_STATUS_OK) return st;
    *out_required = ODM_LAYERED_STYLE_BYTES;
    if (!buffer || capacity < ODM_LAYERED_STYLE_BYTES) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_LAYERED_STYLE_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_LAYERED_STYLE_BYTES); if (st != ODM_STATUS_OK) return st;
#define SW(x) do { st=(x); if(st!=ODM_STATUS_OK) return st; } while(0)
    SW(odm_wire_write_bytes(&w, STYLE_MAGIC, 8u));
    SW(odm_wire_write_u32(&w, s->schema_version));
    SW(odm_wire_write_u32(&w, s->preset_id));
    SW(odm_wire_write_u32(&w, s->layer_mask));
    SW(odm_wire_write_u32(&w, s->flags));
    SW(write_rgba(&w, &s->background_solid)); SW(write_rgba(&w, &s->background_grid));
    SW(write_rgba(&w, &s->core_border)); SW(write_rgba(&w, &s->field_primary));
    SW(write_rgba(&w, &s->field_secondary)); SW(write_rgba(&w, &s->hud_foreground));
    SW(write_rgba(&w, &s->hud_background));
    SW(odm_wire_write_u32(&w, s->background_opacity_q31));
    SW(odm_wire_write_u32(&w, s->field_opacity_q31));
    SW(odm_wire_write_u32(&w, s->hud_opacity_q31));
    SW(odm_wire_write_u32(&w, s->grid_spacing_scale_q16));
    SW(odm_wire_write_u32(&w, s->grid_line_scale_q16));
    SW(odm_wire_write_u32(&w, s->grid_feather_scale_q16));
    SW(odm_wire_write_u32(&w, s->background_zoom_scale_q16));
    SW(odm_wire_write_u32(&w, s->background_warp_scale_q16));
    SW(odm_wire_write_u32(&w, s->background_depth_scale_q16));
    SW(odm_wire_write_u32(&w, s->core_size_scale_q16));
    SW(odm_wire_write_u32(&w, s->core_border_scale_q16));
    SW(odm_wire_write_u32(&w, s->core_feather_scale_q16));
    SW(odm_wire_write_u32(&w, s->core_reactivity_scale_q16));
    SW(odm_wire_write_u32(&w, s->ring_gap_scale_q16));
    SW(odm_wire_write_u32(&w, s->bar_min_scale_q16));
    SW(odm_wire_write_u32(&w, s->bar_max_scale_q16));
    SW(odm_wire_write_u32(&w, s->bar_width_scale_q16));
    SW(odm_wire_write_u32(&w, s->particle_count_scale_q16));
    SW(odm_wire_write_u32(&w, s->particle_radius_scale_q16));
    SW(odm_wire_write_u32(&w, s->hud_margin_scale_q16));
    SW(odm_wire_write_u32(&w, s->hud_progress_scale_q16));
    SW(odm_wire_write_u32(&w, s->hud_text_scale_q16));
    SW(odm_wire_write_u32(&w, s->hud_line_gap_scale_q16));
    SW(odm_wire_write_u32(&w, s->progress_style));
    SW(odm_wire_write_u32(&w, s->metadata_anchor));
    SW(odm_wire_write_u32(&w, s->time_mode));
#undef SW
    return odm_wire_writer_finish(&w, out_required);
}

odm_status odm_layered_style_bytes(const odm_layered_style_profile *style,
                                   uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required) {
    uint64_t used = 0u;
    odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    st = style_encode(style, buffer, capacity, &used);
    *out_required = ODM_LAYERED_STYLE_BYTES;
    return st;
}

odm_status odm_layered_style_sha256(const odm_layered_style_profile *style,
                                    odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_LAYERED_STYLE_BYTES];
    uint64_t req = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = style_encode(style, bytes, sizeof(bytes), &req); if (st != ODM_STATUS_OK) return st;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}

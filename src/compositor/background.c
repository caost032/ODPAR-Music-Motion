#include "compositor_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int64_t abs_i64(int64_t v) {
    return v < 0 ? -v : v;
}

static int64_t div_round_nearest_i64(int64_t num, int64_t den) {
    uint64_t mag;
    if (den <= 0) return 0;
    if (num >= 0) return (num + den / 2) / den;
    mag = (uint64_t)(-(num + 1)) + UINT64_C(1);
    return -(int64_t)((mag + (uint64_t)den / 2u) / (uint64_t)den);
}

static int64_t floor_mod_i64(int64_t value, int64_t mod) {
    int64_t r;
    if (mod <= 0) return 0;
    r = value % mod;
    if (r < 0) r += mod;
    return r;
}

/* Canonical triangular wave in Q16.16, range [-65536,+65536]. */
static int32_t tri_wave_q16(int64_t coord_q16, int64_t period_q16) {
    int64_t r, half, q;
    if (period_q16 <= 0) return 0;
    r = floor_mod_i64(coord_q16, period_q16);
    half = period_q16 / 2;
    if (half <= 0) return 0;
    if (r <= half) {
        q = (r * INT64_C(131072) + half / 2) / half - INT64_C(65536);
    } else {
        q = ((period_q16 - r) * INT64_C(131072) + half / 2) / half - INT64_C(65536);
    }
    if (q > INT32_MAX) q = INT32_MAX;
    if (q < INT32_MIN) q = INT32_MIN;
    return (int32_t)q;
}

static int64_t nearest_grid_distance(int64_t coord_q16, int64_t spacing_q16) {
    int64_t r;
    if (spacing_q16 <= 0) return INT64_MAX;
    r = floor_mod_i64(coord_q16, spacing_q16);
    if (r > spacing_q16 - r) r = spacing_q16 - r;
    return r;
}

typedef struct {
    int active;
    /* Perspective world-space is Q17.17, not Q16.16.  The extra fractional
     * bit is constitutional: pixel centers are half-integers around the
     * canvas center, and Q17 preserves exact opposite coordinates even when
     * the row projection scale is odd. */
    int64_t spacing_q17;
    int64_t x_world0_q17;
    int64_t x_step_q17;
    int64_t z_world_q17;
    int64_t line_half_world_q17;
    int64_t feather_world_q17;
    /* Hot-loop cache.  These are mathematically derived from the canonical
     * world coordinates above, not a second geometry.  Keeping phase in
     * [0,spacing) removes a signed modulo and the row-constant Z modulo from
     * every pixel while preserving exact reference bytes. */
    int64_t x_phase0_q17;
    int64_t x_phase_step_q17;
    int64_t z_distance_q17;
} odm_perspective_row;

/* Perspective-grid inverse projection.  One positive Q16.16 scale is resolved
 * per row; the pixel loop then advances world-x by a constant exact step.
 * The model is symmetric around the canvas horizon and independent of Core. */
/* Matriz de Bayer 8x8 normalizada. El difuminado ordenado es determinista y sin
 * estado -- el mismo pixel siempre recibe el mismo desplazamiento -- asi que no
 * introduce ruido temporal ni rompe la reproducibilidad byte a byte. Es lo que
 * permite un degradado oscuro sin bandas sin recurrir a grano visible. */
static const uint8_t bg_bayer8[64] = {
     0,32, 8,40, 2,34,10,42,
    48,16,56,24,50,18,58,26,
    12,44, 4,36,14,46, 6,38,
    60,28,52,20,62,30,54,22,
     3,35,11,43, 1,33, 9,41,
    51,19,59,27,49,17,57,25,
    15,47, 7,39,13,45, 5,37,
    63,31,55,23,61,29,53,21
};

/* Perfil de profundidad: 1.0 en el centro, cayendo suavemente hacia los bordes.
 * Se evalua sobre la distancia al centro normalizada por la media diagonal, de
 * modo que un lienzo 9:16 y uno 16:9 producen la misma sensacion espacial en
 * lugar de una elipse deformada. */
static uint32_t bg_depth_weight(int64_t dx_q16, int64_t dy_q16, int64_t radius_q16) {
    int64_t d2, r2, t;
    if (radius_q16 <= 0) return 0u;
    d2 = ((dx_q16 >> 8) * (dx_q16 >> 8)) + ((dy_q16 >> 8) * (dy_q16 >> 8));
    r2 = (radius_q16 >> 8) * (radius_q16 >> 8);
    if (d2 >= r2) return 0u;
    /* (1 - (d/r)^2)^2: caida suave, derivada nula en el centro y en el borde,
     * asi que no hay anillo perceptible donde el campo termina. */
    t = (int64_t)INT32_MAX - (d2 * (int64_t)INT32_MAX) / r2;
    t = (t * t) / (int64_t)INT32_MAX;
    if (t < 0) t = 0;
    if (t > (int64_t)INT32_MAX) t = (int64_t)INT32_MAX;
    return (uint32_t)t;
}

static void perspective_row_prepare(const odm_layered_frame_plan *plan,
                                    uint32_t y, odm_perspective_row *row) {
    int64_t spacing, yq, cy, dy, ady, half_h, focal, scale_q16;
    int64_t projected_spacing_q16, period4, row_wave, row_shift;
    int64_t dx0, zbase;
    memset(row, 0, sizeof(*row));
    if (!plan || plan->grid_spacing_q16 <= 0 || plan->height == 0u) return;
    spacing = plan->grid_spacing_q16;
    yq = ((int64_t)y << 16) + INT64_C(32768);
    half_h = ((int64_t)plan->height << 15);
    cy = half_h;
    dy = yq - cy;
    ady = abs_i64(dy);
    /* The exact center singularity is intentionally empty.  A one-pixel guard
     * is enough because a second anti-alias guard below suppresses cells that
     * project below two screen pixels. */
    if (ady < INT64_C(65536) || half_h <= 0) return;
    focal = half_h + (int64_t)plan->grid_depth_q16 * INT64_C(8);
    if (focal <= 0 || focal > (INT64_MAX / INT64_C(65536))) return;
    scale_q16 = (focal * INT64_C(65536) + ady / 2) / ady;
    if (scale_q16 <= 0) return;
    projected_spacing_q16 = (spacing * INT64_C(65536) + scale_q16 / 2) / scale_q16;
    if (projected_spacing_q16 < INT64_C(131072)) return; /* < 2 px: horizon fade */

    period4 = spacing * INT64_C(4);
    row_wave = (int64_t)tri_wave_q16(yq + plan->grid_offset_y_q16, period4);
    row_shift = (row_wave * (int64_t)plan->grid_warp_q16) / INT64_C(65536);
    /* Exact half-pixel inverse projection.  In Q17 the center-relative first
     * pixel is (1-width)*scale and every next pixel advances by 2*scale.
     * Therefore x and width-1-x are exact additive inverses when phase/warp=0;
     * no tie rounding is needed in the hot x path. */
    dx0 = (INT64_C(1) - (int64_t)plan->width) * scale_q16;
    row->x_world0_q17 = dx0 + row_shift * INT64_C(2) +
                        (int64_t)plan->grid_offset_x_q16 * INT64_C(2);
    row->x_step_q17 = scale_q16 * INT64_C(2);
    zbase = half_h - ady;
    row->z_world_q17 = div_round_nearest_i64(zbase * scale_q16, INT64_C(32768)) +
                       (int64_t)plan->grid_offset_y_q16 * INT64_C(2) +
                       div_round_nearest_i64(row_wave * (int64_t)plan->grid_warp_q16,
                                             INT64_C(32768));
    row->line_half_world_q17 = div_round_nearest_i64(
        (int64_t)(plan->grid_line_q16 / 2) * scale_q16, INT64_C(32768));
    row->feather_world_q17 = div_round_nearest_i64(
        (int64_t)plan->grid_feather_q16 * scale_q16, INT64_C(32768));
    row->spacing_q17 = spacing * INT64_C(2);
    row->x_phase0_q17 = floor_mod_i64(row->x_world0_q17, row->spacing_q17);
    row->x_phase_step_q17 = floor_mod_i64(row->x_step_q17, row->spacing_q17);
    row->z_distance_q17 = nearest_grid_distance(row->z_world_q17, row->spacing_q17);
    row->active = 1;
}

static uint32_t perspective_coverage_q31(const odm_perspective_row *row,
                                         uint32_t x) {
    int64_t wx, dx, dz, dist;
    if (!row || !row->active) return 0u;
    wx = row->x_world0_q17 + (int64_t)x * row->x_step_q17;
    dx = nearest_grid_distance(wx, row->spacing_q17);
    dz = nearest_grid_distance(row->z_world_q17, row->spacing_q17);
    dist = dx < dz ? dx : dz;
    return odm_layered_coverage_q31(dist, row->line_half_world_q17,
                                    row->feather_world_q17);
}

odm_status odm_layered_background_pixel(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        uint32_t x, uint32_t y,
                                        odm_layered_pixel16 *out) {
    int64_t xq, yq, cy, dy, xw, yw, half_h;
    int64_t depth_curve, depth_disp, wave_x, wave_y, dist_x, dist_y, dist;
    uint32_t coverage;
    if (!config || !plan || !out) return ODM_STATUS_INVALID_ARGUMENT;
    out->r = 0u; out->g = 0u; out->b = 0u; out->a = 0u;
    if (config->background.style == ODM_BACKGROUND_NONE) return ODM_STATUS_OK;
    odm_layered_blend16(out, &config->background.solid_color, (uint32_t)INT32_MAX,
                        config->background.opacity_q31);
    if (config->background.style == ODM_BACKGROUND_PERSPECTIVE_GRID) {
        odm_perspective_row row;
        perspective_row_prepare(plan, y, &row);
        coverage = perspective_coverage_q31(&row, x);
        odm_layered_blend16(out, &config->background.grid_color, coverage,
                            config->background.opacity_q31);
        return ODM_STATUS_OK;
    }
    if (config->background.style != ODM_BACKGROUND_GRID || plan->grid_spacing_q16 <= 0)
        return ODM_STATUS_OK;

    xq = ((int64_t)x << 16) + INT64_C(32768);
    yq = ((int64_t)y << 16) + INT64_C(32768);
    cy = ((int64_t)plan->height << 15);
    dy = yq - cy;
    xw = xq; yw = yq;

    /* Depth is a symmetric, bounded quadratic displacement of y.  It uses the
     * canvas center, never core geometry, so background remains independently
     * configurable while reacting to Director depth. */
    half_h = ((int64_t)plan->height << 15);
    if (half_h > 0 && plan->grid_depth_q16 != 0) {
        depth_curve = (dy * abs_i64(dy)) / half_h;
        depth_disp = (depth_curve * (int64_t)plan->grid_depth_q16) / half_h;
        yw += depth_disp;
    }

    wave_x = (int64_t)tri_wave_q16(yw + plan->grid_offset_y_q16,
                                   (int64_t)plan->grid_spacing_q16 * 4);
    wave_y = (int64_t)tri_wave_q16(xw + plan->grid_offset_x_q16,
                                   (int64_t)plan->grid_spacing_q16 * 4);
    xw += (wave_x * (int64_t)plan->grid_warp_q16) / INT64_C(65536);
    yw += (wave_y * (int64_t)plan->grid_warp_q16) / INT64_C(65536);

    dist_x = nearest_grid_distance(xw + plan->grid_offset_x_q16,
                                   plan->grid_spacing_q16);
    dist_y = nearest_grid_distance(yw + plan->grid_offset_y_q16,
                                   plan->grid_spacing_q16);
    dist = dist_x < dist_y ? dist_x : dist_y;
    coverage = odm_layered_coverage_q31(dist, plan->grid_line_q16 / 2,
                                        plan->grid_feather_q16);
    odm_layered_blend16(out, &config->background.grid_color, coverage,
                        config->background.opacity_q31);
    return ODM_STATUS_OK;
}


/* Full-frame reference-equivalent raster.  The scalar pixel API above remains
 * the semantic oracle.  This path removes modulo/division work from the hot
 * pixel loop by carrying canonical phases forward one pixel at a time.
 * Because grid_warp_q16 <= grid_spacing_q16, applying the row/column warp to a
 * normalized phase can require at most one wrap in either direction. */
static int64_t normalize_near_phase(int64_t phase_q16, int64_t spacing_q16) {
    if (phase_q16 < 0) phase_q16 += spacing_q16;
    else if (phase_q16 >= spacing_q16) phase_q16 -= spacing_q16;
    return phase_q16;
}

static int32_t tri_wave_phase_q16(int64_t phase_q16, int64_t period_q16) {
    int64_t half, q;
    if (period_q16 <= 0) return 0;
    half = period_q16 / 2;
    if (half <= 0) return 0;
    if (phase_q16 <= half) {
        q = (phase_q16 * INT64_C(131072) + half / 2) / half - INT64_C(65536);
    } else {
        q = ((period_q16 - phase_q16) * INT64_C(131072) + half / 2) / half - INT64_C(65536);
    }
    if (q > INT32_MAX) q = INT32_MAX;
    if (q < INT32_MIN) q = INT32_MIN;
    return (int32_t)q;
}

static int64_t nearest_grid_phase(int64_t phase_q16, int64_t spacing_q16) {
    int64_t other = spacing_q16 - phase_q16;
    return phase_q16 < other ? phase_q16 : other;
}

void odm_layered_background_render_rows(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        uint32_t y_begin, uint32_t y_end,
                                        odm_layered_pixel16 *frame) {
    odm_layered_pixel16 base;
    int64_t spacing, period4, xphase0, xwave0;
    int64_t half_h, cy;
    uint32_t y;
    if (!config || !plan || !frame || y_begin >= y_end || y_begin >= plan->height) return;
    if (y_end > plan->height) y_end = plan->height;
    memset(&base, 0, sizeof(base));
    if (config->background.style == ODM_BACKGROUND_NONE) {
        memset(frame + (uint64_t)y_begin * plan->width, 0,
               (size_t)(y_end - y_begin) * plan->width * sizeof(*frame));
        return;
    }
    odm_layered_blend16(&base, &config->background.solid_color,
                        (uint32_t)INT32_MAX, config->background.opacity_q31);
    if (config->background.style == ODM_BACKGROUND_DEPTH_FIELD) {
        int64_t cx = plan->center_x_q16, cy2 = plan->center_y_q16;
        int64_t radius = ((int64_t)plan->width + (int64_t)plan->height) << 15; /* media * 2^16 */
        for (y = y_begin; y < y_end; ++y) {
            uint32_t x;
            for (x = 0u; x < plan->width; ++x) {
                odm_layered_pixel16 out = base;
                int64_t dx = (((int64_t)x << 16) + 32768) - cx;
                int64_t dy = (((int64_t)y << 16) + 32768) - cy2;
                uint32_t wgt = bg_depth_weight(dx, dy, radius);
                if (wgt != 0u) {
                    /* El difuminado desplaza el peso menos de un paso de
                     * cuantizacion antes de mezclar, que es exactamente donde
                     * aparecen las bandas. */
                    uint32_t dith = bg_bayer8[((y & 7u) << 3) | (x & 7u)];
                    uint64_t w = (uint64_t)wgt + (uint64_t)dith * UINT64_C(2048);
                    if (w > (uint64_t)INT32_MAX) w = (uint64_t)INT32_MAX;
                    odm_layered_blend16(&out, &config->background.grid_color,
                                        (uint32_t)w, config->background.opacity_q31);
                }
                frame[(uint64_t)y * plan->width + x] = out;
            }
        }
        return;
    }
    if (config->background.style == ODM_BACKGROUND_PERSPECTIVE_GRID) {
        for (y = y_begin; y < y_end; ++y) {
            odm_perspective_row row;
            int64_t xphase;
            uint32_t x;
            perspective_row_prepare(plan, y, &row);
            xphase = row.x_phase0_q17;
            for (x = 0u; x < plan->width; ++x) {
                odm_layered_pixel16 out = base;
                uint32_t coverage = 0u;
                if (row.active) {
                    int64_t dx = nearest_grid_phase(xphase, row.spacing_q17);
                    int64_t dist = dx < row.z_distance_q17 ? dx : row.z_distance_q17;
                    coverage = odm_layered_coverage_q31(dist, row.line_half_world_q17,
                                                        row.feather_world_q17);
                    xphase += row.x_phase_step_q17;
                    if (xphase >= row.spacing_q17) xphase -= row.spacing_q17;
                }
                odm_layered_blend16(&out, &config->background.grid_color, coverage,
                                    config->background.opacity_q31);
                frame[(uint64_t)y * plan->width + x] = out;
            }
        }
        return;
    }
    if (config->background.style != ODM_BACKGROUND_GRID || plan->grid_spacing_q16 <= 0) {
        uint64_t i = (uint64_t)y_begin * plan->width;
        uint64_t end = (uint64_t)y_end * plan->width;
        for (; i < end; ++i) frame[i] = base;
        return;
    }

    spacing = plan->grid_spacing_q16;
    period4 = spacing * INT64_C(4);
    xphase0 = floor_mod_i64(INT64_C(32768) + plan->grid_offset_x_q16, spacing);
    xwave0 = floor_mod_i64(INT64_C(32768) + plan->grid_offset_x_q16, period4);
    half_h = ((int64_t)plan->height << 15);
    cy = half_h;

    for (y = y_begin; y < y_end; ++y) {
        int64_t yq = ((int64_t)y << 16) + INT64_C(32768);
        int64_t dy = yq - cy;
        int64_t yw = yq;
        int64_t row_wave, row_shift;
        int64_t yphase_base;
        int64_t xphase = xphase0, xwave = xwave0;
        uint32_t x;
        if (half_h > 0 && plan->grid_depth_q16 != 0) {
            int64_t depth_curve = (dy * abs_i64(dy)) / half_h;
            int64_t depth_disp = (depth_curve * (int64_t)plan->grid_depth_q16) / half_h;
            yw += depth_disp;
        }
        row_wave = (int64_t)tri_wave_q16(yw + plan->grid_offset_y_q16, period4);
        row_shift = (row_wave * (int64_t)plan->grid_warp_q16) / INT64_C(65536);
        yphase_base = floor_mod_i64(yw + plan->grid_offset_y_q16, spacing);

        for (x = 0u; x < plan->width; ++x) {
            int64_t px_phase, py_phase, col_wave, col_shift;
            int64_t dist_x, dist_y, dist;
            uint32_t coverage;
            odm_layered_pixel16 out = base;

            px_phase = normalize_near_phase(xphase + row_shift, spacing);
            col_wave = (int64_t)tri_wave_phase_q16(xwave, period4);
            col_shift = (col_wave * (int64_t)plan->grid_warp_q16) / INT64_C(65536);
            py_phase = normalize_near_phase(yphase_base + col_shift, spacing);
            dist_x = nearest_grid_phase(px_phase, spacing);
            dist_y = nearest_grid_phase(py_phase, spacing);
            dist = dist_x < dist_y ? dist_x : dist_y;
            coverage = odm_layered_coverage_q31(dist, plan->grid_line_q16 / 2,
                                                plan->grid_feather_q16);
            odm_layered_blend16(&out, &config->background.grid_color, coverage,
                                config->background.opacity_q31);
            frame[(uint64_t)y * plan->width + x] = out;

            xphase += INT64_C(65536);
            if (xphase >= spacing) xphase -= spacing;
            xwave += INT64_C(65536);
            if (xwave >= period4) xwave -= period4;
        }
    }
}


void odm_layered_background_prepare_columns(const odm_layered_frame_plan *plan,
                                            int32_t *col_shifts,
                                            uint32_t count) {
    int64_t spacing, period4, xwave;
    uint32_t x;
    if (!plan || !col_shifts || count < plan->width || plan->width == 0u) return;
    if (plan->grid_spacing_q16 <= 0 || plan->grid_warp_q16 == 0) {
        memset(col_shifts, 0, (size_t)plan->width * sizeof(*col_shifts));
        return;
    }
    spacing = plan->grid_spacing_q16;
    period4 = spacing * INT64_C(4);
    xwave = floor_mod_i64(INT64_C(32768) + plan->grid_offset_x_q16, period4);
    for (x = 0u; x < plan->width; ++x) {
        int64_t col_wave = (int64_t)tri_wave_phase_q16(xwave, period4);
        int64_t col_shift = (col_wave * (int64_t)plan->grid_warp_q16) / INT64_C(65536);
        if (col_shift > INT32_MAX) col_shift = INT32_MAX;
        if (col_shift < INT32_MIN) col_shift = INT32_MIN;
        col_shifts[x] = (int32_t)col_shift;
        xwave += INT64_C(65536);
        if (xwave >= period4) xwave -= period4;
    }
}

void odm_layered_background_render_rows_cached(const odm_layered_config *config,
                                               const odm_layered_frame_plan *plan,
                                               const int32_t *col_shifts,
                                               uint32_t y_begin, uint32_t y_end,
                                               odm_layered_pixel16 *frame) {
    odm_layered_pixel16 base;
    int64_t spacing, xphase0;
    int64_t half_h, cy;
    uint32_t y;
    if (!config || !plan || !col_shifts || !frame || y_begin >= y_end ||
        y_begin >= plan->height) return;
    if (y_end > plan->height) y_end = plan->height;
    memset(&base, 0, sizeof(base));
    if (config->background.style == ODM_BACKGROUND_NONE) {
        memset(frame + (uint64_t)y_begin * plan->width, 0,
               (size_t)(y_end - y_begin) * plan->width * sizeof(*frame));
        return;
    }
    odm_layered_blend16(&base, &config->background.solid_color,
                        (uint32_t)INT32_MAX, config->background.opacity_q31);
    if (config->background.style == ODM_BACKGROUND_PERSPECTIVE_GRID) {
        odm_layered_background_render_rows(config, plan, y_begin, y_end, frame);
        return;
    }
    if (config->background.style != ODM_BACKGROUND_GRID || plan->grid_spacing_q16 <= 0) {
        uint64_t i = (uint64_t)y_begin * plan->width;
        uint64_t end = (uint64_t)y_end * plan->width;
        for (; i < end; ++i) frame[i] = base;
        return;
    }

    spacing = plan->grid_spacing_q16;
    xphase0 = floor_mod_i64(INT64_C(32768) + plan->grid_offset_x_q16, spacing);
    half_h = ((int64_t)plan->height << 15);
    cy = half_h;

    for (y = y_begin; y < y_end; ++y) {
        int64_t yq = ((int64_t)y << 16) + INT64_C(32768);
        int64_t dy = yq - cy;
        int64_t yw = yq;
        int64_t row_wave, row_shift;
        int64_t yphase_base;
        int64_t xphase = xphase0;
        uint32_t x;
        if (half_h > 0 && plan->grid_depth_q16 != 0) {
            int64_t depth_curve = (dy * abs_i64(dy)) / half_h;
            int64_t depth_disp = (depth_curve * (int64_t)plan->grid_depth_q16) / half_h;
            yw += depth_disp;
        }
        row_wave = (int64_t)tri_wave_q16(yw + plan->grid_offset_y_q16, spacing * INT64_C(4));
        row_shift = (row_wave * (int64_t)plan->grid_warp_q16) / INT64_C(65536);
        yphase_base = floor_mod_i64(yw + plan->grid_offset_y_q16, spacing);

        for (x = 0u; x < plan->width; ++x) {
            int64_t px_phase = normalize_near_phase(xphase + row_shift, spacing);
            int64_t py_phase = normalize_near_phase(yphase_base + (int64_t)col_shifts[x],
                                                    spacing);
            int64_t dist_x = nearest_grid_phase(px_phase, spacing);
            int64_t dist_y = nearest_grid_phase(py_phase, spacing);
            int64_t dist = dist_x < dist_y ? dist_x : dist_y;
            uint32_t coverage = odm_layered_coverage_q31(dist, plan->grid_line_q16 / 2,
                                                         plan->grid_feather_q16);
            odm_layered_pixel16 out = base;
            odm_layered_blend16(&out, &config->background.grid_color, coverage,
                                config->background.opacity_q31);
            frame[(uint64_t)y * plan->width + x] = out;
            xphase += INT64_C(65536);
            if (xphase >= spacing) xphase -= spacing;
        }
    }
}

void odm_layered_background_render(const odm_layered_config *config,
                                   const odm_layered_frame_plan *plan,
                                   odm_layered_pixel16 *frame) {
    if (!plan) return;
    odm_layered_background_render_rows(config, plan, 0u, plan->height, frame);
}

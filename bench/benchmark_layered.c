#define _POSIX_C_SOURCE 200809L

#include "odm_compositor.h"
#include "odm_renderer.h"
#include "compositor_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_W UINT32_C(1080)
#define BENCH_H UINT32_C(1080)
#define BENCH_ITERS UINT32_C(30)

static uint32_t q31_ratio(uint32_t num, uint32_t den) {
    return (uint32_t)(((uint64_t)INT32_MAX * num + den / 2u) / den);
}

static double monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void fill_config(odm_layered_config *c) {
    memset(c, 0, sizeof(*c));
    c->schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->canvas.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->canvas.aspect = ODM_CANVAS_ASPECT_SQUARE_1_1;
    c->canvas.width = BENCH_W;
    c->canvas.height = BENCH_H;
    c->canvas.fps = 30;
    c->canvas.safe_left_q31 = q31_ratio(1u, 20u);
    c->canvas.safe_top_q31 = q31_ratio(1u, 20u);
    c->canvas.safe_right_q31 = q31_ratio(1u, 20u);
    c->canvas.safe_bottom_q31 = q31_ratio(1u, 20u);

    c->background.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->background.style = ODM_BACKGROUND_GRID;
    c->background.solid_color.a = UINT16_MAX;
    c->background.grid_color.r = UINT16_MAX;
    c->background.grid_color.g = UINT16_MAX;
    c->background.grid_color.b = UINT16_MAX;
    c->background.grid_color.a = UINT16_MAX;
    c->background.grid_spacing_q16 = 32u << 16;
    c->background.grid_line_q16 = 1u << 16;
    c->background.grid_feather_q16 = 1u << 16;
    c->background.zoom_reactivity_q31 = q31_ratio(1u, 4u);
    c->background.warp_reactivity_q31 = q31_ratio(1u, 8u);
    c->background.depth_reactivity_q31 = q31_ratio(1u, 8u);
    c->background.opacity_q31 = (uint32_t)INT32_MAX;

    c->core.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->core.shape = ODM_CORE_SHAPE_CIRCLE;
    c->core.fit = ODM_CORE_FIT_COVER;
    c->core.center_x_q31 = q31_ratio(1u, 2u);
    c->core.center_y_q31 = q31_ratio(1u, 2u);
    c->core.width_q31 = q31_ratio(1u, 2u);
    c->core.height_q31 = q31_ratio(1u, 2u);
    c->core.corner_radius_q31 = q31_ratio(1u, 8u);
    c->core.border_q16 = 1u << 16;
    c->core.feather_q16 = 1u << 16;
    c->core.scale_reactivity_q31 = q31_ratio(1u, 16u);
    c->core.opacity_q31 = (uint32_t)INT32_MAX;
    c->core.border_color.r = UINT16_MAX;
    c->core.border_color.g = UINT16_MAX;
    c->core.border_color.b = UINT16_MAX;
    c->core.border_color.a = UINT16_MAX;

    c->field.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->field.flags = ODM_FIELD_RADIAL_BARS | ODM_FIELD_PARTICLES | ODM_FIELD_ORBIT_RING;
    c->field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS;
    c->field.particle_count = 8u;
    c->field.ring_gap_q16 = 2u << 16;
    c->field.bar_min_q16 = 1u << 16;
    c->field.bar_max_q16 = 4u << 16;
    c->field.bar_width_q16 = 1u << 16;
    c->field.particle_radius_q16 = 1u << 16;
    c->field.field_opacity_q31 = q31_ratio(3u, 4u);
    c->field.primary_color.r = UINT16_MAX;
    c->field.primary_color.g = UINT16_MAX;
    c->field.primary_color.b = UINT16_MAX;
    c->field.primary_color.a = UINT16_MAX;
    c->field.secondary_color.r = UINT16_MAX / 2u;
    c->field.secondary_color.g = UINT16_MAX / 2u;
    c->field.secondary_color.b = UINT16_MAX / 2u;
    c->field.secondary_color.a = UINT16_MAX;
    c->field.seed = UINT64_C(0x0d130d130d130d13);

    c->hud.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->hud.flags = ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE | ODM_HUD_TITLE | ODM_HUD_ARTIST;
    c->hud.margin_q16 = 16u << 16;
    c->hud.progress_height_q16 = 2u << 16;
    c->hud.progress_width_q31 = q31_ratio(3u, 4u);
    c->hud.text_scale_q16 = 1u << 16;
    c->hud.line_gap_q16 = 1u << 16;
    c->hud.opacity_q31 = (uint32_t)INT32_MAX;
    c->hud.foreground_color.r = UINT16_MAX;
    c->hud.foreground_color.g = UINT16_MAX;
    c->hud.foreground_color.b = UINT16_MAX;
    c->hud.foreground_color.a = UINT16_MAX;
    c->hud.background_color.a = UINT16_MAX;
    memcpy(c->hud.title, "QUIET, NOT EMPTY", 17u);
    memcpy(c->hud.artist, "AFTERIMAGE", 11u);
}

static void fill_states(odm_composition_frame_state *comp,
                        odm_director_frame_state *dir) {
    uint32_t i;
    memset(comp, 0, sizeof(*comp));
    memset(dir, 0, sizeof(*dir));
    comp->schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    comp->mode = ODM_COMPOSITION_MODE_FLOW;
    comp->tick_index = 50u;
    comp->center_sample = UINT64_C(24000);
    comp->phase = UINT32_C(0x12345678);
    comp->ring_phase = UINT32_C(0x34567890);
    comp->core_breath_q31 = q31_ratio(1u, 3u);
    comp->grid_q31 = q31_ratio(1u, 2u);
    comp->particles_q31 = q31_ratio(1u, 2u);
    comp->radial_gain_q31 = q31_ratio(3u, 4u);
    comp->fracture_q31 = q31_ratio(1u, 5u);
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i) {
        comp->radial_q31[i] = (uint32_t)(((uint64_t)INT32_MAX * (i + 1u)) /
                                          ODM_COMPOSITION_RADIAL_SEGMENTS);
    }
    dir->schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    dir->tick_index = comp->tick_index;
    dir->layout = ODM_DIRECTOR_LAYOUT_ORBIT;
    dir->depth_q31 = q31_ratio(2u, 5u);
    dir->edge_activity_q31 = q31_ratio(1u, 4u);
    dir->orbit_phase = UINT32_C(0x10203040);
}

static void fill_surface(uint8_t *pixels) {
    uint32_t x, y;
    for (y = 0u; y < BENCH_H; ++y) {
        for (x = 0u; x < BENCH_W; ++x) {
            uint64_t o = ((uint64_t)y * BENCH_W + x) * 4u;
            pixels[o + 0u] = (uint8_t)(x * 131u + y * 17u);
            pixels[o + 1u] = (uint8_t)(x * 19u + y * 137u);
            pixels[o + 2u] = (uint8_t)(x * 53u + y * 71u);
            pixels[o + 3u] = UINT8_MAX;
        }
    }
}

static double run_frames(const odm_layered_config *config,
                         const odm_layered_render_admission *admission,
                         const odm_layered_frame_plan *plan,
                         const odm_render_surface_frame *surface,
                         void *scratch, uint64_t scratch_bytes,
                         odm_layered_worker_pool *pool,
                         odm_sha256_digest *out_digest) {
    const uint8_t *stage = NULL;
    uint64_t frame_bytes = 0u, required_scratch = 0u;
    uint32_t i;
    double begin, end;
    for (i = 0u; i < 2u; ++i) {
        odm_status st;
        if (pool) {
            st = odm_layered_render_frame_stream_stage_admitted_pool(
                config, admission, plan, surface, NULL, scratch, scratch_bytes,
                pool, &stage, &frame_bytes, &required_scratch);
        } else {
            st = odm_layered_render_frame_stream_stage_admitted_workers(
                config, admission, plan, surface, NULL, scratch, scratch_bytes,
                1u, &stage, &frame_bytes, &required_scratch);
        }
        if (st != ODM_STATUS_OK) return -1.0;
    }
    begin = monotonic_seconds();
    for (i = 0u; i < BENCH_ITERS; ++i) {
        odm_status st;
        if (pool) {
            st = odm_layered_render_frame_stream_stage_admitted_pool(
                config, admission, plan, surface, NULL, scratch, scratch_bytes,
                pool, &stage, &frame_bytes, &required_scratch);
        } else {
            st = odm_layered_render_frame_stream_stage_admitted_workers(
                config, admission, plan, surface, NULL, scratch, scratch_bytes,
                1u, &stage, &frame_bytes, &required_scratch);
        }
        if (st != ODM_STATUS_OK) return -1.0;
    }
    end = monotonic_seconds();
    if (!stage || frame_bytes != admission->frame_bytes ||
        required_scratch != admission->scratch_bytes ||
        odm_sha256(stage, frame_bytes, out_digest) != ODM_STATUS_OK) return -1.0;
    return (end - begin) / (double)BENCH_ITERS;
}

int main(void) {
    odm_layered_config config;
    odm_composition_frame_state comp;
    odm_director_frame_state dir;
    odm_layered_frame_plan plan;
    odm_layered_render_admission admission;
    odm_render_surface_frame surface;
    odm_layered_worker_pool pool;
    odm_sha256_digest one_hash, four_hash;
    uint8_t *pixels = NULL;
    void *scratch = NULL;
    uint64_t pixel_bytes = (uint64_t)BENCH_W * BENCH_H * 4u;
    double one_s, four_s;
    char one_hex[65], four_hex[65];
    uint64_t one_hex_required = 0u, four_hex_required = 0u;

    fill_config(&config);
    fill_states(&comp, &dir);
    if (odm_layered_resolve_frame_plan(&config, &comp, &dir,
                                       INT64_C(24000), INT64_C(48000), &plan) != ODM_STATUS_OK)
        return 2;
    memset(&admission, 0, sizeof(admission));
    if (odm_layered_render_admit(&config, ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                 &admission) != ODM_STATUS_OK) return 3;
    if (admission.scratch_bytes > (uint64_t)SIZE_MAX || pixel_bytes > (uint64_t)SIZE_MAX)
        return 4;
    pixels = (uint8_t *)malloc((size_t)pixel_bytes);
    scratch = aligned_alloc(8u, (size_t)((admission.scratch_bytes + 7u) & ~UINT64_C(7)));
    if (!pixels || !scratch) return 5;
    fill_surface(pixels);
    memset(&surface, 0, sizeof(surface));
    surface.width = BENCH_W;
    surface.height = BENCH_H;
    surface.pixel_format = ODM_RENDER_SURFACE_RGBA8;
    surface.primaries = ODM_COLOR_PRIMARIES_BT709;
    surface.transfer = ODM_COLOR_TRANSFER_SRGB;
    surface.alpha_mode = ODM_ALPHA_STRAIGHT;
    surface.pixel_bytes = pixel_bytes;
    surface.pixels = pixels;

    one_s = run_frames(&config, &admission, &plan, &surface, scratch,
                       admission.scratch_bytes, NULL, &one_hash);
    memset(&pool, 0, sizeof(pool));
    if (odm_layered_worker_pool_init(&pool, 4u) != ODM_STATUS_OK) return 6;
    four_s = run_frames(&config, &admission, &plan, &surface, scratch,
                        admission.scratch_bytes, &pool, &four_hash);
    odm_layered_worker_pool_destroy(&pool);
    if (one_s <= 0.0 || four_s <= 0.0 || !odm_sha256_equal(&one_hash, &four_hash)) return 7;
    if (odm_sha256_to_hex(&one_hash, one_hex, sizeof(one_hex), &one_hex_required) != ODM_STATUS_OK ||
        odm_sha256_to_hex(&four_hash, four_hex, sizeof(four_hex), &four_hex_required) != ODM_STATUS_OK ||
        one_hex_required != sizeof(one_hex) || four_hex_required != sizeof(four_hex)) return 8;
    printf("{\"schema\":\"odm_layered_benchmark_v1\",\"semantic_authority\":false,");
    printf("\"width\":%u,\"height\":%u,\"iterations\":%u,", BENCH_W, BENCH_H, BENCH_ITERS);
    printf("\"one_worker_ms\":%.6f,\"pool4_ms\":%.6f,", one_s * 1000.0, four_s * 1000.0);
    printf("\"one_worker_fps\":%.3f,\"pool4_fps\":%.3f,", 1.0 / one_s, 1.0 / four_s);
    printf("\"frame_sha256\":\"%s\",\"pool_frame_sha256\":\"%s\",", one_hex, four_hex);
    printf("\"byte_identical\":true}\n");
    free(scratch);
    free(pixels);
    return 0;
}

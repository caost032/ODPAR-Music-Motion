#include "odm_compositor.h"
#include "odm_renderer.h"
#include "odm_visual.h"
#include "odm_status.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 384u
#define H 216u
#define FPS 24
#define SECONDS 12u
#define FRAMES ((uint32_t)FPS * SECONDS)
#define SRC_W 256u
#define SRC_H 256u

static uint32_t q31_ratio(uint32_t n, uint32_t d) {
    uint64_t v = (uint64_t)(uint32_t)INT32_MAX * (uint64_t)n + (uint64_t)d / 2u;
    if (d == 0u || n > d) return 0u;
    return (uint32_t)(v / d);
}

static uint32_t tri_q31(uint32_t frame, uint32_t period, uint32_t phase) {
    uint32_t p, half;
    uint64_t v;
    if (period < 2u) return 0u;
    p = (frame + phase) % period;
    half = period / 2u;
    if (p <= half) {
        v = (uint64_t)(uint32_t)INT32_MAX * p;
        v /= half ? half : 1u;
    } else {
        v = (uint64_t)(uint32_t)INT32_MAX * (period - p);
        v /= (period - half) ? (period - half) : 1u;
    }
    if (v > (uint64_t)INT32_MAX) v = (uint64_t)INT32_MAX;
    return (uint32_t)v;
}

static uint8_t clamp8_u32(uint32_t v) { return (uint8_t)(v > 255u ? 255u : v); }

static void build_core_texture(uint8_t *px) {
    uint32_t x, y;
    for (y = 0u; y < SRC_H; ++y) {
        for (x = 0u; x < SRC_W; ++x) {
            uint32_t dx = x > SRC_W/2u ? x - SRC_W/2u : SRC_W/2u - x;
            uint32_t dy = y > SRC_H/2u ? y - SRC_H/2u : SRC_H/2u - y;
            uint32_t o = (y * SRC_W + x) * 4u;
            uint32_t diag = (x + y) & 63u;
            uint32_t ring = (dx + dy) & 63u;
            uint32_t glow = 150u;
            if (dx < 72u && dy < 72u) glow += 35u;
            if (diag < 5u || ring < 4u) glow += 45u;
            px[o+0u] = clamp8_u32(glow / 2u + (x * 70u) / SRC_W);
            px[o+1u] = clamp8_u32(glow / 2u + (y * 55u) / SRC_H);
            px[o+2u] = clamp8_u32(glow + ((SRC_W - x) * 45u) / SRC_W);
            px[o+3u] = 255u;
        }
    }
}

static int set_route(odm_layered_config *c, uint32_t target,
                     uint32_t a, uint32_t b, uint32_t combine) {
    uint32_t route = 0u;
    if (odm_reaction_route_make(a, b, combine, &route) != ODM_STATUS_OK) return 0;
    if (odm_layered_config_set_reaction_route(c, target, route) != ODM_STATUS_OK) return 0;
    return 1;
}

static int build_section_config(uint32_t section, odm_layered_config *out) {
    odm_layered_config base, styled;
    odm_layered_style_profile style;
    uint32_t preset;
    const char *title;
    const char *artist = "0.14 STYLE / REACTION WIP";

    if (odm_layered_config_init_default(&base, ODM_CANVAS_ASPECT_HORIZONTAL_16_9,
                                        W, H, FPS) != ODM_STATUS_OK) return 0;

    /* More legible diagnostic HUD at preview resolution. */
    base.hud.text_scale_q16 = 2u << 16;
    base.hud.progress_height_q16 = 1u << 16;
    base.hud.progress_width_q31 = q31_ratio(4u, 5u);

    /* Deliberately visible, bounded reactivity for this diagnostic preview. */
    base.background.zoom_reactivity_q31 = q31_ratio(1u, 2u);
    base.background.warp_reactivity_q31 = q31_ratio(1u, 3u);
    base.background.depth_reactivity_q31 = q31_ratio(1u, 2u);
    base.core.scale_reactivity_q31 = q31_ratio(1u, 3u);

    if (!set_route(&base, ODM_REACTION_TARGET_BACKGROUND_ZOOM,
                   ODM_REACTION_SOURCE_BAND_0, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&base, ODM_REACTION_TARGET_BACKGROUND_WARP,
                   ODM_REACTION_SOURCE_FRACTURE, ODM_REACTION_SOURCE_DIRECTOR_EDGE,
                   ODM_REACTION_COMBINE_MAX)) return 0;
    if (!set_route(&base, ODM_REACTION_TARGET_BACKGROUND_DEPTH,
                   ODM_REACTION_SOURCE_DIRECTOR_DEPTH, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&base, ODM_REACTION_TARGET_CORE_SCALE,
                   ODM_REACTION_SOURCE_DIRECTOR_ENERGY, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&base, ODM_REACTION_TARGET_FIELD_GAIN,
                   ODM_REACTION_SOURCE_RADIAL_GAIN, ODM_REACTION_SOURCE_PARTICLES,
                   ODM_REACTION_COMBINE_MAX)) return 0;
    if (!set_route(&base, ODM_REACTION_TARGET_PARTICLE_DENSITY,
                   ODM_REACTION_SOURCE_BAND_5, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;

    switch (section) {
        case 0u:
            preset = ODM_LAYERED_STYLE_PRESET_MONO_PRECISE;
            title = "STYLE 01  MONO PRECISE";
            base.core.shape = ODM_CORE_SHAPE_CIRCLE;
            break;
        case 1u:
            preset = ODM_LAYERED_STYLE_PRESET_VOID_CINEMATIC;
            title = "STYLE 02  VOID CINEMATIC";
            base.core.shape = ODM_CORE_SHAPE_CIRCLE;
            break;
        case 2u:
            preset = ODM_LAYERED_STYLE_PRESET_DEEP_GRID;
            title = "STYLE 03  DEEP GRID";
            base.core.shape = ODM_CORE_SHAPE_ROUNDED_RECT;
            base.core.width_q31 = q31_ratio(11u, 20u);
            base.core.height_q31 = q31_ratio(7u, 20u);
            break;
        default:
            preset = ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL;
            title = "STYLE 04  MINIMAL EDITORIAL";
            base.core.shape = ODM_CORE_SHAPE_SQUARE;
            base.core.width_q31 = q31_ratio(2u, 5u);
            base.core.height_q31 = q31_ratio(2u, 5u);
            break;
    }
    if (odm_layered_config_validate(&base) != ODM_STATUS_OK) return 0;
    if (odm_layered_style_init(&style, preset) != ODM_STATUS_OK) return 0;
    if (odm_layered_style_apply(&base, &style, &styled) != ODM_STATUS_OK) return 0;
    if (odm_layered_config_set_metadata(&styled, title, artist) != ODM_STATUS_OK) return 0;
    *out = styled;
    return 1;
}

static void fill_states(uint32_t f, uint32_t section,
                        odm_composition_frame_state *comp,
                        odm_director_frame_state *dir) {
    uint32_t i;
    uint32_t local = f % ((uint32_t)FPS * 3u);
    uint32_t energy = tri_q31(local, (uint32_t)FPS * 2u, 0u);
    uint32_t slow = tri_q31(local, (uint32_t)FPS * 3u, (uint32_t)FPS / 2u);
    uint32_t fast = tri_q31(local, (uint32_t)FPS, (uint32_t)FPS / 4u);
    memset(comp, 0, sizeof(*comp));
    memset(dir, 0, sizeof(*dir));
    comp->schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    comp->tick_index = ((uint64_t)f * UINT64_C(2000)) / UINT64_C(480);
    comp->phase = f * UINT32_C(0x01800000);
    comp->ring_phase = f * UINT32_C(0x02500000);
    comp->core_breath_q31 = energy;
    comp->grid_q31 = slow;
    comp->particles_q31 = fast;
    comp->radial_gain_q31 = energy > slow ? energy : slow;
    comp->fracture_q31 = (section == 2u) ? fast : q31_ratio(1u, 8u);
    comp->memory_echo_q31 = slow;
    comp->halo_q31 = energy;
    comp->lateral_field_q31 = fast;
    comp->accent_q31 = tri_q31(local, (uint32_t)FPS / 2u + 2u, 0u);
    comp->band_q31[0] = slow;
    comp->band_q31[1] = energy;
    comp->band_q31[2] = fast;
    comp->band_q31[3] = tri_q31(local, (uint32_t)FPS * 2u, 9u);
    comp->band_q31[4] = tri_q31(local, (uint32_t)FPS + 8u, 5u);
    comp->band_q31[5] = tri_q31(local, (uint32_t)FPS * 2u + 12u, 17u);
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i) {
        comp->radial_q31[i] = tri_q31(f + i * 2u, (uint32_t)FPS * 2u + 8u, i * 3u);
    }

    dir->schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    dir->tick_index = comp->tick_index;
    dir->layout = section == 0u ? ODM_DIRECTOR_LAYOUT_ORBIT :
                  section == 1u ? ODM_DIRECTOR_LAYOUT_VOID :
                  section == 2u ? ODM_DIRECTOR_LAYOUT_FRACTURE : ODM_DIRECTOR_LAYOUT_MONOLITH;
    dir->previous_layout = dir->layout;
    dir->macro_energy_q31 = energy;
    dir->macro_novelty_q31 = fast;
    dir->core_extent_q31 = energy;
    dir->orbit_q31 = section == 0u ? energy : q31_ratio(1u, 5u);
    dir->wings_q31 = section == 3u ? slow : 0u;
    dir->memory_panels_q31 = section == 1u ? slow : 0u;
    dir->tunnel_q31 = section == 2u ? slow : q31_ratio(1u, 6u);
    dir->architecture_q31 = section == 2u ? energy : q31_ratio(1u, 5u);
    dir->backdrop_q31 = slow;
    dir->depth_q31 = slow;
    dir->edge_activity_q31 = fast;
    dir->orbit_phase = f * UINT32_C(0x01200000);
    dir->layout_epoch = section + 1u;
    dir->layer_count = 4u;
    dir->pane_count = section == 1u ? 3u : 1u;
}

int main(void) {
    odm_layered_config cfg[4];
    odm_render_surface_frame surface;
    uint8_t *src = NULL, *frame = NULL, *scratch = NULL;
    uint64_t frame_bytes = 0u, scratch_bytes = 0u, rf = 0u, rs = 0u;
    uint32_t s, f;

    for (s = 0u; s < 4u; ++s) {
        if (!build_section_config(s, &cfg[s])) {
            fprintf(stderr, "preview014: config section %u failed\n", s);
            return 2;
        }
    }
    if (odm_layered_render_requirements(&cfg[0], ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                        &frame_bytes, &scratch_bytes) != ODM_STATUS_OK) return 3;
    src = (uint8_t*)malloc((size_t)SRC_W * SRC_H * 4u);
    frame = (uint8_t*)malloc((size_t)frame_bytes);
    scratch = (uint8_t*)malloc((size_t)scratch_bytes);
    if (!src || !frame || !scratch) return 4;
    build_core_texture(src);
    memset(&surface, 0, sizeof(surface));
    surface.width = SRC_W; surface.height = SRC_H;
    surface.pixel_format = ODM_RENDER_SURFACE_RGBA8;
    surface.primaries = ODM_COLOR_PRIMARIES_BT709;
    surface.transfer = ODM_COLOR_TRANSFER_SRGB;
    surface.alpha_mode = ODM_ALPHA_STRAIGHT;
    surface.start_sample = 0; surface.end_sample = (int64_t)SECONDS * 48000;
    surface.pixel_bytes = (uint64_t)SRC_W * SRC_H * 4u;
    surface.pixels = src;

    for (f = 0u; f < FRAMES; ++f) {
        odm_composition_frame_state comp;
        odm_director_frame_state dir;
        odm_layered_frame_plan plan;
        odm_sha256_digest hash;
        uint32_t section = f / ((uint32_t)FPS * 3u);
        int64_t sample = (int64_t)f * (48000 / FPS);
        fill_states(f, section, &comp, &dir);
        if (odm_layered_resolve_frame_plan(&cfg[section], &comp, &dir, sample,
                                           (int64_t)SECONDS * 48000, &plan) != ODM_STATUS_OK) {
            fprintf(stderr, "preview014: plan frame %u failed\n", f); return 5;
        }
        if (odm_layered_render_frame(&cfg[section], &plan, &surface, NULL,
                                     ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                     scratch, scratch_bytes, frame, frame_bytes,
                                     &rf, &rs, &hash) != ODM_STATUS_OK) {
            fprintf(stderr, "preview014: render frame %u failed\n", f); return 6;
        }
        if (fwrite(frame, 1u, (size_t)frame_bytes, stdout) != (size_t)frame_bytes) return 7;
    }
    free(scratch); free(frame); free(src);
    return 0;
}

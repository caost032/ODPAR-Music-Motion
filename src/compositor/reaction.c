#include "compositor_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int reaction_route_valid(uint32_t route) {
    uint32_t a = route & UINT32_C(0xff);
    uint32_t b = (route >> 8) & UINT32_C(0xff);
    uint32_t combine = (route >> 16) & UINT32_C(0xff);
    if ((route & UINT32_C(0xff000000)) != 0u) return 0;
    if (a > ODM_REACTION_SOURCE_MAX || b > ODM_REACTION_SOURCE_MAX ||
        combine > ODM_REACTION_COMBINE_ABS_DIFF) return 0;
    if (combine == ODM_REACTION_COMBINE_A_ONLY) {
        if (b != ODM_REACTION_SOURCE_NONE) return 0;
    } else {
        if (a == ODM_REACTION_SOURCE_NONE || b == ODM_REACTION_SOURCE_NONE) return 0;
    }
    return 1;
}

static int reaction_legacy_zero(const odm_reaction_config *r) {
    const uint32_t *p;
    size_t i;
    if (!r) return 0;
    p = (const uint32_t *)(const void *)r;
    for (i = 0u; i < sizeof(*r) / sizeof(uint32_t); ++i) {
        if (p[i] != 0u) return 0;
    }
    return 1;
}

int odm_layered_reaction_validate(const odm_reaction_config *r) {
    if (!r) return 0;
    if (reaction_legacy_zero(r)) return 1;
    if (r->schema_version != ODM_REACTION_SCHEMA_VERSION || r->flags != 0u)
        return 0;
    return reaction_route_valid(r->background_zoom_route) &&
           reaction_route_valid(r->background_warp_route) &&
           reaction_route_valid(r->background_depth_route) &&
           reaction_route_valid(r->core_scale_route) &&
           reaction_route_valid(r->field_gain_route) &&
           reaction_route_valid(r->particle_density_route);
}

odm_status odm_reaction_route_make(uint32_t source_a,
                                   uint32_t source_b,
                                   uint32_t combine,
                                   uint32_t *out_route) {
    uint32_t r;
    if (!out_route || source_a > ODM_REACTION_SOURCE_MAX ||
        source_b > ODM_REACTION_SOURCE_MAX || combine > ODM_REACTION_COMBINE_ABS_DIFF)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (combine == ODM_REACTION_COMBINE_A_ONLY) {
        if (source_b != ODM_REACTION_SOURCE_NONE) return ODM_STATUS_INVALID_ARGUMENT;
    } else if (source_a == ODM_REACTION_SOURCE_NONE || source_b == ODM_REACTION_SOURCE_NONE) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    r = source_a | (source_b << 8) | (combine << 16);
    if (!reaction_route_valid(r)) return ODM_STATUS_INVALID_DATA;
    *out_route = r;
    return ODM_STATUS_OK;
}

static uint32_t route_make_const(uint32_t a, uint32_t b, uint32_t combine) {
    return a | (b << 8) | (combine << 16);
}

odm_status odm_layered_reaction_init_default(odm_reaction_config *out_reaction) {
    odm_reaction_config r;
    if (!out_reaction) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&r, 0, sizeof(r));
    r.schema_version = ODM_REACTION_SCHEMA_VERSION;
    r.background_zoom_route = route_make_const(ODM_REACTION_SOURCE_GRID,
                                                ODM_REACTION_SOURCE_NONE,
                                                ODM_REACTION_COMBINE_A_ONLY);
    r.background_warp_route = route_make_const(ODM_REACTION_SOURCE_FRACTURE,
                                                ODM_REACTION_SOURCE_DIRECTOR_EDGE,
                                                ODM_REACTION_COMBINE_MAX);
    r.background_depth_route = route_make_const(ODM_REACTION_SOURCE_DIRECTOR_DEPTH,
                                                 ODM_REACTION_SOURCE_NONE,
                                                 ODM_REACTION_COMBINE_A_ONLY);
    r.core_scale_route = route_make_const(ODM_REACTION_SOURCE_CORE_BREATH,
                                          ODM_REACTION_SOURCE_NONE,
                                          ODM_REACTION_COMBINE_A_ONLY);
    r.field_gain_route = route_make_const(ODM_REACTION_SOURCE_RADIAL_GAIN,
                                          ODM_REACTION_SOURCE_PARTICLES,
                                          ODM_REACTION_COMBINE_MAX);
    r.particle_density_route = route_make_const(ODM_REACTION_SOURCE_PARTICLES,
                                                ODM_REACTION_SOURCE_NONE,
                                                ODM_REACTION_COMBINE_A_ONLY);
    if (!odm_layered_reaction_validate(&r)) return ODM_STATUS_INVALID_DATA;
    *out_reaction = r;
    return ODM_STATUS_OK;
}

odm_status odm_layered_config_set_reaction_route(odm_layered_config *config,
                                                  uint32_t target,
                                                  uint32_t route) {
    odm_layered_config c;
    odm_status st;
    if (!config || !reaction_route_valid(route) ||
        target < ODM_REACTION_TARGET_BACKGROUND_ZOOM ||
        target > ODM_REACTION_TARGET_PARTICLE_DENSITY)
        return ODM_STATUS_INVALID_ARGUMENT;
    c = *config;
    if (reaction_legacy_zero(&c.reaction)) {
        st = odm_layered_reaction_init_default(&c.reaction);
        if (st != ODM_STATUS_OK) return st;
    }
    switch (target) {
        case ODM_REACTION_TARGET_BACKGROUND_ZOOM: c.reaction.background_zoom_route = route; break;
        case ODM_REACTION_TARGET_BACKGROUND_WARP: c.reaction.background_warp_route = route; break;
        case ODM_REACTION_TARGET_BACKGROUND_DEPTH: c.reaction.background_depth_route = route; break;
        case ODM_REACTION_TARGET_CORE_SCALE: c.reaction.core_scale_route = route; break;
        case ODM_REACTION_TARGET_FIELD_GAIN: c.reaction.field_gain_route = route; break;
        case ODM_REACTION_TARGET_PARTICLE_DENSITY: c.reaction.particle_density_route = route; break;
        default: return ODM_STATUS_INVALID_ARGUMENT;
    }
    st = odm_layered_config_validate(&c);
    if (st != ODM_STATUS_OK) return st;
    *config = c;
    return ODM_STATUS_OK;
}

static uint32_t mul_q31u(uint32_t a, uint32_t b) {
    uint64_t p = (uint64_t)a * (uint64_t)b + (uint64_t)INT32_MAX / 2u;
    p /= (uint64_t)INT32_MAX;
    return p > (uint64_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)p;
}

static odm_status source_value(uint32_t source,
                               const odm_composition_frame_state *comp,
                               const odm_director_frame_state *dir,
                               uint32_t *out_value) {
    uint32_t value;
    if (!comp || !dir || !out_value) return ODM_STATUS_INVALID_ARGUMENT;
    switch (source) {
        case ODM_REACTION_SOURCE_NONE: value = 0u; break;
        case ODM_REACTION_SOURCE_CORE_BREATH: value = comp->core_breath_q31; break;
        case ODM_REACTION_SOURCE_GRID: value = comp->grid_q31; break;
        case ODM_REACTION_SOURCE_PARTICLES: value = comp->particles_q31; break;
        case ODM_REACTION_SOURCE_RADIAL_GAIN: value = comp->radial_gain_q31; break;
        case ODM_REACTION_SOURCE_FRACTURE: value = comp->fracture_q31; break;
        case ODM_REACTION_SOURCE_MEMORY_ECHO: value = comp->memory_echo_q31; break;
        case ODM_REACTION_SOURCE_HALO: value = comp->halo_q31; break;
        case ODM_REACTION_SOURCE_LATERAL_FIELD: value = comp->lateral_field_q31; break;
        case ODM_REACTION_SOURCE_ACCENT: value = comp->accent_q31; break;
        case ODM_REACTION_SOURCE_BAND_0: value = comp->band_q31[0]; break;
        case ODM_REACTION_SOURCE_BAND_1: value = comp->band_q31[1]; break;
        case ODM_REACTION_SOURCE_BAND_2: value = comp->band_q31[2]; break;
        case ODM_REACTION_SOURCE_BAND_3: value = comp->band_q31[3]; break;
        case ODM_REACTION_SOURCE_BAND_4: value = comp->band_q31[4]; break;
        case ODM_REACTION_SOURCE_BAND_5: value = comp->band_q31[5]; break;
        case ODM_REACTION_SOURCE_DIRECTOR_ENERGY: value = dir->macro_energy_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_NOVELTY: value = dir->macro_novelty_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_DEPTH: value = dir->depth_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_EDGE: value = dir->edge_activity_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_ORBIT: value = dir->orbit_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_WINGS: value = dir->wings_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_MEMORY: value = dir->memory_panels_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_TUNNEL: value = dir->tunnel_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_BACKDROP: value = dir->backdrop_q31; break;
        case ODM_REACTION_SOURCE_DIRECTOR_ARCHITECTURE: value = dir->architecture_q31; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    if (value > (uint32_t)INT32_MAX) return ODM_STATUS_INVALID_DATA;
    *out_value = value;
    return ODM_STATUS_OK;
}

odm_status odm_layered_reaction_eval(uint32_t route,
                                      const odm_composition_frame_state *comp,
                                      const odm_director_frame_state *dir,
                                      uint32_t *out_value) {
    uint32_t a = 0u, b = 0u, combine, value;
    odm_status st;
    if (!comp || !dir || !out_value) return ODM_STATUS_INVALID_ARGUMENT;
    if (!reaction_route_valid(route)) return ODM_STATUS_INVALID_DATA;
    st = source_value(route & UINT32_C(0xff), comp, dir, &a);
    if (st != ODM_STATUS_OK) return st;
    st = source_value((route >> 8) & UINT32_C(0xff), comp, dir, &b);
    if (st != ODM_STATUS_OK) return st;
    combine = (route >> 16) & UINT32_C(0xff);
    switch (combine) {
        case ODM_REACTION_COMBINE_A_ONLY: value = a; break;
        case ODM_REACTION_COMBINE_MAX: value = a > b ? a : b; break;
        case ODM_REACTION_COMBINE_MIN: value = a < b ? a : b; break;
        case ODM_REACTION_COMBINE_AVERAGE:
            value = (uint32_t)(((uint64_t)a + (uint64_t)b + UINT64_C(1)) / UINT64_C(2));
            break;
        case ODM_REACTION_COMBINE_MUL: value = mul_q31u(a, b); break;
        case ODM_REACTION_COMBINE_ABS_DIFF: value = a > b ? a - b : b - a; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    *out_value = value;
    return ODM_STATUS_OK;
}

void odm_layered_reaction_effective(const odm_layered_config *config,
                                    odm_reaction_config *out_reaction) {
    if (!config || !out_reaction) return;
    if (reaction_legacy_zero(&config->reaction)) {
        (void)odm_layered_reaction_init_default(out_reaction);
    } else {
        *out_reaction = config->reaction;
    }
}

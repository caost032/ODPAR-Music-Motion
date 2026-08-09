#include "odm_score.h"
#include "odm_music_map.h"
#include "odm_time.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define ODM_ROLE_BIT(role) (UINT32_C(1) << (role))
#define ODM_SCORE_TRANSFORM_ABS_MICRO INT64_C(64000000)
#define ODM_SCORE_SCALE_MAX_MICRO INT64_C(16000000)
#define ODM_SCORE_MOD_ABS_MICRO INT64_C(64000000)

static const odm_visual_capability_info odm_caps[] = {
    {ODM_CAP_BLACK_VOID, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_ENVIRONMENT), 0u},
    {ODM_CAP_PRIMARY_EMPTY, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_PRIMARY), 0u},
    {ODM_CAP_PRIMARY_IMAGE, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_PRIMARY), ODM_SCORE_RESOURCE_IMAGE},
    {ODM_CAP_SUBJECT_IMAGE, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_SUBJECT), ODM_SCORE_RESOURCE_IMAGE},
    {ODM_CAP_LOOK_IDENTITY, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_LOOK), 0u},
    {ODM_CAP_PRIMARY_VIDEO, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_PRIMARY), ODM_SCORE_RESOURCE_VIDEO},
    {ODM_CAP_SUBJECT_VIDEO, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_SUBJECT), ODM_SCORE_RESOURCE_VIDEO},
    {ODM_CAP_AUTOMATION, 1u, 0u, ODM_STATELESS, 0u, 0u},
    {ODM_CAP_MUSIC_MODULATOR, 1u, 0u, ODM_STATELESS, 0u, 0u},
    {ODM_CAP_CROSSFADE, 1u, 0u, ODM_STATELESS, 0u, 0u},
    {ODM_CAP_STATE_TRANSFER, 1u, 0u, ODM_STATELESS, 0u, 0u},
    {ODM_CAP_TIMELINE_CUE, 1u, 0u, ODM_STATELESS, 0u, 0u},
    {ODM_CAP_GRID_PROOF, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_EFFECT), 0u},
    {ODM_CAP_HALO, 1u, 0u, ODM_STATELESS,
     ODM_ROLE_BIT(ODM_SCORE_NODE_EFFECT), 0u},
    {ODM_CAP_RESIDUAL_EVENT_FIELD, 1u, 0u, ODM_EVENT_HISTORY,
     ODM_ROLE_BIT(ODM_SCORE_NODE_EFFECT), 0u},
    {ODM_CAP_DETERMINISTIC_PARTICLES, 1u, 0u, ODM_EVENT_HISTORY,
     ODM_ROLE_BIT(ODM_SCORE_NODE_PARTICLE), 0u},
};

static int reserved_zero(const uint32_t *values, uint32_t count) {
    uint32_t i;
    for (i = 0u; i < count; ++i) if (values[i] != 0u) return 0;
    return 1;
}

static int digest_nonzero(const odm_sha256_digest *digest) {
    uint32_t i;
    for (i = 0u; i < ODM_SHA256_DIGEST_BYTES; ++i) {
        if (digest->bytes[i] != 0u) return 1;
    }
    return 0;
}

static int abs_micro_within(int64_t value, int64_t limit) {
    if (value >= 0) return value <= limit;
    if (value == INT64_MIN) return 0;
    return -value <= limit;
}

static const odm_visual_capability_info *find_capability(uint32_t id) {
    uint32_t i;
    for (i = 0u; i < (uint32_t)(sizeof(odm_caps) / sizeof(odm_caps[0])); ++i) {
        if (odm_caps[i].capability_id == id) return &odm_caps[i];
    }
    return NULL;
}

const odm_visual_capability_info *odm_visual_capabilities(uint32_t *out_count) {
    if (out_count) *out_count = (uint32_t)(sizeof(odm_caps) / sizeof(odm_caps[0]));
    return odm_caps;
}

static const odm_score_resource *find_resource(const odm_score_view *score,
                                                uint32_t id) {
    uint32_t lo = 0u, hi = score->header->resource_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t got = score->resources[mid].resource_id;
        if (got == id) return &score->resources[mid];
        if (got < id) lo = mid + 1u; else hi = mid;
    }
    return NULL;
}

static const odm_score_act *find_act(const odm_score_view *score, uint32_t id) {
    uint32_t lo = 0u, hi = score->header->act_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t got = score->acts[mid].act_id;
        if (got == id) return &score->acts[mid];
        if (got < id) lo = mid + 1u; else hi = mid;
    }
    return NULL;
}

static const odm_score_scene *find_scene(const odm_score_view *score,
                                         uint32_t id) {
    uint32_t lo = 0u, hi = score->header->scene_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t got = score->scenes[mid].scene_id;
        if (got == id) return &score->scenes[mid];
        if (got < id) lo = mid + 1u; else hi = mid;
    }
    return NULL;
}

static const odm_score_node *find_node(const odm_score_view *score, uint32_t id) {
    uint32_t lo = 0u, hi = score->header->node_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t got = score->nodes[mid].node_id;
        if (got == id) return &score->nodes[mid];
        if (got < id) lo = mid + 1u; else hi = mid;
    }
    return NULL;
}

static int valid_parameter(uint32_t parameter) {
    return parameter >= ODM_PARAM_X && parameter <= ODM_PARAM_PHASE;
}

static int valid_mod_source(uint32_t source) {
    return source >= ODM_MOD_RMS_LEFT && source <= ODM_MOD_SILENCE;
}

static int value_valid_for_parameter(uint32_t parameter, int64_t value) {
    switch (parameter) {
        case ODM_PARAM_X:
        case ODM_PARAM_Y:
            return abs_micro_within(value, ODM_SCORE_TRANSFORM_ABS_MICRO);
        case ODM_PARAM_SCALE_X:
        case ODM_PARAM_SCALE_Y:
            return value >= 0 && value <= ODM_SCORE_SCALE_MAX_MICRO;
        case ODM_PARAM_OPACITY:
            return value >= 0 && value <= ODM_MICRO_ONE;
        case ODM_PARAM_PHASE:
            return 1;
        default:
            return 0;
    }
}

static odm_status validate_header(const odm_score_view *score) {
    const odm_score_header *h;
    odm_timeline timeline;
    if (!score || !score->header) return ODM_STATUS_INVALID_ARGUMENT;
    h = score->header;
    if (h->schema_version_major != ODM_SCORE_SCHEMA_MAJOR ||
        h->schema_version_minor != ODM_SCORE_SCHEMA_MINOR) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    if (h->flags != 0u || !reserved_zero(h->reserved, 3u) ||
        !odm_time_fps_supported(h->fps) || h->project_end_sample <= 0 ||
        h->width < 2u || h->height < 2u || h->width > 16384u ||
        h->height > 16384u || (h->width & 1u) != 0u || (h->height & 1u) != 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (h->resource_count > ODM_SCORE_MAX_RESOURCES ||
        h->act_count == 0u || h->act_count > ODM_SCORE_MAX_ACTS ||
        h->scene_count == 0u || h->scene_count > ODM_SCORE_MAX_SCENES ||
        h->node_count == 0u || h->node_count > ODM_SCORE_MAX_NODES ||
        h->cue_count > ODM_SCORE_MAX_CUES ||
        h->modulator_count > ODM_SCORE_MAX_MODULATORS ||
        h->automation_count > ODM_SCORE_MAX_AUTOMATION ||
        h->transition_count > ODM_SCORE_MAX_TRANSITIONS ||
        h->transfer_count > ODM_SCORE_MAX_TRANSFERS) {
        return ODM_STATUS_BUDGET_EXCEEDED;
    }
#define NEED_PTR(count, ptr) do { if ((count) != 0u && !(ptr)) return ODM_STATUS_INVALID_ARGUMENT; } while (0)
    NEED_PTR(h->resource_count, score->resources);
    NEED_PTR(h->act_count, score->acts);
    NEED_PTR(h->scene_count, score->scenes);
    NEED_PTR(h->node_count, score->nodes);
    NEED_PTR(h->cue_count, score->cues);
    NEED_PTR(h->modulator_count, score->modulators);
    NEED_PTR(h->automation_count, score->automation);
    NEED_PTR(h->transition_count, score->transitions);
    NEED_PTR(h->transfer_count, score->transfers);
#undef NEED_PTR
    return odm_time_resolve_timeline(h->project_end_sample, h->fps, &timeline);
}

static odm_status validate_resources(const odm_score_view *score) {
    uint32_t i, previous = 0u;
    for (i = 0u; i < score->header->resource_count; ++i) {
        const odm_score_resource *r = &score->resources[i];
        if (r->resource_id == 0u || r->resource_id <= previous || r->flags != 0u ||
            r->reserved != 0u ||
            (r->kind != ODM_SCORE_RESOURCE_IMAGE && r->kind != ODM_SCORE_RESOURCE_VIDEO) ||
            !digest_nonzero(&r->content_sha256)) {
            return ODM_STATUS_INVALID_DATA;
        }
        previous = r->resource_id;
    }
    return ODM_STATUS_OK;
}

static odm_status validate_acts(const odm_score_view *score) {
    uint32_t i, previous_id = 0u;
    int64_t expected_start = 0;
    for (i = 0u; i < score->header->act_count; ++i) {
        const odm_score_act *a = &score->acts[i];
        if (a->act_id == 0u || a->act_id <= previous_id || a->flags != 0u ||
            !reserved_zero(a->reserved, 4u) || a->start_sample != expected_start ||
            a->end_sample <= a->start_sample ||
            a->end_sample > score->header->project_end_sample) {
            return ODM_STATUS_INVALID_DATA;
        }
        previous_id = a->act_id;
        expected_start = a->end_sample;
    }
    return expected_start == score->header->project_end_sample ? ODM_STATUS_OK :
                                                                 ODM_STATUS_INVALID_DATA;
}

static odm_status validate_scenes(const odm_score_view *score,
                                  uint32_t *out_overlap_count) {
    uint32_t i, previous_id = 0u, overlap_count = 0u;
    uint32_t act_index = 0u;
    int64_t coverage_end = 0;
    for (i = 0u; i < score->header->scene_count; ++i) {
        const odm_score_scene *s = &score->scenes[i];
        const odm_score_act *a;
        if (s->scene_id == 0u || s->scene_id <= previous_id || s->flags != 0u ||
            s->reserved != 0u || s->environment_node_id == 0u ||
            s->primary_node_id == 0u || s->end_sample <= s->start_sample) {
            return ODM_STATUS_INVALID_DATA;
        }
        a = find_act(score, s->act_id);
        if (!a || s->start_sample < a->start_sample || s->end_sample > a->end_sample) {
            return ODM_STATUS_INVALID_DATA;
        }
        while (act_index < score->header->act_count &&
               score->acts[act_index].act_id < s->act_id) {
            if (coverage_end != score->acts[act_index].end_sample) return ODM_STATUS_INVALID_DATA;
            ++act_index;
            if (act_index < score->header->act_count) coverage_end = score->acts[act_index].start_sample;
        }
        if (act_index >= score->header->act_count || score->acts[act_index].act_id != s->act_id) {
            return ODM_STATUS_INVALID_DATA;
        }
        if (i == 0u || score->scenes[i - 1u].act_id != s->act_id) {
            if (s->start_sample != a->start_sample) return ODM_STATUS_INVALID_DATA;
            coverage_end = s->end_sample;
        } else {
            const odm_score_scene *prev = &score->scenes[i - 1u];
            if (s->start_sample > prev->end_sample) return ODM_STATUS_INVALID_DATA;
            if (s->start_sample < prev->end_sample) ++overlap_count;
            if (i >= 2u && score->scenes[i - 2u].act_id == s->act_id &&
                s->start_sample < score->scenes[i - 2u].end_sample) {
                return ODM_STATUS_INVALID_DATA; /* triple-scene mix is not v0 */
            }
            if (s->end_sample > coverage_end) coverage_end = s->end_sample;
        }
        previous_id = s->scene_id;
    }
    if (act_index >= score->header->act_count ||
        coverage_end != score->acts[act_index].end_sample) return ODM_STATUS_INVALID_DATA;
    while (++act_index < score->header->act_count) return ODM_STATUS_INVALID_DATA;
    *out_overlap_count = overlap_count;
    return ODM_STATUS_OK;
}

static odm_status validate_nodes(const odm_score_view *score) {
    uint32_t i, previous_id = 0u, previous_scene = 0u;
    uint32_t scene_index = 0u, environment_count = 0u, primary_count = 0u;
    for (i = 0u; i < score->header->node_count; ++i) {
        const odm_score_node *n = &score->nodes[i];
        const odm_score_scene *s;
        const odm_visual_capability_info *cap;
        if (n->node_id == 0u || n->node_id <= previous_id || n->scene_id < previous_scene ||
            n->role < ODM_SCORE_NODE_ENVIRONMENT || n->role > ODM_SCORE_NODE_PARTICLE ||
            n->flags != 0u || !reserved_zero(n->reserved, 2u) ||
            !abs_micro_within(n->x_micro, ODM_SCORE_TRANSFORM_ABS_MICRO) ||
            !abs_micro_within(n->y_micro, ODM_SCORE_TRANSFORM_ABS_MICRO) ||
            n->scale_x_micro < 0 || n->scale_x_micro > ODM_SCORE_SCALE_MAX_MICRO ||
            n->scale_y_micro < 0 || n->scale_y_micro > ODM_SCORE_SCALE_MAX_MICRO ||
            n->opacity_micro < 0 || n->opacity_micro > ODM_MICRO_ONE) {
            return ODM_STATUS_INVALID_DATA;
        }
        s = find_scene(score, n->scene_id);
        if (!s || n->start_sample < s->start_sample || n->end_sample > s->end_sample ||
            n->end_sample <= n->start_sample) return ODM_STATUS_INVALID_DATA;
        cap = find_capability(n->capability_id);
        if (!cap || cap->version_major != n->capability_major ||
            cap->version_minor != n->capability_minor || cap->state_model != n->state_model ||
            (cap->allowed_roles_mask & ODM_ROLE_BIT(n->role)) == 0u) {
            return ODM_STATUS_UNSUPPORTED;
        }
        if ((n->state_model == ODM_STATELESS && n->state_slot != 0u) ||
            (n->state_model != ODM_STATELESS && n->state_slot == 0u) ||
            (n->entry_kind != ODM_ENTRY_NONE && n->entry_kind != ODM_ENTRY_FADE_SCALE) ||
            (n->exit_kind != ODM_EXIT_NONE && n->exit_kind != ODM_EXIT_FADE_SCALE) ||
            (n->entry_kind == ODM_ENTRY_NONE && n->entry_duration_samples != 0u) ||
            (n->exit_kind == ODM_EXIT_NONE && n->exit_duration_samples != 0u) ||
            (n->entry_kind != ODM_ENTRY_NONE && n->entry_duration_samples == 0u) ||
            (n->exit_kind != ODM_EXIT_NONE && n->exit_duration_samples == 0u) ||
            (uint64_t)n->entry_duration_samples > (uint64_t)(n->end_sample - n->start_sample) ||
            (uint64_t)n->exit_duration_samples > (uint64_t)(n->end_sample - n->start_sample)) {
            return ODM_STATUS_INVALID_DATA;
        }
        if (cap->resource_kind != 0u) {
            const odm_score_resource *r = find_resource(score, n->resource_id);
            if (!r || r->kind != cap->resource_kind) return ODM_STATUS_INVALID_DATA;
        } else if (n->resource_id != 0u) {
            return ODM_STATUS_INVALID_DATA;
        }
        while (scene_index < score->header->scene_count &&
               score->scenes[scene_index].scene_id < n->scene_id) {
            if (environment_count != 1u || primary_count != 1u) return ODM_STATUS_INVALID_DATA;
            ++scene_index; environment_count = 0u; primary_count = 0u;
        }
        if (scene_index >= score->header->scene_count ||
            score->scenes[scene_index].scene_id != n->scene_id) return ODM_STATUS_INVALID_DATA;
        if (n->role == ODM_SCORE_NODE_ENVIRONMENT) {
            ++environment_count;
            if (n->node_id != s->environment_node_id || n->capability_id != ODM_CAP_BLACK_VOID ||
                n->start_sample != s->start_sample || n->end_sample != s->end_sample) {
                return ODM_STATUS_INVALID_DATA;
            }
        }
        if (n->role == ODM_SCORE_NODE_PRIMARY) {
            ++primary_count;
            if (n->node_id != s->primary_node_id ||
                (n->capability_id != ODM_CAP_PRIMARY_EMPTY &&
                 n->capability_id != ODM_CAP_PRIMARY_IMAGE &&
                 n->capability_id != ODM_CAP_PRIMARY_VIDEO) ||
                n->start_sample != s->start_sample || n->end_sample != s->end_sample) {
                return ODM_STATUS_INVALID_DATA;
            }
        }
        previous_id = n->node_id;
        previous_scene = n->scene_id;
    }
    if (scene_index >= score->header->scene_count) return ODM_STATUS_INVALID_DATA;
    if (environment_count != 1u || primary_count != 1u) return ODM_STATUS_INVALID_DATA;
    while (++scene_index < score->header->scene_count) return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

static odm_status validate_cues(const odm_score_view *score) {
    uint32_t i, previous_id = 0u;
    int64_t previous_sample = -1;
    for (i = 0u; i < score->header->cue_count; ++i) {
        const odm_score_cue *c = &score->cues[i];
        const odm_score_scene *s = find_scene(score, c->scene_id);
        if (c->cue_id == 0u || c->cue_id <= previous_id || c->sample < previous_sample ||
            c->flags != 0u || !reserved_zero(c->reserved, 2u) || !s ||
            c->sample < s->start_sample || c->sample >= s->end_sample ||
            (c->kind != ODM_SCORE_CUE_GENERIC &&
             c->kind != ODM_SCORE_CUE_BEAT_AUTHORED &&
             c->kind != ODM_SCORE_CUE_SECTION_AUTHORED)) return ODM_STATUS_INVALID_DATA;
        previous_id = c->cue_id; previous_sample = c->sample;
    }
    return ODM_STATUS_OK;
}

static odm_status validate_modulators(const odm_score_view *score) {
    uint32_t i, previous_id = 0u, prev_node = 0u, prev_param = 0u;
    for (i = 0u; i < score->header->modulator_count; ++i) {
        const odm_score_modulator *m = &score->modulators[i];
        if (m->modulator_id == 0u || m->modulator_id <= previous_id ||
            m->node_id < prev_node || (m->node_id == prev_node && m->target_parameter < prev_param) ||
            !find_node(score, m->node_id) || !valid_parameter(m->target_parameter) ||
            m->target_parameter == ODM_PARAM_PHASE || !valid_mod_source(m->source) ||
            (m->source == ODM_MOD_BAND_ENERGY && m->band_index >= ODM_MUSIC_BAND_COUNT) ||
            (m->source != ODM_MOD_BAND_ENERGY && m->band_index != 0u) || m->flags != 0u ||
            !reserved_zero(m->reserved, 4u) ||
            !abs_micro_within(m->scale_micro, ODM_SCORE_MOD_ABS_MICRO) ||
            !abs_micro_within(m->bias_micro, ODM_SCORE_MOD_ABS_MICRO)) {
            return ODM_STATUS_INVALID_DATA;
        }
        previous_id = m->modulator_id; prev_node = m->node_id; prev_param = m->target_parameter;
    }
    return ODM_STATUS_OK;
}

static odm_status validate_automation(const odm_score_view *score) {
    uint32_t i, previous_id = 0u, prev_node = 0u, prev_param = 0u;
    int64_t previous_end = -1;
    for (i = 0u; i < score->header->automation_count; ++i) {
        const odm_score_automation *a = &score->automation[i];
        const odm_score_node *n = find_node(score, a->node_id);
        int same_target = (a->node_id == prev_node && a->target_parameter == prev_param);
        if (a->automation_id == 0u || a->automation_id <= previous_id || !n ||
            a->node_id < prev_node || (a->node_id == prev_node && a->target_parameter < prev_param) ||
            !valid_parameter(a->target_parameter) ||
            (a->interpolation != ODM_AUTOMATION_STEP && a->interpolation != ODM_AUTOMATION_LINEAR) ||
            (a->target_parameter == ODM_PARAM_PHASE && a->interpolation != ODM_AUTOMATION_STEP) ||
            (a->interpolation == ODM_AUTOMATION_STEP && a->from_micro != a->to_micro) ||
            a->flags != 0u || a->reserved0 != 0u || !reserved_zero(a->reserved, 2u) ||
            a->start_sample < n->start_sample || a->end_sample > n->end_sample ||
            a->end_sample <= a->start_sample ||
            !value_valid_for_parameter(a->target_parameter, a->from_micro) ||
            !value_valid_for_parameter(a->target_parameter, a->to_micro) ||
            (same_target && a->start_sample < previous_end)) {
            return ODM_STATUS_INVALID_DATA;
        }
        previous_id = a->automation_id; prev_node = a->node_id; prev_param = a->target_parameter;
        previous_end = a->end_sample;
    }
    return ODM_STATUS_OK;
}

static odm_status validate_transitions(const odm_score_view *score,
                                       uint32_t overlap_count) {
    uint32_t i, t = 0u, previous_id = 0u;
    if (score->header->transition_count != overlap_count) return ODM_STATUS_INVALID_DATA;
    for (i = 1u; i < score->header->scene_count; ++i) {
        const odm_score_scene *prev = &score->scenes[i - 1u];
        const odm_score_scene *cur = &score->scenes[i];
        if (prev->act_id == cur->act_id && cur->start_sample < prev->end_sample) {
            const odm_score_transition *tr;
            if (t >= score->header->transition_count) return ODM_STATUS_INVALID_DATA;
            tr = &score->transitions[t++];
            if (tr->transition_id == 0u || tr->transition_id <= previous_id ||
                tr->from_scene_id != prev->scene_id || tr->to_scene_id != cur->scene_id ||
                tr->kind != ODM_TRANSITION_CROSSFADE || tr->start_sample != cur->start_sample ||
                tr->end_sample != prev->end_sample || tr->end_sample <= tr->start_sample ||
                tr->flags != 0u || !reserved_zero(tr->reserved, 3u)) return ODM_STATUS_INVALID_DATA;
            previous_id = tr->transition_id;
        }
    }
    return t == score->header->transition_count ? ODM_STATUS_OK : ODM_STATUS_INVALID_DATA;
}

static int scene_has_state_slot(const odm_score_view *score, uint32_t scene_id,
                                uint32_t slot) {
    uint32_t i;
    for (i = 0u; i < score->header->node_count; ++i) {
        const odm_score_node *n = &score->nodes[i];
        if (n->scene_id == scene_id && n->state_slot == slot &&
            n->state_model != ODM_STATELESS) return 1;
    }
    return 0;
}

static odm_status validate_transfers(const odm_score_view *score) {
    uint32_t i, previous_id = 0u, previous_to = 0u, previous_slot = 0u;
    int64_t previous_sample = -1;
    for (i = 0u; i < score->header->transfer_count; ++i) {
        const odm_score_state_transfer *tr = &score->transfers[i];
        const odm_score_scene *from = find_scene(score, tr->from_scene_id);
        const odm_score_scene *to = find_scene(score, tr->to_scene_id);
        if (tr->transfer_id == 0u || tr->transfer_id <= previous_id || tr->state_slot == 0u ||
            tr->at_sample < previous_sample || !from || !to || from->scene_id == to->scene_id ||
            from->start_sample >= to->start_sample ||
            (tr->mode != ODM_TRANSFER_COPY && tr->mode != ODM_TRANSFER_RESET) ||
            tr->at_sample != to->start_sample || tr->flags != 0u ||
            !reserved_zero(tr->reserved, 4u) ||
            !scene_has_state_slot(score, to->scene_id, tr->state_slot) ||
            (tr->mode == ODM_TRANSFER_COPY &&
             !scene_has_state_slot(score, from->scene_id, tr->state_slot))) return ODM_STATUS_INVALID_DATA;
        if (tr->at_sample == previous_sample &&
            (tr->to_scene_id < previous_to ||
             (tr->to_scene_id == previous_to && tr->state_slot <= previous_slot)))
            return ODM_STATUS_INVALID_DATA;
        previous_id = tr->transfer_id; previous_sample = tr->at_sample;
        previous_to = tr->to_scene_id; previous_slot = tr->state_slot;
    }
    return ODM_STATUS_OK;
}

odm_status odm_score_validate(const odm_score_view *score) {
    odm_status status;
    uint32_t overlap_count = 0u;
    status = validate_header(score); if (status != ODM_STATUS_OK) return status;
    status = validate_resources(score); if (status != ODM_STATUS_OK) return status;
    status = validate_acts(score); if (status != ODM_STATUS_OK) return status;
    status = validate_scenes(score, &overlap_count); if (status != ODM_STATUS_OK) return status;
    status = validate_nodes(score); if (status != ODM_STATUS_OK) return status;
    status = validate_cues(score); if (status != ODM_STATUS_OK) return status;
    status = validate_modulators(score); if (status != ODM_STATUS_OK) return status;
    status = validate_automation(score); if (status != ODM_STATUS_OK) return status;
    status = validate_transitions(score, overlap_count); if (status != ODM_STATUS_OK) return status;
    return validate_transfers(score);
}

static void le32(uint8_t out[4], uint32_t v) {
    out[0]=(uint8_t)v; out[1]=(uint8_t)(v>>8); out[2]=(uint8_t)(v>>16); out[3]=(uint8_t)(v>>24);
}
static void le64(uint8_t out[8], uint64_t v) {
    uint32_t i; for (i=0u;i<8u;++i) out[i]=(uint8_t)(v>>(i*8u));
}
static odm_status hu32(odm_sha256_context *c, uint32_t v) { uint8_t b[4]; le32(b,v); return odm_sha256_update(c,b,4u); }
static odm_status hi32(odm_sha256_context *c, int32_t v) { return hu32(c,(uint32_t)v); }
static odm_status hu64(odm_sha256_context *c, uint64_t v) { uint8_t b[8]; le64(b,v); return odm_sha256_update(c,b,8u); }
static odm_status hi64(odm_sha256_context *c, int64_t v) { return hu64(c,(uint64_t)v); }
#define H(expr) do { odm_status _s=(expr); if (_s!=ODM_STATUS_OK) return _s; } while(0)

odm_status odm_score_hash(const odm_score_view *score,
                          odm_sha256_digest *out_score_sha256) {
    odm_sha256_context c = ODM_SHA256_CONTEXT_INITIALIZER;
    const odm_score_header *h; uint32_t i;
    static const uint8_t domain[] = {'O','D','P','A','R','_','S','C','O','R','E','_','V','1',0};
    odm_status status = odm_score_validate(score);
    if (status != ODM_STATUS_OK) return status;
    if (!out_score_sha256) return ODM_STATUS_INVALID_ARGUMENT;
    h=score->header; H(odm_sha256_init(&c)); H(odm_sha256_update(&c,domain,sizeof(domain)));
    H(hu32(&c,h->schema_version_major)); H(hu32(&c,h->schema_version_minor)); H(hu32(&c,h->flags)); H(hi32(&c,h->fps));
    H(hu32(&c,h->width)); H(hu32(&c,h->height)); H(hi64(&c,h->project_end_sample)); H(hu64(&c,h->project_seed));
    H(hu32(&c,h->resource_count)); H(hu32(&c,h->act_count)); H(hu32(&c,h->scene_count)); H(hu32(&c,h->node_count));
    H(hu32(&c,h->cue_count)); H(hu32(&c,h->modulator_count)); H(hu32(&c,h->automation_count)); H(hu32(&c,h->transition_count)); H(hu32(&c,h->transfer_count));
    for(i=0u;i<h->resource_count;++i){const odm_score_resource*r=&score->resources[i];H(hu32(&c,r->resource_id));H(hu32(&c,r->kind));H(hu32(&c,r->flags));H(odm_sha256_update(&c,r->content_sha256.bytes,32u));}
    for(i=0u;i<h->act_count;++i){const odm_score_act*a=&score->acts[i];H(hu32(&c,a->act_id));H(hu32(&c,a->flags));H(hi64(&c,a->start_sample));H(hi64(&c,a->end_sample));}
    for(i=0u;i<h->scene_count;++i){const odm_score_scene*s=&score->scenes[i];H(hu32(&c,s->scene_id));H(hu32(&c,s->act_id));H(hu32(&c,s->environment_node_id));H(hu32(&c,s->primary_node_id));H(hi64(&c,s->start_sample));H(hi64(&c,s->end_sample));H(hu32(&c,s->flags));}
    for(i=0u;i<h->node_count;++i){const odm_score_node*n=&score->nodes[i];H(hu32(&c,n->node_id));H(hu32(&c,n->scene_id));H(hu32(&c,n->role));H(hu32(&c,n->capability_id));H(hu32(&c,n->capability_major));H(hu32(&c,n->capability_minor));H(hu32(&c,n->resource_id));H(hu32(&c,n->state_model));H(hi32(&c,n->z_index));H(hu32(&c,n->flags));H(hi64(&c,n->start_sample));H(hi64(&c,n->end_sample));H(hi64(&c,n->x_micro));H(hi64(&c,n->y_micro));H(hi64(&c,n->scale_x_micro));H(hi64(&c,n->scale_y_micro));H(hi64(&c,n->opacity_micro));H(hi64(&c,n->phase_microturns));H(hu64(&c,n->seed_domain));H(hu32(&c,n->state_slot));H(hu32(&c,n->entry_kind));H(hu32(&c,n->exit_kind));H(hu32(&c,n->entry_duration_samples));H(hu32(&c,n->exit_duration_samples));}
    for(i=0u;i<h->cue_count;++i){const odm_score_cue*q=&score->cues[i];H(hu32(&c,q->cue_id));H(hu32(&c,q->scene_id));H(hu32(&c,q->kind));H(hu32(&c,q->flags));H(hi64(&c,q->sample));}
    for(i=0u;i<h->modulator_count;++i){const odm_score_modulator*m=&score->modulators[i];H(hu32(&c,m->modulator_id));H(hu32(&c,m->node_id));H(hu32(&c,m->target_parameter));H(hu32(&c,m->source));H(hu32(&c,m->band_index));H(hu32(&c,m->flags));H(hi64(&c,m->scale_micro));H(hi64(&c,m->bias_micro));}
    for(i=0u;i<h->automation_count;++i){const odm_score_automation*a=&score->automation[i];H(hu32(&c,a->automation_id));H(hu32(&c,a->node_id));H(hu32(&c,a->target_parameter));H(hu32(&c,a->interpolation));H(hu32(&c,a->flags));H(hi64(&c,a->start_sample));H(hi64(&c,a->end_sample));H(hi64(&c,a->from_micro));H(hi64(&c,a->to_micro));}
    for(i=0u;i<h->transition_count;++i){const odm_score_transition*t=&score->transitions[i];H(hu32(&c,t->transition_id));H(hu32(&c,t->from_scene_id));H(hu32(&c,t->to_scene_id));H(hu32(&c,t->kind));H(hi64(&c,t->start_sample));H(hi64(&c,t->end_sample));H(hu32(&c,t->flags));}
    for(i=0u;i<h->transfer_count;++i){const odm_score_state_transfer*t=&score->transfers[i];H(hu32(&c,t->transfer_id));H(hu32(&c,t->from_scene_id));H(hu32(&c,t->to_scene_id));H(hu32(&c,t->state_slot));H(hu32(&c,t->mode));H(hu32(&c,t->flags));H(hi64(&c,t->at_sample));}
    return odm_sha256_final(&c,out_score_sha256);
}

static int score_uses_capability(const odm_score_view *score, uint32_t id) {
    uint32_t i;
    for(i=0u;i<score->header->node_count;++i) if(score->nodes[i].capability_id==id) return 1;
    if(id==ODM_CAP_AUTOMATION && score->header->automation_count!=0u) return 1;
    if(id==ODM_CAP_MUSIC_MODULATOR && score->header->modulator_count!=0u) return 1;
    if(id==ODM_CAP_CROSSFADE && score->header->transition_count!=0u) return 1;
    if(id==ODM_CAP_STATE_TRANSFER && score->header->transfer_count!=0u) return 1;
    if(id==ODM_CAP_TIMELINE_CUE && score->header->cue_count!=0u) return 1;
    return 0;
}

odm_status odm_score_capability_hash(const odm_score_view *score,
                                     odm_sha256_digest *out_capability_sha256) {
    odm_sha256_context c=ODM_SHA256_CONTEXT_INITIALIZER; uint32_t i,count=0u;
    static const uint8_t domain[]={'O','D','P','A','R','_','C','A','P','S','_','V','1',0};
    odm_status status=odm_score_validate(score); if(status!=ODM_STATUS_OK)return status;
    if(!out_capability_sha256)return ODM_STATUS_INVALID_ARGUMENT;
    for(i=0u;i<(uint32_t)(sizeof(odm_caps)/sizeof(odm_caps[0]));++i) if(score_uses_capability(score,odm_caps[i].capability_id))++count;
    H(odm_sha256_init(&c));H(odm_sha256_update(&c,domain,sizeof(domain)));H(hu32(&c,count));
    for(i=0u;i<(uint32_t)(sizeof(odm_caps)/sizeof(odm_caps[0]));++i){const odm_visual_capability_info*x=&odm_caps[i];if(!score_uses_capability(score,x->capability_id))continue;H(hu32(&c,x->capability_id));H(hu32(&c,x->version_major));H(hu32(&c,x->version_minor));H(hu32(&c,x->state_model));H(hu32(&c,x->allowed_roles_mask));H(hu32(&c,x->resource_kind));}
    return odm_sha256_final(&c,out_capability_sha256);
}
#undef H

#include "visual_internal.h"

#include "odm_rng.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define ODM_VISUAL_EVENT_SEED_DOMAIN UINT64_C(0x56495355414c4556) /* VISUALEV */

static const uint8_t *node_find(const odm_ir_parsed *p, uint32_t node_id) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_NODES - 1u];
    uint32_t lo = 0u, hi = s->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const uint8_t *r = s->records + (uint64_t)mid * s->record_bytes;
        uint32_t got = odm_ir_read_u32(r);
        if (got == node_id) return r;
        if (got < node_id) lo = mid + 1u; else hi = mid;
    }
    return NULL;
}

static const uint8_t *transfer_into(const odm_ir_parsed *p, uint32_t to_scene,
                                    uint32_t slot, int64_t upper_sample) {
    const odm_ir_section_view *s = &p->sections[ODM_IR_SECTION_STATE_TRANSFER - 1u];
    const uint8_t *found = NULL;
    uint32_t i;
    for (i = 0u; i < s->count; ++i) {
        const uint8_t *r = s->records + (uint64_t)i * s->record_bytes;
        if (odm_ir_read_u32(r + 8u) == to_scene &&
            odm_ir_read_u32(r + 12u) == slot &&
            odm_ir_read_i64(r + 24u) <= upper_sample) {
            found = r;
        }
    }
    return found;
}

static int chain_index(const uint32_t *scenes, uint32_t count, uint32_t scene) {
    uint32_t i;
    for (i = 0u; i < count; ++i) if (scenes[i] == scene) return (int)i;
    return -1;
}

static void push_latest(odm_visual_history *h, const odm_visual_event *e) {
    if (h->event_count < ODM_VISUAL_HISTORY_MAX_EVENTS) {
        h->events[h->event_count++] = *e;
    } else {
        memmove(&h->events[0], &h->events[1],
                (ODM_VISUAL_HISTORY_MAX_EVENTS - 1u) * sizeof(h->events[0]));
        h->events[ODM_VISUAL_HISTORY_MAX_EVENTS - 1u] = *e;
    }
}

odm_status odm_visual_history_resolve_parsed(
    const odm_ir_parsed *p, const uint8_t *node_record, int64_t sample,
    odm_visual_history *out_history) {
    uint32_t scenes[ODM_VISUAL_HISTORY_MAX_TRANSFER_DEPTH];
    int64_t uppers[ODM_VISUAL_HISTORY_MAX_TRANSFER_DEPTH];
    uint32_t depth = 0u, scene, slot, node_id, i;
    uint64_t seed_domain;
    odm_visual_history h;
    const odm_ir_section_view *timeline;

    if (!p || !node_record || !out_history) return ODM_STATUS_INVALID_ARGUMENT;
    node_id = odm_ir_read_u32(node_record);
    scene = odm_ir_read_u32(node_record + 4u);
    slot = odm_ir_read_u32(node_record + 104u);
    seed_domain = odm_ir_read_u64(node_record + 96u);
    if (slot == 0u || odm_ir_read_u32(node_record + 28u) == ODM_STATELESS)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (sample < odm_ir_read_i64(node_record + 40u) ||
        sample >= odm_ir_read_i64(node_record + 48u)) return ODM_STATUS_INVALID_ARGUMENT;

    memset(&h, 0, sizeof(h));
    h.schema_version = ODM_VISUAL_HISTORY_SCHEMA_VERSION;
    h.node_id = node_id;
    h.scene_id = scene;
    h.state_slot = slot;
    h.sample = sample;
    h.root_seed = odm_rng_derive_seed(p->info.project_seed ^ seed_domain,
                                     ODM_VISUAL_EVENT_SEED_DOMAIN, slot);

    while (depth < ODM_VISUAL_HISTORY_MAX_TRANSFER_DEPTH) {
        const uint8_t *tr;
        uint32_t previous;
        scenes[depth] = scene;
        uppers[depth] = sample;
        if (depth != 0u) uppers[depth] = uppers[depth - 1u];
        ++depth;
        tr = transfer_into(p, scene, slot, uppers[depth - 1u]);
        if (!tr || odm_ir_read_u32(tr + 16u) == ODM_TRANSFER_RESET) break;
        previous = odm_ir_read_u32(tr + 4u);
        if (chain_index(scenes, depth, previous) >= 0) return ODM_STATUS_INVARIANT_BROKEN;
        scene = previous;
        sample = odm_ir_read_i64(tr + 24u);
    }
    if (depth == ODM_VISUAL_HISTORY_MAX_TRANSFER_DEPTH) {
        const uint8_t *tr = transfer_into(p, scene, slot, sample);
        if (tr && odm_ir_read_u32(tr + 16u) == ODM_TRANSFER_COPY)
            return ODM_STATUS_BUDGET_EXCEEDED;
    }

    /* Rebuild per-scene upper bounds from newest to oldest. */
    sample = h.sample;
    scene = h.scene_id;
    for (i = 0u; i < depth; ++i) {
        const uint8_t *tr;
        scenes[i] = scene;
        uppers[i] = sample;
        tr = transfer_into(p, scene, slot, sample);
        if (!tr || odm_ir_read_u32(tr + 16u) != ODM_TRANSFER_COPY) break;
        scene = odm_ir_read_u32(tr + 4u);
        sample = odm_ir_read_i64(tr + 24u);
    }
    depth = i + 1u;

    timeline = &p->sections[ODM_IR_SECTION_TIMELINE - 1u];
    for (i = 0u; i < timeline->count; ++i) {
        const uint8_t *r = timeline->records + (uint64_t)i * timeline->record_bytes;
        uint32_t cue_scene, cue_id, cue_kind;
        int64_t cue_sample;
        int ci;
        odm_visual_event e;
        uint64_t entity;
        if (odm_ir_read_u32(r) != ODM_IR_TIMELINE_CUE) continue;
        cue_id = odm_ir_read_u32(r + 4u);
        cue_scene = odm_ir_read_u32(r + 8u);
        cue_sample = odm_ir_read_i64(r + 16u);
        cue_kind = odm_ir_read_u32(r + 32u);
        ci = chain_index(scenes, depth, cue_scene);
        if (ci < 0 || cue_sample > uppers[(uint32_t)ci] || cue_sample > h.sample) continue;
        memset(&e, 0, sizeof(e));
        e.cue_id = cue_id; e.cue_kind = cue_kind; e.scene_id = cue_scene;
        e.sample = cue_sample; e.age_samples = h.sample - cue_sample;
        entity = ((uint64_t)cue_id << 32u) ^ (uint64_t)cue_sample;
        e.seed = odm_rng_derive_seed(h.root_seed, ODM_VISUAL_EVENT_SEED_DOMAIN ^ slot, entity);
        push_latest(&h, &e);
    }
    *out_history = h;
    return ODM_STATUS_OK;
}

odm_status odm_visual_history_resolve(const uint8_t *render_ir,
                                      uint64_t render_ir_bytes,
                                      uint32_t node_id, int64_t sample,
                                      odm_visual_history *out_history) {
    odm_ir_parsed p;
    const uint8_t *node;
    odm_status st;
    if (!render_ir || !out_history || node_id == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_ir_parse_internal(render_ir, render_ir_bytes, &p);
    if (st != ODM_STATUS_OK) return st;
    node = node_find(&p, node_id);
    if (!node) return ODM_STATUS_INVALID_DATA;
    return odm_visual_history_resolve_parsed(&p, node, sample, out_history);
}

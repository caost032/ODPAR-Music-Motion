static odm_status root_for_domain(uint32_t domain, uint64_t *out_root) {
    uint32_t i;
    if (!out_root || domain == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < g_ws.object_count; ++i) {
        if (g_ws.objects[i].capability_id == ODM_PRESENTATION_CAP_DOMAIN_ROOT) {
            uint32_t d = 0u;
            if (odm_presentation_builtin_object_domain(&g_ws.document, g_ws.objects[i].object_id, &d) == ODM_STATUS_OK && d == domain) {
                *out_root = g_ws.objects[i].object_id;
                return ODM_STATUS_OK;
            }
        }
    }
    return ODM_STATUS_INVALID_DATA;
}

static odm_status add_object(uint32_t domain, uint32_t capability_id, uint64_t *out_id) {
    const odm_presentation_capability *cap = find_cap(capability_id);
    uint64_t root = 0u, id;
    odm_presentation_object candidate;
    odm_presentation_document doc;
    odm_status st;
    if (!cap || capability_id == ODM_PRESENTATION_CAP_DOMAIN_ROOT) return ODM_STATUS_INVALID_DATA;
    st = root_for_domain(domain, &root);
    if (st != ODM_STATUS_OK) return st;
    if ((cap->flags & ODM_PRESENTATION_CAP_SINGLETON) != 0u) {
        uint32_t i;
        for (i = 0u; i < g_ws.object_count; ++i)
            if (g_ws.objects[i].capability_id == capability_id) return ODM_STATUS_INVALID_STATE;
    }
    id = g_ws.next_object_id++;
    memset(&candidate, 0, sizeof(candidate));
    candidate.object_id = id;
    candidate.parent_id = root;
    candidate.capability_id = capability_id;
    candidate.flags = ODM_PRESENTATION_OBJECT_ENABLED;
    if (!ensure_objects(g_ws.object_count + 1u)) return ODM_STATUS_OUT_OF_MEMORY;
    g_ws.objects[g_ws.object_count] = candidate;
    doc = g_ws.document;
    doc.object_count = g_ws.object_count + 1u;
    st = odm_presentation_builtin_domains_validate(&doc);
    if (st != ODM_STATUS_OK) return st;
    g_ws.object_count++;
    g_ws.document.object_count = g_ws.object_count;
    (void)odm_authoring_session_enter_domain(&g_ws.session, domain, 0u);
    (void)odm_authoring_session_select(&g_ws.session, &g_ws.document, id);
    if (out_id) *out_id = id;
    return ODM_STATUS_OK;
}

static int descends_from(uint64_t object_id, uint64_t ancestor_id) {
    const odm_presentation_object *o = NULL;
    uint32_t hops = 0u;
    uint64_t cur = object_id;
    while (cur && hops++ <= g_ws.object_count) {
        if (cur == ancestor_id) return 1;
        if (odm_presentation_find_object(&g_ws.document, cur, &o) != ODM_STATUS_OK || !o) break;
        cur = o->parent_id;
    }
    return 0;
}

static odm_status delete_object(uint64_t object_id) {
    const odm_presentation_object *target = NULL;
    odm_presentation_object *new_objects = NULL;
    odm_presentation_parameter_value *new_values = NULL;
    odm_presentation_binding *new_bindings = NULL;
    uint32_t oc = 0u, vc = 0u, bc = 0u, i;
    int selected_removed;
    odm_presentation_document doc;
    odm_status st;
    if (odm_presentation_find_object(&g_ws.document, object_id, &target) != ODM_STATUS_OK || !target)
        return ODM_STATUS_INVALID_DATA;
    if (target->capability_id == ODM_PRESENTATION_CAP_DOMAIN_ROOT) return ODM_STATUS_INVALID_STATE;
    selected_removed = g_ws.session.selected_object_id != 0u && descends_from(g_ws.session.selected_object_id, object_id);
    new_objects = (odm_presentation_object *)malloc((size_t)g_ws.object_count * sizeof(*new_objects));
    new_values = (odm_presentation_parameter_value *)malloc((size_t)(g_ws.value_count ? g_ws.value_count : 1u) * sizeof(*new_values));
    new_bindings = (odm_presentation_binding *)malloc((size_t)(g_ws.binding_count ? g_ws.binding_count : 1u) * sizeof(*new_bindings));
    if (!new_objects || !new_values || !new_bindings) { free(new_objects); free(new_values); free(new_bindings); return ODM_STATUS_OUT_OF_MEMORY; }
    for (i = 0u; i < g_ws.object_count; ++i)
        if (!descends_from(g_ws.objects[i].object_id, object_id)) new_objects[oc++] = g_ws.objects[i];
    for (i = 0u; i < g_ws.value_count; ++i)
        if (!descends_from(g_ws.values[i].object_id, object_id)) new_values[vc++] = g_ws.values[i];
    for (i = 0u; i < g_ws.binding_count; ++i) {
        const odm_presentation_binding *x = &g_ws.bindings[i];
        if (descends_from(x->target_object_id, object_id)) continue;
        if (x->source_object_id && descends_from(x->source_object_id, object_id)) continue;
        new_bindings[bc++] = *x;
    }
    doc = g_ws.document;
    doc.objects = new_objects; doc.object_count = oc;
    doc.values = new_values; doc.value_count = vc;
    doc.bindings = new_bindings; doc.binding_count = bc;
    st = odm_presentation_builtin_domains_validate(&doc);
    if (st == ODM_STATUS_OK) {
        if (!ensure_objects(oc) || !ensure_values(vc) || !ensure_bindings(bc)) st = ODM_STATUS_OUT_OF_MEMORY;
        else {
            memcpy(g_ws.objects, new_objects, (size_t)oc * sizeof(*new_objects));
            memcpy(g_ws.values, new_values, (size_t)vc * sizeof(*new_values));
            if (bc) memcpy(g_ws.bindings, new_bindings, (size_t)bc * sizeof(*new_bindings));
            g_ws.object_count = oc; g_ws.value_count = vc; g_ws.binding_count = bc;
            g_ws.document.objects = g_ws.objects; g_ws.document.object_count = oc;
            g_ws.document.values = g_ws.values; g_ws.document.value_count = vc;
            g_ws.document.bindings = g_ws.bindings; g_ws.document.binding_count = bc;
            if (selected_removed) g_ws.session.selected_object_id = 0u;
        }
    }
    free(new_objects); free(new_values); free(new_bindings);
    return st;
}

static odm_status set_enabled(uint64_t object_id, int enabled) {
    const odm_presentation_object *found = NULL;
    uint32_t idx;
    odm_presentation_object old;
    odm_status st;
    if (odm_presentation_find_object(&g_ws.document, object_id, &found) != ODM_STATUS_OK || !found)
        return ODM_STATUS_INVALID_DATA;
    idx = (uint32_t)(found - g_ws.objects);
    old = g_ws.objects[idx];
    if (enabled) g_ws.objects[idx].flags |= ODM_PRESENTATION_OBJECT_ENABLED;
    else g_ws.objects[idx].flags &= ~ODM_PRESENTATION_OBJECT_ENABLED;
    st = odm_presentation_builtin_domains_validate(&g_ws.document);
    if (st != ODM_STATUS_OK) g_ws.objects[idx] = old;
    return st;
}

static odm_status add_music_binding(uint64_t object_id, uint32_t parameter_id,
                                    uint32_t component, uint32_t lane, uint32_t op) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    const odm_presentation_object *o = NULL;
    const odm_presentation_parameter *p = NULL;
    odm_presentation_binding b;
    odm_presentation_document doc;
    odm_status st;
    if (op == 0u || op >= ODM_PRESENTATION_BIND_COUNT) return ODM_STATUS_INVALID_ARGUMENT;
    if (odm_presentation_find_object(&g_ws.document, object_id, &o) != ODM_STATUS_OK || !o) return ODM_STATUS_INVALID_DATA;
    if (odm_presentation_find_parameter(schema, o->capability_id, parameter_id, &p) != ODM_STATUS_OK || !p) return ODM_STATUS_INVALID_DATA;
    if ((p->flags & ODM_PRESENTATION_PARAM_MODULATABLE) == 0u || component >= value_components(p->type))
        return ODM_STATUS_INVALID_DATA;
    if (!ensure_bindings(g_ws.binding_count + 1u)) return ODM_STATUS_OUT_OF_MEMORY;
    memset(&b, 0, sizeof(b));
    b.binding_id = g_ws.next_binding_id++;
    b.target_object_id = object_id;
    b.target_parameter_id = parameter_id;
    b.target_component = component;
    b.source_kind = ODM_PRESENTATION_SOURCE_MUSIC_LANE;
    b.source_index = lane;
    b.op = op;
    b.flags = ODM_PRESENTATION_BINDING_ENABLED;
    b.scale_q32_32 = Q32_ONE;
    b.bias_q32_32 = 0;
    b.clamp_min_q32_32 = INT64_MIN;
    b.clamp_max_q32_32 = INT64_MAX;
    g_ws.bindings[g_ws.binding_count] = b;
    doc = g_ws.document; doc.binding_count = g_ws.binding_count + 1u;
    st = odm_presentation_document_validate(schema, &doc);
    if (st == ODM_STATUS_OK) {
        g_ws.binding_count++;
        g_ws.document.binding_count = g_ws.binding_count;
    }
    return st;
}

static char *timeline_json(int64_t sample, int32_t fps) {
    odm_authoring_playhead p;
    sbuf b = {0};
    odm_status st;
    if (sample < 0) sample = 0;
    if (sample > g_ws.timeline.duration_samples) sample = g_ws.timeline.duration_samples;
    st = odm_authoring_playhead_resolve(&g_ws.timeline, sample, fps, &p);
    if (st != ODM_STATUS_OK) { set_error("timeline: %s", odm_status_name(st)); return NULL; }
    g_ws.playhead_sample = sample;
    if (!sb_printf(&b,
        "{\"sample\":%" PRId64 ",\"duration_samples\":%" PRId64 ",\"fps\":%d"
        ",\"frame_index\":%" PRId64 ",\"frame_start_sample\":%" PRId64
        ",\"sample_into_frame\":%" PRId64 ",\"music_map_tick\":%" PRId64
        ",\"sample_into_music_map_tick\":%" PRId64 "}",
        p.sample, p.duration_samples, p.fps, p.frame_index, p.frame_start_sample,
        p.sample_into_frame, p.music_map_tick, p.sample_into_music_map_tick)) { sb_free(&b); return NULL; }
    return b.data;
}

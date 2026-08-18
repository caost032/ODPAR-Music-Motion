static odm_status root_for_domain(uint32_t domain, uint64_t *out_root);

static odm_status workspace_init(void) {
    uint32_t d;
    odm_status st;
    static const struct { uint32_t domain, cap; } seeded[] = {
        {ODM_PRESENTATION_DOMAIN_CAMERA, ODM_PRESENTATION_CAP_CAMERA3D},
        {ODM_PRESENTATION_DOMAIN_MODELS3D, ODM_PRESENTATION_CAP_TRANSFORM3D},
        {ODM_PRESENTATION_DOMAIN_MODELS3D, ODM_PRESENTATION_CAP_MATERIAL3D},
        {ODM_PRESENTATION_DOMAIN_MODELS3D, ODM_PRESENTATION_CAP_MODEL3D_STATE},
        {ODM_PRESENTATION_DOMAIN_ENVIRONMENT, ODM_PRESENTATION_CAP_ENVIRONMENT3D},
        {ODM_PRESENTATION_DOMAIN_COMPOSITION, ODM_PRESENTATION_CAP_GROUP},
        {ODM_PRESENTATION_DOMAIN_CORE_MEDIA, ODM_PRESENTATION_CAP_GROUP},
        {ODM_PRESENTATION_DOMAIN_PARTICLES, ODM_PRESENTATION_CAP_GROUP},
        {ODM_PRESENTATION_DOMAIN_LIGHTING, ODM_PRESENTATION_CAP_LIGHT3D},
        {ODM_PRESENTATION_DOMAIN_OVERLAY2D, ODM_PRESENTATION_CAP_TRANSFORM2D},
        {ODM_PRESENTATION_DOMAIN_OVERLAY2D, ODM_PRESENTATION_CAP_SHAPE2D},
        {ODM_PRESENTATION_DOMAIN_OVERLAY2D, ODM_PRESENTATION_CAP_TEXT2D},
        {ODM_PRESENTATION_DOMAIN_OVERLAY2D, ODM_PRESENTATION_CAP_RESOURCE2D},
        {ODM_PRESENTATION_DOMAIN_AUDIO_REACTION, ODM_PRESENTATION_CAP_GROUP},
        {ODM_PRESENTATION_DOMAIN_POST, ODM_PRESENTATION_CAP_GROUP},
        {ODM_PRESENTATION_DOMAIN_OUTPUT, ODM_PRESENTATION_CAP_OUTPUT_SURFACE},
        {ODM_PRESENTATION_DOMAIN_ASSETS, ODM_PRESENTATION_CAP_GROUP}
    };
    if (g_ws.initialized) return ODM_STATUS_OK;
    memset(&g_ws, 0, sizeof(g_ws));
    g_ws.document.schema_major = ODM_PRESENTATION_SCHEMA_MAJOR;
    g_ws.document.schema_minor = ODM_PRESENTATION_SCHEMA_MINOR;
    g_ws.document.project_seed = UINT64_C(0x4f445041525f4d4d);

    st = odm_presentation_builtin_schema_validate();
    if (st != ODM_STATUS_OK) return st;
    {
        const odm_presentation_schema *schema = odm_presentation_builtin_schema();
        const odm_presentation_parameter *domain_param = NULL;
        uint32_t oi;
        st = odm_presentation_find_parameter(schema, ODM_PRESENTATION_CAP_DOMAIN_ROOT,
                                             ODM_PRESENTATION_DOMAIN_ROOT_KIND, &domain_param);
        if (st != ODM_STATUS_OK || !domain_param || domain_param->option_count == 0u)
            return ODM_STATUS_INVALID_DATA;
        for (oi = 0u; oi < domain_param->option_count; ++oi) {
            uint32_t index = domain_param->first_option_index + oi;
            uint64_t id = UINT64_C(100) * (uint64_t)(oi + 1u);
            uint32_t domain;
            if (index >= schema->option_count) return ODM_STATUS_INVALID_DATA;
            domain = schema->options[index].value;
            st = append_object(id, 0u, ODM_PRESENTATION_CAP_DOMAIN_ROOT, ODM_PRESENTATION_OBJECT_ENABLED);
            if (st != ODM_STATUS_OK) return st;
            st = append_root_value(id, domain);
            if (st != ODM_STATUS_OK) return st;
        }
    }
    for (d = 0u; d < (uint32_t)(sizeof(seeded)/sizeof(seeded[0])); ++d) {
        uint64_t id = UINT64_C(10000) + d;
        uint64_t parent = 0u;
        st = root_for_domain(seeded[d].domain, &parent);
        if (st != ODM_STATUS_OK) return st;
        st = append_object(id, parent, seeded[d].cap, ODM_PRESENTATION_OBJECT_ENABLED);
        if (st != ODM_STATUS_OK) return st;
    }
    g_ws.next_object_id = UINT64_C(1000000);
    g_ws.next_binding_id = UINT64_C(1);
    st = odm_authoring_session_init(&g_ws.session);
    if (st != ODM_STATUS_OK) return st;
    memset(&g_ws.timeline, 0, sizeof(g_ws.timeline));
    g_ws.timeline.schema_version = ODM_AUTHORING_TIMELINE_SCHEMA_VERSION;
    g_ws.timeline.duration_samples = INT64_C(48000) * INT64_C(300);
    st = odm_authoring_timeline_validate(&g_ws.timeline);
    if (st != ODM_STATUS_OK) return st;
    st = odm_timeline_view_init(g_ws.timeline.duration_samples, 1200u, &g_ws.timeline_view);
    if (st != ODM_STATUS_OK) return st;
    st = mesh_init();
    if (st != ODM_STATUS_OK) return st;
    st = validate_workspace();
    if (st != ODM_STATUS_OK) return st;
    g_ws.initialized = 1;
    set_error("ok");
    return ODM_STATUS_OK;
}

static int digest_hex(const odm_sha256_digest *d, char out[65]) {
    uint64_t req = 0;
    if (odm_sha256_to_hex(d, out, 65u, &req) != ODM_STATUS_OK) return 0;
    return 1;
}

static int append_workspace_json(sbuf *b) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    odm_sha256_digest doc_hash;
    char hex[65] = {0};
    odm_editor_catalog catalog;
    uint64_t workspace_bytes = 0u;
    void *workspace = NULL;
    uint32_t i;
    odm_status st;
    st = odm_presentation_document_sha256(schema, &g_ws.document, &doc_hash);
    if (st != ODM_STATUS_OK || !digest_hex(&doc_hash, hex)) return 0;
    st = odm_editor_catalog_workspace_bytes(&g_ws.document, &workspace_bytes);
    if (st != ODM_STATUS_OK) return 0;
    workspace = workspace_bytes ? malloc((size_t)workspace_bytes) : NULL;
    if (workspace_bytes && !workspace) return 0;
    st = odm_editor_catalog_prepare(&g_ws.document, workspace, workspace_bytes, &catalog);
    if (st != ODM_STATUS_OK) { free(workspace); return 0; }

    if (!sb_printf(b,
        "{\"document_sha256\":\"%s\",\"project_seed\":%" PRIu64
        ",\"mode\":%u,\"active_domain\":%u,\"context_domain_mask\":%u"
        ",\"selected_object_id\":%" PRIu64 ",\"camera_preview_object_id\":%" PRIu64
        ",\"preview_quality\":%u,\"object_count\":%u,\"value_count\":%u,\"binding_count\":%u"
        ",\"playhead_sample\":%" PRId64 ",\"duration_samples\":%" PRId64
        ",\"mesh\":{\"vertices\":%u,\"faces\":%u,\"vertex_capacity\":%u,\"face_capacity\":%u},\"objects\":[",
        hex, g_ws.document.project_seed, g_ws.session.mode, g_ws.session.active_domain,
        g_ws.session.context_domain_mask, g_ws.session.selected_object_id,
        g_ws.session.camera_preview_object_id, g_ws.session.preview_quality,
        g_ws.object_count, g_ws.value_count, g_ws.binding_count,
        g_ws.playhead_sample, g_ws.timeline.duration_samples,
        g_ws.mesh.vertex_count, g_ws.mesh.face_count, g_ws.mesh_vertex_cap, g_ws.mesh_face_cap)) { free(workspace); return 0; }

    for (i = 0u; i < g_ws.object_count; ++i) {
        const odm_presentation_object *o = &g_ws.objects[i];
        const odm_presentation_capability *cap = find_cap(o->capability_id);
        uint32_t domain = 0u;
        if (i) if (!sb_add(b, ",")) { free(workspace); return 0; }
        (void)odm_presentation_builtin_object_domain(&g_ws.document, o->object_id, &domain);
        if (!sb_printf(b, "{\"id\":%" PRIu64 ",\"parent\":%" PRIu64
                         ",\"capability\":%u,\"flags\":%u,\"z\":%d,\"domain\":%u,\"key\":" ,
                         o->object_id, o->parent_id, o->capability_id, o->flags, o->z_index, domain)) { free(workspace); return 0; }
        if (!sb_json_string(b, cap ? cap->key : "unknown") || !sb_add(b, ",\"label\":") ||
            !sb_json_string(b, cap ? cap->label : "Unknown") || !sb_add(b, "}")) { free(workspace); return 0; }
    }
    free(workspace);
    return sb_add(b, "]}");
}

static char *workspace_json(void) {
    sbuf b = {0};
    if (!append_workspace_json(&b)) { sb_free(&b); return NULL; }
    return b.data;
}

static char *schema_json(void) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    uint64_t req = 0u;
    char *buf;
    odm_status st = odm_presentation_schema_manifest_json(schema, NULL, 0u, &req);
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || req == 0u || req > SIZE_MAX) return NULL;
    buf = (char *)malloc((size_t)req);
    if (!buf) return NULL;
    st = odm_presentation_schema_manifest_json(schema, buf, req, &req);
    if (st != ODM_STATUS_OK) { free(buf); return NULL; }
    return buf;
}

static char *spine_json(odm_status (*fn)(char *, size_t, size_t *)) {
    size_t req = 0u;
    char *buf;
    odm_status st = fn(NULL, 0u, &req);
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || req == 0u) return NULL;
    buf = (char *)malloc(req);
    if (!buf) return NULL;
    st = fn(buf, req, &req);
    if (st != ODM_STATUS_OK) { free(buf); return NULL; }
    return buf;
}

static int append_value_json(sbuf *b, const odm_presentation_value *v) {
    uint32_t lanes = value_components(v->type), i;
    if (!sb_printf(b, "{\"type\":%u,\"lanes\":[", v->type)) return 0;
    for (i = 0u; i < lanes; ++i) {
        if (i && !sb_add(b, ",")) return 0;
        if (!sb_printf(b, "%" PRId64, v->lane[i])) return 0;
    }
    return sb_add(b, "]}");
}

static char *inspector_json(uint64_t object_id) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    const odm_presentation_object *o = NULL;
    const odm_presentation_capability *cap;
    sbuf b = {0};
    uint32_t i;
    if (odm_presentation_find_object(&g_ws.document, object_id, &o) != ODM_STATUS_OK || !o) {
        set_error("object not found");
        return NULL;
    }
    cap = find_cap(o->capability_id);
    if (!cap) return NULL;
    if (!sb_printf(&b, "{\"object_id\":%" PRIu64 ",\"capability\":%u,\"key\":" , object_id, o->capability_id) ||
        !sb_json_string(&b, cap->key) || !sb_add(&b, ",\"label\":") || !sb_json_string(&b, cap->label) ||
        !sb_printf(&b, ",\"flags\":%u,\"z\":%d,\"parameters\":[", o->flags, o->z_index)) goto oom;
    for (i = 0u; i < cap->parameter_count; ++i) {
        const odm_presentation_parameter *p = &schema->parameters[cap->first_parameter_index + i];
        const odm_presentation_parameter_value *auth = NULL;
        const odm_presentation_value *value = &p->default_value;
        uint32_t oi;
        if (odm_presentation_find_value(&g_ws.document, object_id, p->parameter_id, &auth) == ODM_STATUS_OK && auth)
            value = &auth->value;
        if (i && !sb_add(&b, ",")) goto oom;
        if (!sb_printf(&b, "{\"id\":%u,\"key\":" , p->parameter_id) || !sb_json_string(&b, p->key) ||
            !sb_add(&b, ",\"label\":") || !sb_json_string(&b, p->label) ||
            !sb_add(&b, ",\"group\":") || !sb_json_string(&b, p->group) ||
            !sb_add(&b, ",\"unit\":") || !sb_json_string(&b, p->unit) ||
            !sb_printf(&b, ",\"type\":%u,\"flags\":%u,\"authored\":%s,\"value\":" ,
                       p->type, p->flags, auth ? "true" : "false") ||
            !append_value_json(&b, value) || !sb_add(&b, ",\"minimum\":") || !append_value_json(&b, &p->minimum) ||
            !sb_add(&b, ",\"maximum\":") || !append_value_json(&b, &p->maximum) || !sb_add(&b, ",\"default\":") ||
            !append_value_json(&b, &p->default_value) || !sb_add(&b, ",\"options\":[")) goto oom;
        for (oi = 0u; oi < p->option_count; ++oi) {
            const odm_presentation_option *op = &schema->options[p->first_option_index + oi];
            if (oi && !sb_add(&b, ",")) goto oom;
            if (!sb_printf(&b, "{\"value\":%u,\"key\":" , op->value) || !sb_json_string(&b, op->key) ||
                !sb_add(&b, ",\"label\":") || !sb_json_string(&b, op->label) || !sb_add(&b, "}")) goto oom;
        }
        if (!sb_add(&b, "]}")) goto oom;
    }
    if (!sb_add(&b, "]}")) goto oom;
    return b.data;
oom:
    sb_free(&b);
    set_error("out of memory building inspector");
    return NULL;
}

static int value_key_cmp(const odm_presentation_parameter_value *a, uint64_t object_id, uint32_t parameter_id) {
    if (a->object_id < object_id) return -1;
    if (a->object_id > object_id) return 1;
    if (a->parameter_id < parameter_id) return -1;
    if (a->parameter_id > parameter_id) return 1;
    return 0;
}

static odm_status set_value_lane(uint64_t object_id, uint32_t parameter_id, uint32_t component, int64_t raw) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    const odm_presentation_object *o = NULL;
    const odm_presentation_parameter *p = NULL;
    const odm_presentation_parameter_value *auth = NULL;
    odm_presentation_parameter_value candidate;
    odm_presentation_document doc;
    odm_presentation_parameter_value *tmp = NULL;
    uint32_t pos = 0u, new_count, i;
    odm_status st;
    st = odm_presentation_find_object(&g_ws.document, object_id, &o);
    if (st != ODM_STATUS_OK || !o) return ODM_STATUS_INVALID_DATA;
    st = odm_presentation_find_parameter(schema, o->capability_id, parameter_id, &p);
    if (st != ODM_STATUS_OK || !p || (p->flags & ODM_PRESENTATION_PARAM_READONLY)) return ODM_STATUS_INVALID_DATA;
    if (component >= value_components(p->type)) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&candidate, 0, sizeof(candidate));
    candidate.object_id = object_id;
    candidate.parameter_id = parameter_id;
    candidate.value = p->default_value;
    if (odm_presentation_find_value(&g_ws.document, object_id, parameter_id, &auth) == ODM_STATUS_OK && auth)
        candidate.value = auth->value;
    candidate.value.lane[component] = raw;
    if (p->type == ODM_PRESENTATION_VALUE_BOOL && raw != 0 && raw != 1) return ODM_STATUS_INVALID_DATA;
    if (p->type == ODM_PRESENTATION_VALUE_ENUM && p->option_count) {
        int found = 0;
        for (i = 0u; i < p->option_count; ++i)
            if ((int64_t)schema->options[p->first_option_index + i].value == candidate.value.lane[0]) found = 1;
        if (!found) return ODM_STATUS_INVALID_DATA;
    }
    while (pos < g_ws.value_count && value_key_cmp(&g_ws.values[pos], object_id, parameter_id) < 0) ++pos;
    new_count = g_ws.value_count + ((pos < g_ws.value_count && value_key_cmp(&g_ws.values[pos], object_id, parameter_id) == 0) ? 0u : 1u);
    tmp = (odm_presentation_parameter_value *)malloc((size_t)new_count * sizeof(*tmp));
    if (!tmp) return ODM_STATUS_OUT_OF_MEMORY;
    if (new_count == g_ws.value_count) {
        memcpy(tmp, g_ws.values, (size_t)g_ws.value_count * sizeof(*tmp));
        tmp[pos] = candidate;
    } else {
        if (pos) memcpy(tmp, g_ws.values, (size_t)pos * sizeof(*tmp));
        tmp[pos] = candidate;
        if (pos < g_ws.value_count) memcpy(tmp + pos + 1u, g_ws.values + pos, (size_t)(g_ws.value_count - pos) * sizeof(*tmp));
    }
    doc = g_ws.document;
    doc.values = tmp;
    doc.value_count = new_count;
    st = odm_presentation_builtin_domains_validate(&doc);
    if (st == ODM_STATUS_OK) {
        if (!ensure_values(new_count)) st = ODM_STATUS_OUT_OF_MEMORY;
        else {
            memcpy(g_ws.values, tmp, (size_t)new_count * sizeof(*tmp));
            g_ws.value_count = new_count;
            g_ws.document.values = g_ws.values;
            g_ws.document.value_count = new_count;
        }
    }
    free(tmp);
    return st;
}

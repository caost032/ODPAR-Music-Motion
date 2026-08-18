JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeBootstrap(JNIEnv *env, jobject thiz) {
    char *schema = NULL, *workspace = NULL, *summary = NULL;
    sbuf b = {0};
    odm_status st;
    (void)thiz;
    st = workspace_init();
    if (st != ODM_STATUS_OK) { set_error("workspace init: %s", odm_status_name(st)); return jstr(env, "{\"ok\":false}"); }
    schema = schema_json(); workspace = workspace_json(); summary = spine_json(odm_spine_summary_json);
    if (!schema || !workspace) goto fail;
    if (!sb_add(&b, "{\"ok\":true,\"engine\":{\"name\":\"ODPAR: Music Motion\",\"version\":") ||
        !sb_json_string(&b, odm_version_string()) || !sb_printf(&b, ",\"abi\":%u,\"source\":" , odm_abi_version()) ||
        !sb_json_string(&b, odm_source_id()) || !sb_add(&b, ",\"compiler\":") || !sb_json_string(&b, odm_compiler_id()) ||
        !sb_add(&b, "},\"schema\":") || !sb_add(&b, schema) || !sb_add(&b, ",\"workspace\":") || !sb_add(&b, workspace) ||
        !sb_add(&b, ",\"spine_summary\":") || !sb_add(&b, summary ? summary : "null") || !sb_add(&b, "}")) goto fail;
    free(schema); free(workspace); free(summary);
    { jstring out = jstr(env, b.data); sb_free(&b); return out; }
fail:
    free(schema); free(workspace); free(summary); sb_free(&b); set_error("bootstrap allocation failure"); return jstr(env, "{\"ok\":false}");
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeWorkspace(JNIEnv *env, jobject thiz) {
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    s = workspace_json();
    if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeInspector(JNIEnv *env, jobject thiz, jlong objectId) {
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    s = inspector_json((uint64_t)objectId);
    if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeEnterDomain(JNIEnv *env, jobject thiz, jint domain) {
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = domain == 0 ? odm_authoring_session_enter_world(&g_ws.session) :
        odm_authoring_session_enter_domain(&g_ws.session, (uint32_t)domain, 0u);
    if (st != ODM_STATUS_OK) set_error("enter domain: %s", odm_status_name(st));
    s = workspace_json(); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSelect(JNIEnv *env, jobject thiz, jlong objectId) {
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = odm_authoring_session_select(&g_ws.session, &g_ws.document, (uint64_t)objectId);
    if (st != ODM_STATUS_OK) set_error("select: %s", odm_status_name(st));
    s = workspace_json(); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSetValueRaw(JNIEnv *env, jobject thiz,
    jlong objectId, jint parameterId, jint component, jstring rawText) {
    const char *raw;
    char *end = NULL;
    int64_t value;
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK || !rawText) return jstr(env, "{}");
    raw = (*env)->GetStringUTFChars(env, rawText, NULL);
    if (!raw) return jstr(env, "{}");
    errno = 0;
    value = strtoimax(raw, &end, 10);
    if (errno == ERANGE || !end || *end != '\0') { (*env)->ReleaseStringUTFChars(env, rawText, raw); set_error("invalid canonical integer"); return jstr(env, "{}"); }
    (*env)->ReleaseStringUTFChars(env, rawText, raw);
    st = set_value_lane((uint64_t)objectId, (uint32_t)parameterId, (uint32_t)component, value);
    if (st != ODM_STATUS_OK) set_error("set value: %s", odm_status_name(st)); else set_error("ok");
    s = inspector_json((uint64_t)objectId); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeAddObject(JNIEnv *env, jobject thiz, jint domain, jint capabilityId) {
    uint64_t id = 0u;
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = add_object((uint32_t)domain, (uint32_t)capabilityId, &id);
    if (st != ODM_STATUS_OK) set_error("add object: %s", odm_status_name(st)); else set_error("added object %" PRIu64, id);
    s = workspace_json(); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeDeleteObject(JNIEnv *env, jobject thiz, jlong objectId) {
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = delete_object((uint64_t)objectId);
    if (st != ODM_STATUS_OK) set_error("delete: %s", odm_status_name(st)); else set_error("ok");
    s = workspace_json(); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSetEnabled(JNIEnv *env, jobject thiz, jlong objectId, jboolean enabled) {
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = set_enabled((uint64_t)objectId, enabled ? 1 : 0);
    if (st != ODM_STATUS_OK) set_error("enabled: %s", odm_status_name(st)); else set_error("ok");
    s = workspace_json(); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeAddMusicBinding(JNIEnv *env, jobject thiz,
    jlong objectId, jint parameterId, jint component, jint lane, jint op) {
    odm_status st;
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = add_music_binding((uint64_t)objectId, (uint32_t)parameterId, (uint32_t)component, (uint32_t)lane, (uint32_t)op);
    if (st != ODM_STATUS_OK) set_error("binding: %s", odm_status_name(st)); else set_error("music binding added");
    s = workspace_json(); if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeTimeline(JNIEnv *env, jobject thiz, jlong sample, jint fps) {
    char *s;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    s = timeline_json((int64_t)sample, (int32_t)fps);
    if (!s) return jstr(env, "{}");
    { jstring out = jstr(env, s); free(s); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshTopology(JNIEnv *env, jobject thiz) {
    sbuf b = {0};
    odm_sha256_digest digest;
    char hex[65] = {0};
    uint32_t i;
    odm_vec3_q32 bmin = {0}, bmax = {0};
    odm_status st;
    (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return jstr(env, "{}");
    st = odm_mesh_authoring_validate(&g_ws.mesh);
    if (st != ODM_STATUS_OK) { set_error("mesh validate: %s", odm_status_name(st)); return jstr(env, "{}"); }
    st = odm_mesh_authoring_document_snapshot_sha256(&g_ws.mesh, &digest);
    if (st != ODM_STATUS_OK || !digest_hex(&digest, hex)) return jstr(env, "{}");
    st = odm_mesh_authoring_measure_bounds(&g_ws.mesh, &bmin, &bmax);
    if (st != ODM_STATUS_OK) return jstr(env, "{}");
    if (!sb_printf(&b, "{\"sha256\":\"%s\",\"vertex_count\":%u,\"face_count\":%u,"
                      "\"vertex_capacity\":%u,\"face_capacity\":%u,"
                      "\"bounds_min\":[%" PRId64 ",%" PRId64 ",%" PRId64 "],"
                      "\"bounds_max\":[%" PRId64 ",%" PRId64 ",%" PRId64 "],\"faces\":[",
                   hex, g_ws.mesh.vertex_count, g_ws.mesh.face_count,
                   g_ws.mesh.vertex_capacity, g_ws.mesh.face_capacity,
                   bmin.x,bmin.y,bmin.z,bmax.x,bmax.y,bmax.z)) goto fail;
    for (i = 0u; i < g_ws.mesh.face_count; ++i) {
        const odm_mesh_authoring_face *f = &g_ws.mesh.faces[i];
        odm_mesh_face_measurement m;
        uint32_t j;
        if (i && !sb_add(&b, ",")) goto fail;
        memset(&m, 0, sizeof(m));
        st = odm_mesh_authoring_measure_face(&g_ws.mesh, f->face_id, &m);
        if (st != ODM_STATUS_OK) goto fail;
        if (!sb_printf(&b, "{\"id\":%" PRIu64 ",\"material\":%u,\"smoothing\":%u,"
                          "\"centroid\":[%" PRId64 ",%" PRId64 ",%" PRId64 "],"
                          "\"normal\":[%d,%d,%d],\"vertices\":[",
                       f->face_id, f->material_slot, f->smoothing_group,
                       m.centroid.x,m.centroid.y,m.centroid.z,
                       m.normal.x,m.normal.y,m.normal.z)) goto fail;
        for (j = 0u; j < f->vertex_count; ++j) {
            if (j && !sb_add(&b, ",")) goto fail;
            if (!sb_printf(&b, "%" PRIu64, f->vertex_ids[j])) goto fail;
        }
        if (!sb_add(&b, "]}")) goto fail;
    }
    if (!sb_add(&b, "],\"vertices\":[")) goto fail;
    for (i = 0u; i < g_ws.mesh.vertex_count; ++i) {
        const odm_mesh_authoring_vertex *v = &g_ws.mesh.vertices[i];
        if (i && !sb_add(&b, ",")) goto fail;
        if (!sb_printf(&b, "{\"id\":%" PRIu64 ",\"p\":[%" PRId64 ",%" PRId64 ",%" PRId64 "]}",
                       v->vertex_id, v->position.x, v->position.y, v->position.z)) goto fail;
    }
    if (!sb_add(&b, "]}")) goto fail;
    { jstring out = jstr(env, b.data); sb_free(&b); return out; }
fail:
    sb_free(&b); set_error("mesh topology serialization failed"); return jstr(env, "{}");
}

JNIEXPORT jlong JNICALL
Java_com_odpar_musicmotion_MainActivity_nativePickFace(JNIEnv *env, jobject thiz,
    jint width, jint height, jdouble pixelX, jdouble pixelY) {
    odm_ray3d ray;
    odm_transform3d identity;
    odm_mesh_surface_segment_hit3d hit;
    odm_status st;
    (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK || width <= 0 || height <= 0) return 0;
    st = odm_camera3d_pixel_ray(&g_ws.editor_view.camera, (uint32_t)width, (uint32_t)height,
        (odm_q32_32)(pixelX * (double)Q32_ONE), (odm_q32_32)(pixelY * (double)Q32_ONE), &ray);
    if (st != ODM_STATUS_OK) { set_error("pick ray: %s", odm_status_name(st)); return 0; }
    memset(&identity, 0, sizeof(identity));
    identity.basis = odm_scene3d_basis_identity();
    identity.scale.x = Q32_ONE; identity.scale.y = Q32_ONE; identity.scale.z = Q32_ONE;
    memset(&hit, 0, sizeof(hit));
    st = odm_mesh_surface_point3d_bind_ray_world(&g_ws.mesh, &identity, &ray, Q32_ONE * 1000, &hit);
    if (st != ODM_STATUS_OK) { set_error("pick surface: %s", odm_status_name(st)); return 0; }
    if (!hit.hit) { set_error("pick miss"); return 0; }
    set_error("picked face %" PRIu64, hit.surface_point.face_id);
    return (jlong)hit.surface_point.face_id;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshDeleteFace(JNIEnv *env, jobject thiz, jlong faceId) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return JNI_FALSE;
    st = odm_mesh_authoring_delete_face(&g_ws.mesh, (uint64_t)faceId);
    if (st != ODM_STATUS_OK) set_error("mesh delete face: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshSetFaceMaterial(JNIEnv *env, jobject thiz, jlong faceId, jint slot) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK || slot < 0) return JNI_FALSE;
    st = odm_mesh_authoring_set_face_material_slot(&g_ws.mesh, (uint64_t)faceId, (uint32_t)slot);
    if (st != ODM_STATUS_OK) set_error("mesh material: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshSetFaceSmoothing(JNIEnv *env, jobject thiz, jlong faceId, jint group) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK || group < 0) return JNI_FALSE;
    st = odm_mesh_authoring_set_face_smoothing_group(&g_ws.mesh, (uint64_t)faceId, (uint32_t)group);
    if (st != ODM_STATUS_OK) set_error("mesh smoothing: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeFocusMesh(JNIEnv *env, jobject thiz, jint width, jint height) {
    odm_vec3_q32 bmin, bmax;
    odm_status st;
    (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK || width <= 0 || height <= 0) return JNI_FALSE;
    st = odm_mesh_authoring_measure_bounds(&g_ws.mesh, &bmin, &bmax);
    if (st == ODM_STATUS_OK)
        st = odm_editor_view3d_focus_aabb(&g_ws.editor_view, bmin, bmax,
            (uint32_t)width, (uint32_t)height, (odm_q1_31)(INT32_MAX / 10));
    if (st != ODM_STATUS_OK) set_error("focus mesh: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeNavigate(JNIEnv *env, jobject thiz,
    jdouble panX, jdouble panY, jdouble dolly) {
    odm_status st = ODM_STATUS_OK;
    (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return JNI_FALSE;
    if (panX != 0.0 || panY != 0.0)
        st = odm_editor_view3d_pan(&g_ws.editor_view,
            (odm_q32_32)(panX * (double)Q32_ONE),
            (odm_q32_32)(panY * (double)Q32_ONE));
    if (st == ODM_STATUS_OK && dolly != 0.0)
        st = odm_editor_view3d_dolly(&g_ws.editor_view,
            (odm_q32_32)(dolly * (double)Q32_ONE));
    if (st != ODM_STATUS_OK) set_error("navigate: %s", odm_status_name(st));
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshPrimitive(JNIEnv *env, jobject thiz, jint kind) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return JNI_FALSE;
    st = mesh_primitive((uint32_t)kind);
    if (st != ODM_STATUS_OK) set_error("mesh primitive: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshExtrudeFace(JNIEnv *env, jobject thiz, jlong faceId, jdouble meters) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return JNI_FALSE;
    st = mesh_grow_retry_extrude((uint64_t)faceId, (odm_q32_32)(meters * (double)Q32_ONE));
    if (st != ODM_STATUS_OK) set_error("mesh extrude: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshInsetFace(JNIEnv *env, jobject thiz, jlong faceId, jdouble remainingScale) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return JNI_FALSE;
    st = mesh_grow_retry_inset((uint64_t)faceId, (odm_q32_32)(remainingScale * (double)Q32_ONE));
    if (st != ODM_STATUS_OK) set_error("mesh inset: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMeshSetDimension(JNIEnv *env, jobject thiz,
    jint axis, jdouble meters, jint anchorMode) {
    odm_status st; (void)env; (void)thiz;
    if (workspace_init() != ODM_STATUS_OK) return JNI_FALSE;
    st = odm_mesh_authoring_set_axis_span(&g_ws.mesh, (uint32_t)axis, (uint32_t)anchorMode,
        (odm_q32_32)(meters * (double)Q32_ONE));
    if (st != ODM_STATUS_OK) set_error("mesh dimension: %s", odm_status_name(st)); else set_error("ok");
    return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jintArray JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeRenderViewport(JNIEnv *env, jobject thiz, jint width, jint height) {
    odm_vertex3d *render_vertices = NULL;
    odm_triangle3d *render_triangles = NULL;
    odm_mesh3d render_mesh;
    uint8_t *rgba = NULL;
    uint32_t *depth = NULL;
    jint *argb = NULL;
    jintArray arr = NULL;
    uint32_t rv = 0u, rt = 0u;
    uint64_t pixels, fragments = 0u, i;
    odm_material3d material;
    odm_asset3d asset;
    odm_instance3d instance;
    odm_lighting3d lights;
    odm_raster3d_target target;
    uint8_t clear[4] = {9u, 12u, 17u, 255u};
    odm_status st;
    (void)thiz;
    if (width <= 0 || height <= 0) return NULL;
    pixels = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height;
    if (pixels > ODM_RENDER_MAX_REFERENCE_PIXELS || pixels > SIZE_MAX / 4u || pixels > (uint64_t)INT32_MAX) {
        set_error("viewport exceeds engine/JNI pixel policy"); return NULL;
    }
    if (workspace_init() != ODM_STATUS_OK) return NULL;
    st = odm_mesh_authoring_compile_counts(&g_ws.mesh, &rv, &rt);
    if (st != ODM_STATUS_OK) { set_error("mesh counts: %s", odm_status_name(st)); return NULL; }
    if (rv > ODM_SCENE3D_MAX_VERTICES || rt > ODM_SCENE3D_MAX_TRIANGLES) {
        set_error("compiled mesh exceeds Scene3D engine limits"); return NULL;
    }
    render_vertices = rv ? (odm_vertex3d *)malloc((size_t)rv * sizeof(*render_vertices)) : NULL;
    render_triangles = rt ? (odm_triangle3d *)malloc((size_t)rt * sizeof(*render_triangles)) : NULL;
    rgba = (uint8_t *)malloc((size_t)pixels * 4u);
    depth = (uint32_t *)calloc((size_t)pixels, sizeof(*depth));
    argb = (jint *)malloc((size_t)pixels * sizeof(*argb));
    if ((rv && !render_vertices) || (rt && !render_triangles) || !rgba || !depth || !argb) {
        set_error("viewport allocation failed"); goto done;
    }
    memset(&render_mesh, 0, sizeof(render_mesh));
    st = odm_mesh_authoring_compile(&g_ws.mesh, render_vertices, rv, render_triangles, rt, &render_mesh);
    if (st != ODM_STATUS_OK) { set_error("mesh compile: %s", odm_status_name(st)); goto done; }
    memset(&material, 0, sizeof(material));
    material.shading_model = ODM_MATERIAL_PBR_LITE;
    material.alpha_mode = ODM_ALPHA_OPAQUE;
    material.base_rgba[0] = 170u; material.base_rgba[1] = 184u; material.base_rgba[2] = 210u; material.base_rgba[3] = 255u;
    material.roughness_u8 = 90u; material.metallic_u8 = 45u; material.specular_u8 = 190u;
    memset(&asset, 0, sizeof(asset)); asset.asset_id = UINT64_C(9001); asset.mesh = &render_mesh; asset.material = &material;
    memset(&instance, 0, sizeof(instance)); instance.instance_id = UINT64_C(9001); instance.asset = &asset;
    instance.transform.basis = odm_scene3d_basis_identity();
    instance.transform.scale.x = Q32_ONE; instance.transform.scale.y = Q32_ONE; instance.transform.scale.z = Q32_ONE;
    memset(&lights, 0, sizeof(lights));
    lights.ambient_rgb16[0] = 12000u; lights.ambient_rgb16[1] = 13000u; lights.ambient_rgb16[2] = 16000u; lights.light_count = 1u;
    lights.lights[0].type = ODM_LIGHT_DIRECTIONAL;
    lights.lights[0].direction.x = (odm_q1_31)(INT32_MAX/3);
    lights.lights[0].direction.y = (odm_q1_31)(-INT32_MAX/2);
    lights.lights[0].direction.z = (odm_q1_31)(INT32_MAX*3LL/4LL);
    lights.lights[0].rgb16[0] = 65535u; lights.lights[0].rgb16[1] = 62000u; lights.lights[0].rgb16[2] = 56000u;
    lights.lights[0].intensity_q31 = (odm_q1_31)(INT32_MAX*3LL/4LL);
    target.width = (uint32_t)width; target.height = (uint32_t)height; target.stride_bytes = (uint32_t)width * 4u;
    target.rgba8 = rgba; target.inv_z_q16_16 = depth;
    st = odm_raster3d_clear(&target, clear);
    if (st == ODM_STATUS_OK) st = odm_raster3d_draw_instance(&g_ws.editor_view.camera, &lights, &instance, &target, &fragments);
    if (st != ODM_STATUS_OK) { set_error("viewport render: %s", odm_status_name(st)); goto done; }
    for (i = 0u; i < pixels; ++i) {
        const uint8_t *px = rgba + i * 4u;
        argb[i] = (jint)(((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2]);
    }
    arr = (*env)->NewIntArray(env, (jsize)pixels);
    if (arr) (*env)->SetIntArrayRegion(env, arr, 0, (jsize)pixels, argb);
    set_error("ok; topology=%uV/%uF; render=%uV/%uT; fragments=%" PRIu64,
              g_ws.mesh.vertex_count, g_ws.mesh.face_count, rv, rt, fragments);
done:
    free(render_vertices); free(render_triangles); free(rgba); free(depth); free(argb);
    return arr;
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeEngineReport(JNIEnv *env, jobject thiz) {
    char *report;
    (void)thiz;
    report = spine_json(odm_spine_report_json);
    if (!report) return jstr(env, "{\"error\":\"spine unavailable\"}");
    { jstring out = jstr(env, report); free(report); return out; }
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSelfTest(JNIEnv *env, jobject thiz) {
    odm_selftest_result r;
    odm_status st;
    char out[1024];
    (void)thiz;
    memset(&r, 0, sizeof(r));
    st = odm_selftest_run(&r);
    snprintf(out, sizeof(out),
        "{\"status\":\"%s\",\"checks\":%u,\"passed\":%u,\"failed\":%u,\"failure_status\":\"%s\"}",
        st == ODM_STATUS_OK ? "pass" : "fail", r.checks, r.passed,
        r.checks >= r.passed ? r.checks-r.passed : 0u,
        odm_status_name(st == ODM_STATUS_OK ? ODM_STATUS_OK : r.failure_status));
    return jstr(env, out);
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeLastError(JNIEnv *env, jobject thiz) {
    (void)thiz;
    return jstr(env, g_error);
}

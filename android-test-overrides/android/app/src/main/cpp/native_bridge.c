#include <jni.h>

#include "odm_app_authoring3d.h"
#include "odm_compositor.h"
#include "odm_fixed.h"
#include "odm_status.h"
#include "odm_supersample.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static odm_app_authoring3d_session *g_session;
static uint32_t g_width;
static uint32_t g_height;
static uint8_t *g_raster_rgba;
static uint64_t g_raster_rgba_capacity;
static uint32_t *g_raster_depth;
static uint64_t g_raster_depth_capacity;
static uint8_t *g_raster_scratch;
static uint64_t g_raster_scratch_capacity;
static uint8_t *g_output_rgba;
static uint64_t g_output_rgba_capacity;
static uint32_t g_quality_tier = ODM_QUALITY_PREVIEW_FAST;
static uint64_t g_last_node_id;
static uint64_t g_pointer_id = UINT64_C(0x4f44504152544f55);
static uint8_t *g_comp_scratch;
static uint64_t g_comp_scratch_capacity;
static uint8_t *g_comp_rgba;
static uint64_t g_comp_rgba_capacity;
static uint8_t *g_core_image_rgba;
static uint64_t g_core_image_capacity;
static uint32_t g_core_image_width;
static uint32_t g_core_image_height;
static uint32_t g_comp_background = ODM_BACKGROUND_DEPTH_FIELD;
static uint32_t g_comp_shape = ODM_CORE_SHAPE_ROUNDED_RECT;
static uint32_t g_comp_layout = ODM_FIELD_LAYOUT_RADIAL;
static uint32_t g_comp_energy = 150u;
static char g_comp_title[ODM_LAYERED_METADATA_BYTES] = "ODPAR MUSIC MOTION";
static char g_comp_artist[ODM_LAYERED_METADATA_BYTES] = "AUTHORING STUDIO";

static const uint8_t demo_obj[] =
    "# ODPAR built-in cube\n"
    "o ODPAR_Cube\n"
    "v -1 -1 -1\n"
    "v  1 -1 -1\n"
    "v  1  1 -1\n"
    "v -1  1 -1\n"
    "v -1 -1  1\n"
    "v  1 -1  1\n"
    "v  1  1  1\n"
    "v -1  1  1\n"
    "f 1 4 3 2\n"
    "f 5 6 7 8\n"
    "f 1 2 6 5\n"
    "f 2 3 7 6\n"
    "f 3 4 8 7\n"
    "f 4 1 5 8\n";

static void buffers_release(void) {
    free(g_raster_rgba);
    free(g_raster_depth);
    free(g_raster_scratch);
    free(g_output_rgba);
    free(g_comp_scratch);
    free(g_comp_rgba);
    free(g_core_image_rgba);
    g_raster_rgba = NULL;
    g_raster_depth = NULL;
    g_raster_scratch = NULL;
    g_output_rgba = NULL;
    g_comp_scratch = NULL;
    g_comp_rgba = NULL;
    g_core_image_rgba = NULL;
    g_raster_rgba_capacity = 0u;
    g_raster_depth_capacity = 0u;
    g_raster_scratch_capacity = 0u;
    g_output_rgba_capacity = 0u;
    g_comp_scratch_capacity = 0u;
    g_comp_rgba_capacity = 0u;
    g_core_image_capacity = 0u;
    g_core_image_width = 0u;
    g_core_image_height = 0u;
}

static int reserve_bytes(void **storage, uint64_t *capacity, uint64_t required) {
    void *next;
    if (!storage || !capacity || required == 0u || required > (uint64_t)SIZE_MAX)
        return 0;
    if (*capacity >= required) return 1;
    next = realloc(*storage, (size_t)required);
    if (!next) return 0;
    *storage = next;
    *capacity = required;
    return 1;
}

static odm_status q32_from_float(jfloat value, odm_q32_32 *out) {
    double scaled;
    if (!out || !isfinite((double)value)) return ODM_STATUS_INVALID_ARGUMENT;
    scaled = (double)value * 4294967296.0;
    if (scaled > (double)INT64_MAX || scaled < (double)INT64_MIN)
        return ODM_STATUS_OVERFLOW;
    *out = (odm_q32_32)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
    return ODM_STATUS_OK;
}

static double q32_to_double(odm_q32_32 value) {
    return (double)value / 4294967296.0;
}

static odm_status collect_vertex_ids(uint64_t **out_ids, uint32_t *out_count,
                                     odm_vec3_q32 *out_center) {
    odm_app_authoring3d_snapshot snapshot;
    uint64_t *ids;
    odm_vec3_q32 bmin = {0, 0, 0};
    odm_vec3_q32 bmax = {0, 0, 0};
    uint32_t i;
    odm_status status;
    if (!g_session || !out_ids || !out_count) return ODM_STATUS_INVALID_ARGUMENT;
    *out_ids = NULL;
    *out_count = 0u;
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK) return status;
    if (snapshot.vertex_count == 0u)
        return ODM_STATUS_INVALID_STATE;
    ids = (uint64_t *)malloc((size_t)snapshot.vertex_count * sizeof(uint64_t));
    if (!ids) return ODM_STATUS_OUT_OF_MEMORY;
    for (i = 0u; i < snapshot.vertex_count; ++i) {
        odm_mesh_authoring_vertex vertex;
        status = odm_app_authoring3d_mesh_vertex_read(g_session, i, &vertex);
        if (status != ODM_STATUS_OK) { free(ids); return status; }
        ids[i] = vertex.vertex_id;
        if (out_center) {
            if (i == 0u) {
                bmin = vertex.position;
                bmax = vertex.position;
            } else {
                if (vertex.position.x < bmin.x) bmin.x = vertex.position.x;
                if (vertex.position.y < bmin.y) bmin.y = vertex.position.y;
                if (vertex.position.z < bmin.z) bmin.z = vertex.position.z;
                if (vertex.position.x > bmax.x) bmax.x = vertex.position.x;
                if (vertex.position.y > bmax.y) bmax.y = vertex.position.y;
                if (vertex.position.z > bmax.z) bmax.z = vertex.position.z;
            }
        }
    }
    if (out_center) {
        odm_vec3_q32 span;
        status = odm_q32_32_sub(bmax.x, bmin.x, &span.x);
        if (status == ODM_STATUS_OK) status = odm_q32_32_sub(bmax.y, bmin.y, &span.y);
        if (status == ODM_STATUS_OK) status = odm_q32_32_sub(bmax.z, bmin.z, &span.z);
        if (status == ODM_STATUS_OK) status = odm_q32_32_add(bmin.x, span.x / 2, &out_center->x);
        if (status == ODM_STATUS_OK) status = odm_q32_32_add(bmin.y, span.y / 2, &out_center->y);
        if (status == ODM_STATUS_OK) status = odm_q32_32_add(bmin.z, span.z / 2, &out_center->z);
        if (status != ODM_STATUS_OK) { free(ids); return status; }
    }
    *out_ids = ids;
    *out_count = snapshot.vertex_count;
    return ODM_STATUS_OK;
}

static odm_status session_replace_with_obj(const uint8_t *bytes, uint64_t byte_count) {
    odm_model_import_report report;
    odm_status status;
    if (!g_session || !bytes || byte_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_app_authoring3d_import_obj(g_session, bytes, byte_count,
                                             ODM_Q32_ONE, &report);
    if (status != ODM_STATUS_OK) return status;
    g_last_node_id = 0u;
    return ODM_STATUS_OK;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeCreate(JNIEnv *env, jclass type,
                                                       jint width, jint height) {
    odm_app_authoring3d_limits limits;
    odm_status status;
    (void)env;
    (void)type;
    if (width <= 0 || height <= 0) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    if (g_session) (void)odm_app_authoring3d_session_destroy(&g_session);
    buffers_release();
    g_quality_tier = ODM_QUALITY_PREVIEW_FAST;
    g_last_node_id = 0u;
    status = odm_app_authoring3d_limits_default(&limits);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_session_create(&limits, &g_session);
    if (status != ODM_STATUS_OK) return (jint)status;
    g_width = (uint32_t)width;
    g_height = (uint32_t)height;
    status = odm_app_authoring3d_viewport_set(g_session, g_width, g_height);
    if (status == ODM_STATUS_OK) {
        status = session_replace_with_obj(demo_obj, (uint64_t)sizeof(demo_obj) - 1u);
    }
    if (status != ODM_STATUS_OK) {
        (void)odm_app_authoring3d_session_destroy(&g_session);
        g_width = 0u;
        g_height = 0u;
    }
    return (jint)status;
}

JNIEXPORT void JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeDestroy(JNIEnv *env, jclass type) {
    (void)env;
    (void)type;
    if (g_session) (void)odm_app_authoring3d_session_destroy(&g_session);
    buffers_release();
    g_width = 0u;
    g_height = 0u;
    g_last_node_id = 0u;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeResize(JNIEnv *env, jclass type,
                                                       jint width, jint height) {
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session || width <= 0 || height <= 0)
        return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = odm_app_authoring3d_viewport_set(g_session, (uint32_t)width,
                                               (uint32_t)height);
    if (status == ODM_STATUS_OK) {
        g_width = (uint32_t)width;
        g_height = (uint32_t)height;
    }
    return (jint)status;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeImportObj(JNIEnv *env, jclass type,
                                                          jbyteArray source) {
    jbyte *bytes;
    jsize byte_count;
    odm_status status;
    (void)type;
    if (!g_session || !source) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    byte_count = (*env)->GetArrayLength(env, source);
    if (byte_count <= 0) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    bytes = (*env)->GetByteArrayElements(env, source, NULL);
    if (!bytes) return (jint)ODM_STATUS_OUT_OF_MEMORY;
    status = session_replace_with_obj((const uint8_t *)bytes, (uint64_t)byte_count);
    (*env)->ReleaseByteArrayElements(env, source, bytes, JNI_ABORT);
    return (jint)status;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeFocus(JNIEnv *env, jclass type) {
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    return (jint)odm_app_authoring3d_view_focus_model(g_session, INT32_MAX / 8);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeResetDemo(JNIEnv *env, jclass type) {
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = session_replace_with_obj(demo_obj, (uint64_t)sizeof(demo_obj) - 1u);
    if (status == ODM_STATUS_OK) g_last_node_id = 0u;
    return (jint)status;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativePan(JNIEnv *env, jclass type,
                                                   jfloat right, jfloat up) {
    odm_q32_32 right_q32;
    odm_q32_32 up_q32;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(right, &right_q32);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(up, &up_q32);
    if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_view_pan(g_session, right_q32, up_q32);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeDolly(JNIEnv *env, jclass type,
                                                     jfloat forward) {
    odm_q32_32 forward_q32;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(forward, &forward_q32);
    if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_view_dolly(g_session, forward_q32);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeMoveModel(JNIEnv *env, jclass type,
                                                         jfloat x, jfloat y, jfloat z) {
    odm_app_authoring3d_snapshot snapshot;
    odm_vec3_q32 delta;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(x, &delta.x);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(y, &delta.y);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(z, &delta.z);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_q32_32_add(snapshot.model_transform.position.x, delta.x,
                            &snapshot.model_transform.position.x);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_q32_32_add(snapshot.model_transform.position.y, delta.y,
                            &snapshot.model_transform.position.y);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_q32_32_add(snapshot.model_transform.position.z, delta.z,
                            &snapshot.model_transform.position.z);
    if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_model_transform_set(g_session,
                                                          &snapshot.model_transform);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSetQuality(JNIEnv *env, jclass type,
                                                          jint quality) {
    odm_app_authoring3d_render_requirements requirements;
    uint64_t total;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    if (quality < 0 || quality >= (jint)ODM_QUALITY_COUNT)
        return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = odm_app_authoring3d_render_requirements_query(
        g_session, (uint32_t)quality, &requirements);
    if (status != ODM_STATUS_OK) return (jint)status;
    total = requirements.raster_rgba_bytes;
    if (UINT64_MAX - total < requirements.raster_depth_count * sizeof(uint32_t))
        return (jint)ODM_STATUS_OVERFLOW;
    total += requirements.raster_depth_count * sizeof(uint32_t);
    if (UINT64_MAX - total < requirements.raster_scratch_bytes)
        return (jint)ODM_STATUS_OVERFLOW;
    total += requirements.raster_scratch_bytes;
    if (UINT64_MAX - total < requirements.output_rgba_bytes)
        return (jint)ODM_STATUS_OVERFLOW;
    total += requirements.output_rgba_bytes;
    /* Preview state is never degraded silently. A tier that would require
     * more than 512 MiB is rejected explicitly; project/geometry authority is
     * unaffected and the user can choose a smaller viewport/tier. */
    if (total > UINT64_C(536870912)) return (jint)ODM_STATUS_BUDGET_EXCEEDED;
    g_quality_tier = (uint32_t)quality;
    return (jint)ODM_STATUS_OK;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeOrbit(JNIEnv *env, jclass type,
                                                     jfloat yaw_degrees,
                                                     jfloat pitch_degrees) {
    odm_app_authoring3d_snapshot snapshot;
    odm_vec3_q32 position;
    odm_vec3_q32 target;
    odm_vec3_q31 world_up = {0, INT32_MAX, 0};
    double px, py, pz, radius, yaw, pitch, current_yaw, current_pitch;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session || !isfinite((double)yaw_degrees) ||
        !isfinite((double)pitch_degrees)) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK) return (jint)status;
    target = snapshot.editor_view.pivot;
    px = q32_to_double(snapshot.editor_view.camera.position.x) - q32_to_double(target.x);
    py = q32_to_double(snapshot.editor_view.camera.position.y) - q32_to_double(target.y);
    pz = q32_to_double(snapshot.editor_view.camera.position.z) - q32_to_double(target.z);
    radius = sqrt(px * px + py * py + pz * pz);
    if (!(radius > 0.0001)) return (jint)ODM_STATUS_INVALID_STATE;
    current_yaw = atan2(px, pz);
    current_pitch = asin(fmax(-1.0, fmin(1.0, py / radius)));
    yaw = current_yaw + (double)yaw_degrees * 0.017453292519943295;
    pitch = current_pitch + (double)pitch_degrees * 0.017453292519943295;
    pitch = fmax(-1.4835298641951802, fmin(1.4835298641951802, pitch));
    px = radius * cos(pitch) * sin(yaw);
    py = radius * sin(pitch);
    pz = radius * cos(pitch) * cos(yaw);
    status = q32_from_float((jfloat)(q32_to_double(target.x) + px), &position.x);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float((jfloat)(q32_to_double(target.y) + py), &position.y);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float((jfloat)(q32_to_double(target.z) + pz), &position.z);
    if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_view_set_pose(g_session, position, target,
                                                    world_up, 0u);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeScaleModel(JNIEnv *env, jclass type,
                                                          jfloat factor) {
    odm_app_authoring3d_snapshot snapshot;
    odm_q32_32 factor_q32;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session || factor <= 0.0f) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = q32_from_float(factor, &factor_q32);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_q32_32_mul(snapshot.model_transform.scale.x, factor_q32,
                            &snapshot.model_transform.scale.x);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_q32_32_mul(snapshot.model_transform.scale.y, factor_q32,
                            &snapshot.model_transform.scale.y);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_q32_32_mul(snapshot.model_transform.scale.z, factor_q32,
                            &snapshot.model_transform.scale.z);
    if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_model_transform_set(g_session,
                                                          &snapshot.model_transform);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeRotateGeometry(JNIEnv *env, jclass type,
                                                              jint axis,
                                                              jfloat degrees) {
    uint64_t *ids;
    uint32_t count;
    odm_vec3_q32 pivot;
    odm_vec3_q31 rotation_axis = {0, 0, 0};
    double turns;
    uint32_t phase;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session || axis < 0 || axis > 2 || !isfinite((double)degrees))
        return (jint)ODM_STATUS_INVALID_ARGUMENT;
    if (axis == 0) rotation_axis.x = INT32_MAX;
    else if (axis == 1) rotation_axis.y = INT32_MAX;
    else rotation_axis.z = INT32_MAX;
    turns = fmod((double)degrees / 360.0, 1.0);
    if (turns < 0.0) turns += 1.0;
    phase = (uint32_t)(turns * 4294967296.0);
    status = collect_vertex_ids(&ids, &count, &pivot);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_mesh_vertices_rotate(g_session, ids, count,
                                                       pivot, rotation_axis, phase);
    free(ids);
    return (jint)status;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeScaleGeometry(JNIEnv *env, jclass type,
                                                             jfloat factor) {
    uint64_t *ids;
    uint32_t count;
    odm_vec3_q32 pivot;
    odm_vec3_q32 scale;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session || factor <= 0.0f) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = q32_from_float(factor, &scale.x);
    if (status != ODM_STATUS_OK) return (jint)status;
    scale.y = scale.x;
    scale.z = scale.x;
    status = collect_vertex_ids(&ids, &count, &pivot);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_mesh_vertices_scale(g_session, ids, count,
                                                      pivot, scale);
    free(ids);
    return (jint)status;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeTranslateGeometry(JNIEnv *env, jclass type,
                                                                 jfloat x, jfloat y,
                                                                 jfloat z) {
    uint64_t *ids;
    uint32_t count;
    odm_vec3_q32 delta;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(x, &delta.x);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(y, &delta.y);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(z, &delta.z);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = collect_vertex_ids(&ids, &count, NULL);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_mesh_vertices_translate(g_session, ids, count,
                                                          delta);
    free(ids);
    return (jint)status;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSetMaterial(JNIEnv *env, jclass type,
                                                           jint preset,
                                                           jint roughness,
                                                           jint metallic) {
    static const uint8_t colors[][4] = {
        {74u, 144u, 226u, 255u}, {226u, 82u, 92u, 255u},
        {92u, 205u, 142u, 255u}, {230u, 178u, 70u, 255u},
        {205u, 210u, 224u, 255u}, {35u, 39u, 48u, 255u}
    };
    odm_app_authoring3d_snapshot snapshot;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session || preset < 0 || preset >= (jint)(sizeof(colors) / sizeof(colors[0])) ||
        roughness < 0 || roughness > 255 || metallic < 0 || metallic > 255)
        return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK) return (jint)status;
    snapshot.material.shading_model = ODM_MATERIAL_PBR_LITE;
    snapshot.material.alpha_mode = ODM_ALPHA_OPAQUE;
    memcpy(snapshot.material.base_rgba, colors[preset], 4u);
    snapshot.material.roughness_u8 = (uint8_t)roughness;
    snapshot.material.metallic_u8 = (uint8_t)metallic;
    snapshot.material.specular_u8 = 128u;
    return (jint)odm_app_authoring3d_material_set(g_session, &snapshot.material);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeAddSurfaceNode(JNIEnv *env, jclass type,
                                                              jfloat x, jfloat y,
                                                              jint connect) {
    odm_q32_32 x_q32, y_q32;
    uint64_t node_id = 0u;
    uint64_t edge_id = 0u;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(x, &x_q32); if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(y, &y_q32); if (status != ODM_STATUS_OK) return (jint)status;
    status = odm_app_authoring3d_node_add_surface_at_pixel(
        g_session, x_q32, y_q32, ODM_Q32_ONE * INT64_C(1000),
        ODM_NODE3D_ROLE_SURFACE_ANCHOR, &node_id);
    if (status != ODM_STATUS_OK) return (jint)status;
    if (connect != 0 && g_last_node_id != 0u) {
        status = odm_app_authoring3d_node_connect(
            g_session, g_last_node_id, node_id, ODM_NODE3D_EDGE_PATH_SEGMENT,
            UINT64_C(0x4150505041544831), &edge_id);
        if (status != ODM_STATUS_OK) return (jint)status;
    }
    g_last_node_id = node_id;
    return (jint)odm_app_authoring3d_node_select(g_session, node_id);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeClearSelection(JNIEnv *env, jclass type) {
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    return (jint)odm_app_authoring3d_node_select(g_session, 0u);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeManipulationBegin(JNIEnv *env, jclass type,
                                                                 jfloat x, jfloat y) {
    odm_q32_32 x_q32, y_q32;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(x, &x_q32); if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(y, &y_q32); if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_manipulation_begin(
        g_session, g_pointer_id, ODM_DIRECT_MANIPULATION3D_MESH_SURFACE,
        x_q32, y_q32);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeManipulationUpdate(JNIEnv *env, jclass type,
                                                                  jfloat x, jfloat y) {
    odm_direct_manipulation3d_result result;
    odm_q32_32 x_q32, y_q32;
    odm_status status;
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    status = q32_from_float(x, &x_q32); if (status != ODM_STATUS_OK) return (jint)status;
    status = q32_from_float(y, &y_q32); if (status != ODM_STATUS_OK) return (jint)status;
    return (jint)odm_app_authoring3d_manipulation_update(
        g_session, g_pointer_id, x_q32, y_q32,
        ODM_Q32_ONE * INT64_C(1000), &result);
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeManipulationEnd(JNIEnv *env, jclass type) {
    (void)env;
    (void)type;
    if (!g_session) return (jint)ODM_STATUS_INVALID_STATE;
    return (jint)odm_app_authoring3d_manipulation_end(g_session, g_pointer_id);
}

JNIEXPORT jintArray JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeNodePoints(JNIEnv *env, jclass type) {
    odm_app_authoring3d_snapshot snapshot;
    jintArray result;
    jint *points = NULL;
    uint64_t value_count;
    uint32_t written = 0u;
    uint32_t i;
    odm_status status;
    (void)type;
    if (!g_session) return NULL;
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK) return NULL;
    value_count = (uint64_t)snapshot.node_count * UINT64_C(4);
    if (value_count > (uint64_t)INT_MAX ||
        value_count > (uint64_t)SIZE_MAX / sizeof(jint)) return NULL;
    if (value_count != 0u) {
        points = (jint *)malloc((size_t)value_count * sizeof(jint));
        if (!points) return NULL;
    }
    for (i = 0u; i < snapshot.node_count; ++i) {
        odm_authoring_node3d node;
        odm_projected3d projected;
        status = odm_app_authoring3d_node_read(g_session, i, &node);
        if (status != ODM_STATUS_OK) { free(points); return NULL; }
        status = odm_app_authoring3d_node_project(g_session, node.node_id, &projected);
        if (status != ODM_STATUS_OK || projected.visible == 0u) continue;
        points[written * 4u] = projected.x_subpixel /
            (jint)(UINT32_C(1) << ODM_RASTER3D_SUBPIXEL_BITS);
        points[written * 4u + 1u] = projected.y_subpixel /
            (jint)(UINT32_C(1) << ODM_RASTER3D_SUBPIXEL_BITS);
        points[written * 4u + 2u] =
            (node.node_id == snapshot.selected_node_id) ? 1 : 0;
        points[written * 4u + 3u] = (jint)(node.node_id & UINT64_C(0x7fffffff));
        ++written;
    }
    result = (*env)->NewIntArray(env, (jsize)(written * 4u));
    if (result && written != 0u)
        (*env)->SetIntArrayRegion(env, result, 0, (jsize)(written * 4u), points);
    free(points);
    return result;
}

static odm_status copy_metadata_bytes(JNIEnv *env, jbyteArray source,
                                      char out[ODM_LAYERED_METADATA_BYTES]) {
    jbyte *bytes;
    jsize byte_count;
    if (!env || !source || !out) return ODM_STATUS_INVALID_ARGUMENT;
    byte_count = (*env)->GetArrayLength(env, source);
    if (byte_count < 0 || byte_count >= (jsize)ODM_LAYERED_METADATA_BYTES)
        return ODM_STATUS_INVALID_DATA;
    bytes = (*env)->GetByteArrayElements(env, source, NULL);
    if (!bytes && byte_count != 0) return ODM_STATUS_OUT_OF_MEMORY;
    memset(out, 0, ODM_LAYERED_METADATA_BYTES);
    if (byte_count != 0) memcpy(out, bytes, (size_t)byte_count);
    if (bytes) (*env)->ReleaseByteArrayElements(env, source, bytes, JNI_ABORT);
    return ODM_STATUS_OK;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSetComposition(JNIEnv *env, jclass type,
                                                              jbyteArray title,
                                                              jbyteArray artist,
                                                              jint background,
                                                              jint shape,
                                                              jint layout,
                                                              jint energy) {
    char next_title[ODM_LAYERED_METADATA_BYTES];
    char next_artist[ODM_LAYERED_METADATA_BYTES];
    odm_status status;
    (void)type;
    if (!title || !artist || background < (jint)ODM_BACKGROUND_NONE ||
        background > (jint)ODM_BACKGROUND_STYLE_MAX ||
        shape < (jint)ODM_CORE_SHAPE_CIRCLE ||
        shape > (jint)ODM_CORE_SHAPE_ROUNDED_RECT ||
        layout < (jint)ODM_FIELD_LAYOUT_RADIAL ||
        layout > (jint)ODM_FIELD_LAYOUT_MAX || energy < 0 || energy > 255)
        return (jint)ODM_STATUS_INVALID_ARGUMENT;
    status = copy_metadata_bytes(env, title, next_title);
    if (status != ODM_STATUS_OK) return (jint)status;
    status = copy_metadata_bytes(env, artist, next_artist);
    if (status != ODM_STATUS_OK) return (jint)status;
    memcpy(g_comp_title, next_title, sizeof(g_comp_title));
    memcpy(g_comp_artist, next_artist, sizeof(g_comp_artist));
    g_comp_background = (uint32_t)background;
    g_comp_shape = (uint32_t)shape;
    g_comp_layout = (uint32_t)layout;
    g_comp_energy = (uint32_t)energy;
    return (jint)ODM_STATUS_OK;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSetCoreImage(JNIEnv *env, jclass type,
                                                            jbyteArray rgba,
                                                            jint width,
                                                            jint height) {
    jbyte *bytes;
    jsize byte_count;
    uint64_t required;
    uint8_t *next;
    (void)type;
    if (!rgba || width <= 0 || height <= 0) return (jint)ODM_STATUS_INVALID_ARGUMENT;
    required = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height * UINT64_C(4);
    if (required == 0u || required > UINT64_C(268435456) ||
        required > (uint64_t)SIZE_MAX || required > (uint64_t)INT_MAX)
        return (jint)ODM_STATUS_BUDGET_EXCEEDED;
    byte_count = (*env)->GetArrayLength(env, rgba);
    if ((uint64_t)byte_count != required) return (jint)ODM_STATUS_INVALID_DATA;
    bytes = (*env)->GetByteArrayElements(env, rgba, NULL);
    if (!bytes) return (jint)ODM_STATUS_OUT_OF_MEMORY;
    next = (uint8_t *)malloc((size_t)required);
    if (!next) {
        (*env)->ReleaseByteArrayElements(env, rgba, bytes, JNI_ABORT);
        return (jint)ODM_STATUS_OUT_OF_MEMORY;
    }
    memcpy(next, bytes, (size_t)required);
    (*env)->ReleaseByteArrayElements(env, rgba, bytes, JNI_ABORT);
    free(g_core_image_rgba);
    g_core_image_rgba = next;
    g_core_image_capacity = required;
    g_core_image_width = (uint32_t)width;
    g_core_image_height = (uint32_t)height;
    return (jint)ODM_STATUS_OK;
}

JNIEXPORT jint JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeClearCoreImage(JNIEnv *env, jclass type) {
    (void)env;
    (void)type;
    free(g_core_image_rgba);
    g_core_image_rgba = NULL;
    g_core_image_capacity = 0u;
    g_core_image_width = 0u;
    g_core_image_height = 0u;
    return (jint)ODM_STATUS_OK;
}

JNIEXPORT jintArray JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeRenderComposition(
    JNIEnv *env, jclass type, jint width, jint height,
    jlong sample, jlong duration) {
    static const uint8_t fallback_core[16] = {
        42u, 91u, 188u, 255u, 100u, 224u, 190u, 255u,
        224u, 91u, 155u, 255u, 246u, 191u, 82u, 255u
    };
    odm_layered_config config;
    odm_composition_frame_state composition;
    odm_director_frame_state director;
    odm_layered_frame_plan plan;
    odm_render_surface_frame surface;
    odm_sha256_digest pixel_sha;
    uint64_t frame_bytes = 0u;
    uint64_t scratch_bytes = 0u;
    uint64_t required_frame = 0u;
    uint64_t required_scratch = 0u;
    uint64_t pixels;
    uint32_t energy_q31;
    uint32_t i;
    jintArray result;
    jint *argb;
    odm_status status;
    (void)type;
    if (width <= 0 || height <= 0 || duration <= 0 || sample < 0 || sample > duration)
        return NULL;
    status = odm_layered_config_init_default(&config, ODM_CANVAS_ASPECT_CUSTOM,
                                              (uint32_t)width, (uint32_t)height, 30);
    if (status != ODM_STATUS_OK) return NULL;
    config.background.style = g_comp_background;
    config.field.layout = g_comp_layout;
    status = odm_layered_config_set_core_shape(&config, g_comp_shape);
    if (status != ODM_STATUS_OK) return NULL;
    status = odm_layered_config_set_metadata(&config, g_comp_title, g_comp_artist);
    if (status != ODM_STATUS_OK) return NULL;
    status = odm_layered_config_validate(&config);
    if (status != ODM_STATUS_OK) return NULL;
    memset(&composition, 0, sizeof(composition));
    memset(&director, 0, sizeof(director));
    composition.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    composition.mode = ODM_COMPOSITION_MODE_FLOW;
    composition.center_sample = (uint64_t)sample;
    composition.tick_index = (uint64_t)sample / ODM_MUSIC_TICK_SAMPLES;
    energy_q31 = (uint32_t)(((uint64_t)INT32_MAX * g_comp_energy + 127u) / 255u);
    composition.core_scale_q31 = energy_q31;
    composition.core_breath_q31 = energy_q31;
    composition.radial_gain_q31 = energy_q31;
    composition.particles_q31 = energy_q31 / 2u;
    composition.grid_q31 = energy_q31 / 3u;
    composition.accent_q31 = energy_q31;
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i)
        composition.radial_q31[i] =
            (uint32_t)(((uint64_t)energy_q31 * (uint64_t)(i + 17u)) /
                       (uint64_t)(ODM_COMPOSITION_RADIAL_SEGMENTS_MAX + 16u));
    director.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    director.tick_index = composition.tick_index;
    director.layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
    director.macro_energy_q31 = energy_q31;
    status = odm_layered_resolve_frame_plan(&config, &composition, &director,
                                             (int64_t)sample, (int64_t)duration,
                                             &plan);
    if (status != ODM_STATUS_OK) return NULL;
    memset(&surface, 0, sizeof(surface));
    surface.width = g_core_image_rgba ? g_core_image_width : 2u;
    surface.height = g_core_image_rgba ? g_core_image_height : 2u;
    surface.pixel_format = ODM_RENDER_SURFACE_RGBA8;
    surface.primaries = ODM_COLOR_PRIMARIES_BT709;
    surface.transfer = ODM_COLOR_TRANSFER_SRGB;
    surface.alpha_mode = ODM_ALPHA_STRAIGHT;
    surface.start_sample = 0;
    surface.end_sample = (int64_t)duration;
    surface.pixel_bytes = (uint64_t)surface.width * surface.height * UINT64_C(4);
    surface.pixels = g_core_image_rgba ? g_core_image_rgba : fallback_core;
    status = odm_layered_render_requirements(
        &config, ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
        &frame_bytes, &scratch_bytes);
    if (status != ODM_STATUS_OK || frame_bytes > UINT64_C(268435456) ||
        scratch_bytes > UINT64_C(268435456)) return NULL;
    if (!reserve_bytes((void **)&g_comp_scratch, &g_comp_scratch_capacity,
                       scratch_bytes) ||
        !reserve_bytes((void **)&g_comp_rgba, &g_comp_rgba_capacity, frame_bytes))
        return NULL;
    status = odm_layered_render_frame(
        &config, &plan, &surface, NULL,
        ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
        g_comp_scratch, g_comp_scratch_capacity,
        g_comp_rgba, g_comp_rgba_capacity,
        &required_frame, &required_scratch, &pixel_sha);
    if (status != ODM_STATUS_OK || required_frame != frame_bytes ||
        required_scratch > g_comp_scratch_capacity) return NULL;
    pixels = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height;
    if (pixels == 0u || pixels > (uint64_t)INT_MAX ||
        pixels > (uint64_t)SIZE_MAX / sizeof(jint)) return NULL;
    argb = (jint *)malloc((size_t)pixels * sizeof(jint));
    if (!argb) return NULL;
    for (i = 0u; (uint64_t)i < pixels; ++i) {
        const uint8_t *rgba = g_comp_rgba + (uint64_t)i * 4u;
        argb[i] = (jint)(((uint32_t)rgba[3] << 24u) |
                         ((uint32_t)rgba[0] << 16u) |
                         ((uint32_t)rgba[1] << 8u) |
                         (uint32_t)rgba[2]);
    }
    result = (*env)->NewIntArray(env, (jsize)pixels);
    if (result) (*env)->SetIntArrayRegion(env, result, 0, (jsize)pixels, argb);
    free(argb);
    return result;
}

JNIEXPORT jintArray JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeRender(JNIEnv *env, jclass type) {
    odm_app_authoring3d_render_requirements requirements;
    const uint8_t clear_rgba[4] = {14u, 18u, 28u, 255u};
    uint64_t required_output = 0u;
    uint64_t fragments = 0u;
    uint64_t pixels;
    jintArray result;
    jint *argb;
    uint64_t i;
    odm_status status;
    (void)type;
    if (!g_session || g_width == 0u || g_height == 0u) return NULL;
    status = odm_app_authoring3d_render_requirements_query(
        g_session, g_quality_tier, &requirements);
    if (status != ODM_STATUS_OK) return NULL;
    if (!reserve_bytes((void **)&g_raster_rgba, &g_raster_rgba_capacity,
                       requirements.raster_rgba_bytes) ||
        !reserve_bytes((void **)&g_raster_depth, &g_raster_depth_capacity,
                       requirements.raster_depth_count * (uint64_t)sizeof(uint32_t)) ||
        !reserve_bytes((void **)&g_raster_scratch, &g_raster_scratch_capacity,
                       requirements.raster_scratch_bytes) ||
        !reserve_bytes((void **)&g_output_rgba, &g_output_rgba_capacity,
                       requirements.output_rgba_bytes)) {
        return NULL;
    }
    status = odm_app_authoring3d_render_rgba8(
        g_session, g_quality_tier, NULL, clear_rgba,
        g_raster_rgba, g_raster_rgba_capacity,
        g_raster_depth, g_raster_depth_capacity / sizeof(uint32_t),
        g_raster_scratch, g_raster_scratch_capacity,
        g_output_rgba, g_output_rgba_capacity,
        &required_output, &fragments);
    if (status != ODM_STATUS_OK || required_output != requirements.output_rgba_bytes)
        return NULL;
    pixels = (uint64_t)g_width * (uint64_t)g_height;
    if (pixels == 0u || pixels > (uint64_t)INT_MAX ||
        pixels > (uint64_t)SIZE_MAX / sizeof(jint)) return NULL;
    argb = (jint *)malloc((size_t)pixels * sizeof(jint));
    if (!argb) return NULL;
    for (i = 0u; i < pixels; ++i) {
        const uint8_t *rgba = g_output_rgba + i * 4u;
        argb[i] = (jint)(((uint32_t)rgba[3] << 24u) |
                         ((uint32_t)rgba[0] << 16u) |
                         ((uint32_t)rgba[1] << 8u) |
                         (uint32_t)rgba[2]);
    }
    result = (*env)->NewIntArray(env, (jsize)pixels);
    if (result) (*env)->SetIntArrayRegion(env, result, 0, (jsize)pixels, argb);
    free(argb);
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_odpar_musicmotion_MainActivity_nativeSummary(JNIEnv *env, jclass type) {
    odm_app_authoring3d_snapshot snapshot;
    odm_app_authoring3d_abi_info abi;
    static const char *quality_names[] = {"1x", "2x", "3x", "4x"};
    char text[384];
    odm_status status;
    (void)type;
    if (!g_session) return (*env)->NewStringUTF(env, "Motor no iniciado");
    status = odm_app_authoring3d_snapshot_read(g_session, &snapshot);
    if (status != ODM_STATUS_OK)
        return (*env)->NewStringUTF(env, odm_status_name(status));
    status = odm_app_authoring3d_abi_query(&abi);
    if (status != ODM_STATUS_OK)
        return (*env)->NewStringUTF(env, odm_status_name(status));
    (void)snprintf(text, sizeof(text),
                   "ABI %" PRIu32 ".%" PRIu32 " · %" PRIu32 " vtx / %" PRIu32
                   " caras / %" PRIu32 " tri · %" PRIu32 " nodos / %" PRIu32
                   " rutas · PBR r%u m%u · %s · %.1f MiB · gen %" PRIu64,
                   abi.abi_major, abi.abi_minor, snapshot.vertex_count,
                   snapshot.face_count, snapshot.render_triangle_count,
                   snapshot.node_count, snapshot.edge_count,
                   (unsigned)snapshot.material.roughness_u8,
                   (unsigned)snapshot.material.metallic_u8,
                   g_quality_tier < ODM_QUALITY_COUNT ? quality_names[g_quality_tier] : "?",
                   (double)snapshot.memory.current_bytes / (1024.0 * 1024.0),
                   snapshot.generation);
    return (*env)->NewStringUTF(env, text);
}

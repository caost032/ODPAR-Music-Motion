#define _POSIX_C_SOURCE 200809L
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "odm_authoring_session.h"
#include "odm_authoring_timeline.h"
#include "odm_editor_view3d.h"
#include "odm_mesh_authoring.h"
#include "odm_mesh_authoring_primitives.h"
#include "odm_mesh_authoring_dimensions.h"
#include "odm_presentation.h"
#include "odm_presentation_builtin.h"
#include "odm_scene3d.h"
#include "odm_spine.h"
#include "odm_status.h"
#include "odm_timeline_view.h"
#include "odm_version.h"

#define MAX_OBJECTS 1024u
#define MAX_VALUES 8192u
#define MESH_VERTS 4096u
#define MESH_FACES 4096u
#define RENDER_VERTS 32768u
#define RENDER_TRIS 32768u

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_ready = 0;
static char g_error[768];
static odm_presentation_object g_objects[MAX_OBJECTS];
static odm_presentation_parameter_value g_values[MAX_VALUES];
static uint32_t g_object_count = 0u;
static uint32_t g_value_count = 0u;
static uint64_t g_next_object_id = UINT64_C(10000);
static odm_presentation_document g_doc;
static odm_authoring_session g_session;
static odm_editor_view3d g_view;
static odm_timeline_view g_timeline_view;
static int64_t g_duration_samples = INT64_C(48000) * INT64_C(180);
static int64_t g_playhead_sample = 0;

static odm_mesh_authoring_vertex g_mesh_vertices[MESH_VERTS];
static odm_mesh_authoring_face g_mesh_faces[MESH_FACES];
static odm_mesh_authoring_document g_mesh;
static odm_vertex3d g_render_vertices[RENDER_VERTS];
static odm_triangle3d g_render_tris[RENDER_TRIS];
static odm_mesh3d g_render_mesh;

static const char *domain_name(uint32_t d) {
    static const char *names[] = {
        "", "Camera", "Models3D", "Environment", "Composition", "Core/Media",
        "Particles", "Lighting", "Overlay2D", "Audio/Reaction", "Post", "Output", "Assets"
    };
    return d <= 12u ? names[d] : "Unknown";
}

static uint64_t root_id_for_domain(uint32_t d) { return UINT64_C(100) * (uint64_t)d; }

static jstring jstr(JNIEnv *env, const char *s) {
    return (*env)->NewStringUTF(env, s ? s : "");
}

static void set_error_status(const char *where, odm_status st) {
    snprintf(g_error, sizeof(g_error), "%s: %s", where, odm_status_name(st));
}

static void refresh_doc(void) {
    memset(&g_doc, 0, sizeof(g_doc));
    g_doc.schema_major = ODM_PRESENTATION_SCHEMA_MAJOR;
    g_doc.schema_minor = ODM_PRESENTATION_SCHEMA_MINOR;
    g_doc.project_seed = UINT64_C(0x4f445041525f4d4d);
    g_doc.objects = g_objects;
    g_doc.object_count = g_object_count;
    g_doc.values = g_values;
    g_doc.value_count = g_value_count;
}

static int value_components(uint32_t type) {
    switch (type) {
        case ODM_PRESENTATION_VALUE_BOOL:
        case ODM_PRESENTATION_VALUE_U32:
        case ODM_PRESENTATION_VALUE_I64:
        case ODM_PRESENTATION_VALUE_Q1_31:
        case ODM_PRESENTATION_VALUE_Q32_32:
        case ODM_PRESENTATION_VALUE_PHASE_U32:
        case ODM_PRESENTATION_VALUE_RESOURCE_ID:
        case ODM_PRESENTATION_VALUE_ENUM: return 1;
        case ODM_PRESENTATION_VALUE_VEC2_Q32_32: return 2;
        case ODM_PRESENTATION_VALUE_VEC3_Q32_32:
        case ODM_PRESENTATION_VALUE_COLOR_RGB16: return 3;
        case ODM_PRESENTATION_VALUE_COLOR_RGBA16: return 4;
        default: return 0;
    }
}

static int cmp_value_key(uint64_t object_id, uint32_t parameter_id,
                         const odm_presentation_parameter_value *v) {
    if (object_id < v->object_id) return -1;
    if (object_id > v->object_id) return 1;
    if (parameter_id < v->parameter_id) return -1;
    if (parameter_id > v->parameter_id) return 1;
    return 0;
}

static void find_value_index(uint64_t object_id, uint32_t parameter_id,
                             uint32_t *out_index, int *out_found) {
    uint32_t lo = 0u, hi = g_value_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        int c = cmp_value_key(object_id, parameter_id, &g_values[mid]);
        if (c == 0) { *out_index = mid; *out_found = 1; return; }
        if (c < 0) hi = mid; else lo = mid + 1u;
    }
    *out_index = lo; *out_found = 0;
}

static const odm_presentation_object *find_object(uint64_t object_id, uint32_t *out_index) {
    uint32_t lo = 0u, hi = g_object_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (g_objects[mid].object_id == object_id) {
            if (out_index) *out_index = mid;
            return &g_objects[mid];
        }
        if (object_id < g_objects[mid].object_id) hi = mid; else lo = mid + 1u;
    }
    return NULL;
}

static int insert_value(const odm_presentation_parameter_value *v) {
    uint32_t index = 0u; int found = 0;
    if (g_value_count >= MAX_VALUES) return 0;
    find_value_index(v->object_id, v->parameter_id, &index, &found);
    if (found) { g_values[index] = *v; return 1; }
    memmove(&g_values[index + 1u], &g_values[index],
            (g_value_count - index) * sizeof(g_values[0]));
    g_values[index] = *v;
    ++g_value_count;
    return 1;
}

static int init_project_locked(void) {
    uint32_t d;
    odm_status st;
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    memset(g_objects, 0, sizeof(g_objects));
    memset(g_values, 0, sizeof(g_values));
    g_object_count = 0u; g_value_count = 0u; g_next_object_id = UINT64_C(10000);
    if (!schema) { snprintf(g_error, sizeof(g_error), "builtin schema unavailable"); return 0; }
    st = odm_presentation_builtin_schema_validate();
    if (st != ODM_STATUS_OK) { set_error_status("schema_validate", st); return 0; }
    for (d = 1u; d <= 12u; ++d) {
        odm_presentation_parameter_value pv;
        odm_presentation_object *o = &g_objects[g_object_count++];
        memset(o, 0, sizeof(*o));
        o->object_id = root_id_for_domain(d);
        o->capability_id = ODM_PRESENTATION_CAP_DOMAIN_ROOT;
        o->flags = ODM_PRESENTATION_OBJECT_ENABLED;
        memset(&pv, 0, sizeof(pv));
        pv.object_id = o->object_id;
        pv.parameter_id = ODM_PRESENTATION_DOMAIN_ROOT_KIND;
        pv.value.type = ODM_PRESENTATION_VALUE_ENUM;
        pv.value.lane[0] = (int64_t)d;
        if (!insert_value(&pv)) return 0;
    }
    refresh_doc();
    st = odm_presentation_builtin_domains_validate(&g_doc);
    if (st != ODM_STATUS_OK) { set_error_status("domains_validate", st); return 0; }
    st = odm_authoring_session_init(&g_session);
    if (st != ODM_STATUS_OK) { set_error_status("session_init", st); return 0; }
    st = odm_editor_view3d_init(&g_view);
    if (st != ODM_STATUS_OK) { set_error_status("view_init", st); return 0; }
    st = odm_timeline_view_init(g_duration_samples, 1200u, &g_timeline_view);
    if (st != ODM_STATUS_OK) { set_error_status("timeline_view_init", st); return 0; }
    st = odm_mesh_authoring_init(g_mesh_vertices, MESH_VERTS,
                                 g_mesh_faces, MESH_FACES, &g_mesh);
    if (st != ODM_STATUS_OK) { set_error_status("mesh_init", st); return 0; }
    st = odm_mesh_authoring_make_box(&g_mesh,
                                     ODM_Q32_ONE * 2, ODM_Q32_ONE * 2, ODM_Q32_ONE * 2);
    if (st != ODM_STATUS_OK) { set_error_status("mesh_box", st); return 0; }
    st = odm_editor_view3d_focus_aabb(
        &g_view,
        (odm_vec3_q32){-ODM_Q32_ONE, -ODM_Q32_ONE, -ODM_Q32_ONE},
        (odm_vec3_q32){ ODM_Q32_ONE,  ODM_Q32_ONE,  ODM_Q32_ONE},
        800u, 600u, (odm_q1_31)(INT32_MAX / 8));
    if (st != ODM_STATUS_OK) { set_error_status("view_focus", st); return 0; }
    g_playhead_sample = 0;
    g_error[0] = '\0';
    g_ready = 1;
    return 1;
}

static int ensure_ready_locked(void) { return g_ready || init_project_locked(); }

static char *schema_json_alloc(void) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    uint64_t need = 0u;
    char *buf;
    odm_status st = odm_presentation_schema_manifest_json(schema, NULL, 0u, &need);
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || need == 0u ||
        need > UINT64_C(32) * 1024u * 1024u) return NULL;
    buf = (char *)malloc((size_t)need);
    if (!buf) return NULL;
    st = odm_presentation_schema_manifest_json(schema, buf, need, &need);
    if (st != ODM_STATUS_OK) { free(buf); return NULL; }
    return buf;
}

static void json_escape(FILE *f, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    fputc('"', f);
    while (*p) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p >= 0x20u) fputc(*p, f);
        ++p;
    }
    fputc('"', f);
}

static char *project_json_alloc(void) {
    char *buf = NULL; size_t size = 0; FILE *f = open_memstream(&buf, &size);
    uint32_t i;
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    if (!f) return NULL;
    fprintf(f, "{\"engine\":{\"version\":"); json_escape(f, odm_version_string());
    fprintf(f, ",\"abi\":%u,\"schema_major\":%u,\"schema_minor\":%u,\"capabilities\":%u,\"parameters\":%u},",
            odm_abi_version(), schema->schema_major, schema->schema_minor,
            schema->capability_count, schema->parameter_count);
    fprintf(f, "\"session\":{\"mode\":%u,\"active_domain\":%u,\"selected\":%llu,\"preview_quality\":%u},",
            g_session.mode, g_session.active_domain,
            (unsigned long long)g_session.selected_object_id, g_session.preview_quality);
    fprintf(f, "\"timeline\":{\"sample\":%lld,\"duration\":%lld,\"start\":%lld,\"end\":%lld},",
            (long long)g_playhead_sample, (long long)g_duration_samples,
            (long long)g_timeline_view.start_sample, (long long)g_timeline_view.end_sample);
    fprintf(f, "\"mesh\":{\"vertices\":%u,\"faces\":%u},\"domains\":[",
            g_mesh.vertex_count, g_mesh.face_count);
    for (i = 1u; i <= 12u; ++i) {
        if (i > 1u) fputc(',', f);
        fprintf(f, "{\"id\":%u,\"root\":%llu,\"name\":",
                i, (unsigned long long)root_id_for_domain(i));
        json_escape(f, domain_name(i)); fputc('}', f);
    }
    fprintf(f, "],\"objects\":[");
    for (i = 0u; i < g_object_count; ++i) {
        const odm_presentation_object *o = &g_objects[i];
        const odm_presentation_capability *cap = NULL;
        uint32_t ci;
        if (i) fputc(',', f);
        for (ci = 0u; ci < schema->capability_count; ++ci)
            if (schema->capabilities[ci].capability_id == o->capability_id) {
                cap = &schema->capabilities[ci]; break;
            }
        fprintf(f, "{\"id\":%llu,\"parent\":%llu,\"capability\":%u,\"flags\":%u,\"label\":",
                (unsigned long long)o->object_id,
                (unsigned long long)o->parent_id, o->capability_id, o->flags);
        json_escape(f, cap ? cap->label : "Unknown");
        fprintf(f, ",\"key\":"); json_escape(f, cap ? cap->key : "unknown");
        fputc('}', f);
    }
    fprintf(f, "]}");
    fclose(f);
    return buf;
}

static char *inspector_json_alloc(uint64_t object_id) {
    char *buf = NULL; size_t size = 0; FILE *f = open_memstream(&buf, &size);
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    const odm_presentation_object *o = find_object(object_id, NULL);
    uint32_t i; int first = 1;
    if (!f) return NULL;
    if (!o) { fputs("{\"error\":\"object not found\"}", f); fclose(f); return buf; }
    fprintf(f, "{\"object\":%llu,\"capability\":%u,\"flags\":%u,\"parameters\":[",
            (unsigned long long)object_id, o->capability_id, o->flags);
    for (i = 0u; i < schema->parameter_count; ++i) {
        const odm_presentation_parameter *p = &schema->parameters[i];
        const odm_presentation_parameter_value *pv = NULL;
        const odm_presentation_value *v;
        int comps, c;
        if (p->capability_id != o->capability_id ||
            !(p->flags & ODM_PRESENTATION_PARAM_EXPOSED)) continue;
        if (odm_presentation_find_value(&g_doc, object_id, p->parameter_id, &pv) == ODM_STATUS_OK && pv)
            v = &pv->value;
        else v = &p->default_value;
        comps = value_components(p->type);
        if (!first) fputc(',', f);
        first = 0;
        fprintf(f, "{\"id\":%u,\"type\":%u,\"flags\":%u,\"key\":",
                p->parameter_id, p->type, p->flags); json_escape(f, p->key);
        fprintf(f, ",\"label\":"); json_escape(f, p->label);
        fprintf(f, ",\"group\":"); json_escape(f, p->group);
        fprintf(f, ",\"unit\":"); json_escape(f, p->unit);
        fprintf(f, ",\"lanes\":[");
        for (c = 0; c < comps; ++c) { if (c) fputc(',', f); fprintf(f, "%lld", (long long)v->lane[c]); }
        fprintf(f, "],\"minimum\":[");
        for (c = 0; c < comps; ++c) { if (c) fputc(',', f); fprintf(f, "%lld", (long long)p->minimum.lane[c]); }
        fprintf(f, "],\"maximum\":[");
        for (c = 0; c < comps; ++c) { if (c) fputc(',', f); fprintf(f, "%lld", (long long)p->maximum.lane[c]); }
        fprintf(f, "],\"options\":[");
        for (c = 0; c < (int)p->option_count; ++c) {
            const odm_presentation_option *op = &schema->options[p->first_option_index + (uint32_t)c];
            if (c) fputc(',', f);
            fprintf(f, "{\"value\":%u,\"label\":", op->value);
            json_escape(f, op->label); fputc('}', f);
        }
        fputs("]}", f);
    }
    fputs("]}", f); fclose(f); return buf;
}

JNIEXPORT jstring JNICALL Java_com_odpar_musicmotion_MainActivity_nativeBootstrap(JNIEnv *env, jobject thiz) {
    char *json; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return jstr(env, g_error); }
    json = project_json_alloc(); pthread_mutex_unlock(&g_lock);
    if (!json) return jstr(env, "{\"error\":\"allocation\"}");
    { jstring out = jstr(env, json); free(json); return out; }
}

JNIEXPORT jstring JNICALL Java_com_odpar_musicmotion_MainActivity_nativeSchemaManifest(JNIEnv *env, jobject thiz) {
    char *json; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return jstr(env, g_error); }
    json = schema_json_alloc(); pthread_mutex_unlock(&g_lock);
    if (!json) return jstr(env, "{\"error\":\"schema manifest unavailable\"}");
    { jstring out = jstr(env, json); free(json); return out; }
}

JNIEXPORT jstring JNICALL Java_com_odpar_musicmotion_MainActivity_nativeProjectSnapshot(JNIEnv *env, jobject thiz) {
    char *json; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return jstr(env, g_error); }
    json = project_json_alloc(); pthread_mutex_unlock(&g_lock);
    if (!json) return jstr(env, "{\"error\":\"allocation\"}");
    { jstring out = jstr(env, json); free(json); return out; }
}

JNIEXPORT jstring JNICALL Java_com_odpar_musicmotion_MainActivity_nativeInspector(JNIEnv *env, jobject thiz, jlong objectId) {
    char *json; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return jstr(env, g_error); }
    json = inspector_json_alloc((uint64_t)objectId); pthread_mutex_unlock(&g_lock);
    if (!json) return jstr(env, "{\"error\":\"allocation\"}");
    { jstring out = jstr(env, json); free(json); return out; }
}

JNIEXPORT jlong JNICALL Java_com_odpar_musicmotion_MainActivity_nativeAddObject(JNIEnv *env, jobject thiz, jint capabilityId, jlong parentId) {
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    uint64_t id, parent = (uint64_t)parentId; uint32_t i; int exists = 0; odm_status st;
    (void)env; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return 0; }
    for (i = 0u; i < schema->capability_count; ++i)
        if (schema->capabilities[i].capability_id == (uint32_t)capabilityId) { exists = 1; break; }
    if (!exists || (uint32_t)capabilityId == ODM_PRESENTATION_CAP_DOMAIN_ROOT || g_object_count >= MAX_OBJECTS) {
        snprintf(g_error, sizeof(g_error), "capability not addable"); pthread_mutex_unlock(&g_lock); return 0;
    }
    if (parent == 0u)
        parent = root_id_for_domain(g_session.active_domain ? g_session.active_domain : ODM_PRESENTATION_DOMAIN_MODELS3D);
    if (!find_object(parent, NULL)) { snprintf(g_error, sizeof(g_error), "parent not found"); pthread_mutex_unlock(&g_lock); return 0; }
    id = g_next_object_id++;
    memset(&g_objects[g_object_count], 0, sizeof(g_objects[0]));
    g_objects[g_object_count].object_id = id;
    g_objects[g_object_count].parent_id = parent;
    g_objects[g_object_count].capability_id = (uint32_t)capabilityId;
    g_objects[g_object_count].flags = ODM_PRESENTATION_OBJECT_ENABLED;
    ++g_object_count; refresh_doc();
    st = odm_presentation_builtin_domains_validate(&g_doc);
    if (st != ODM_STATUS_OK) {
        --g_object_count; refresh_doc(); set_error_status("add_object", st);
        pthread_mutex_unlock(&g_lock); return 0;
    }
    (void)odm_authoring_session_select(&g_session, &g_doc, id);
    pthread_mutex_unlock(&g_lock); return (jlong)id;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeDeleteObject(JNIEnv *env, jobject thiz, jlong objectId) {
    uint64_t id = (uint64_t)objectId; uint32_t i, j, write; odm_status st; (void)env; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    if (id < UINT64_C(10000) || !find_object(id, NULL)) {
        snprintf(g_error, sizeof(g_error), "cannot delete domain root or missing object");
        pthread_mutex_unlock(&g_lock); return JNI_FALSE;
    }
    for (i = 0u; i < g_object_count; ++i) {
        uint64_t cur = g_objects[i].object_id, p = g_objects[i].parent_id; int remove = (cur == id);
        while (!remove && p != 0u) {
            const odm_presentation_object *po = find_object(p, NULL);
            if (p == id) { remove = 1; break; }
            if (!po) break; p = po->parent_id;
        }
        if (remove) g_objects[i].reserved = UINT32_MAX;
    }
    write = 0u;
    for (i = 0u; i < g_object_count; ++i)
        if (g_objects[i].reserved != UINT32_MAX) {
            g_objects[i].reserved = 0u; if (write != i) g_objects[write] = g_objects[i]; ++write;
        }
    g_object_count = write;
    write = 0u;
    for (i = 0u; i < g_value_count; ++i) {
        int keep = 0;
        for (j = 0u; j < g_object_count; ++j)
            if (g_values[i].object_id == g_objects[j].object_id) { keep = 1; break; }
        if (keep) { if (write != i) g_values[write] = g_values[i]; ++write; }
    }
    g_value_count = write; refresh_doc();
    st = odm_presentation_builtin_domains_validate(&g_doc);
    if (st != ODM_STATUS_OK) { set_error_status("delete_validate", st); pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    if (g_session.selected_object_id == id) (void)odm_authoring_session_select(&g_session, &g_doc, 0u);
    pthread_mutex_unlock(&g_lock); return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeSetObjectFlags(JNIEnv *env, jobject thiz, jlong objectId, jint flags) {
    uint32_t idx; odm_status st; uint32_t old; (void)env; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    if (!find_object((uint64_t)objectId, &idx)) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    old = g_objects[idx].flags;
    g_objects[idx].flags = (uint32_t)flags & ODM_PRESENTATION_OBJECT_KNOWN_MASK;
    refresh_doc(); st = odm_presentation_builtin_domains_validate(&g_doc);
    if (st != ODM_STATUS_OK) { g_objects[idx].flags = old; refresh_doc(); set_error_status("flags", st); pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    pthread_mutex_unlock(&g_lock); return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeSetValueLane(JNIEnv *env, jobject thiz, jlong objectId, jint parameterId, jint lane, jlong rawValue) {
    const odm_presentation_object *o;
    const odm_presentation_parameter *p = NULL;
    const odm_presentation_schema *schema = odm_presentation_builtin_schema();
    uint32_t i, index; int found, comps; odm_presentation_parameter_value candidate, old; odm_status st;
    (void)env; (void)thiz;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    o = find_object((uint64_t)objectId, NULL);
    if (!o) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    for (i = 0u; i < schema->parameter_count; ++i) {
        if (schema->parameters[i].capability_id == o->capability_id &&
            schema->parameters[i].parameter_id == (uint32_t)parameterId) {
            p = &schema->parameters[i]; break;
        }
    }
    if (!p || (p->flags & ODM_PRESENTATION_PARAM_READONLY)) {
        snprintf(g_error, sizeof(g_error), "parameter not writable"); pthread_mutex_unlock(&g_lock); return JNI_FALSE;
    }
    comps = value_components(p->type);
    if (lane < 0 || lane >= comps) { snprintf(g_error, sizeof(g_error), "invalid lane"); pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    find_value_index((uint64_t)objectId, (uint32_t)parameterId, &index, &found);
    memset(&candidate, 0, sizeof(candidate));
    candidate.object_id = (uint64_t)objectId; candidate.parameter_id = (uint32_t)parameterId;
    candidate.value = found ? g_values[index].value : p->default_value;
    candidate.value.type = p->type; candidate.value.lane[lane] = (int64_t)rawValue;
    if (found) old = g_values[index];
    if (found) g_values[index] = candidate;
    else if (!insert_value(&candidate)) { snprintf(g_error, sizeof(g_error), "value capacity"); pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    refresh_doc(); st = odm_presentation_builtin_domains_validate(&g_doc);
    if (st != ODM_STATUS_OK) {
        if (found) g_values[index] = old;
        else {
            find_value_index((uint64_t)objectId, (uint32_t)parameterId, &index, &found);
            if (found) { memmove(&g_values[index], &g_values[index + 1u], (g_value_count - index - 1u) * sizeof(g_values[0])); --g_value_count; }
        }
        refresh_doc(); set_error_status("set_value", st); pthread_mutex_unlock(&g_lock); return JNI_FALSE;
    }
    pthread_mutex_unlock(&g_lock); return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeEnterWorld(JNIEnv *env, jobject thiz) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_authoring_session_enter_world(&g_session); if (st != ODM_STATUS_OK) set_error_status("enter_world", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeEnterDomain(JNIEnv *env, jobject thiz, jint domain, jint contextMask) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_authoring_session_enter_domain(&g_session, (uint32_t)domain, (uint32_t)contextMask);
    if (st != ODM_STATUS_OK) set_error_status("enter_domain", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeSelect(JNIEnv *env, jobject thiz, jlong objectId) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_authoring_session_select(&g_session, &g_doc, (uint64_t)objectId);
    if (st != ODM_STATUS_OK) set_error_status("select", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeNavigate(JNIEnv *env, jobject thiz, jdouble panX, jdouble panY, jdouble dolly) {
    odm_status st = ODM_STATUS_OK; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    if (panX != 0.0 || panY != 0.0)
        st = odm_editor_view3d_pan(&g_view,
            (odm_q32_32)(panX * (double)ODM_Q32_ONE),
            (odm_q32_32)(panY * (double)ODM_Q32_ONE));
    if (st == ODM_STATUS_OK && dolly != 0.0)
        st = odm_editor_view3d_dolly(&g_view,
            (odm_q32_32)(dolly * (double)ODM_Q32_ONE));
    if (st != ODM_STATUS_OK) set_error_status("navigate", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_com_odpar_musicmotion_MainActivity_nativeTimelinePixelToSample(JNIEnv *env, jobject thiz, jlong pixelQ16) {
    int64_t s = -1; odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return -1; }
    st = odm_timeline_view_pixel_to_sample(&g_timeline_view, (uint64_t)pixelQ16, &s);
    if (st == ODM_STATUS_OK) g_playhead_sample = s; else set_error_status("timeline_pixel", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? (jlong)s : -1;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeTimelineResize(JNIEnv *env, jobject thiz, jint width) {
    odm_timeline_view v; odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_timeline_view_init(g_duration_samples, (uint32_t)(width > 0 ? width : 1), &v);
    if (st == ODM_STATUS_OK) {
        v.start_sample = g_timeline_view.start_sample; v.end_sample = g_timeline_view.end_sample;
        if (odm_timeline_view_validate(&v) == ODM_STATUS_OK) g_timeline_view = v;
        else st = odm_timeline_view_init(g_duration_samples, (uint32_t)(width > 0 ? width : 1), &g_timeline_view);
    }
    if (st != ODM_STATUS_OK) set_error_status("timeline_resize", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeTimelineZoom(JNIEnv *env, jobject thiz, jlong anchorPixelQ16, jlong newSpan) {
    odm_timeline_view out; odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_timeline_view_zoom_at_pixel(&g_timeline_view, (uint64_t)anchorPixelQ16, (int64_t)newSpan, &out);
    if (st == ODM_STATUS_OK) g_timeline_view = out; else set_error_status("timeline_zoom", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeTimelinePan(JNIEnv *env, jobject thiz, jlong deltaSamples) {
    odm_timeline_view out; odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_timeline_view_pan_samples(&g_timeline_view, (int64_t)deltaSamples, &out);
    if (st == ODM_STATUS_OK) g_timeline_view = out; else set_error_status("timeline_pan", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeMeshPrimitive(JNIEnv *env, jobject thiz, jint kind) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    switch (kind) {
        case 1: st = odm_mesh_authoring_make_box(&g_mesh, ODM_Q32_ONE*2, ODM_Q32_ONE*2, ODM_Q32_ONE*2); break;
        case 2: st = odm_mesh_authoring_make_cylinder(&g_mesh, ODM_Q32_ONE, ODM_Q32_ONE*2, 32u, ODM_MESH_PRIMITIVE_CAP_BOTH, 0u, 1u); break;
        case 3: st = odm_mesh_authoring_make_cone(&g_mesh, ODM_Q32_ONE, ODM_Q32_ONE*2, 32u, ODM_MESH_PRIMITIVE_CAP_BOTTOM, 0u, 1u); break;
        default: st = ODM_STATUS_INVALID_ARGUMENT; break;
    }
    if (st != ODM_STATUS_OK) set_error_status("mesh_primitive", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeMeshExtrudeFace(JNIEnv *env, jobject thiz, jlong faceId, jdouble meters) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_mesh_authoring_extrude_face_normal(&g_mesh, (uint64_t)faceId,
        (odm_q32_32)(meters*(double)ODM_Q32_ONE));
    if (st != ODM_STATUS_OK) set_error_status("mesh_extrude", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeMeshInsetFace(JNIEnv *env, jobject thiz, jlong faceId, jdouble remainingScale) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_mesh_authoring_inset_face(&g_mesh, (uint64_t)faceId,
        (odm_q32_32)(remainingScale*(double)ODM_Q32_ONE));
    if (st != ODM_STATUS_OK) set_error_status("mesh_inset", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeMeshSetDimension(JNIEnv *env, jobject thiz, jint axis, jdouble meters, jint anchor) {
    odm_status st; (void)env; (void)thiz; pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return JNI_FALSE; }
    st = odm_mesh_authoring_set_axis_span(&g_mesh, (uint32_t)axis, (uint32_t)anchor,
        (odm_q32_32)(meters*(double)ODM_Q32_ONE));
    if (st != ODM_STATUS_OK) set_error_status("mesh_dimension", st);
    pthread_mutex_unlock(&g_lock); return st == ODM_STATUS_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jintArray JNICALL Java_com_odpar_musicmotion_MainActivity_nativeRenderViewport(JNIEnv *env, jobject thiz, jint width, jint height) {
    uint8_t *rgba = NULL; uint32_t *depth = NULL; jint *argb = NULL; jintArray arr = NULL;
    uint64_t pixels, frags = 0u; uint32_t rv = 0u, rt = 0u; uint64_t i;
    odm_material3d mat; odm_asset3d asset; odm_instance3d inst; odm_lighting3d lights;
    odm_raster3d_target target; uint8_t clear[4] = {14u,16u,21u,255u}; odm_status st;
    (void)thiz;
    if (width < 32 || height < 32 || width > 1600 || height > 1600) return NULL;
    pthread_mutex_lock(&g_lock);
    if (!ensure_ready_locked()) { pthread_mutex_unlock(&g_lock); return NULL; }
    st = odm_mesh_authoring_compile_counts(&g_mesh, &rv, &rt);
    if (st != ODM_STATUS_OK || rv > RENDER_VERTS || rt > RENDER_TRIS) {
        set_error_status("mesh_compile_counts", st); pthread_mutex_unlock(&g_lock); return NULL;
    }
    st = odm_mesh_authoring_compile(&g_mesh, g_render_vertices, RENDER_VERTS,
                                     g_render_tris, RENDER_TRIS, &g_render_mesh);
    if (st != ODM_STATUS_OK) { set_error_status("mesh_compile", st); pthread_mutex_unlock(&g_lock); return NULL; }
    pixels = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height;
    rgba = (uint8_t*)malloc((size_t)pixels*4u);
    depth = (uint32_t*)calloc((size_t)pixels,sizeof(uint32_t));
    argb = (jint*)malloc((size_t)pixels*sizeof(jint));
    if (!rgba || !depth || !argb) { snprintf(g_error,sizeof(g_error),"viewport allocation"); goto done; }
    memset(&mat,0,sizeof(mat));
    mat.shading_model=ODM_MATERIAL_PBR_LITE; mat.alpha_mode=ODM_ALPHA_OPAQUE;
    mat.base_rgba[0]=170u;mat.base_rgba[1]=184u;mat.base_rgba[2]=210u;mat.base_rgba[3]=255u;
    mat.roughness_u8=90u;mat.metallic_u8=45u;mat.specular_u8=190u;
    memset(&asset,0,sizeof(asset)); asset.asset_id=UINT64_C(9001);asset.mesh=&g_render_mesh;asset.material=&mat;
    memset(&inst,0,sizeof(inst)); inst.instance_id=UINT64_C(9001);inst.asset=&asset;
    inst.transform.basis=odm_scene3d_basis_identity();inst.transform.scale.x=ODM_Q32_ONE;inst.transform.scale.y=ODM_Q32_ONE;inst.transform.scale.z=ODM_Q32_ONE;
    memset(&lights,0,sizeof(lights));lights.ambient_rgb16[0]=12000u;lights.ambient_rgb16[1]=13000u;lights.ambient_rgb16[2]=16000u;lights.light_count=1u;
    lights.lights[0].type=ODM_LIGHT_DIRECTIONAL;
    lights.lights[0].direction.x=(odm_q1_31)(INT32_MAX/3);
    lights.lights[0].direction.y=(odm_q1_31)(-INT32_MAX/2);
    lights.lights[0].direction.z=(odm_q1_31)(INT32_MAX*3LL/4LL);
    lights.lights[0].rgb16[0]=65535u;lights.lights[0].rgb16[1]=62000u;lights.lights[0].rgb16[2]=56000u;
    lights.lights[0].intensity_q31=(odm_q1_31)(INT32_MAX*3LL/4LL);
    target.width=(uint32_t)width;target.height=(uint32_t)height;target.stride_bytes=(uint32_t)width*4u;target.rgba8=rgba;target.inv_z_q16_16=depth;
    st=odm_raster3d_clear(&target,clear); if(st!=ODM_STATUS_OK){set_error_status("raster_clear",st);goto done;}
    st=odm_raster3d_draw_instance(&g_view.camera,&lights,&inst,&target,&frags); if(st!=ODM_STATUS_OK){set_error_status("raster_draw",st);goto done;}
    for(i=0u;i<pixels;++i){const uint8_t *p=&rgba[i*4u];argb[i]=(jint)(((uint32_t)p[3]<<24)|((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2]);}
    arr=(*env)->NewIntArray(env,(jsize)pixels); if(arr)(*env)->SetIntArrayRegion(env,arr,0,(jsize)pixels,argb);
    snprintf(g_error,sizeof(g_error),"ok; mesh=%uV/%uF; raster=%uV/%uT; fragments=%llu",
             g_mesh.vertex_count,g_mesh.face_count,rv,rt,(unsigned long long)frags);
done:
    free(rgba);free(depth);free(argb);pthread_mutex_unlock(&g_lock);return arr;
}

JNIEXPORT jstring JNICALL Java_com_odpar_musicmotion_MainActivity_nativeLastError(JNIEnv *env, jobject thiz) {
    char tmp[sizeof(g_error)]; (void)thiz; pthread_mutex_lock(&g_lock);
    snprintf(tmp,sizeof(tmp),"%s",g_error); pthread_mutex_unlock(&g_lock); return jstr(env,tmp);
}

JNIEXPORT jboolean JNICALL Java_com_odpar_musicmotion_MainActivity_nativeResetProject(JNIEnv *env, jobject thiz) {
    int ok; (void)env; (void)thiz; pthread_mutex_lock(&g_lock); g_ready=0; ok=init_project_locked(); pthread_mutex_unlock(&g_lock); return ok?JNI_TRUE:JNI_FALSE;
}

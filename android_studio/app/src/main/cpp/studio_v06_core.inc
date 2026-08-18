#define _POSIX_C_SOURCE 200809L
#include <jni.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "odm_authoring_session.h"
#include "odm_authoring_timeline.h"
#include "odm_editor_catalog.h"
#include "odm_editor_view3d.h"
#include "odm_mesh_authoring.h"
#include "odm_mesh_authoring_primitives.h"
#include "odm_mesh_authoring_dimensions.h"
#include "odm_mesh_authoring_surface.h"
#include "odm_mesh_authoring_query.h"
#include "odm_scene3d.h"
#include "odm_renderer.h"
#include "odm_timeline_view.h"
#include "odm_hash.h"
#include "odm_presentation.h"
#include "odm_presentation_builtin.h"
#include "odm_spine.h"
#include "odm_status.h"
#include "odm_version.h"
#include "selftest.h"

#define Q32_ONE INT64_C(4294967296)

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} sbuf;

typedef struct {
    odm_presentation_object *objects;
    uint32_t object_count;
    uint32_t object_cap;
    odm_presentation_parameter_value *values;
    uint32_t value_count;
    uint32_t value_cap;
    odm_presentation_binding *bindings;
    uint32_t binding_count;
    uint32_t binding_cap;
    odm_presentation_document document;
    odm_authoring_session session;
    odm_authoring_timeline timeline;
    odm_timeline_view timeline_view;
    odm_editor_view3d editor_view;
    odm_mesh_authoring_vertex *mesh_vertices;
    uint32_t mesh_vertex_cap;
    odm_mesh_authoring_face *mesh_faces;
    uint32_t mesh_face_cap;
    odm_mesh_authoring_document mesh;
    uint64_t next_object_id;
    uint64_t next_binding_id;
    int64_t playhead_sample;
    int initialized;
} workspace_state;

static workspace_state g_ws;
static _Thread_local char g_error[512];

static void set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof(g_error), fmt, ap);
    va_end(ap);
}

static jstring jstr(JNIEnv *env, const char *s) {
    return (*env)->NewStringUTF(env, s ? s : "");
}

static int sb_reserve(sbuf *b, size_t extra) {
    size_t need;
    char *p;
    if (!b || extra > SIZE_MAX - b->len - 1u) return 0;
    need = b->len + extra + 1u;
    if (need <= b->cap) return 1;
    size_t next = b->cap ? b->cap : 4096u;
    while (next < need) {
        if (next > SIZE_MAX / 2u) { next = need; break; }
        next *= 2u;
    }
    p = (char *)realloc(b->data, next);
    if (!p) return 0;
    b->data = p;
    b->cap = next;
    return 1;
}

static int sb_addn(sbuf *b, const char *s, size_t n) {
    if (!sb_reserve(b, n)) return 0;
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int sb_add(sbuf *b, const char *s) {
    return sb_addn(b, s ? s : "", s ? strlen(s) : 0u);
}

static int sb_printf(sbuf *b, const char *fmt, ...) {
    va_list ap, cp;
    int n;
    va_start(ap, fmt);
    va_copy(cp, ap);
    n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n < 0 || !sb_reserve(b, (size_t)n)) { va_end(ap); return 0; }
    (void)vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 1;
}

static int sb_json_string(sbuf *b, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    if (!sb_add(b, "\"")) return 0;
    while (*p) {
        char tmp[8];
        if (*p == '"' || *p == '\\') {
            tmp[0] = '\\'; tmp[1] = (char)*p; tmp[2] = 0;
            if (!sb_add(b, tmp)) return 0;
        } else if (*p == '\n') {
            if (!sb_add(b, "\\n")) return 0;
        } else if (*p == '\r') {
            if (!sb_add(b, "\\r")) return 0;
        } else if (*p == '\t') {
            if (!sb_add(b, "\\t")) return 0;
        } else if (*p < 0x20u) {
            if (!sb_printf(b, "\\u%04x", (unsigned)*p)) return 0;
        } else {
            tmp[0] = (char)*p; tmp[1] = 0;
            if (!sb_add(b, tmp)) return 0;
        }
        ++p;
    }
    return sb_add(b, "\"");
}

static void sb_free(sbuf *b) {
    if (!b) return;
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static uint32_t value_components(uint32_t type) {
    switch (type) {
        case ODM_PRESENTATION_VALUE_VEC2_Q32_32: return 2u;
        case ODM_PRESENTATION_VALUE_VEC3_Q32_32: return 3u;
        case ODM_PRESENTATION_VALUE_COLOR_RGB16: return 3u;
        case ODM_PRESENTATION_VALUE_COLOR_RGBA16: return 4u;
        case ODM_PRESENTATION_VALUE_BOOL:
        case ODM_PRESENTATION_VALUE_U32:
        case ODM_PRESENTATION_VALUE_I64:
        case ODM_PRESENTATION_VALUE_Q1_31:
        case ODM_PRESENTATION_VALUE_Q32_32:
        case ODM_PRESENTATION_VALUE_PHASE_U32:
        case ODM_PRESENTATION_VALUE_RESOURCE_ID:
        case ODM_PRESENTATION_VALUE_ENUM:
            return 1u;
        default: return 0u;
    }
}

static const odm_presentation_capability *find_cap(uint32_t id) {
    const odm_presentation_schema *s = odm_presentation_builtin_schema();
    uint32_t i;
    for (i = 0u; i < s->capability_count; ++i)
        if (s->capabilities[i].capability_id == id) return &s->capabilities[i];
    return NULL;
}

static int ensure_objects(uint32_t need) {
    odm_presentation_object *p;
    uint32_t cap = g_ws.object_cap ? g_ws.object_cap : 64u;
    if (need <= g_ws.object_cap) return 1;
    while (cap < need) {
        if (cap > ODM_PRESENTATION_MAX_OBJECTS / 2u) { cap = ODM_PRESENTATION_MAX_OBJECTS; break; }
        cap *= 2u;
    }
    if (cap < need) return 0;
    p = (odm_presentation_object *)realloc(g_ws.objects, (size_t)cap * sizeof(*p));
    if (!p) return 0;
    g_ws.objects = p;
    g_ws.object_cap = cap;
    g_ws.document.objects = g_ws.objects;
    return 1;
}

static int ensure_values(uint32_t need) {
    odm_presentation_parameter_value *p;
    uint32_t cap = g_ws.value_cap ? g_ws.value_cap : 128u;
    if (need <= g_ws.value_cap) return 1;
    while (cap < need) {
        if (cap > ODM_PRESENTATION_MAX_VALUES / 2u) { cap = ODM_PRESENTATION_MAX_VALUES; break; }
        cap *= 2u;
    }
    if (cap < need) return 0;
    p = (odm_presentation_parameter_value *)realloc(g_ws.values, (size_t)cap * sizeof(*p));
    if (!p) return 0;
    g_ws.values = p;
    g_ws.value_cap = cap;
    g_ws.document.values = g_ws.values;
    return 1;
}

static int ensure_bindings(uint32_t need) {
    odm_presentation_binding *p;
    uint32_t cap = g_ws.binding_cap ? g_ws.binding_cap : 32u;
    if (need <= g_ws.binding_cap) return 1;
    while (cap < need) {
        if (cap > ODM_PRESENTATION_MAX_BINDINGS / 2u) { cap = ODM_PRESENTATION_MAX_BINDINGS; break; }
        cap *= 2u;
    }
    if (cap < need) return 0;
    p = (odm_presentation_binding *)realloc(g_ws.bindings, (size_t)cap * sizeof(*p));
    if (!p) return 0;
    g_ws.bindings = p;
    g_ws.binding_cap = cap;
    g_ws.document.bindings = g_ws.bindings;
    return 1;
}

static int mesh_reserve(uint32_t need_vertices, uint32_t need_faces) {
    uint32_t vc = g_ws.mesh_vertex_cap ? g_ws.mesh_vertex_cap : 64u;
    uint32_t fc = g_ws.mesh_face_cap ? g_ws.mesh_face_cap : 64u;
    odm_mesh_authoring_vertex *nv;
    odm_mesh_authoring_face *nf;
    while (vc < need_vertices) {
        if (vc >= ODM_SCENE3D_MAX_VERTICES) break;
        vc = vc > ODM_SCENE3D_MAX_VERTICES / 2u ? ODM_SCENE3D_MAX_VERTICES : vc * 2u;
    }
    while (fc < need_faces) {
        if (fc >= ODM_SCENE3D_MAX_TRIANGLES) break;
        fc = fc > ODM_SCENE3D_MAX_TRIANGLES / 2u ? ODM_SCENE3D_MAX_TRIANGLES : fc * 2u;
    }
    if (vc < need_vertices || fc < need_faces) return 0;
    if (vc == g_ws.mesh_vertex_cap && fc == g_ws.mesh_face_cap) return 1;
    nv = (odm_mesh_authoring_vertex *)malloc((size_t)vc * sizeof(*nv));
    nf = (odm_mesh_authoring_face *)malloc((size_t)fc * sizeof(*nf));
    if (!nv || !nf) { free(nv); free(nf); return 0; }
    if (g_ws.mesh.vertex_count && g_ws.mesh.vertices)
        memcpy(nv, g_ws.mesh.vertices, (size_t)g_ws.mesh.vertex_count * sizeof(*nv));
    if (g_ws.mesh.face_count && g_ws.mesh.faces)
        memcpy(nf, g_ws.mesh.faces, (size_t)g_ws.mesh.face_count * sizeof(*nf));
    free(g_ws.mesh_vertices);
    free(g_ws.mesh_faces);
    g_ws.mesh_vertices = nv;
    g_ws.mesh_faces = nf;
    g_ws.mesh_vertex_cap = vc;
    g_ws.mesh_face_cap = fc;
    g_ws.mesh.vertices = nv;
    g_ws.mesh.vertex_capacity = vc;
    g_ws.mesh.faces = nf;
    g_ws.mesh.face_capacity = fc;
    return 1;
}

static odm_status mesh_init(void) {
    odm_status st;
    if (!mesh_reserve(64u, 64u)) return ODM_STATUS_OUT_OF_MEMORY;
    st = odm_mesh_authoring_init(g_ws.mesh_vertices, g_ws.mesh_vertex_cap,
                                 g_ws.mesh_faces, g_ws.mesh_face_cap, &g_ws.mesh);
    if (st != ODM_STATUS_OK) return st;
    st = odm_mesh_authoring_make_box(&g_ws.mesh, Q32_ONE * 2, Q32_ONE * 2, Q32_ONE * 2);
    if (st != ODM_STATUS_OK) return st;
    st = odm_editor_view3d_init(&g_ws.editor_view);
    if (st != ODM_STATUS_OK) return st;
    return odm_editor_view3d_focus_aabb(
        &g_ws.editor_view,
        (odm_vec3_q32){-Q32_ONE, -Q32_ONE, -Q32_ONE},
        (odm_vec3_q32){ Q32_ONE,  Q32_ONE,  Q32_ONE},
        800u, 600u, (odm_q1_31)(INT32_MAX / 8));
}

static odm_status mesh_primitive(uint32_t kind) {
    odm_status st;
    uint32_t attempts = 0u;
    for (;;) {
        switch (kind) {
            case 1u: st = odm_mesh_authoring_make_box(&g_ws.mesh, Q32_ONE*2, Q32_ONE*2, Q32_ONE*2); break;
            case 2u: st = odm_mesh_authoring_make_cylinder(&g_ws.mesh, Q32_ONE, Q32_ONE*2, 32u, ODM_MESH_PRIMITIVE_CAP_BOTH, 0u, 1u); break;
            case 3u: st = odm_mesh_authoring_make_cone(&g_ws.mesh, Q32_ONE, Q32_ONE*2, 32u, ODM_MESH_PRIMITIVE_CAP_BOTTOM, 0u, 1u); break;
            default: return ODM_STATUS_INVALID_ARGUMENT;
        }
        if (st != ODM_STATUS_BUFFER_TOO_SMALL) return st;
        if (++attempts > 31u || !mesh_reserve(
                g_ws.mesh_vertex_cap < ODM_SCENE3D_MAX_VERTICES ? g_ws.mesh_vertex_cap + 1u : g_ws.mesh_vertex_cap,
                g_ws.mesh_face_cap < ODM_SCENE3D_MAX_TRIANGLES ? g_ws.mesh_face_cap + 1u : g_ws.mesh_face_cap))
            return ODM_STATUS_BUFFER_TOO_SMALL;
    }
}

static odm_status mesh_grow_retry_extrude(uint64_t face_id, odm_q32_32 distance) {
    uint32_t attempts = 0u;
    for (;;) {
        odm_status st = odm_mesh_authoring_extrude_face_normal(&g_ws.mesh, face_id, distance);
        if (st != ODM_STATUS_BUFFER_TOO_SMALL) return st;
        if (++attempts > 31u || !mesh_reserve(
                g_ws.mesh_vertex_cap < ODM_SCENE3D_MAX_VERTICES ? g_ws.mesh_vertex_cap + 1u : g_ws.mesh_vertex_cap,
                g_ws.mesh_face_cap < ODM_SCENE3D_MAX_TRIANGLES ? g_ws.mesh_face_cap + 1u : g_ws.mesh_face_cap))
            return ODM_STATUS_BUFFER_TOO_SMALL;
    }
}

static odm_status mesh_grow_retry_inset(uint64_t face_id, odm_q32_32 scale) {
    uint32_t attempts = 0u;
    for (;;) {
        odm_status st = odm_mesh_authoring_inset_face(&g_ws.mesh, face_id, scale);
        if (st != ODM_STATUS_BUFFER_TOO_SMALL) return st;
        if (++attempts > 31u || !mesh_reserve(
                g_ws.mesh_vertex_cap < ODM_SCENE3D_MAX_VERTICES ? g_ws.mesh_vertex_cap + 1u : g_ws.mesh_vertex_cap,
                g_ws.mesh_face_cap < ODM_SCENE3D_MAX_TRIANGLES ? g_ws.mesh_face_cap + 1u : g_ws.mesh_face_cap))
            return ODM_STATUS_BUFFER_TOO_SMALL;
    }
}

static odm_status validate_workspace(void) {
    odm_status st = odm_presentation_builtin_domains_validate(&g_ws.document);
    if (st != ODM_STATUS_OK) return st;
    return odm_authoring_session_validate(&g_ws.session, &g_ws.document);
}

static odm_status append_object(uint64_t id, uint64_t parent, uint32_t cap, uint32_t flags) {
    odm_presentation_object *o;
    if (!ensure_objects(g_ws.object_count + 1u)) return ODM_STATUS_OUT_OF_MEMORY;
    if (g_ws.object_count && g_ws.objects[g_ws.object_count - 1u].object_id >= id)
        return ODM_STATUS_INVALID_DATA;
    o = &g_ws.objects[g_ws.object_count++];
    memset(o, 0, sizeof(*o));
    o->object_id = id;
    o->parent_id = parent;
    o->capability_id = cap;
    o->flags = flags;
    g_ws.document.object_count = g_ws.object_count;
    return ODM_STATUS_OK;
}

static odm_status append_root_value(uint64_t id, uint32_t domain) {
    odm_presentation_parameter_value *v;
    if (!ensure_values(g_ws.value_count + 1u)) return ODM_STATUS_OUT_OF_MEMORY;
    v = &g_ws.values[g_ws.value_count++];
    memset(v, 0, sizeof(*v));
    v->object_id = id;
    v->parameter_id = ODM_PRESENTATION_DOMAIN_ROOT_KIND;
    v->value.type = ODM_PRESENTATION_VALUE_ENUM;
    v->value.lane[0] = (int64_t)domain;
    g_ws.document.value_count = g_ws.value_count;
    return ODM_STATUS_OK;
}

#include "odpar_lab_core.h"
#include "odm_scene3d.h"
#include "odm_spine.h"
#include "odm_status.h"
#include "odm_version.h"
#include "odm_hash.h"
#include "selftest.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static odm_q32_32 m_um(int64_t um) {
    odm_q32_32 v = 0;
    return odm_q32_32_from_microunits(um, &v) == ODM_STATUS_OK ? v : 0;
}

static odm_transform3d transform_at(int64_t x_um, int64_t y_um, int64_t z_um) {
    odm_transform3d t;
    memset(&t, 0, sizeof(t));
    t.position.x = m_um(x_um); t.position.y = m_um(y_um); t.position.z = m_um(z_um);
    t.basis = odm_scene3d_basis_identity();
    t.scale.x = ODM_Q32_ONE; t.scale.y = ODM_Q32_ONE; t.scale.z = ODM_Q32_ONE;
    return t;
}

static odm_material3d material_pbr(uint8_t r, uint8_t g, uint8_t b,
                                   uint8_t rough, uint8_t metal, uint8_t spec) {
    odm_material3d m;
    memset(&m, 0, sizeof(m));
    m.shading_model = ODM_MATERIAL_PBR_LITE;
    m.alpha_mode = ODM_ALPHA_OPAQUE;
    m.base_rgba[0] = r; m.base_rgba[1] = g; m.base_rgba[2] = b; m.base_rgba[3] = 255u;
    m.roughness_u8 = rough; m.metallic_u8 = metal; m.specular_u8 = spec;
    return m;
}

typedef struct {
    odm_vertex3d vertices[24];
    odm_triangle3d triangles[12];
    odm_mesh3d mesh;
    odm_material3d material;
    odm_asset3d asset;
    odm_instance3d instance;
} lab_box;

typedef struct {
    odm_vertex3d vertices[81];
    odm_triangle3d triangles[128];
    odm_mesh3d mesh;
    odm_material3d material;
    odm_asset3d asset;
    odm_instance3d instance;
} lab_quad;

static int make_box(lab_box *o, uint64_t id,
                    int64_t w, int64_t h, int64_t d,
                    int64_t x, int64_t y, int64_t z,
                    uint8_t r, uint8_t g, uint8_t b,
                    uint8_t rough, uint8_t metal, uint8_t spec) {
    memset(o, 0, sizeof(*o));
    if (odm_mesh3d_make_box(o->vertices, 24u, o->triangles, 12u,
                            m_um(w), m_um(h), m_um(d), &o->mesh) != ODM_STATUS_OK) return 0;
    o->material = material_pbr(r,g,b,rough,metal,spec);
    o->asset.asset_id = id; o->asset.mesh = &o->mesh; o->asset.material = &o->material;
    o->instance.instance_id = id; o->instance.asset = &o->asset;
    o->instance.transform = transform_at(x,y,z);
    return 1;
}

static int make_quad(lab_quad *o, uint64_t id,
                     odm_vec3_q32 p00, odm_vec3_q32 p10,
                     odm_vec3_q32 p01, odm_vec3_q32 p11,
                     odm_vec3_q31 normal,
                     uint8_t r, uint8_t g, uint8_t b) {
    memset(o, 0, sizeof(*o));
    if (odm_mesh3d_make_quad_grid(o->vertices,81u,o->triangles,128u,8u,8u,
                                  p00,p10,p01,p11,normal,&o->mesh) != ODM_STATUS_OK) return 0;
    o->material = material_pbr(r,g,b,190u,5u,145u);
    o->asset.asset_id = id; o->asset.mesh=&o->mesh; o->asset.material=&o->material;
    o->instance.instance_id=id; o->instance.asset=&o->asset; o->instance.transform=transform_at(0,0,0);
    return 1;
}

static int append_digest_hex(char *dst, size_t cap, const odm_sha256_digest *d) {
    static const char hex[]="0123456789abcdef";
    size_t i;
    if (!dst || cap < 65u || !d) return 0;
    for (i=0;i<32u;i++) { dst[i*2u]=hex[d->bytes[i]>>4]; dst[i*2u+1u]=hex[d->bytes[i]&15u]; }
    dst[64]='\0'; return 1;
}

int odpar_lab_status(char *out, size_t cap) {
    odm_spine_summary s;
    odm_status st = odm_spine_validate(&s);
    if (!out || cap==0u) return -1;
    if (st != ODM_STATUS_OK) return snprintf(out,cap,"engine_error=%s",odm_status_name(st));
    return snprintf(out,cap,
        "ODPAR: Music Motion\nversion=%s\nabi=%u\nsource=%s\ncompiler=%s\n"
        "spine.modules=%u\nspine.capabilities=%u\nspine.invariants=%u\nspine.dependencies=%u\n"
        "spine.state_models=%u\nspine.security_investigations=%u\ngraph_acyclic=%u",
        odm_version_string(), odm_abi_version(), odm_source_id(), odm_compiler_id(),
        s.module_count, s.capability_count, s.invariant_count, s.dependency_count,
        s.state_model_count, s.security_investigation_count, s.graph_acyclic);
}

int odpar_lab_selftest(char *out, size_t cap) {
    odm_selftest_result r;
    odm_status st;
    if (!out || cap==0u) return -1;
    st = odm_selftest_run(&r);
    return snprintf(out,cap,
        "{\"checks\":%u,\"passed\":%u,\"failed\":%u,\"status\":\"%s\",\"failed_check\":%s,\"error\":\"%s\"}",
        r.checks,r.passed,r.checks-r.passed, st==ODM_STATUS_OK?"pass":"fail",
        r.failed_check?"\"present\"":"null",
        odm_status_name(st==ODM_STATUS_OK?ODM_STATUS_OK:r.failure_status));
}

static int spine_json(int full, char *out, size_t cap) {
    size_t required=0u;
    odm_status st = full ? odm_spine_report_json(NULL,0u,&required)
                         : odm_spine_summary_json(NULL,0u,&required);
    if (!out || cap==0u) return (int)required;
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || required==0u) return -2;
    if (required > cap) return (int)required;
    st = full ? odm_spine_report_json(out,cap,&required)
              : odm_spine_summary_json(out,cap,&required);
    return st==ODM_STATUS_OK ? (int)required : -3;
}
int odpar_lab_spine_summary(char *out,size_t cap){return spine_json(0,out,cap);}
int odpar_lab_spine_full(char *out,size_t cap){return spine_json(1,out,cap);}

int odpar_lab_render_demo(uint32_t width, uint32_t height,
                          int32_t pan_x_mm, int32_t pan_y_mm, int32_t zoom_mm,
                          uint8_t *rgba, uint64_t rgba_bytes,
                          char *meta, size_t meta_cap) {
    uint32_t *depth=NULL,*shadow_depth=NULL;
    void *scratch=NULL;
    uint64_t scratch_bytes=0u, fragments=0u, sf=0u;
    odm_camera3d camera;
    odm_lighting3d lighting;
    odm_raster3d_target target;
    odm_shadow3d_map shadow;
    lab_box center,left,right;
    lab_quad floor_o,back;
    odm_sha256_digest digest;
    uint8_t clear[4]={10u,12u,16u,255u};
    odm_vec3_q32 target_pos={m_um(0),m_um(1100000),m_um(2600000)};
    odm_vec3_q31 world_up={0,INT32_MAX,0};
    odm_status st;
#define V3(X,Y,Z) ((odm_vec3_q32){m_um((X)),m_um((Y)),m_um((Z))})
    if (!rgba || width<64u || height<64u || width>1024u || height>1024u ||
        rgba_bytes < (uint64_t)width*height*4u) return -1;
    depth=(uint32_t*)calloc((size_t)width*height,sizeof(uint32_t));
    shadow_depth=(uint32_t*)malloc(256u*256u*sizeof(uint32_t));
    if (odm_raster3d_prepared_scratch_bytes(128u,&scratch_bytes)!=ODM_STATUS_OK) goto fail;
    scratch=malloc((size_t)scratch_bytes);
    if (!depth||!shadow_depth||!scratch) goto fail;
    if (odm_camera3d_init(&camera)!=ODM_STATUS_OK) goto fail;
    camera.position.x=m_um(-1900000 + (int64_t)pan_x_mm*1000);
    camera.position.y=m_um(1350000 + (int64_t)pan_y_mm*1000);
    camera.position.z=m_um(-3300000 + (int64_t)zoom_mm*1000);
    camera.focal_length_um=34000u; camera.sensor_width_um=36000u; camera.sensor_height_um=30000u;
    camera.exposure_gain_q16_16=2u*ODM_CAMERA_EXPOSURE_ONE_Q16_16;
    camera.tone_mapper=ODM_CAMERA_TONEMAP_REINHARD;
    if (odm_camera3d_look_at(&camera,target_pos,world_up)!=ODM_STATUS_OK) goto fail;

    memset(&lighting,0,sizeof(lighting));
    lighting.ambient_rgb16[0]=9000u; lighting.ambient_rgb16[1]=10000u; lighting.ambient_rgb16[2]=12500u;
    lighting.light_count=1u;
    lighting.lights[0].type=ODM_LIGHT_DIRECTIONAL;
    lighting.lights[0].direction=(odm_vec3_q31){INT32_MAX/4,-INT32_MAX/2,(odm_q1_31)(INT32_MAX*3LL/4)};
    lighting.lights[0].rgb16[0]=65535u; lighting.lights[0].rgb16[1]=61000u; lighting.lights[0].rgb16[2]=56000u;
    lighting.lights[0].intensity_q31=(odm_q1_31)(INT32_MAX*3LL/4LL);
    if (odm_lighting3d_validate(&lighting)!=ODM_STATUS_OK) goto fail;

    if(!make_quad(&floor_o,1001u,V3(-3500000,0,-1500000),V3(3500000,0,-1500000),V3(-3500000,0,6500000),V3(3500000,0,6500000),(odm_vec3_q31){0,INT32_MAX,0},31u,35u,43u))goto fail;
    if(!make_quad(&back,1002u,V3(-3500000,3600000,6000000),V3(3500000,3600000,6000000),V3(-3500000,0,6000000),V3(3500000,0,6000000),(odm_vec3_q31){0,0,-INT32_MAX},44u,47u,57u))goto fail;
    if(!make_box(&center,2001u,1500000,1500000,1500000,0,850000,2700000,86u,105u,156u,58u,185u,230u))goto fail;
    if(!make_box(&left,2002u,650000,2300000,650000,-1800000,1150000,3900000,52u,58u,70u,105u,45u,180u))goto fail;
    if(!make_box(&right,2003u,650000,1800000,900000,1850000,900000,4200000,74u,55u,48u,125u,20u,160u))goto fail;

    if(odm_shadow3d_init_directional(&shadow,shadow_depth,256u,256u,
         V3(0,1400000,2600000),lighting.lights[0].direction,world_up,
         m_um(9000000),m_um(7000000),m_um(14000000),m_um(15000),
         (odm_q1_31)(INT32_MAX*3LL/10LL),0u,1u)!=ODM_STATUS_OK)goto fail;
    if(odm_shadow3d_clear(&shadow)!=ODM_STATUS_OK)goto fail;
#define SHADOW(I) do{uint64_t f_=0; if(odm_shadow3d_draw_instance(&shadow,(I),&f_)!=ODM_STATUS_OK)goto fail;sf+=f_;}while(0)
    SHADOW(&floor_o.instance); SHADOW(&back.instance); SHADOW(&center.instance); SHADOW(&left.instance); SHADOW(&right.instance);
#undef SHADOW
    target.width=width; target.height=height; target.stride_bytes=width*4u; target.rgba8=rgba; target.inv_z_q16_16=depth;
    if(odm_raster3d_clear(&target,clear)!=ODM_STATUS_OK)goto fail;
#define DRAW(I) do{uint64_t f_=0; st=odm_raster3d_draw_instance_prepared(&camera,&lighting,&shadow,(I),&target,scratch,scratch_bytes,&f_); if(st!=ODM_STATUS_OK)goto fail;fragments+=f_;}while(0)
    DRAW(&floor_o.instance); DRAW(&back.instance); DRAW(&left.instance); DRAW(&right.instance); DRAW(&center.instance);
#undef DRAW
    if(odm_sha256(rgba,(uint64_t)width*height*4u,&digest)!=ODM_STATUS_OK)goto fail;
    if(meta&&meta_cap){char h[65];append_digest_hex(h,sizeof(h),&digest);snprintf(meta,meta_cap,"renderer=ODPAR Scene3D\n%ux%u\nfragments=%" PRIu64 "\nshadow_fragments=%" PRIu64 "\nsha256=%s",width,height,fragments,sf,h);}
    free(depth);free(shadow_depth);free(scratch);return 0;
fail:
    if(meta&&meta_cap)snprintf(meta,meta_cap,"render_failed");
    free(depth);free(shadow_depth);free(scratch);return -2;
#undef V3
}

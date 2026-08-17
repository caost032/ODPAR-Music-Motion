#include "odpar_lab_scene.h"
#include "odm_scene3d.h"
#include "odm_status.h"
#include "odm_fixed.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    odm_vertex3d v[24];
    odm_triangle3d t[12];
    odm_mesh3d mesh;
    odm_material3d material;
    odm_asset3d asset;
    odm_instance3d instance;
} lab_box;

typedef struct {
    odm_vertex3d v[2048];
    odm_triangle3d t[2048];
    odm_mesh3d mesh;
    odm_material3d material;
    odm_asset3d asset;
    odm_instance3d instance;
} lab_ring;

typedef struct {
    odm_vertex3d v[81];
    odm_triangle3d t[128];
    odm_mesh3d mesh;
    odm_material3d material;
    odm_asset3d asset;
    odm_instance3d instance;
} lab_floor;

static odm_q32_32 q_m(double meters) {
    odm_q32_32 out = 0;
    double um_d = meters * 1000000.0;
    int64_t um;
    if (um_d > 9.0e15 || um_d < -9.0e15) return 0;
    um = (int64_t)llround(um_d);
    if (odm_q32_32_from_microunits(um, &out) != ODM_STATUS_OK) return 0;
    return out;
}

static odm_transform3d transform_xyz(double x, double y, double z) {
    odm_transform3d t;
    memset(&t, 0, sizeof(t));
    t.position.x = q_m(x); t.position.y = q_m(y); t.position.z = q_m(z);
    t.basis = odm_scene3d_basis_identity();
    t.scale.x = ODM_Q32_ONE; t.scale.y = ODM_Q32_ONE; t.scale.z = ODM_Q32_ONE;
    return t;
}

static odm_material3d material_pbr(uint8_t r, uint8_t g, uint8_t b,
                                   uint8_t roughness, uint8_t metallic,
                                   uint16_t er, uint16_t eg, uint16_t eb) {
    odm_material3d m;
    memset(&m, 0, sizeof(m));
    m.shading_model = ODM_MATERIAL_PBR_LITE;
    m.alpha_mode = ODM_ALPHA_OPAQUE;
    m.base_rgba[0] = r; m.base_rgba[1] = g; m.base_rgba[2] = b; m.base_rgba[3] = 255u;
    m.roughness_u8 = roughness;
    m.metallic_u8 = metallic;
    m.specular_u8 = 150u;
    m.emissive_rgb16[0] = er; m.emissive_rgb16[1] = eg; m.emissive_rgb16[2] = eb;
    return m;
}

static int init_box(lab_box *o, uint64_t id, double sx, double sy, double sz,
                    double x, double y, double z, odm_material3d mat) {
    odm_status s;
    memset(o, 0, sizeof(*o));
    s = odm_mesh3d_make_box(o->v, 24u, o->t, 12u, q_m(sx), q_m(sy), q_m(sz), &o->mesh);
    if (s != ODM_STATUS_OK) return 0;
    o->material = mat;
    o->asset.asset_id = id; o->asset.mesh = &o->mesh; o->asset.material = &o->material;
    o->instance.instance_id = id; o->instance.asset = &o->asset; o->instance.transform = transform_xyz(x,y,z);
    return odm_asset3d_validate(&o->asset) == ODM_STATUS_OK;
}

static int init_ring(lab_ring *o, uint64_t id, double inner, double outer, double depth,
                     double x, double y, double z, odm_material3d mat) {
    odm_status s;
    memset(o, 0, sizeof(*o));
    s = odm_mesh3d_make_annular_prism(o->v, 2048u, o->t, 2048u, 96u,
                                      q_m(inner), q_m(outer), q_m(depth), &o->mesh);
    if (s != ODM_STATUS_OK) return 0;
    o->material = mat;
    o->asset.asset_id = id; o->asset.mesh = &o->mesh; o->asset.material = &o->material;
    o->instance.instance_id = id; o->instance.asset = &o->asset; o->instance.transform = transform_xyz(x,y,z);
    return odm_asset3d_validate(&o->asset) == ODM_STATUS_OK;
}

static int init_floor(lab_floor *o, uint64_t id, odm_material3d mat) {
    odm_vec3_q32 p00 = {q_m(-5.0), q_m(0.0), q_m(-5.0)};
    odm_vec3_q32 p10 = {q_m( 5.0), q_m(0.0), q_m(-5.0)};
    odm_vec3_q32 p01 = {q_m(-5.0), q_m(0.0), q_m( 5.0)};
    odm_vec3_q32 p11 = {q_m( 5.0), q_m(0.0), q_m( 5.0)};
    odm_vec3_q31 normal = {0, INT32_MAX, 0};
    odm_status s;
    memset(o,0,sizeof(*o));
    s = odm_mesh3d_make_quad_grid(o->v,81u,o->t,128u,8u,8u,p00,p10,p01,p11,normal,&o->mesh);
    if (s != ODM_STATUS_OK) return 0;
    o->material = mat;
    o->asset.asset_id=id; o->asset.mesh=&o->mesh; o->asset.material=&o->material;
    o->instance.instance_id=id; o->instance.asset=&o->asset; o->instance.transform=transform_xyz(0,0,0);
    return odm_asset3d_validate(&o->asset) == ODM_STATUS_OK;
}

static void set_error(char *error, size_t cap, const char *where, odm_status s) {
    if (error && cap) snprintf(error, cap, "%s: %s", where, odm_status_name(s));
}

static int draw_one(const odm_camera3d *cam, const odm_lighting3d *light,
                    const odm_shadow3d_map *shadow, const odm_instance3d *inst,
                    odm_raster3d_target *target, uint64_t *frags, char *err, size_t errcap) {
    odm_status s;
    uint64_t f=0;
    if (shadow) s=odm_raster3d_draw_instance_shadowed(cam,light,shadow,inst,target,&f);
    else s=odm_raster3d_draw_instance(cam,light,inst,target,&f);
    if (s != ODM_STATUS_OK) { set_error(err,errcap,"raster",s); return 0; }
    *frags += f;
    return 1;
}

int odpar_lab_render_scene(uint8_t *rgba, uint32_t width, uint32_t height,
                           double yaw_deg, double pitch_deg, double distance_m,
                           double phase, int shadows, uint64_t *out_fragments,
                           char *error, size_t error_cap) {
    odm_camera3d camera;
    odm_lighting3d lighting;
    odm_raster3d_target target;
    odm_shadow3d_map shadow;
    lab_floor floor_o;
    lab_box box1, box2, pedestal;
    lab_ring ring;
    uint32_t *depth = NULL, *shadow_depth = NULL;
    uint64_t fragments=0, sf=0;
    const uint8_t clear[4] = {7u, 9u, 13u, 255u};
    double yaw=yaw_deg*M_PI/180.0, pitch=pitch_deg*M_PI/180.0;
    double cx=sin(yaw)*cos(pitch)*distance_m;
    double cy=1.25 + sin(pitch)*distance_m;
    double cz=cos(yaw)*cos(pitch)*distance_m;
    double bob=0.18*sin(phase*2.0*M_PI);
    odm_vec3_q32 target_point={q_m(0),q_m(0.9),q_m(0)};
    odm_vec3_q31 world_up={0,INT32_MAX,0};
    odm_status s;
    odm_shadow3d_map *shadow_ptr=NULL;

    if (error && error_cap) error[0]='\0';
    if (!rgba || width<64u || height<64u || width>2048u || height>2048u || distance_m<1.5 || distance_m>20.0) {
        if (error && error_cap) snprintf(error,error_cap,"invalid render arguments");
        return 0;
    }
    if (phase < 0.0) phase=0.0;
    depth=(uint32_t*)calloc((size_t)width*(size_t)height,sizeof(uint32_t));
    if (!depth) { if(error&&error_cap) snprintf(error,error_cap,"depth allocation failed"); return 0; }
    memset(&target,0,sizeof(target)); target.width=width; target.height=height; target.stride_bytes=width*4u; target.rgba8=rgba; target.inv_z_q16_16=depth;
    s=odm_raster3d_clear(&target,clear); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"clear",s);goto fail;}

    s=odm_camera3d_init(&camera); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"camera_init",s);goto fail;}
    camera.position.x=q_m(cx); camera.position.y=q_m(cy); camera.position.z=q_m(cz);
    camera.exposure_gain_q16_16=82000u;
    camera.tone_mapper=ODM_CAMERA_TONEMAP_REINHARD;
    s=odm_camera3d_look_at(&camera,target_point,world_up); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"camera_look_at",s);goto fail;}

    memset(&lighting,0,sizeof(lighting));
    lighting.ambient_rgb16[0]=10000u; lighting.ambient_rgb16[1]=11000u; lighting.ambient_rgb16[2]=15000u;
    lighting.light_count=2u;
    lighting.lights[0].type=ODM_LIGHT_DIRECTIONAL;
    lighting.lights[0].direction=(odm_vec3_q31){-850000000, -1800000000, -650000000};
    lighting.lights[0].rgb16[0]=65535u; lighting.lights[0].rgb16[1]=60000u; lighting.lights[0].rgb16[2]=52000u;
    lighting.lights[0].intensity_q31=INT32_MAX;
    lighting.lights[1].type=ODM_LIGHT_POINT;
    lighting.lights[1].position=(odm_vec3_q32){q_m(-2.5),q_m(3.0),q_m(2.0)};
    lighting.lights[1].rgb16[0]=26000u; lighting.lights[1].rgb16[1]=42000u; lighting.lights[1].rgb16[2]=65535u;
    lighting.lights[1].intensity_q31=1500000000;
    lighting.lights[1].range_m=q_m(8.0);
    s=odm_lighting3d_validate(&lighting); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"lighting",s);goto fail;}

    if(!init_floor(&floor_o,1u,material_pbr(38,42,52,210,0,0,0,0)) ||
       !init_box(&pedestal,2u,2.4,0.35,2.4,0,0.175,0,material_pbr(42,46,58,170,20,0,0,0)) ||
       !init_box(&box1,3u,1.15,1.15,1.15,-1.05,0.93+bob,0,material_pbr(205,78,58,95,25,5000,500,200)) ||
       !init_box(&box2,4u,0.9,1.65,0.9,1.1,1.0-bob*0.35,-0.25,material_pbr(48,108,215,62,145,300,800,6000)) ||
       !init_ring(&ring,5u,0.75,1.04,0.18,0,1.45,0.85+bob*0.5,material_pbr(230,178,56,45,210,5000,2600,200))) {
        if(error&&error_cap) snprintf(error,error_cap,"scene asset construction failed"); goto fail;
    }

    if (shadows) {
        odm_vec3_q31 ldir;
        odm_vec3_q32 ldq={q_m(-0.8),q_m(-1.6),q_m(-0.7)};
        shadow_depth=(uint32_t*)malloc(512u*512u*sizeof(uint32_t));
        if(!shadow_depth){if(error&&error_cap)snprintf(error,error_cap,"shadow allocation failed");goto fail;}
        s=odm_scene3d_vec3_normalize(ldq,&ldir); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"shadow_dir",s);goto fail;}
        lighting.lights[0].direction=ldir;
        s=odm_shadow3d_init_directional(&shadow,shadow_depth,512u,512u,target_point,ldir,world_up,
                                        q_m(8.0),q_m(8.0),q_m(10.0),q_m(0.006),
                                        1250000000,0u,1u);
        if(s!=ODM_STATUS_OK){set_error(error,error_cap,"shadow_init",s);goto fail;}
        s=odm_shadow3d_clear(&shadow); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"shadow_clear",s);goto fail;}
#define SHADOW_ADD(inst) do { sf=0; s=odm_shadow3d_draw_instance(&shadow,(inst),&sf); if(s!=ODM_STATUS_OK){set_error(error,error_cap,"shadow_draw",s);goto fail;} fragments+=sf; } while(0)
        SHADOW_ADD(&pedestal.instance); SHADOW_ADD(&box1.instance); SHADOW_ADD(&box2.instance); SHADOW_ADD(&ring.instance);
#undef SHADOW_ADD
        shadow_ptr=&shadow;
    }

    if(!draw_one(&camera,&lighting,shadow_ptr,&floor_o.instance,&target,&fragments,error,error_cap) ||
       !draw_one(&camera,&lighting,shadow_ptr,&pedestal.instance,&target,&fragments,error,error_cap) ||
       !draw_one(&camera,&lighting,shadow_ptr,&box1.instance,&target,&fragments,error,error_cap) ||
       !draw_one(&camera,&lighting,shadow_ptr,&box2.instance,&target,&fragments,error,error_cap) ||
       !draw_one(&camera,&lighting,shadow_ptr,&ring.instance,&target,&fragments,error,error_cap)) goto fail;

    if(out_fragments)*out_fragments=fragments;
    free(shadow_depth); free(depth); return 1;
fail:
    free(shadow_depth); free(depth); return 0;
}

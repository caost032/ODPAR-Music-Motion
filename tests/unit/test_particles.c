/* Simulacion de particulas. Contrato en odm_particles.h. */

#include "odm_particles.h"
#include "odm_compositor.h"
#include "odm_supersample.h"

#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

static void pa_cfg(odm_particles_config *c, uint32_t nature) {
    memset(c, 0, sizeof(*c));
    c->schema_version = ODM_PARTICLES_SCHEMA_VERSION;
    c->count = 64u;
    c->nature = nature;
    c->flow_q31 = (uint32_t)INT32_MAX / 4u;
    c->drag_q31 = (uint32_t)(((uint64_t)INT32_MAX * 92u) / 100u);
    c->impulse_q31 = (uint32_t)INT32_MAX / 2u;
    c->seed = UINT64_C(0x0d130d130d130d13);
}

void odm_test_particles(odm_test_context *context) {
    odm_particles_config cfg;
    odm_particles_state st;
    uint32_t i, n;

    /* Colocacion estable: la misma semilla da el mismo aire. */
    {
        odm_particles_state a, b;
        pa_cfg(&cfg, ODM_PARTICLE_NATURE_FLOAT);
        ODM_TEST_CHECK(context, odm_particles_init(&cfg, 640u, 360u, &a) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_particles_init(&cfg, 640u, 360u, &b) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&a, &b, sizeof(a)) == 0);
        /* Y todas dentro del lienzo. */
        for (i = 0u; i < a.count; ++i) {
            ODM_TEST_CHECK(context, a.x_q16[i] >= 0 && a.x_q16[i] < (int32_t)(640u << 16));
            ODM_TEST_CHECK(context, a.y_q16[i] >= 0 && a.y_q16[i] < (int32_t)(360u << 16));
        }
    }
    {
        odm_particles_config malo = cfg;
        malo.count = ODM_PARTICLES_MAX + 1u;
        ODM_TEST_CHECK(context, odm_particles_init(&malo, 64u, 64u, &st) == ODM_STATUS_INVALID_ARGUMENT);
        malo = cfg; malo.nature = ODM_PARTICLE_NATURE_COUNT;
        ODM_TEST_CHECK(context, odm_particles_init(&malo, 64u, 64u, &st) == ODM_STATUS_INVALID_ARGUMENT);
    }

    /* NINGUNA PARTICULA QUEDA DENTRO DEL NUCLEO.
     * Es la razon de ser de la colision: si una se colara, el campo dejaria de
     * leerse como algo que el nucleo aparta. */
    for (n = 0u; n < ODM_PARTICLE_NATURE_COUNT; ++n) {
        int32_t cx = (int32_t)(320u << 16), cy = (int32_t)(180u << 16);
        int32_t radio = (int32_t)(80u << 16);
        uint32_t paso;
        pa_cfg(&cfg, n);
        ODM_TEST_CHECK(context, odm_particles_init(&cfg, 640u, 360u, &st) == ODM_STATUS_OK);
        for (paso = 0u; paso < 200u; ++paso) {
            ODM_TEST_CHECK(context,
                odm_particles_step(&st, &cfg, cx, cy, radio, paso % 20u == 0u ? (int32_t)(2u << 16) : 0)
                    == ODM_STATUS_OK);
            for (i = 0u; i < st.count; ++i) {
                int64_t dx = (int64_t)st.x_q16[i] - cx, dy = (int64_t)st.y_q16[i] - cy;
                /* Comparacion en el cuadrado para no depender de la raiz. */
                int64_t d2 = (dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8);
                int64_t r2 = ((int64_t)radio >> 8) * ((int64_t)radio >> 8);
                /* Un LSB de margen: la expulsion cae justo sobre la superficie. */
                ODM_TEST_CHECK(context, d2 >= r2 - (r2 / 1000));
            }
            /* Y siempre dentro del lienzo: el reciclado no puede perderlas. */
            for (i = 0u; i < st.count; ++i) {
                ODM_TEST_CHECK(context, st.x_q16[i] >= 0 && st.x_q16[i] < (int32_t)(640u << 16));
                ODM_TEST_CHECK(context, st.y_q16[i] >= 0 && st.y_q16[i] < (int32_t)(360u << 16));
            }
        }
    }

    /* EL CHOQUE DEJA HUELLA: hay inercia, no solo desplazamiento.
     * Un nucleo que crece tiene que dejar el campo con mas velocidad que uno
     * quieto, y esa diferencia debe sobrevivir varios pasos despues del golpe. */
    {
        odm_particles_state quieto, golpeado;
        uint64_t vq = 0u, vg = 0u;
        uint32_t paso;
        int32_t cx = (int32_t)(320u << 16), cy = (int32_t)(180u << 16);
        int32_t radio = (int32_t)(100u << 16);
        pa_cfg(&cfg, ODM_PARTICLE_NATURE_FLOAT);
        ODM_TEST_CHECK(context, odm_particles_init(&cfg, 640u, 360u, &quieto) == ODM_STATUS_OK);
        golpeado = quieto;
        for (paso = 0u; paso < 12u; ++paso) {
            ODM_TEST_CHECK(context, odm_particles_step(&quieto, &cfg, cx, cy, radio, 0) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                odm_particles_step(&golpeado, &cfg, cx, cy, radio,
                                   paso == 0u ? (int32_t)(6u << 16) : 0) == ODM_STATUS_OK);
        }
        for (i = 0u; i < quieto.count; ++i) {
            int64_t a = quieto.vx_q16[i], b = quieto.vy_q16[i];
            int64_t c = golpeado.vx_q16[i], d = golpeado.vy_q16[i];
            vq += (uint64_t)((a < 0 ? -a : a) + (b < 0 ? -b : b));
            vg += (uint64_t)((c < 0 ? -c : c) + (d < 0 ? -d : d));
        }
        /* Doce pasos despues del golpe la diferencia sigue ahi. */
        ODM_TEST_CHECK(context, vg > vq);
    }

    /* CADA NATURALEZA ARRASTRA HACIA SU LADO. No basta con que existan: tienen
     * que producir movimientos distinguibles, o serian una lista decorativa. */
    {
        int64_t dx_der = 0, dx_izq = 0, dy_cae = 0, dy_sube = 0;
        uint32_t paso;
        struct { uint32_t nat; int64_t *acc; int eje; } casos[4];
        uint32_t k;
        casos[0].nat = ODM_PARTICLE_NATURE_WIND_RIGHT; casos[0].acc = &dx_der;  casos[0].eje = 0;
        casos[1].nat = ODM_PARTICLE_NATURE_WIND_LEFT;  casos[1].acc = &dx_izq;  casos[1].eje = 0;
        casos[2].nat = ODM_PARTICLE_NATURE_FALL;       casos[2].acc = &dy_cae;  casos[2].eje = 1;
        casos[3].nat = ODM_PARTICLE_NATURE_RISE;       casos[3].acc = &dy_sube; casos[3].eje = 1;
        for (k = 0u; k < 4u; ++k) {
            pa_cfg(&cfg, casos[k].nat);
            ODM_TEST_CHECK(context, odm_particles_init(&cfg, 640u, 360u, &st) == ODM_STATUS_OK);
            for (paso = 0u; paso < 5u; ++paso)
                ODM_TEST_CHECK(context, odm_particles_step(&st, &cfg, 0, 0, 0, 0) == ODM_STATUS_OK);
            *casos[k].acc = 0;
            for (i = 0u; i < st.count; ++i)
                *casos[k].acc += casos[k].eje == 0 ? st.vx_q16[i] : st.vy_q16[i];
        }
        ODM_TEST_CHECK(context, dx_der > 0);
        ODM_TEST_CHECK(context, dx_izq < 0);
        ODM_TEST_CHECK(context, dy_cae > 0);
        ODM_TEST_CHECK(context, dy_sube < 0);
    }

    /* EL AIRE LLEGA AL PLAN, Y SOLO SI EL PLAN LO PIDE.
     *
     * La posicion simulada es autoridad del plan: si el rasterizador la
     * re-derivara, el campo dependeria de cuando se dibuja. Y una configuracion
     * que declara aire simulado con un plan sin campo NO puede degradarse en
     * silencio al campo por semilla -- ese es exactamente el fallo que la
     * bandera explicita existe para hacer ruidoso. */
    {
        odm_layered_config c;
        odm_layered_frame_plan plan;
        odm_composition_frame_state comp;
        odm_director_frame_state dir;

        ODM_TEST_CHECK(context,
            odm_layered_config_init_default(&c, ODM_CANVAS_ASPECT_HORIZONTAL_16_9,
                                            640u, 360u, 30) == ODM_STATUS_OK);
        c.field.flags |= ODM_FIELD_PARTICLES;
        c.field.particle_count = 64u;
        memset(&comp, 0, sizeof(comp));
        memset(&dir, 0, sizeof(dir));
        comp.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
        comp.mode = ODM_COMPOSITION_MODE_FLOW;
        comp.tick_index = 0u;
        comp.center_sample = 0;
        dir.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
        dir.tick_index = 0u;
        dir.layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&c, &comp, &dir, 0, 48000, &plan) == ODM_STATUS_OK);
        /* Un plan recien resuelto no trae campo: la resolucion es pura. */
        ODM_TEST_CHECK(context, plan.particle_sim == 0u);

        pa_cfg(&cfg, ODM_PARTICLE_NATURE_WIND_RIGHT);
        ODM_TEST_CHECK(context, odm_particles_init(&cfg, 640u, 360u, &st) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
            odm_layered_frame_plan_set_particles(&plan, &st) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, plan.particle_sim == 1u);
        for (i = 0u; i < st.count; ++i) {
            ODM_TEST_CHECK(context, plan.particle_x_q16[i] == st.x_q16[i]);
            ODM_TEST_CHECK(context, plan.particle_y_q16[i] == st.y_q16[i]);
            ODM_TEST_CHECK(context, plan.particle_vx_q16[i] == st.vx_q16[i]);
            ODM_TEST_CHECK(context, plan.particle_vy_q16[i] == st.vy_q16[i]);
        }

        /* Un campo de otro lienzo no vale: el reciclado por los bordes dejaria
         * de cerrar y el aire se acumularia en una franja. */
        {
            odm_particles_state ajeno;
            ODM_TEST_CHECK(context,
                odm_particles_init(&cfg, 641u, 360u, &ajeno) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                odm_layered_frame_plan_set_particles(&plan, &ajeno) == ODM_STATUS_INVALID_DATA);
        }

        /* El supermuestreo escala el campo como el resto de la geometria: son
         * pixeles, no razones. Si no escalara, el aire se apinaria en la
         * esquina del raster fino. */
        {
            odm_layered_config alta;
            odm_layered_frame_plan plan_alta;
            c.field.flags |= ODM_FIELD_PARTICLE_SIM;
            ODM_TEST_CHECK(context, odm_layered_config_validate(&c) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                odm_supersample_scale(&c, &plan, 2u, &alta, &plan_alta) == ODM_STATUS_OK);
            for (i = 0u; i < st.count; ++i) {
                ODM_TEST_CHECK(context, plan_alta.particle_x_q16[i] == st.x_q16[i] * 2);
                ODM_TEST_CHECK(context, plan_alta.particle_vy_q16[i] == st.vy_q16[i] * 2);
            }
        }

        /* Fallo explicito en los dos sentidos: config con aire y plan sin el,
         * y plan con aire y config sin el. */
        {
            odm_layered_config sin_aire = c;
            uint64_t fb = 0u, sb = 0u;
            uint8_t *marco = NULL, *scratch = NULL;
            odm_render_surface_frame sup;
            uint8_t px[8];
            odm_sha256_digest h;
            uint64_t rf = 0u, rs = 0u;
            odm_status st_sin, st_con;

            memset(px, 0, sizeof(px));
            memset(&sup, 0, sizeof(sup));
            sup.width = 1u; sup.height = 1u;
            sup.pixel_format = ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL;
            sup.primaries = ODM_COLOR_PRIMARIES_BT709;
            sup.transfer = ODM_COLOR_TRANSFER_LINEAR;
            sup.alpha_mode = ODM_ALPHA_PREMULTIPLIED;
            sup.pixel_bytes = sizeof(px); sup.pixels = px;

            sin_aire.field.flags &= ~ODM_FIELD_PARTICLE_SIM;
            ODM_TEST_CHECK(context,
                odm_layered_render_requirements(&c, ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,
                                                &fb, &sb) == ODM_STATUS_OK);
            marco = (uint8_t *)malloc((size_t)fb);
            scratch = (uint8_t *)aligned_alloc(8u, (size_t)sb);
            if (marco && scratch) {
                /* plan CON campo, config SIN bandera */
                st_sin = odm_layered_render_frame(&sin_aire, &plan, &sup, 0,
                                                  ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,
                                                  scratch, sb, marco, fb, &rf, &rs, &h);
                ODM_TEST_CHECK(context, st_sin == ODM_STATUS_INVALID_DATA);
                /* config CON bandera, plan SIN campo */
                plan.particle_sim = 0u;
                st_con = odm_layered_render_frame(&c, &plan, &sup, 0,
                                                  ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,
                                                  scratch, sb, marco, fb, &rf, &rs, &h);
                ODM_TEST_CHECK(context, st_con == ODM_STATUS_INVALID_DATA);
            }
            free(marco);
            free(scratch);
        }
    }

    /* Identidad de politica. */
    {
        uint8_t bytes[ODM_PARTICLES_POLICY_BYTES];
        uint64_t required = 0u;
        ODM_TEST_CHECK(context,
            odm_particles_policy_bytes(NULL, 0u, &required) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == ODM_PARTICLES_POLICY_BYTES);
        ODM_TEST_CHECK(context,
            odm_particles_policy_bytes(bytes, sizeof(bytes), &required) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(bytes, "ODMPART1", 8u) == 0);
    }
}

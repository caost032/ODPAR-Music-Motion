/* Escena dinamica: propiedades que deben sostenerse sobre la composicion.
 * Contrato en odm_visual_scene.h. */

#include "odm_visual_scene.h"

#include "test_harness.h"

#include <string.h>

#define VS_SR UINT64_C(48000)
#define VS_Q  ODM_VD_Q31_ONE

/* Composicion minima valida con un golpe en un lane concreto. */
static void build_frame(odm_composition_frame_state *f, uint32_t hit) {
    uint32_t i;
    memset(f, 0, sizeof(*f));
    f->schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    f->core_breath_q31 = hit;
    f->core_border_q31 = hit;
    f->halo_q31 = hit;
    f->grid_q31 = hit;
    f->particles_q31 = hit;
    f->memory_echo_q31 = hit;
    f->accent_q31 = hit;
    f->fracture_q31 = hit;
    f->chroma_split_q31 = hit;
    for (i = 0u; i < (uint32_t)ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
        f->radial_body_q31[i] = 0u;
        f->radial_release_q31[i] = 0u;
        f->radial_attack_q31[i] = (i == 40u) ? hit : 0u;
        f->radial_q31[i] = (i == 40u) ? hit : 0u;
    }
}

void odm_test_visual_scene(odm_test_context *context) {
    odm_visual_scene scene;
    odm_composition_frame_state in, out;
    uint32_t i;

    ODM_TEST_CHECK(context, odm_visual_scene_init(&scene, 0u) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, scene.schema_version == ODM_VISUAL_SCENE_SCHEMA_VERSION);
    ODM_TEST_CHECK(context, scene.enabled == 1u);

    /* Cada objetivo tiene kernel valido; los desconocidos fallan cerrado. */
    for (i = 0u; i < (uint32_t)ODM_VS_TARGET_COUNT; ++i) {
        odm_vd_kernel k;
        ODM_TEST_CHECK(context, odm_visual_scene_target_kernel(i, &k) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_vd_kernel_validate(&k) == ODM_STATUS_OK);
    }
    {
        odm_vd_kernel k;
        ODM_TEST_CHECK(context,
            odm_visual_scene_target_kernel(ODM_VS_TARGET_COUNT, &k) == ODM_STATUS_INVALID_ARGUMENT);
    }

    /* SILENCIO: entrada en cero durante mucho tiempo no produce movimiento. */
    build_frame(&in, 0u);
    for (i = 0u; i < 600u; ++i) {
        uint64_t sample = (uint64_t)i * (VS_SR / 60u);
        ODM_TEST_CHECK(context,
            odm_visual_scene_apply(&scene, &in, sample, &out) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, out.core_breath_q31 == 0u);
        ODM_TEST_CHECK(context, out.halo_q31 == 0u);
        ODM_TEST_CHECK(context, out.grid_q31 == 0u);
        ODM_TEST_CHECK(context, out.radial_attack_q31[40] == 0u);
    }

    /* LA PROPIEDAD CENTRAL: un golpe seguido de evidencia nula sigue viendose.
     * Sin dinamica, el fotograma siguiente valdria exactamente cero. */
    ODM_TEST_CHECK(context, odm_visual_scene_init(&scene, 0u) == ODM_STATUS_OK);
    {
        uint64_t frame_60 = VS_SR / 60u;
        uint32_t after_hit, next_frame, previous;
        build_frame(&in, VS_Q);
        /* En la muestra exacta del disparo el nivel todavia es el de partida:
         * el ataque de 1 ms tiene duracion real y la respuesta es continua, no
         * un escalon. Lo que importa es que un fotograma despues ya subio. */
        ODM_TEST_CHECK(context,
            odm_visual_scene_apply(&scene, &in, VS_SR, &out) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
            odm_visual_scene_apply(&scene, &in, VS_SR + frame_60, &out) == ODM_STATUS_OK);
        after_hit = out.radial_attack_q31[40];
        ODM_TEST_CHECK(context, after_hit > 0u);

        build_frame(&in, 0u); /* la evidencia desaparece por completo */
        ODM_TEST_CHECK(context,
            odm_visual_scene_apply(&scene, &in, VS_SR + 2u * frame_60, &out) == ODM_STATUS_OK);
        next_frame = out.radial_attack_q31[40];
        ODM_TEST_CHECK(context, next_frame > 0u);   /* no se apago en un fotograma */
        ODM_TEST_CHECK(context, out.core_breath_q31 > 0u);
        ODM_TEST_CHECK(context, out.halo_q31 > 0u);

        /* Y decae de forma monotona hasta el reposo, sin quedarse encendido. */
        previous = next_frame;
        for (i = 3u; i < 400u; ++i) {
            uint32_t now;
            ODM_TEST_CHECK(context,
                odm_visual_scene_apply(&scene, &in, VS_SR + (uint64_t)i * frame_60, &out)
                    == ODM_STATUS_OK);
            now = out.radial_attack_q31[40];
            ODM_TEST_CHECK(context, now <= previous);
            previous = now;
        }
        ODM_TEST_CHECK(context,
            odm_visual_scene_apply(&scene, &in, VS_SR + VS_SR * UINT64_C(30), &out)
                == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, out.radial_attack_q31[40] == 0u);
    }

    /* AISLAMIENTO DE LANE: el golpe del 40 no puede encender a sus vecinos. */
    ODM_TEST_CHECK(context, odm_visual_scene_init(&scene, 0u) == ODM_STATUS_OK);
    build_frame(&in, VS_Q);
    ODM_TEST_CHECK(context, odm_visual_scene_apply(&scene, &in, 1000u, &out) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_visual_scene_apply(&scene, &in, 1000u + VS_SR / 60u, &out) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, out.radial_attack_q31[40] > 0u);
    ODM_TEST_CHECK(context, out.radial_attack_q31[39] == 0u);
    ODM_TEST_CHECK(context, out.radial_attack_q31[41] == 0u);

    /* PROVENANCE RECONSTRUIBLE: `final` debe seguir derivandose de la tupla con
     * la misma aritmetica que usa el render para validar, o el plan resultante
     * seria rechazado. Se recomputa aqui de forma independiente. */
    {
        uint32_t body = out.radial_body_q31[40];
        uint32_t release = out.radial_release_q31[40];
        uint32_t attack = out.radial_attack_q31[40];
        uint64_t q = (uint64_t)VS_Q;
        uint64_t mix = ((uint64_t)release * UINT64_C(751619277) + q / 2u) / q;
        uint64_t base = (uint64_t)body + (((q - body) * mix + q / 2u) / q);
        uint64_t final_expected;
        if (base > q) base = q;
        final_expected = base + (((q - base) * (uint64_t)attack + q / 2u) / q);
        if (final_expected > q) final_expected = q;
        ODM_TEST_CHECK(context, out.radial_q31[40] == (uint32_t)final_expected);
    }

    /* EL TIEMPO NO RETROCEDE: reproducir hacia atras dentro de una escena
     * revivificaria ataques historicos, asi que se rechaza. */
    ODM_TEST_CHECK(context,
        odm_visual_scene_apply(&scene, &in, 500u, &out) == ODM_STATUS_INVALID_STATE);

    /* SEEK: reposiciona en reposo, sin arrastrar cola ni repetir ataques. */
    ODM_TEST_CHECK(context, odm_visual_scene_seek(&scene, 5u * VS_SR) == ODM_STATUS_OK);
    build_frame(&in, 0u);
    ODM_TEST_CHECK(context,
        odm_visual_scene_apply(&scene, &in, 5u * VS_SR, &out) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, out.radial_attack_q31[40] == 0u);
    ODM_TEST_CHECK(context, out.core_breath_q31 == 0u);

    /* IDEMPOTENCIA EN LA MISMA MUESTRA: evaluar dos veces el mismo instante no
     * puede producir dos respuestas distintas. */
    ODM_TEST_CHECK(context, odm_visual_scene_init(&scene, 0u) == ODM_STATUS_OK);
    build_frame(&in, VS_Q);
    {
        odm_composition_frame_state first, second;
        ODM_TEST_CHECK(context, odm_visual_scene_apply(&scene, &in, 2000u, &first) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_visual_scene_apply(&scene, &in, 2000u, &second) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&first, &second, sizeof(first)) == 0);
    }

    /* CONTROL A/B: desactivada, la escena es un paso directo exacto. */
    ODM_TEST_CHECK(context, odm_visual_scene_init(&scene, 0u) == ODM_STATUS_OK);
    scene.enabled = 0u;
    build_frame(&in, VS_Q / 3u);
    ODM_TEST_CHECK(context, odm_visual_scene_apply(&scene, &in, 7000u, &out) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(&in, &out, sizeof(in)) == 0);

    /* INVARIANZA DE FPS: la misma muestra da el mismo estado con independencia
     * de la rejilla de fotogramas que la haya alcanzado. */
    {
        odm_visual_scene a, b;
        odm_composition_frame_state fa, fb;
        uint64_t target = VS_SR * UINT64_C(2);
        uint64_t step24 = VS_SR / 24u, step60 = VS_SR / 60u;
        uint64_t s;
        ODM_TEST_CHECK(context, odm_visual_scene_init(&a, 0u) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_visual_scene_init(&b, 0u) == ODM_STATUS_OK);
        build_frame(&in, VS_Q);
        /* Un unico disparo en la misma muestra exacta para ambos. */
        ODM_TEST_CHECK(context, odm_visual_scene_apply(&a, &in, VS_SR, &fa) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_visual_scene_apply(&b, &in, VS_SR, &fb) == ODM_STATUS_OK);
        build_frame(&in, 0u);
        for (s = VS_SR + step24; s < target; s += step24)
            ODM_TEST_CHECK(context, odm_visual_scene_apply(&a, &in, s, &fa) == ODM_STATUS_OK);
        for (s = VS_SR + step60; s < target; s += step60)
            ODM_TEST_CHECK(context, odm_visual_scene_apply(&b, &in, s, &fb) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_visual_scene_apply(&a, &in, target, &fa) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_visual_scene_apply(&b, &in, target, &fb) == ODM_STATUS_OK);
        /* Ninguna evidencia intermedia disparo nada, asi que la respuesta en
         * `target` depende solo del golpe original y de la muestra. */
        ODM_TEST_CHECK(context, fa.radial_attack_q31[40] == fb.radial_attack_q31[40]);
        ODM_TEST_CHECK(context, fa.core_breath_q31 == fb.core_breath_q31);
        ODM_TEST_CHECK(context, fa.halo_q31 == fb.halo_q31);
        ODM_TEST_CHECK(context, fa.grid_q31 == fb.grid_q31);
    }
}

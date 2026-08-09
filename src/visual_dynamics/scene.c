/* Escena dinamica. Contrato en odm_visual_scene.h.
 *
 * Aqui es donde la evidencia deja de ser un valor instantaneo y pasa a ser la
 * causa de una respuesta con duracion. Todo lo que se publica sigue siendo
 * reconstruible: la provenance radial se recalcula con la misma aritmetica que
 * usa el render para validar, asi que la cadena causal no se rompe.
 */

#include "odm_visual_scene.h"

#include <string.h>

#define VS_Q ODM_VD_Q31_ONE

/* Misma multiplicacion Q1.31 que composicion y raster. Repetirla aqui es
 * deliberado: este modulo no debe depender de cabeceras de composicion para
 * algo tan basico, y el oraculo verifica que las tres coinciden bit a bit. */
static uint32_t vs_mul(uint32_t a, uint32_t b) {
    uint64_t p = (uint64_t)a * (uint64_t)b + (uint64_t)(VS_Q / 2u);
    p /= (uint64_t)VS_Q;
    return p > (uint64_t)VS_Q ? VS_Q : (uint32_t)p;
}

static uint32_t vs_add(uint32_t a, uint32_t b) {
    uint64_t s = (uint64_t)a + (uint64_t)b;
    return s > (uint64_t)VS_Q ? VS_Q : (uint32_t)s;
}

/* Clase de respuesta por objetivo. Es la unica tabla donde se decide el
 * caracter temporal de cada elemento visual, y su orden fija la separacion
 * micro/meso/macro: la rejilla no puede latir como una punta radial porque su
 * clase no se lo permite. */
static uint32_t vs_target_class(uint32_t target) {
    switch (target) {
        case ODM_VS_TARGET_CORE_BREATH: return ODM_VD_CLASS_CORE_IMPACT;
        case ODM_VS_TARGET_CORE_BORDER: return ODM_VD_CLASS_CORE_IMPACT;
        case ODM_VS_TARGET_HALO:        return ODM_VD_CLASS_HALO_BODY;
        case ODM_VS_TARGET_GRID:        return ODM_VD_CLASS_GRID_FIELD;
        case ODM_VS_TARGET_PARTICLES:   return ODM_VD_CLASS_PARTICLE_SPAWN;
        case ODM_VS_TARGET_MEMORY:      return ODM_VD_CLASS_MEMORY_FIELD;
        case ODM_VS_TARGET_ACCENT:      return ODM_VD_CLASS_CORE_IMPACT;
        case ODM_VS_TARGET_FRACTURE:    return ODM_VD_CLASS_CORE_IMPACT;
        case ODM_VS_TARGET_CHROMA:      return ODM_VD_CLASS_RADIAL_TIP;
        default:                        return ODM_VD_CLASS_COUNT;
    }
}

odm_status odm_visual_scene_target_kernel(uint32_t target, odm_vd_kernel *out_kernel) {
    uint32_t cls;
    if (!out_kernel || target >= (uint32_t)ODM_VS_TARGET_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    cls = vs_target_class(target);
    if (cls >= (uint32_t)ODM_VD_CLASS_COUNT) return ODM_STATUS_INVARIANT_BROKEN;
    return odm_vd_kernel_class(cls, out_kernel);
}

odm_status odm_visual_scene_init(odm_visual_scene *scene, uint64_t sample) {
    uint32_t i;
    odm_status st;
    if (!scene) return ODM_STATUS_INVALID_ARGUMENT;
    memset(scene, 0, sizeof(*scene));
    scene->schema_version = ODM_VISUAL_SCENE_SCHEMA_VERSION;
    scene->enabled = 1u;
    scene->last_sample = sample;
    for (i = 0u; i < (uint32_t)ODM_VS_TARGET_COUNT; ++i) {
        odm_vd_kernel k;
        st = odm_visual_scene_target_kernel(i, &k);
        if (st != ODM_STATUS_OK) return st;
        st = odm_vd_state_init(&k, sample, &scene->global[i]);
        if (st != ODM_STATUS_OK) return st;
    }
    {
        odm_vd_kernel tip;
        st = odm_vd_kernel_class(ODM_VD_CLASS_RADIAL_TIP, &tip);
        if (st != ODM_STATUS_OK) return st;
        for (i = 0u; i < (uint32_t)ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
            st = odm_vd_state_init(&tip, sample, &scene->radial_attack[i]);
            if (st != ODM_STATUS_OK) return st;
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_visual_scene_seek(odm_visual_scene *scene, uint64_t sample) {
    uint32_t enabled;
    odm_status st;
    if (!scene) return ODM_STATUS_INVALID_ARGUMENT;
    enabled = scene->enabled;
    st = odm_visual_scene_init(scene, sample);
    if (st != ODM_STATUS_OK) return st;
    scene->enabled = enabled;
    return ODM_STATUS_OK;
}

/* Ofrece evidencia y devuelve el nivel resultante en la misma muestra. */
static odm_status vs_step(const odm_vd_kernel *kernel, odm_vd_state *state,
                          uint64_t sample, uint32_t evidence, uint32_t *out_level) {
    odm_status st = odm_vd_trigger(kernel, state, sample, evidence, NULL);
    if (st != ODM_STATUS_OK) return st;
    return odm_vd_eval(kernel, state, sample, out_level);
}

odm_status odm_visual_scene_apply(odm_visual_scene *scene,
                                  const odm_composition_frame_state *in,
                                  uint64_t presentation_sample,
                                  odm_composition_frame_state *out) {
    odm_composition_frame_state f;
    odm_vd_kernel kernels[ODM_VS_TARGET_COUNT];
    odm_vd_kernel tip;
    odm_status st;
    uint32_t i;
    uint32_t level;

    if (!scene || !in || !out) return ODM_STATUS_INVALID_ARGUMENT;
    if (scene->schema_version != ODM_VISUAL_SCENE_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    /* El tiempo no retrocede dentro de una escena. Un salto hacia atras es un
     * seek explicito, no una evaluacion: reproducir la cola de un ataque ya
     * ocurrido seria inventar movimiento. */
    if (presentation_sample < scene->last_sample) return ODM_STATUS_INVALID_STATE;

    f = *in;
    if (!scene->enabled) { *out = f; scene->last_sample = presentation_sample; return ODM_STATUS_OK; }

    for (i = 0u; i < (uint32_t)ODM_VS_TARGET_COUNT; ++i) {
        st = odm_visual_scene_target_kernel(i, &kernels[i]);
        if (st != ODM_STATUS_OK) return st;
    }
    st = odm_vd_kernel_class(ODM_VD_CLASS_RADIAL_TIP, &tip);
    if (st != ODM_STATUS_OK) return st;

    /* --- Objetivos globales --------------------------------------------- */

    st = vs_step(&kernels[ODM_VS_TARGET_CORE_BREATH], &scene->global[ODM_VS_TARGET_CORE_BREATH],
                 presentation_sample, in->core_breath_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.core_breath_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_CORE_BORDER], &scene->global[ODM_VS_TARGET_CORE_BORDER],
                 presentation_sample, in->core_border_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.core_border_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_HALO], &scene->global[ODM_VS_TARGET_HALO],
                 presentation_sample, in->halo_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.halo_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_GRID], &scene->global[ODM_VS_TARGET_GRID],
                 presentation_sample, in->grid_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.grid_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_PARTICLES], &scene->global[ODM_VS_TARGET_PARTICLES],
                 presentation_sample, in->particles_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.particles_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_MEMORY], &scene->global[ODM_VS_TARGET_MEMORY],
                 presentation_sample, in->memory_echo_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.memory_echo_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_ACCENT], &scene->global[ODM_VS_TARGET_ACCENT],
                 presentation_sample, in->accent_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.accent_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_FRACTURE], &scene->global[ODM_VS_TARGET_FRACTURE],
                 presentation_sample, in->fracture_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.fracture_q31 = level;

    st = vs_step(&kernels[ODM_VS_TARGET_CHROMA], &scene->global[ODM_VS_TARGET_CHROMA],
                 presentation_sample, in->chroma_split_q31, &level);
    if (st != ODM_STATUS_OK) return st;
    f.chroma_split_q31 = level;

    /* --- Puntas radiales -------------------------------------------------
     * Cada lane conserva su propia punta. El cuerpo y el release no llevan
     * dinamica: ya son envolventes lentas por construccion, y darles memoria
     * encima solo emborronaria el espectro.
     *
     * `final` se recompone con la formula exacta que el render usa para
     * validar la provenance. Si no se recompusiera, un plan con la punta
     * dinamica y el final antiguo seria rechazado -- correctamente. */
    for (i = 0u; i < (uint32_t)ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
        uint32_t body = in->radial_body_q31[i];
        uint32_t release = in->radial_release_q31[i];
        uint32_t attack;
        uint32_t after_release;

        st = vs_step(&tip, &scene->radial_attack[i], presentation_sample,
                     in->radial_attack_q31[i], &attack);
        if (st != ODM_STATUS_OK) return st;

        /* 0.35 = peso de release, congelado por Layered Policy v11. */
        after_release = vs_add(body, vs_mul(VS_Q - body,
                                            vs_mul(release, UINT32_C(751619277))));
        f.radial_body_q31[i] = body;
        f.radial_release_q31[i] = release;
        f.radial_attack_q31[i] = attack;
        f.radial_q31[i] = vs_add(after_release,
                                 vs_mul(VS_Q - after_release, attack));
    }

    scene->last_sample = presentation_sample;
    *out = f;
    return ODM_STATUS_OK;
}

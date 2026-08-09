#ifndef ODM_VISUAL_SCENE_H
#define ODM_VISUAL_SCENE_H

/* Escena dinamica: aplica Visual Dynamics a un Composition Frame State.
 *
 * La composicion sigue siendo pura y sin estado: la misma entrada produce el
 * mismo resultado siempre, y todos los oraculos existentes la siguen viendo
 * igual. Esta capa va ENCIMA y aporta lo unico que la composicion no puede
 * tener por definicion: memoria de lo que ya paso.
 *
 *   Composition (pura, exacta)  ->  Visual Scene (con memoria)  ->  Frame Plan
 *
 * Sin esta capa, un valor visual existe exactamente mientras la evidencia esta
 * alta: sube, aparece; baja, desaparece. Con ella, la evidencia DISPARA una
 * respuesta que tiene ataque, cuerpo y caida propios, medidos en muestras.
 *
 * Reglas que se conservan:
 *   - la evidencia nunca se modifica, solo se consume;
 *   - sin disparo aceptado no hay movimiento (silencio -> quietud);
 *   - la provenance radial se recalcula con la MISMA formula que valida el
 *     render, de modo que un plan sigue siendo reconstruible y un plan forjado
 *     sigue fallando cerrado;
 *   - el estado es un POD de enteros de ancho fijo: serializable, comparable y
 *     checkpointable para seek exacto.
 */

#include "odm_visual.h"
#include "odm_visual_dynamics.h"
#include "odm_status.h"

#include <stdint.h>

#define ODM_VISUAL_SCENE_SCHEMA_VERSION UINT32_C(1)

/* Objetivos globales con respuesta propia. El orden es el del contrato de
 * serializacion y no debe reordenarse sin versionar. */
enum {
    ODM_VS_TARGET_CORE_BREATH = 0u,
    ODM_VS_TARGET_CORE_BORDER = 1u,
    ODM_VS_TARGET_HALO        = 2u,
    ODM_VS_TARGET_GRID        = 3u,
    ODM_VS_TARGET_PARTICLES   = 4u,
    ODM_VS_TARGET_MEMORY      = 5u,
    ODM_VS_TARGET_ACCENT      = 6u,
    ODM_VS_TARGET_FRACTURE    = 7u,
    ODM_VS_TARGET_CHROMA      = 8u,
    ODM_VS_TARGET_COUNT       = 9u
};

typedef struct {
    uint32_t schema_version;
    uint32_t enabled;            /* 0 = paso directo, util para A/B contra v1 */
    uint64_t last_sample;        /* ultima muestra evaluada; detecta retroceso */
    odm_vd_state global[ODM_VS_TARGET_COUNT];
    /* Cada lane espectral conserva su propia punta transitoria: es lo que
     * impide que una linea desaparezca en un fotograma. */
    odm_vd_state radial_attack[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
} odm_visual_scene;

/* Deja la escena en reposo en `sample`. Todos los objetivos valen su suelo. */
odm_status odm_visual_scene_init(odm_visual_scene *scene, uint64_t sample);

/* Kernel asociado a un objetivo global. Expuesto para inspeccion y oraculos. */
odm_status odm_visual_scene_target_kernel(uint32_t target, odm_vd_kernel *out_kernel);

/* Aplica dinamica al frame de composicion.
 *
 * `in` es evidencia resuelta y no se modifica. `presentation_sample` es la
 * coordenada exacta del fotograma en la linea de 48 kHz: no un indice de
 * fotograma, no un tick. Avanzar el estado con la misma muestra dos veces es
 * idempotente; retroceder se rechaza para no reproducir ataques historicos.
 *
 * `in` y `out` pueden apuntar al mismo objeto. */
odm_status odm_visual_scene_apply(odm_visual_scene *scene,
                                  const odm_composition_frame_state *in,
                                  uint64_t presentation_sample,
                                  odm_composition_frame_state *out);

/* Reposiciona la escena para un salto de reproduccion. Deja todos los
 * objetivos en reposo en la muestra destino: un seek no reproduce ataques que
 * ya ocurrieron ni arrastra colas de antes del salto. */
odm_status odm_visual_scene_seek(odm_visual_scene *scene, uint64_t sample);

#endif /* ODM_VISUAL_SCENE_H */

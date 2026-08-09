#ifndef ODM_SUPERSAMPLE_H
#define ODM_SUPERSAMPLE_H

/* Supermuestreo: calidad de raster sin tocar la semantica.
 *
 * El problema que resuelve no es "falta resolucion". Es que un borde curvo, una
 * diagonal o una linea fina de un pixel se cuantizan a la rejilla de salida y
 * producen escalones. Subir la resolucion de salida no lo arregla: produce el
 * mismo escalon mas pequeno.
 *
 * La solucion es rasterizar en una rejilla N veces mas fina y resolver por area
 * exacta. Cada pixel final es la media aritmetica exacta de N*N muestras, lo que
 * da cobertura fraccionaria real en los bordes.
 *
 * Frontera explicita, y es la parte importante:
 *
 *   el nivel de calidad decide FIDELIDAD DEL RASTER, jamas interpretacion.
 *
 * El plan escalado conserva exactamente los mismos valores Q1.31 de reaccion,
 * la misma provenance radial, el mismo instante de muestra y el mismo
 * config_sha256 recalculado sobre la configuracion escalada. Solo cambian
 * coordenadas geometricas, y cambian por un factor entero. Dos niveles de
 * calidad del mismo proyecto en la misma muestra tienen el mismo estado
 * semantico; difieren unicamente dentro de esta frontera.
 */

#include "odm_compositor.h"
#include "odm_status.h"

#include <stdint.h>

#define ODM_SUPERSAMPLE_SCHEMA_VERSION UINT32_C(1)
#define ODM_SUPERSAMPLE_MAX_FACTOR     UINT32_C(4)

/* Niveles de calidad. El nombre describe el coste, no el resultado musical. */
enum {
    ODM_QUALITY_PREVIEW_FAST = 0u,  /* 1x: sin supermuestreo                 */
    ODM_QUALITY_PREVIEW_HIGH = 1u,  /* 2x: AA completo, coste 4x en raster   */
    ODM_QUALITY_MASTER       = 2u,  /* 3x: para entrega                      */
    ODM_QUALITY_MASTER_ULTRA = 3u,  /* 4x: contenido offline muy detallado   */
    ODM_QUALITY_COUNT        = 4u
};

/* Factor de supermuestreo de un nivel de calidad. 0 si el nivel no existe. */
uint32_t odm_supersample_factor(uint32_t quality_tier);

/* Escala configuracion y plan por un factor entero.
 *
 * Toda coordenada geometrica en Q16.16 y toda dimension en pixeles se
 * multiplican por `factor`. Ningun valor Q1.31 de reaccion se toca, ni el
 * instante de muestra, ni la duracion, ni las banderas, ni la provenance.
 * `out_plan->config_sha256` se recalcula sobre `out_config`, de modo que el
 * plan escalado sigue pasando la validacion del render sin excepciones.
 *
 * Falla cerrado si el resultado desbordaria los limites de lienzo. */
odm_status odm_supersample_scale(const odm_layered_config *config,
                                 const odm_layered_frame_plan *plan,
                                 uint32_t factor,
                                 odm_layered_config *out_config,
                                 odm_layered_frame_plan *out_plan);

/* Resuelve un raster RGBA16LE lineal premultiplicado de alta resolucion a la
 * resolucion de salida promediando bloques de factor*factor.
 *
 * La media se hace en enteros de 64 bits con redondeo al mas cercano, asi que
 * es exacta, determinista y bit-identica en cualquier plataforma. Promediar
 * componentes premultiplicados es lo correcto: es una integral de cobertura,
 * no una mezcla de colores.
 *
 * `hi_width` y `hi_height` deben ser exactamente `out_width*factor` y
 * `out_height*factor`. */
odm_status odm_supersample_resolve_rgba16(const uint8_t *hi_frame,
                                          uint64_t hi_bytes,
                                          uint32_t hi_width, uint32_t hi_height,
                                          uint32_t factor,
                                          uint8_t *out_frame,
                                          uint64_t out_capacity,
                                          uint32_t out_width, uint32_t out_height,
                                          uint64_t *out_required_bytes);

/* Resuelve un raster RGBA8 sRGB de alta resolucion a la resolucion de salida.
 *
 * Promediar codigos sRGB directamente es un error de bulto: sRGB no es lineal,
 * asi que la media de dos codigos no es el color medio. Aqui cada muestra se
 * decodifica a luz lineal con la EOTF congelada del motor, se promedia en ese
 * dominio y se vuelve a codificar. Esa es la unica forma de que un borde
 * suavizado tenga la luminancia correcta en vez de verse sucio.
 *
 * El alfa si es lineal por definicion y se promedia tal cual. */
odm_status odm_supersample_resolve_rgba8_srgb(const uint8_t *hi_frame,
                                              uint64_t hi_bytes,
                                              uint32_t hi_width, uint32_t hi_height,
                                              uint32_t factor,
                                              uint8_t *out_frame,
                                              uint64_t out_capacity,
                                              uint32_t out_width, uint32_t out_height,
                                              uint64_t *out_required_bytes);

/* Camino completo con supermuestreo. Reserva internamente el raster de alta
 * resolucion y su scratch; devuelve el frame ya resuelto en la resolucion
 * pedida. Con factor 1 es exactamente odm_layered_render_frame. */
odm_status odm_supersample_render_frame(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        const odm_render_surface_frame *core_surface,
                                        const odm_job_ticket *job_ticket,
                                        uint32_t quality_tier,
                                        uint8_t *frame, uint64_t frame_capacity,
                                        uint64_t *out_required_frame_bytes,
                                        odm_sha256_digest *out_pixel_sha256);

#endif /* ODM_SUPERSAMPLE_H */

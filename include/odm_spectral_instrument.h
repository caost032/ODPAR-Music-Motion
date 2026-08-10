#ifndef ODM_SPECTRAL_INSTRUMENT_H
#define ODM_SPECTRAL_INSTRUMENT_H

/* Instrumento espectral directo.
 *
 * QUE ES Y POR QUE EXISTE
 *
 * El extremo de cada aguja radial sale DIRECTAMENTE de la magnitud FFT de la
 * ventana de audio actual. No participa ninguna cola, retencion, pulso de
 * evento, senal de Director ni reloj procedural.
 *
 * Es una capa de PRESENTACION, no de inferencia, y por eso vive separada de
 * Music-Reaction en lugar de reemplazarla. Music-Reaction sigue siendo la
 * autoridad sobre lo que la musica SIGNIFICA -- eventos, energia, novedad --
 * con su confianza publicada. El instrumento no afirma nada: muestra lo que la
 * DSP midio. Mezclar las dos cosas fue lo que hizo que el halo dejara de leerse
 * como un espectro y pasara a ser un resplandor generico.
 *
 * LAS DOS DECISIONES QUE LO HACEN EXACTO
 *
 * 1. INTEGRACION POR BANDA, NO MUESTREO PUNTUAL.
 *
 *    Las bandas son logaritmicas y las de arriba son anchas: la ultima abarca
 *    41 bins de FFT. Leer un solo punto interpolado por banda descarta el resto
 *    -- de 659 bins utiles solo llegarian 192 -- y lo que llega depende de
 *    donde cayo el ancla. El resultado es una lectura que subestima los agudos
 *    y ademas tiembla, porque un bin aislado fluctua mucho mas que su entorno.
 *
 *    Aqui cada banda es la MEDIA EXACTA del espectro sobre su intervalo
 *    [borde_inferior, borde_superior), tratando el espectro como la funcion
 *    lineal a trozos que pasa por los centros de bin. Esa eleccion es la unica
 *    que se comporta bien en los dos extremos: cuando la banda es mas ancha que
 *    un bin integra toda su energia, y cuando es mas estrecha que un bin -- las
 *    graves lo son: la banda 0 mide 0.09 bins -- se reduce exactamente al valor
 *    interpolado en su centro. Sin discontinuidad en el cruce.
 *
 * 2. LOGARITMO EXACTO.
 *
 *    La escala perceptual compartida es logaritmica. Aproximar el log2 con una
 *    mantisa lineal cuesta hasta 0.086 octavas -- 0.52 dB -- y el error es
 *    periodico dentro de cada octava, asi que no es ruido: es un festoneado
 *    regular a lo largo del espectro que no viene de la musica. Aqui el log2 se
 *    calcula bit a bit por cuadrados sucesivos, en enteros, con un error por
 *    debajo de 1 LSB de Q16.16 (0.000092 dB).
 *
 * HONESTIDAD SOBRE LA RESOLUCION
 *
 * Con ventana de 2048 a 48 kHz, un bin son 23.44 Hz. Por debajo de unos 500 Hz
 * varias bandas caen dentro del mismo bin: ahi el instrumento NO resuelve 96
 * frecuencias distintas, las interpola. `odm_spectral_instrument_band_bins_q16`
 * publica cuantos bins cubre cada banda para que nadie afirme una resolucion
 * que la DSP no da.
 *
 * TIEMPO
 *
 * El analisis va a 100 Hz y la presentacion puede ir a 60 fps o a la que sea.
 * En un centro exacto no se interpola. Entre centros, cada banda es la
 * interpolacion lineal de las dos medidas que la rodean, en el dominio
 * perceptual. Esa interpolacion mira hacia adelante y por tanto NO es causal:
 * por eso se expone en una API de proyeccion aparte y jamas se disfraza de
 * cuadro de reaccion en tiempo real.
 */

#include "odm_hash.h"
#include "odm_music_map.h"
#include "odm_status.h"
#include "odm_visual.h"

#include <stdint.h>

#define ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION UINT32_C(1)
#define ODM_SPECTRAL_INSTRUMENT_POLICY_VERSION UINT32_C(5)
#define ODM_SPECTRAL_INSTRUMENT_BAND_COUNT     UINT32_C(96)
#define ODM_SPECTRAL_INSTRUMENT_MIN_HZ         UINT32_C(34)
#define ODM_SPECTRAL_INSTRUMENT_MAX_HZ         UINT32_C(15000)
#define ODM_SPECTRAL_INSTRUMENT_POLICY_BYTES   UINT32_C(1024)

#define ODM_SPECTRAL_INSTRUMENT_FLAG_SHARED_LOG_SCALE      UINT32_C(1)
#define ODM_SPECTRAL_INSTRUMENT_FLAG_DIRECT_FFT            UINT32_C(2)
#define ODM_SPECTRAL_INSTRUMENT_FLAG_CENTERED_INTERPOLATION UINT32_C(4)
/* La banda integra su intervalo completo en vez de muestrear un punto. */
#define ODM_SPECTRAL_INSTRUMENT_FLAG_BAND_INTEGRATION      UINT32_C(8)
/* El logaritmo es exacto bit a bit, no una mantisa lineal. */
#define ODM_SPECTRAL_INSTRUMENT_FLAG_EXACT_LOG2            UINT32_C(16)

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t tick_index;
    uint64_t center_sample;
    /* Magnitud sobre la escala perceptual logaritmica compartida. Es lo que
     * consume el render. */
    uint32_t band_presence_q31[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT];
    /* Media integrada del espectro en la banda, antes de la transferencia
     * logaritmica. Se conserva para trazabilidad y para las pruebas de
     * monotonia: permite comprobar la transferencia por separado. */
    uint32_t band_magnitude_q31[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT];
    uint32_t reserved[8];
} odm_spectral_instrument_tick;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t presentation_sample;
    uint64_t lower_tick_index;
    uint64_t upper_tick_index;
    uint64_t lower_center_sample;
    uint64_t upper_center_sample;
    uint32_t upper_weight_q31;
    uint32_t band_presence_q31[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT];
    uint32_t reserved[7];
} odm_spectral_instrument_projection;

/* Coordenada Q16.16 congelada, en bins de FFT, del ancla de una banda. */
odm_status odm_spectral_instrument_band_bin_q16(uint32_t band, uint32_t *out_bin_q16);

/* Bordes Q16.16 del intervalo de integracion de una banda. `band` admite el
 * valor BAND_COUNT para leer el borde superior final. */
odm_status odm_spectral_instrument_band_edge_q16(uint32_t edge, uint32_t *out_bin_q16);

/* Cuantos bins de FFT cubre una banda, en Q16.16. Por debajo de 1.0 la banda
 * esta limitada por la resolucion de la ventana y no resuelve: interpola. */
odm_status odm_spectral_instrument_band_bins_q16(uint32_t band, uint32_t *out_bins_q16);

/* log2 exacto en Q16.16, expuesto porque es la pieza que define la escala. */
uint32_t odm_spectral_instrument_log2_q16(uint32_t value);

/* Simetria del reparto de bandas sobre el arco. */
enum {
    ODM_SPECTRAL_SYMMETRY_NONE     = 0u, /* 96 bandas, de graves a agudos      */
    ODM_SPECTRAL_SYMMETRY_MIRROR   = 1u, /* espejo izquierda/derecha           */
    ODM_SPECTRAL_SYMMETRY_QUADRANT = 2u, /* espejo en los dos ejes             */
    ODM_SPECTRAL_SYMMETRY_COUNT    = 3u
};

/* Suaviza la secuencia de ticks EN EL SITIO.
 *
 * POR QUE HACE FALTA
 *
 * La medida directa no tiene memoria: es su virtud y tambien su problema. A 60
 * cuadros por segundo cada uno es una medicion independiente, asi que la aguja
 * salta sin continuidad; y en el eje de frecuencia la musica tiene armonicos,
 * de modo que bandas vecinas caen encima o al lado de un parcial y difieren
 * muchisimo. Las dos cosas son fisicamente ciertas y las dos se leen como
 * ruido: una corona de puas que cambia de forma cada cuadro no comunica la
 * musica, distrae de ella.
 *
 * QUE HACE, EXACTAMENTE
 *
 * - TEMPORAL: un polo asimetrico sobre la rejilla de 100 Hz, con peso de
 *   subida y de bajada distintos. Subir rapido conserva el ataque -- que es
 *   informacion real -- y bajar despacio da la cola que el ojo necesita para
 *   leer continuidad. Al operar sobre TICKS y no sobre cuadros, el resultado no
 *   depende de los FPS: la misma cancion da la misma envolvente a 24, 30 o 60.
 *
 * - LATERAL: un nucleo simetrico [1,2,1]/4 sobre el eje de bandas, aplicado
 *   `band_blur` veces. No inventa energia ni la desplaza: reparte la de un
 *   parcial entre sus vecinas, que es lo que convierte un peine de armonicos en
 *   una envolvente legible.
 *
 * Ninguna de las dos es una afirmacion sobre la musica: son filtros declarados,
 * con sus parametros publicados, aplicados a una medida que sigue siendo la
 * medida. Por eso viven aqui y no dentro de la extraccion. */
odm_status odm_spectral_instrument_smooth(odm_spectral_instrument_tick *ticks,
                                          uint64_t tick_count,
                                          uint32_t rise_q31,
                                          uint32_t fall_q31,
                                          uint32_t band_blur);

/* Empuje del nucleo a partir de las bandas graves medidas.
 *
 * El nucleo respiraba con una senal generica de Music-Reaction y apenas se
 * movia. Atarlo a un rango de bandas DECLARADO -- por omision 34-130 Hz, que es
 * donde vive el bombo -- hace que el golpe empuje la imagen, y ademas se puede
 * explicar: el empuje es la media de la presencia medida en ese rango, nada
 * mas. No hay senal inventada ni estado oculto. */
#define ODM_SPECTRAL_CORE_BAND_LO UINT32_C(0)
#define ODM_SPECTRAL_CORE_BAND_HI UINT32_C(23)
odm_status odm_spectral_instrument_core_drive(
    const odm_spectral_instrument_projection *projection,
    uint32_t band_lo, uint32_t band_hi, uint32_t *out_drive_q31);

/* Reparte las bandas sobre el arco con la simetria pedida. */
odm_status odm_spectral_instrument_symmetry_band(uint32_t symmetry, uint32_t sector,
                                                 uint32_t *out_band);

/* Extrae despues de odm_music_analyze_tick(), mientras el scratch sigue siendo
 * el del mismo tick. No modifica el estado de Music Map. */
odm_status odm_spectral_instrument_extract_tick(
    const odm_music_analysis_tick *base_tick,
    const odm_music_analysis_scratch *scratch,
    odm_spectral_instrument_tick *out_tick);

/* Proyeccion centrada en el tiempo, offline y explicitamente no causal. */
odm_status odm_spectral_instrument_project_centered(
    const odm_spectral_instrument_tick *ticks,
    uint64_t tick_count,
    uint64_t presentation_sample,
    odm_spectral_instrument_projection *out_projection);

/* Instala la proyeccion como autoridad del extremo radial.
 *
 * Reescribe tambien la provenance para que siga describiendo el valor: bajo
 * este gobierno, cuerpo = presencia y cola = ataque = 0. Dejar la provenance de
 * Music-Reaction junto a un extremo que ya no viene de ella seria una identidad
 * que no cubre lo que gobierna. */
odm_status odm_composition_apply_spectral_instrument_projection(
    const odm_spectral_instrument_projection *projection,
    uint32_t symmetry,
    odm_composition_frame_state *in_out_frame);

odm_status odm_spectral_instrument_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                                uint64_t *out_required);
odm_status odm_spectral_instrument_policy_current_sha256(odm_sha256_digest *out_hash);

#endif /* ODM_SPECTRAL_INSTRUMENT_H */

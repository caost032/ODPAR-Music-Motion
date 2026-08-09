#ifndef ODM_MEDIA_ADAPTER_H
#define ODM_MEDIA_ADAPTER_H

/* Frontera de canonicalizacion de medios.
 *
 * El motor decodifica de forma nativa WAV y PNG, y eso es lo unico en lo que
 * confia. Para todo lo demas (MP3, AAC/M4A, FLAC, OGG, Opus, JPEG, WebP,
 * MP4/WebM) existe un adaptador externo cuyo unico trabajo es traducir a un
 * formato que ODPAR ya sabe verificar.
 *
 * La arquitectura es deliberada:
 *
 *   archivo hostil -> adaptador aislado -> WAV/PNG canonico
 *                  -> decodificador nativo fail-closed -> PCM Q1.31 / RGBA8
 *
 * El decodificador externo NUNCA es autoridad. Su salida vuelve a pasar por el
 * mismo camino verificado que un WAV escrito a mano, con los mismos limites y
 * los mismos rechazos. Si el adaptador miente sobre lo que produjo, el
 * decodificador nativo lo detecta.
 *
 * Reglas duras del proceso adaptador:
 *   - argv directo via execvp, jamas una shell: ningun nombre de archivo puede
 *     convertirse en comando;
 *   - stdin cerrado, stdout capturado, stderr descartado;
 *   - limite de bytes de salida: al superarlo se mata el proceso, no se trunca
 *     en silencio;
 *   - limite de tiempo de pared, porque un archivo malicioso puede colgar un
 *     decodificador;
 *   - la procedencia (identidad y version del adaptador) queda registrada.
 */

#include "odm_media.h"
#include "odm_status.h"
#include "odm_hash.h"

#include <stdint.h>

#define ODM_MEDIA_ADAPTER_SCHEMA_VERSION UINT32_C(1)

/* Techos de admision. Un archivo que los supera se rechaza; nunca se recorta en
 * silencio para que "quepa". */
#define ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES   UINT64_C(2147483648) /* 2 GiB */
#define ODM_MEDIA_ADAPTER_MAX_SECONDS        UINT32_C(600)        /* 10 min de pared */
#define ODM_MEDIA_ADAPTER_MAX_AUDIO_SECONDS  UINT64_C(7200)       /* 2 h de audio */
#define ODM_MEDIA_ADAPTER_ID_BYTES           UINT32_C(128)

/* Procedencia del decodificador externo. Va al recibo: sin esto no se puede
 * decir de donde salieron las muestras. */
typedef struct {
    uint32_t schema_version;
    uint32_t available;              /* 0 = adaptador ausente en este host    */
    char     identity[ODM_MEDIA_ADAPTER_ID_BYTES]; /* p.ej. "ffmpeg 7.1.5"    */
    odm_sha256_digest argv_sha256;   /* hash del argv exacto ejecutado        */
    odm_sha256_digest input_sha256;  /* hash de los bytes de entrada          */
    odm_sha256_digest output_sha256; /* hash del WAV/PNG canonico producido   */
    uint64_t input_bytes;
    uint64_t output_bytes;
} odm_media_adapter_provenance;

/* Consulta si hay adaptador en este host y cual es su identidad.
 * Devuelve ODM_STATUS_OK con available=0 cuando no lo hay: la ausencia de un
 * adaptador es un hecho, no un error. */
odm_status odm_media_adapter_query(odm_media_adapter_provenance *out_provenance);

/* True si el contenedor detectado necesita adaptador para poder decodificarse.
 * WAV y PNG devuelven 0 porque el motor los decodifica el mismo. */
int odm_media_adapter_required(uint32_t container);

/* Canonicaliza audio de cualquier contenedor soportado a WAV PCM 16-bit estereo
 * 48 kHz en memoria. El resultado esta pensado para pasar despues por
 * odm_media_decode_audio_q31, que es quien manda.
 *
 * `out_wav` puede ser NULL para consultar el tamano requerido. */
odm_status odm_media_adapter_canonicalize_audio(const char *path,
                                                uint8_t *out_wav,
                                                uint64_t wav_capacity,
                                                uint64_t *out_required_bytes,
                                                odm_media_adapter_provenance *out_provenance);

/* Canonicaliza una imagen de cualquier contenedor soportado a PNG RGBA8. */
odm_status odm_media_adapter_canonicalize_image(const char *path,
                                                uint8_t *out_png,
                                                uint64_t png_capacity,
                                                uint64_t *out_required_bytes,
                                                odm_media_adapter_provenance *out_provenance);

/* Extrae un fotograma de video en el instante exacto `sample` (coordenada de la
 * linea de tiempo canonica a 48 kHz) y lo entrega como PNG RGBA8. El video se
 * trata como una fuente de imagenes indexada por muestra, nunca por FPS. */
odm_status odm_media_adapter_video_frame_at_sample(const char *path,
                                                   uint64_t sample,
                                                   uint8_t *out_png,
                                                   uint64_t png_capacity,
                                                   uint64_t *out_required_bytes,
                                                   odm_media_adapter_provenance *out_provenance);

/* Camino completo: archivo en disco -> PCM Q1.31 estereo 48 kHz canonico.
 * Usa el decodificador nativo cuando basta y el adaptador cuando hace falta, y
 * en ambos casos la salida final la produce el decodificador nativo. */
odm_status odm_media_load_audio_q31(const char *path,
                                    odm_pcm_stereo_q31 *frames,
                                    uint64_t frame_capacity,
                                    uint64_t *out_required_frames,
                                    odm_media_facts *out_facts,
                                    odm_media_adapter_provenance *out_provenance);

/* Camino completo: archivo en disco -> RGBA8 canonico. */
odm_status odm_media_load_image_rgba8(const char *path,
                                      uint8_t *rgba, uint64_t rgba_capacity,
                                      uint64_t *out_required_bytes,
                                      odm_media_facts *out_facts,
                                      odm_media_adapter_provenance *out_provenance);

#endif /* ODM_MEDIA_ADAPTER_H */

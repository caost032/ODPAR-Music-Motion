/* Frontera de canonicalizacion de medios. Contrato en odm_media_adapter.h.
 *
 * Todo lo que entra por aqui sale convertido a WAV o PNG y vuelve a pasar por el
 * decodificador nativo. El proceso externo se ejecuta con execvp y argv fijo:
 * ninguna ruta de archivo puede convertirse en comando porque nunca hay shell.
 */

#define _POSIX_C_SOURCE 200809L

#include "odm_media_adapter.h"

#include "odm_limits.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Bloque de lectura del pipe. Suficientemente grande para no hacer millones de
 * llamadas y suficientemente pequeno para no reservar de golpe. */
extern char **environ;

#define ADAPTER_CHUNK_BYTES ((size_t)262144)

/* --------------------------------------------------------------------------
 * Ejecucion aislada del adaptador
 * -------------------------------------------------------------------------- */

typedef struct {
    uint8_t *bytes;
    size_t   size;
    size_t   capacity;
} adapter_buffer;

static void adapter_buffer_free(adapter_buffer *b) {
    if (b->bytes) free(b->bytes);
    b->bytes = NULL; b->size = 0u; b->capacity = 0u;
}

static int adapter_buffer_reserve(adapter_buffer *b, size_t needed) {
    size_t cap;
    uint8_t *grown;
    if (needed <= b->capacity) return 1;
    cap = b->capacity ? b->capacity : ADAPTER_CHUNK_BYTES;
    while (cap < needed) {
        if (cap > (size_t)ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES) return 0;
        cap *= 2u;
    }
    grown = (uint8_t *)realloc(b->bytes, cap);
    if (!grown) return 0;
    b->bytes = grown; b->capacity = cap;
    return 1;
}

/* Ejecuta argv y captura stdout completo.
 *
 * Fail-closed en todos los ejes: si el proceso supera el limite de bytes se le
 * manda SIGKILL y se devuelve error, nunca una salida truncada que parezca
 * valida. Lo mismo al agotarse el tiempo de pared. */
static odm_status adapter_run_ex(char *const argv[], adapter_buffer *out,
                                 uint64_t max_bytes, uint32_t max_seconds,
                                 int require_stdout) {
    posix_spawn_file_actions_t actions;
    int fds[2];
    pid_t pid = -1;
    int spawn_rc;
    odm_status st = ODM_STATUS_OK;
    time_t deadline;

    if (!argv || !out) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (pipe(fds) != 0) return ODM_STATUS_INTERNAL_ERROR;

    /* Sin shell: argv se pasa tal cual, asi que ninguna ruta de archivo puede
     * convertirse en comando. stdin y stderr van a /dev/null para que el
     * adaptador solo pueda producir bytes por stdout y sus mensajes no puedan
     * contaminar la salida del motor. */
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]); close(fds[1]); return ODM_STATUS_INTERNAL_ERROR;
    }
    if (posix_spawn_file_actions_addclose(&actions, fds[0]) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&actions, fds[1]) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
        (void)posix_spawn_file_actions_destroy(&actions);
        close(fds[0]); close(fds[1]);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    spawn_rc = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    (void)posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (spawn_rc != 0) {
        close(fds[0]);
        return ODM_STATUS_UNSUPPORTED; /* adaptador ausente en este host */
    }
    deadline = time(NULL) + (time_t)max_seconds;
    for (;;) {
        ssize_t n;
        if (!adapter_buffer_reserve(out, out->size + ADAPTER_CHUNK_BYTES)) {
            st = ODM_STATUS_OUT_OF_MEMORY;
            break;
        }
        n = read(fds[0], out->bytes + out->size, ADAPTER_CHUNK_BYTES);
        if (n < 0) {
            if (errno == EINTR) continue;
            st = ODM_STATUS_INTERNAL_ERROR;
            break;
        }
        if (n == 0) break; /* fin de salida */
        out->size += (size_t)n;
        if ((uint64_t)out->size > max_bytes) {
            st = ODM_STATUS_BUDGET_EXCEEDED;
            break;
        }
        if (time(NULL) > deadline) {
            st = ODM_STATUS_BUDGET_EXCEEDED;
            break;
        }
    }
    close(fds[0]);

    if (st != ODM_STATUS_OK) {
        kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        adapter_buffer_free(out);
        return st;
    }

    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { adapter_buffer_free(out); return ODM_STATUS_INTERNAL_ERROR; }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            adapter_buffer_free(out);
            return ODM_STATUS_UNSUPPORTED; /* el adaptador no supo con este archivo */
        }
    }
    if (require_stdout && out->size == 0u) {
        adapter_buffer_free(out);
        return ODM_STATUS_INVALID_DATA;
    }
    return ODM_STATUS_OK;
}

/* Caso habitual: la salida util viene por stdout y estar vacia es un fallo. */
static odm_status adapter_run(char *const argv[], adapter_buffer *out,
                              uint64_t max_bytes, uint32_t max_seconds) {
    return adapter_run_ex(argv, out, max_bytes, max_seconds, 1);
}

/* Hash del argv exacto que se ejecuto. Es lo que hace auditable la procedencia:
 * dos decodificaciones con el mismo hash usaron literalmente el mismo comando. */
static odm_status adapter_hash_argv(char *const argv[], odm_sha256_digest *out) {
    odm_sha256_context ctx;
    odm_status st;
    uint32_t i;
    st = odm_sha256_init(&ctx);
    if (st != ODM_STATUS_OK) return st;
    for (i = 0u; argv[i] != NULL; ++i) {
        size_t n = strlen(argv[i]);
        uint8_t len_be[4];
        len_be[0] = (uint8_t)((n >> 24) & 0xffu);
        len_be[1] = (uint8_t)((n >> 16) & 0xffu);
        len_be[2] = (uint8_t)((n >> 8) & 0xffu);
        len_be[3] = (uint8_t)(n & 0xffu);
        st = odm_sha256_update(&ctx, len_be, 4u);
        if (st != ODM_STATUS_OK) return st;
        st = odm_sha256_update(&ctx, (const uint8_t *)argv[i], (uint64_t)n);
        if (st != ODM_STATUS_OK) return st;
    }
    return odm_sha256_final(&ctx, out);
}

/* --------------------------------------------------------------------------
 * Identidad del adaptador
 * -------------------------------------------------------------------------- */

static odm_status adapter_identity(char *out, uint32_t capacity, uint32_t *out_available) {
    char *argv[3];
    adapter_buffer buf;
    odm_status st;
    uint32_t i, n = 0u;

    if (!out || capacity == 0u || !out_available) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, capacity);
    *out_available = 0u;

    argv[0] = (char *)"ffmpeg";
    argv[1] = (char *)"-version";
    argv[2] = NULL;
    st = adapter_run(argv, &buf, UINT64_C(65536), 20u);
    if (st != ODM_STATUS_OK) return ODM_STATUS_OK; /* ausente: hecho, no error */

    /* Primera linea, recortada y saneada a ASCII imprimible. */
    for (i = 0u; i < buf.size && n + 1u < capacity; ++i) {
        unsigned char c = buf.bytes[i];
        if (c == '\n' || c == '\r') break;
        if (c < 32u || c > 126u) c = ' ';
        out[n++] = (char)c;
    }
    out[n] = '\0';
    adapter_buffer_free(&buf);
    *out_available = n > 0u ? 1u : 0u;
    return ODM_STATUS_OK;
}

odm_status odm_media_adapter_query(odm_media_adapter_provenance *out_provenance) {
    if (!out_provenance) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out_provenance, 0, sizeof(*out_provenance));
    out_provenance->schema_version = ODM_MEDIA_ADAPTER_SCHEMA_VERSION;
    return adapter_identity(out_provenance->identity,
                            ODM_MEDIA_ADAPTER_ID_BYTES,
                            &out_provenance->available);
}

int odm_media_adapter_required(uint32_t container) {
    switch (container) {
        case ODM_MEDIA_CONTAINER_WAV:
        case ODM_MEDIA_CONTAINER_PNG:
            return 0; /* el motor los decodifica el mismo */
        default:
            return 1;
    }
}

/* --------------------------------------------------------------------------
 * Lectura de archivo con techo de tamano
 * -------------------------------------------------------------------------- */

static odm_status adapter_read_file(const char *path, adapter_buffer *out) {
    FILE *fh;
    if (!path || !out) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    fh = fopen(path, "rb");
    if (!fh) return ODM_STATUS_INVALID_ARGUMENT;
    for (;;) {
        size_t n;
        if (!adapter_buffer_reserve(out, out->size + ADAPTER_CHUNK_BYTES)) {
            fclose(fh); adapter_buffer_free(out); return ODM_STATUS_OUT_OF_MEMORY;
        }
        n = fread(out->bytes + out->size, 1u, ADAPTER_CHUNK_BYTES, fh);
        out->size += n;
        if ((uint64_t)out->size > ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES) {
            fclose(fh); adapter_buffer_free(out); return ODM_STATUS_BUDGET_EXCEEDED;
        }
        if (n < ADAPTER_CHUNK_BYTES) {
            if (ferror(fh)) { fclose(fh); adapter_buffer_free(out); return ODM_STATUS_INTERNAL_ERROR; }
            break;
        }
    }
    fclose(fh);
    if (out->size == 0u) { adapter_buffer_free(out); return ODM_STATUS_INVALID_DATA; }
    return ODM_STATUS_OK;
}

/* Ejecuta el adaptador escribiendo a un archivo temporal y devuelve su
 * contenido. Es necesario para formatos cuya cabecera exige seek (RIFF/WAV):
 * por un pipe, ffmpeg emite tamanos de streaming que el decodificador nativo
 * rechaza -- correctamente, porque no confia en el adaptador. */
static odm_status adapter_run_to_file(char *argv[], uint32_t path_slot,
                                      const char *suffix, adapter_buffer *out) {
    char tmpl[] = "/tmp/odm-adapter-XXXXXX";
    char path[64];
    int fd;
    odm_status st;
    adapter_buffer discard;

    if (!argv || !out || !suffix) return ODM_STATUS_INVALID_ARGUMENT;
    fd = mkstemp(tmpl);
    if (fd < 0) return ODM_STATUS_INTERNAL_ERROR;
    close(fd);
    if (unlink(tmpl) != 0) { /* nos quedamos con el nombre reservado */ }
    if (snprintf(path, sizeof(path), "%s.%s", tmpl, suffix) < 0)
        return ODM_STATUS_INVARIANT_BROKEN;
    argv[path_slot] = path;

    /* La salida util esta en el archivo, no en stdout: stdout vacio es lo
     * esperado aqui, no un fallo. */
    st = adapter_run_ex(argv, &discard,
                        ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES,
                        ODM_MEDIA_ADAPTER_MAX_SECONDS, 0);
    adapter_buffer_free(&discard);
    if (st != ODM_STATUS_OK) { (void)unlink(path); return st; }

    st = adapter_read_file(path, out);
    (void)unlink(path);
    return st;
}

/* --------------------------------------------------------------------------
 * Canonicalizacion
 * -------------------------------------------------------------------------- */

static odm_status adapter_publish(const adapter_buffer *produced,
                                   const adapter_buffer *source,
                                   char *const argv[],
                                   uint8_t *out_bytes, uint64_t capacity,
                                   uint64_t *out_required,
                                   odm_media_adapter_provenance *prov) {
    odm_status st;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = (uint64_t)produced->size;
    if (prov) {
        memset(prov, 0, sizeof(*prov));
        prov->schema_version = ODM_MEDIA_ADAPTER_SCHEMA_VERSION;
        (void)adapter_identity(prov->identity, ODM_MEDIA_ADAPTER_ID_BYTES, &prov->available);
        st = adapter_hash_argv(argv, &prov->argv_sha256);
        if (st != ODM_STATUS_OK) return st;
        st = odm_sha256(source->bytes, (uint64_t)source->size, &prov->input_sha256);
        if (st != ODM_STATUS_OK) return st;
        st = odm_sha256(produced->bytes, (uint64_t)produced->size, &prov->output_sha256);
        if (st != ODM_STATUS_OK) return st;
        prov->input_bytes = (uint64_t)source->size;
        prov->output_bytes = (uint64_t)produced->size;
    }
    if (!out_bytes) return ODM_STATUS_OK;              /* consulta de tamano */
    if (capacity < (uint64_t)produced->size) return ODM_STATUS_BUFFER_TOO_SMALL;
    memcpy(out_bytes, produced->bytes, produced->size);
    return ODM_STATUS_OK;
}

odm_status odm_media_adapter_canonicalize_audio(const char *path,
                                                uint8_t *out_wav,
                                                uint64_t wav_capacity,
                                                uint64_t *out_required_bytes,
                                                odm_media_adapter_provenance *out_provenance) {
    /* Salida fija: WAV PCM 16-bit little endian, estereo, 48 kHz. Es exactamente
     * lo que el decodificador nativo sabe verificar, asi que la unica libertad
     * que tiene el adaptador es acertar o fallar. */
    char *argv[] = {
        (char *)"ffmpeg",
        (char *)"-hide_banner", (char *)"-loglevel", (char *)"error",
        (char *)"-nostdin",
        (char *)"-i", NULL,
        (char *)"-map", (char *)"0:a:0",          /* solo la primera pista de audio */
        (char *)"-vn", (char *)"-sn", (char *)"-dn",
        (char *)"-ac", (char *)"2",
        (char *)"-ar", (char *)"48000",
        (char *)"-c:a", (char *)"pcm_s16le",
        (char *)"-f", (char *)"wav",
        (char *)"-t", (char *)"7200",             /* techo duro de duracion */
        (char *)"-y",
        NULL,                                     /* [22] archivo temporal    */
        NULL
    };
    adapter_buffer source, produced;
    odm_status st;

    if (!path || !out_required_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    argv[6] = (char *)path;

    st = adapter_read_file(path, &source);
    if (st != ODM_STATUS_OK) return st;

    st = adapter_run_to_file(argv, 22u, "wav", &produced);
    if (st != ODM_STATUS_OK) { adapter_buffer_free(&source); return st; }

    st = adapter_publish(&produced, &source, argv, out_wav, wav_capacity,
                         out_required_bytes, out_provenance);
    adapter_buffer_free(&produced);
    adapter_buffer_free(&source);
    return st;
}

odm_status odm_media_adapter_canonicalize_image(const char *path,
                                                uint8_t *out_png,
                                                uint64_t png_capacity,
                                                uint64_t *out_required_bytes,
                                                odm_media_adapter_provenance *out_provenance) {
    char *argv[] = {
        (char *)"ffmpeg",
        (char *)"-hide_banner", (char *)"-loglevel", (char *)"error",
        (char *)"-nostdin",
        (char *)"-i", NULL,
        (char *)"-map", (char *)"0:v:0",
        (char *)"-frames:v", (char *)"1",
        (char *)"-pix_fmt", (char *)"rgba",
        (char *)"-c:v", (char *)"png",
        (char *)"-f", (char *)"image2pipe",
        (char *)"-",
        NULL
    };
    adapter_buffer source, produced;
    odm_status st;

    if (!path || !out_required_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    argv[6] = (char *)path;

    st = adapter_read_file(path, &source);
    if (st != ODM_STATUS_OK) return st;
    st = adapter_run(argv, &produced,
                     ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES,
                     ODM_MEDIA_ADAPTER_MAX_SECONDS);
    if (st != ODM_STATUS_OK) { adapter_buffer_free(&source); return st; }
    st = adapter_publish(&produced, &source, argv, out_png, png_capacity,
                         out_required_bytes, out_provenance);
    adapter_buffer_free(&produced);
    adapter_buffer_free(&source);
    return st;
}

odm_status odm_media_adapter_video_frame_at_sample(const char *path,
                                                   uint64_t sample,
                                                   uint8_t *out_png,
                                                   uint64_t png_capacity,
                                                   uint64_t *out_required_bytes,
                                                   odm_media_adapter_provenance *out_provenance) {
    /* El instante se expresa en la linea de tiempo canonica de 48 kHz y se
     * traduce a segundos con precision de microsegundo. El FPS del video de
     * origen no participa: es una fuente de imagenes indexada por muestra. */
    char seek[32];
    char *argv[] = {
        (char *)"ffmpeg",
        (char *)"-hide_banner", (char *)"-loglevel", (char *)"error",
        (char *)"-nostdin",
        (char *)"-ss", NULL,
        (char *)"-i", NULL,
        (char *)"-map", (char *)"0:v:0",
        (char *)"-frames:v", (char *)"1",
        (char *)"-pix_fmt", (char *)"rgba",
        (char *)"-c:v", (char *)"png",
        (char *)"-f", (char *)"image2pipe",
        (char *)"-",
        NULL
    };
    adapter_buffer source, produced;
    odm_status st;
    uint64_t whole, micros;

    if (!path || !out_required_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    if (sample > (uint64_t)INT64_MAX) return ODM_STATUS_BUDGET_EXCEEDED;

    whole = sample / UINT64_C(48000);
    micros = ((sample % UINT64_C(48000)) * UINT64_C(1000000)) / UINT64_C(48000);
    if (whole > ODM_MEDIA_ADAPTER_MAX_AUDIO_SECONDS) return ODM_STATUS_BUDGET_EXCEEDED;
    if (snprintf(seek, sizeof(seek), "%llu.%06llu",
                 (unsigned long long)whole, (unsigned long long)micros) < 0)
        return ODM_STATUS_INVARIANT_BROKEN;
    argv[6] = seek;
    argv[8] = (char *)path;

    st = adapter_read_file(path, &source);
    if (st != ODM_STATUS_OK) return st;
    st = adapter_run(argv, &produced,
                     ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES,
                     ODM_MEDIA_ADAPTER_MAX_SECONDS);
    if (st != ODM_STATUS_OK) { adapter_buffer_free(&source); return st; }
    st = adapter_publish(&produced, &source, argv, out_png, png_capacity,
                         out_required_bytes, out_provenance);
    adapter_buffer_free(&produced);
    adapter_buffer_free(&source);
    return st;
}

/* --------------------------------------------------------------------------
 * Camino completo: archivo -> muestras canonicas
 * -------------------------------------------------------------------------- */

odm_status odm_media_load_audio_q31(const char *path,
                                    odm_pcm_stereo_q31 *frames,
                                    uint64_t frame_capacity,
                                    uint64_t *out_required_frames,
                                    odm_media_facts *out_facts,
                                    odm_media_adapter_provenance *out_provenance) {
    adapter_buffer raw;
    odm_media_probe_result probe;
    odm_status st;

    if (!path || !out_required_frames) return ODM_STATUS_INVALID_ARGUMENT;
    if (out_provenance) {
        memset(out_provenance, 0, sizeof(*out_provenance));
        out_provenance->schema_version = ODM_MEDIA_ADAPTER_SCHEMA_VERSION;
    }

    st = adapter_read_file(path, &raw);
    if (st != ODM_STATUS_OK) return st;

    memset(&probe, 0, sizeof(probe));
    st = odm_media_probe(raw.bytes, (uint64_t)raw.size, &probe);
    if (st != ODM_STATUS_OK) { adapter_buffer_free(&raw); return st; }
    if (probe.media_kind != ODM_MEDIA_KIND_AUDIO &&
        probe.media_kind != ODM_MEDIA_KIND_VIDEO) {
        adapter_buffer_free(&raw);
        return ODM_STATUS_UNSUPPORTED;
    }

    if (!odm_media_adapter_required(probe.container)) {
        /* WAV: camino nativo, sin proceso externo. */
        st = odm_media_decode_audio_q31(raw.bytes, (uint64_t)raw.size, frames,
                                        frame_capacity, out_required_frames, out_facts);
        adapter_buffer_free(&raw);
        return st;
    }

    adapter_buffer_free(&raw);
    {
        uint64_t wav_bytes = 0u;
        uint8_t *wav = NULL;
        st = odm_media_adapter_canonicalize_audio(path, NULL, 0u, &wav_bytes, out_provenance);
        if (st != ODM_STATUS_OK) return st;
        if (wav_bytes == 0u || wav_bytes > ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES)
            return ODM_STATUS_BUDGET_EXCEEDED;
        wav = (uint8_t *)malloc((size_t)wav_bytes);
        if (!wav) return ODM_STATUS_OUT_OF_MEMORY;
        st = odm_media_adapter_canonicalize_audio(path, wav, wav_bytes, &wav_bytes, out_provenance);
        if (st == ODM_STATUS_OK) {
            /* El WAV producido vuelve a pasar por el decodificador nativo: el
             * adaptador propone, el motor dispone. */
            st = odm_media_decode_audio_q31(wav, wav_bytes, frames, frame_capacity,
                                            out_required_frames, out_facts);
        }
        free(wav);
    }
    return st;
}

odm_status odm_media_load_image_rgba8(const char *path,
                                      uint8_t *rgba, uint64_t rgba_capacity,
                                      uint64_t *out_required_bytes,
                                      odm_media_facts *out_facts,
                                      odm_media_adapter_provenance *out_provenance) {
    adapter_buffer raw;
    odm_media_probe_result probe;
    odm_status st;

    if (!path || !out_required_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    if (out_provenance) {
        memset(out_provenance, 0, sizeof(*out_provenance));
        out_provenance->schema_version = ODM_MEDIA_ADAPTER_SCHEMA_VERSION;
    }

    st = adapter_read_file(path, &raw);
    if (st != ODM_STATUS_OK) return st;
    memset(&probe, 0, sizeof(probe));
    st = odm_media_probe(raw.bytes, (uint64_t)raw.size, &probe);
    if (st != ODM_STATUS_OK) { adapter_buffer_free(&raw); return st; }

    if (probe.container == ODM_MEDIA_CONTAINER_PNG) {
        st = odm_media_decode_image_rgba8(raw.bytes, (uint64_t)raw.size, rgba,
                                          rgba_capacity, out_required_bytes, out_facts);
        adapter_buffer_free(&raw);
        return st;
    }
    adapter_buffer_free(&raw);

    {
        uint64_t png_bytes = 0u;
        uint8_t *png = NULL;
        st = odm_media_adapter_canonicalize_image(path, NULL, 0u, &png_bytes, out_provenance);
        if (st != ODM_STATUS_OK) return st;
        if (png_bytes == 0u || png_bytes > ODM_MEDIA_ADAPTER_MAX_OUTPUT_BYTES)
            return ODM_STATUS_BUDGET_EXCEEDED;
        png = (uint8_t *)malloc((size_t)png_bytes);
        if (!png) return ODM_STATUS_OUT_OF_MEMORY;
        st = odm_media_adapter_canonicalize_image(path, png, png_bytes, &png_bytes, out_provenance);
        if (st == ODM_STATUS_OK) {
            st = odm_media_decode_image_rgba8(png, png_bytes, rgba, rgba_capacity,
                                              out_required_bytes, out_facts);
        }
        free(png);
    }
    return st;
}

#ifndef ODM_EXPORT_H
#define ODM_EXPORT_H

#include "odm_compositor.h"
#include "odm_hash.h"
#include "odm_media.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_EXPORT_SCHEMA_VERSION UINT32_C(1)
#define ODM_EXPORT_POLICY_VERSION UINT32_C(1)
#define ODM_EXPORT_POLICY_BYTES UINT32_C(1024)
#define ODM_EXPORT_PROFILE_BYTES UINT32_C(256)
#define ODM_EXPORT_RECIPE_BYTES UINT32_C(512)
#define ODM_EXPORT_ARGV_MAX_BYTES UINT32_C(4096)
#define ODM_EXPORT_ARGV_MAX_TOKENS UINT32_C(64)
#define ODM_EXPORT_PATH_MAX_BYTES UINT32_C(1024)

#define ODM_EXPORT_ADAPTER_FFMPEG UINT32_C(1)
#define ODM_EXPORT_CONTAINER_MP4 UINT32_C(1)
#define ODM_EXPORT_VIDEO_H264 UINT32_C(1)
#define ODM_EXPORT_AUDIO_AAC UINT32_C(1)
#define ODM_EXPORT_SOURCE_RGBA8_SRGB UINT32_C(1)
#define ODM_EXPORT_SOURCE_PCM_S32LE_Q31_STEREO UINT32_C(1)
#define ODM_EXPORT_PIXEL_YUV420P UINT32_C(1)
#define ODM_EXPORT_PRIMARIES_BT709 UINT32_C(1)
#define ODM_EXPORT_TRANSFER_SRGB UINT32_C(1)
#define ODM_EXPORT_MATRIX_BT709 UINT32_C(1)
#define ODM_EXPORT_RANGE_TV UINT32_C(1)
#define ODM_EXPORT_RATE_CONTROL_CRF UINT32_C(1)

#define ODM_EXPORT_PRESET_ULTRAFAST UINT32_C(1)
#define ODM_EXPORT_PRESET_VERYFAST UINT32_C(2)
#define ODM_EXPORT_PRESET_MEDIUM UINT32_C(3)
#define ODM_EXPORT_PRESET_SLOW UINT32_C(4)

#define ODM_EXPORT_FLAG_FASTSTART UINT32_C(1)

#define ODM_EXPORT_PLACEHOLDER_VIDEO "{ODPAR_VIDEO_RGBA8}"
#define ODM_EXPORT_PLACEHOLDER_AUDIO "{ODPAR_AUDIO_S32LE}"
#define ODM_EXPORT_PLACEHOLDER_OUTPUT "{ODPAR_OUTPUT_MP4}"

typedef struct {
    uint32_t schema_version;
    uint32_t adapter;
    uint32_t container;
    uint32_t video_codec;
    uint32_t audio_codec;
    uint32_t source_video_format;
    uint32_t source_audio_format;
    uint32_t output_pixel_format;
    uint32_t color_primaries;
    uint32_t color_transfer;
    uint32_t color_matrix;
    uint32_t color_range;
    uint32_t rate_control;
    uint32_t crf;
    uint32_t preset;
    uint32_t video_threads;
    uint32_t audio_bitrate_kbps;
    uint32_t flags;
    uint32_t reserved[10];
} odm_export_profile;

typedef struct {
    uint32_t schema_version;
    uint32_t adapter;
    uint32_t container;
    uint32_t video_codec;
    uint32_t audio_codec;
    uint32_t source_video_format;
    uint32_t source_audio_format;
    uint32_t output_pixel_format;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t audio_sample_rate;
    uint32_t audio_channels;
    uint32_t crf;
    uint32_t preset;
    uint32_t video_threads;
    uint32_t audio_bitrate_kbps;
    uint32_t flags;
    uint64_t frame_count;
    int64_t samples_per_frame;
    int64_t project_end_sample;
    int64_t output_end_sample;
    uint64_t audio_source_frames;
    uint64_t audio_pad_frames;
    uint64_t source_video_bytes_per_frame;
    uint64_t source_video_total_bytes;
    uint64_t source_audio_total_frames;
    uint64_t source_audio_total_bytes;
    odm_sha256_digest layered_policy_sha256;
    odm_sha256_digest layered_config_sha256;
    odm_sha256_digest export_profile_sha256;
    odm_sha256_digest export_recipe_sha256;
    odm_sha256_digest export_policy_sha256;
} odm_export_recipe;

/* Canonical semantics of Export Engine v1.  The policy binds timeline/audio
 * padding, color/codec adapter contract, shell-free argv generation, receipt
 * identity and transactional publication.  Recipes carry this SHA so the same
 * parameter record can never silently acquire different export semantics. */
odm_status odm_export_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required);
odm_status odm_export_policy_current_sha256(odm_sha256_digest *out_hash);

/* Exact H.264/AAC profile builder. The caller chooses all quality/performance
 * knobs; the engine fills the fixed adapter/container/color/source contract.
 * Transactional: out_profile is unchanged on error. */
odm_status odm_export_profile_init_h264_aac(
    odm_export_profile *out_profile,
    uint32_t crf,
    uint32_t preset,
    uint32_t video_threads,
    uint32_t audio_bitrate_kbps,
    uint32_t flags);

odm_status odm_export_profile_validate(const odm_export_profile *profile);
odm_status odm_export_profile_bytes(const odm_export_profile *profile,
                                    uint8_t *buffer, uint64_t capacity,
                                    uint64_t *out_required);
odm_status odm_export_profile_sha256(const odm_export_profile *profile,
                                     odm_sha256_digest *out_hash);

odm_status odm_export_recipe_build(const odm_export_plan *layered_plan,
                                   const odm_export_profile *profile,
                                   odm_export_recipe *out_recipe);
odm_status odm_export_recipe_bytes(const odm_export_recipe *recipe,
                                   uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required);
odm_status odm_export_recipe_validate(const odm_export_recipe *recipe);

odm_status odm_export_frame_sample(const odm_export_recipe *recipe,
                                   uint64_t frame_index,
                                   int64_t *out_sample);

/* Writes exactly frame_count stereo S32LE frames beginning at start_frame.
 * Frames beyond canonical_pcm_frames are exact digital silence. */
odm_status odm_export_audio_chunk_s32le(
    const odm_export_recipe *recipe,
    const odm_pcm_stereo_q31 *canonical_pcm,
    uint64_t canonical_pcm_frames,
    uint64_t start_frame,
    uint64_t frame_count,
    uint8_t *output,
    uint64_t output_capacity,
    uint64_t *out_required_bytes);

/* Canonical FFmpeg argv template. Tokens are NUL-separated and contain three
 * literal placeholders above. Callers substitute paths and exec argv directly;
 * no shell parsing/quoting is part of the contract. */
odm_status odm_export_ffmpeg_argv_template(
    const odm_export_recipe *recipe,
    char *buffer,
    uint64_t capacity,
    uint64_t *out_required_bytes,
    uint32_t *out_token_count);

/* Binds the three path placeholders into a final argv token buffer. Paths are
 * never shell-quoted because no shell is involved. token_offsets, when non-NULL,
 * receives byte offsets into buffer suitable for constructing char *argv[]. */
odm_status odm_export_ffmpeg_argv_bind(
    const odm_export_recipe *recipe,
    const char *video_rgba8_path,
    const char *audio_s32le_path,
    const char *output_mp4_path,
    char *buffer, uint64_t capacity,
    uint64_t *out_required_bytes, uint32_t *out_token_count,
    uint32_t *token_offsets, uint32_t token_offsets_capacity);


/* -------------------------------------------------------------------------
 * Native Export Session v1
 *
 * The engine owns frame/sample mapping, layered composition, raw stream
 * identity and padded canonical audio. Callers provide only exact-time visual
 * states/core frames and a transactional byte sink. No encoder command logic
 * is required outside odm_export_ffmpeg_argv_template().
 * ------------------------------------------------------------------------- */

#define ODM_EXPORT_RUN_SCHEMA_VERSION UINT32_C(2)
#define ODM_EXPORT_RUN_RECEIPT_BYTES UINT32_C(512)
#define ODM_EXPORT_RUN_AUDIO_CHUNK_FRAMES UINT32_C(1024)

/* Runtime-only execution controls. These flags MUST NOT change Recipe, Receipt
 * or any exported byte. They select non-semantic implementation backends only. */
#define ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH UINT32_C(1)
#define ODM_EXPORT_RUN_FLAG_DIRECT_STAGING UINT32_C(2)
#define ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2 UINT32_C(4)
#define ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4 UINT32_C(8)
#define ODM_EXPORT_RUN_FLAGS_ALLOWED \
    (ODM_EXPORT_RUN_FLAG_FORCE_PORTABLE_HASH | ODM_EXPORT_RUN_FLAG_DIRECT_STAGING | \
     ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_2 | ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4)

/* Runtime planning is deliberately non-semantic.  The plan recommends only
 * execution flags that have byte-parity tests against the portable path.
 * It is never serialized into Recipe/Receipt and callers may override it. */
#define ODM_EXPORT_RUNTIME_PLAN_SCHEMA_VERSION UINT32_C(1)
#define ODM_EXPORT_RUNTIME_MAX_WORKERS UINT32_C(4)
#define ODM_EXPORT_RUNTIME_PARALLEL2_MIN_PIXELS UINT64_C(16384)
#define ODM_EXPORT_RUNTIME_PARALLEL4_MIN_PIXELS UINT64_C(65536)

typedef struct {
    uint32_t schema_version;
    uint32_t worker_budget;
    uint32_t selected_workers;
    uint32_t recommended_flags;
    uint64_t frame_pixels;
    uint64_t frame_bytes;
    uint64_t scratch_bytes;
    uint64_t work_units;
    uint32_t reserved[8];
} odm_export_runtime_plan;

typedef odm_status (*odm_export_state_provider_fn)(
    void *user, uint64_t frame_index, int64_t sample,
    odm_composition_frame_state *out_composition,
    odm_director_frame_state *out_director);

typedef odm_status (*odm_export_core_provider_fn)(
    void *user, uint64_t frame_index, int64_t sample,
    odm_render_surface_frame *out_surface);

/* Campo de particulas simulado para el cuadro pedido.
 *
 * Es un proveedor y no un calculo interno porque la simulacion tiene memoria:
 * la posicion en el cuadro N depende de todos los ticks anteriores, y el
 * exportador puede recorrer los cuadros en cualquier orden. Quien avanza el
 * tiempo es quien conoce el estado; aqui solo se pide el resultado. */
typedef odm_status (*odm_export_particles_provider_fn)(
    void *user, uint64_t frame_index, int64_t sample,
    odm_particles_state *out_state);

typedef odm_status (*odm_export_sink_begin_fn)(void *user,
                                                const odm_export_recipe *recipe);
/* rgba8/pcm_s32le are borrowed immutable views valid only until the callback
 * returns. A sink that needs longer lifetime MUST copy them into its own
 * staging storage before returning OK. */
typedef odm_status (*odm_export_sink_video_fn)(void *user,
                                                uint64_t frame_index,
                                                int64_t sample,
                                                const uint8_t *rgba8,
                                                uint64_t bytes);
typedef odm_status (*odm_export_sink_audio_fn)(void *user,
                                                uint64_t start_frame,
                                                const uint8_t *pcm_s32le,
                                                uint64_t frame_count);
typedef odm_status (*odm_export_sink_commit_fn)(void *user,
                                                 const uint8_t *receipt,
                                                 uint64_t receipt_bytes);
typedef void (*odm_export_sink_abort_fn)(void *user, odm_status reason);

/* Transactional sink contract:
 * - begin() opens staging only; no externally visible final artifact exists yet.
 * - write_video()/write_audio() append to that staging transaction.
 * - commit() is the sole publication point.  If commit() returns non-OK it MUST
 *   leave the transaction abortable; callers must not report an irreversible
 *   publish and then return an error.
 * - abort() discards every staged byte and is called exactly once by the engine
 *   for any post-begin failure, including cancellation and commit failure.
 * The engine publishes receipt_buffer/out_receipt only after commit() succeeds. */
typedef struct {
    void *user;
    odm_export_sink_begin_fn begin;
    odm_export_sink_video_fn write_video;
    odm_export_sink_audio_fn write_audio;
    odm_export_sink_commit_fn commit;
    odm_export_sink_abort_fn abort;
} odm_export_sink;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    const odm_layered_config *config;
    const odm_export_recipe *recipe;
    odm_export_state_provider_fn state_provider;
    void *state_user;
    odm_export_core_provider_fn core_provider;
    void *core_user;
    /* Obligatorio si y solo si la configuracion declara ODM_FIELD_PARTICLE_SIM.
     * Declarar aire simulado sin quien lo avance no se degrada en silencio al
     * campo por semilla: falla explicitamente. */
    odm_export_particles_provider_fn particles_provider;
    void *particles_user;
    const odm_pcm_stereo_q31 *canonical_pcm;
    uint64_t canonical_pcm_frames;
    const odm_job_ticket *job_ticket;
    /* Nivel de calidad de raster (odm_supersample.h). 0 = sin supermuestreo,
     * que es exactamente el comportamiento historico, asi que una peticion
     * puesta a cero sigue significando lo mismo que siempre. El nivel decide
     * fidelidad del raster y nunca interpretacion: el recipe, el numero de
     * fotogramas, sus bytes y su instante de muestra son los mismos. */
    uint32_t quality_tier;
    uint32_t reserved[3];
} odm_export_run_request;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t pixel_format;
    uint32_t audio_sample_rate;
    uint32_t audio_channels;
    uint64_t frame_count;
    int64_t samples_per_frame;
    int64_t project_end_sample;
    int64_t output_end_sample;
    uint64_t video_bytes;
    uint64_t audio_frames;
    uint64_t audio_bytes;
    odm_sha256_digest export_recipe_sha256;
    odm_sha256_digest export_policy_sha256;
    odm_sha256_digest layered_policy_sha256;
    odm_sha256_digest layered_config_sha256;
    odm_sha256_digest canonical_pcm_sha256;
    odm_sha256_digest video_stream_sha256;
    odm_sha256_digest audio_stream_sha256;
    odm_sha256_digest export_run_sha256;
    uint32_t reserved[8];
} odm_export_run_receipt;

odm_status odm_export_run_requirements(
    const odm_export_run_request *request,
    uint64_t *out_frame_bytes,
    uint64_t *out_scratch_bytes,
    uint64_t *out_work_units);

/* Portable, transparent runtime recommendation. worker_budget is 1..4.
 * The function preserves only FORCE_PORTABLE_HASH from request->flags and
 * recomputes staging/row scheduling from admitted frame work.  The returned
 * flags cannot alter canonical output and are intentionally absent from every
 * semantic hash/Receipt. */
odm_status odm_export_runtime_plan_recommend(
    const odm_export_run_request *request,
    uint32_t worker_budget,
    odm_export_runtime_plan *out_plan);

odm_status odm_export_run_receipt_bytes(
    const odm_export_run_receipt *receipt,
    uint8_t *buffer, uint64_t capacity,
    uint64_t *out_required);

odm_status odm_export_run_receipt_validate(
    const uint8_t *record, uint64_t record_bytes,
    const odm_sha256_digest *expected_recipe_sha256,
    odm_export_run_receipt *out_receipt);

odm_status odm_export_run(
    const odm_export_run_request *request,
    const odm_export_sink *sink,
    void *scratch, uint64_t scratch_bytes,
    uint8_t *frame, uint64_t frame_capacity,
    uint8_t *receipt_buffer, uint64_t receipt_capacity,
    uint64_t *out_receipt_required,
    odm_export_run_receipt *out_receipt);

#ifdef __cplusplus
}
#endif
#endif

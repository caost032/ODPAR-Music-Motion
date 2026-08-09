#include "odm_compositor.h"
#include "odm_export.h"
#include "odm_media.h"
#include "odm_media_adapter.h"
#include "odm_visual_scene.h"
#include "odm_music_map.h"
#include "odm_music_reaction.h"
#include "odm_status.h"
#include "odm_visual.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const odm_composition_frame_state *composition;
    const odm_director_frame_state *director;
    uint64_t frame_count;
} state_ctx;

typedef struct {
    uint8_t *rgba;
    uint32_t width;
    uint32_t height;
    uint64_t frame_count;
    uint64_t bytes_per_frame;
    int64_t samples_per_frame;
} core_ctx;

typedef struct {
    FILE *v;
    FILE *a;
    char stage_dir[1024];
    char final_dir[1024];
    char video_path[1024];
    char audio_path[1024];
    char receipt_path[1024];
    int active;
} file_sink;

static uint32_t q31_ratio(uint32_t n, uint32_t d) {
    uint64_t v;
    if (d == 0u || n > d) return 0u;
    v = (uint64_t)(uint32_t)INT32_MAX * (uint64_t)n + (uint64_t)d / 2u;
    return (uint32_t)(v / d);
}

static uint64_t digest_seed64(const odm_sha256_digest *d) {
    uint64_t v = 0u;
    unsigned i;
    for (i = 0u; i < 8u; ++i) v = (v << 8) | d->bytes[i];
    return v ^ UINT64_C(0x0d14014a5eedc0de);
}

static int read_file_all(const char *path, uint8_t **out_bytes, uint64_t *out_size) {
    FILE *f = NULL;
    uint8_t *buf = NULL;
    long sz;
    size_t got;
    if (!path || !out_bytes || !out_size) return 0;
    *out_bytes = NULL;
    *out_size = 0u;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return 0; }
    *out_bytes = buf;
    *out_size = (uint64_t)sz;
    return 1;
}

static int remove_tree(const char *dir) {
    char cmd[1200];
    int n;
    if (!dir) return 0;
    n = snprintf(cmd, sizeof(cmd), "rm -rf -- '%s'", dir);
    if (n < 0 || (size_t)n >= sizeof(cmd)) return 0;
    return system(cmd) == 0;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = NULL, *out = NULL;
    uint8_t buf[65536];
    size_t n;
    in = fopen(src, "rb");
    if (!in) return 0;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return 0; }
    while ((n = fread(buf, 1u, sizeof(buf), in)) > 0u) {
        if (fwrite(buf, 1u, n, out) != n) { fclose(in); fclose(out); return 0; }
    }
    if (ferror(in)) { fclose(in); fclose(out); return 0; }
    if (fclose(in) != 0 || fclose(out) != 0) return 0;
    return 1;
}

static odm_status state_provider(void *user, uint64_t frame_index, int64_t sample,
                                 odm_composition_frame_state *out_comp,
                                 odm_director_frame_state *out_dir) {
    state_ctx *ctx = (state_ctx *)user;
    uint64_t expected_tick;
    if (!ctx || !out_comp || !out_dir || !ctx->composition || !ctx->director ||
        ctx->frame_count == 0u || frame_index >= ctx->frame_count || sample < 0)
        return ODM_STATUS_INVALID_ARGUMENT;
    expected_tick = (uint64_t)sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
    *out_comp = ctx->composition[frame_index];
    *out_dir = ctx->director[frame_index];
    if (out_comp->tick_index != expected_tick || out_dir->tick_index != expected_tick ||
        out_comp->center_sample != expected_tick * (uint64_t)ODM_MUSIC_TICK_SAMPLES)
        return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

static odm_status core_provider(void *user, uint64_t frame_index, int64_t sample,
                                odm_render_surface_frame *out_surface) {
    core_ctx *ctx = (core_ctx *)user;
    uint64_t idx;
    if (!ctx || !out_surface || !ctx->rgba || ctx->frame_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    idx = frame_index % ctx->frame_count;
    memset(out_surface, 0, sizeof(*out_surface));
    out_surface->width = ctx->width;
    out_surface->height = ctx->height;
    out_surface->pixel_format = ODM_RENDER_SURFACE_RGBA8;
    out_surface->primaries = ODM_COLOR_PRIMARIES_BT709;
    out_surface->transfer = ODM_COLOR_TRANSFER_SRGB;
    out_surface->alpha_mode = ODM_ALPHA_STRAIGHT;
    out_surface->start_sample = sample;
    out_surface->end_sample = sample + ctx->samples_per_frame;
    out_surface->pixel_bytes = ctx->bytes_per_frame;
    out_surface->pixels = ctx->rgba + idx * ctx->bytes_per_frame;
    return ODM_STATUS_OK;
}

static odm_status sink_begin(void *user, const odm_export_recipe *recipe) {
    file_sink *s = (file_sink *)user;
    (void)recipe;
    if (!s) return ODM_STATUS_INVALID_ARGUMENT;
    remove_tree(s->stage_dir);
    remove_tree(s->final_dir);
    if (mkdir(s->stage_dir, 0700) != 0) return ODM_STATUS_INTERNAL_ERROR;
    s->v = fopen(s->video_path, "wb");
    if (!s->v) { rmdir(s->stage_dir); return ODM_STATUS_INTERNAL_ERROR; }
    s->a = fopen(s->audio_path, "wb");
    if (!s->a) {
        fclose(s->v); s->v = NULL;
        remove(s->video_path);
        rmdir(s->stage_dir);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    s->active = 1;
    return ODM_STATUS_OK;
}

static odm_status sink_write_video(void *user, uint64_t frame_index, int64_t sample,
                                   const uint8_t *rgba8, uint64_t bytes) {
    file_sink *s = (file_sink *)user;
    (void)frame_index; (void)sample;
    if (!s || !s->v || (!rgba8 && bytes)) return ODM_STATUS_INVALID_ARGUMENT;
    if (fwrite(rgba8, 1u, (size_t)bytes, s->v) != (size_t)bytes) return ODM_STATUS_INTERNAL_ERROR;
    return ODM_STATUS_OK;
}

static odm_status sink_write_audio(void *user, uint64_t start_frame,
                                   const uint8_t *pcm_s32le, uint64_t frame_count) {
    file_sink *s = (file_sink *)user;
    (void)start_frame;
    if (!s || !s->a || (!pcm_s32le && frame_count)) return ODM_STATUS_INVALID_ARGUMENT;
    if (fwrite(pcm_s32le, 8u, (size_t)frame_count, s->a) != (size_t)frame_count) return ODM_STATUS_INTERNAL_ERROR;
    return ODM_STATUS_OK;
}

static odm_status sink_commit(void *user, const uint8_t *receipt, uint64_t receipt_bytes) {
    file_sink *s = (file_sink *)user;
    FILE *rf = NULL;
    if (!s || !s->active) return ODM_STATUS_INVALID_STATE;
    if (s->v && fclose(s->v) != 0) { s->v = NULL; return ODM_STATUS_INTERNAL_ERROR; }
    if (s->a && fclose(s->a) != 0) { s->a = NULL; return ODM_STATUS_INTERNAL_ERROR; }
    s->v = NULL; s->a = NULL;
    rf = fopen(s->receipt_path, "wb");
    if (!rf) return ODM_STATUS_INTERNAL_ERROR;
    if (fwrite(receipt, 1u, (size_t)receipt_bytes, rf) != (size_t)receipt_bytes || fclose(rf) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    if (rename(s->stage_dir, s->final_dir) != 0) return ODM_STATUS_INTERNAL_ERROR;
    s->active = 0;
    return ODM_STATUS_OK;
}

static void sink_abort(void *user, odm_status reason) {
    file_sink *s = (file_sink *)user;
    fprintf(stderr, "sink_abort reason=%d\n", (int)reason);
    if (!s) return;
    if (s->v) fclose(s->v);
    if (s->a) fclose(s->a);
    s->v = NULL; s->a = NULL;
    remove(s->video_path);
    remove(s->audio_path);
    remove(s->receipt_path);
    rmdir(s->stage_dir);
    s->active = 0;
}

static int set_route(odm_layered_config *c, uint32_t target,
                     uint32_t a, uint32_t b, uint32_t combine) {
    uint32_t route = 0u;
    if (odm_reaction_route_make(a, b, combine, &route) != ODM_STATUS_OK) return 0;
    if (odm_layered_config_set_reaction_route(c, target, route) != ODM_STATUS_OK) return 0;
    return 1;
}

static int build_config(odm_layered_config *out_config, uint32_t width, uint32_t height,
                        int32_t fps, uint64_t seed) {
    odm_layered_config base, styled;
    odm_layered_style_profile style;
    if (odm_layered_config_init_default(&base, ODM_CANVAS_ASPECT_SQUARE_1_1,
                                        width, height, fps) != ODM_STATUS_OK) return 0;
    if (odm_layered_style_init(&style, ODM_LAYERED_STYLE_PRESET_DEEP_GRID) != ODM_STATUS_OK) return 0;
    if (odm_layered_style_apply(&base, &style, &styled) != ODM_STATUS_OK) return 0;

    styled.background.style = ODM_BACKGROUND_PERSPECTIVE_GRID;
    styled.background.zoom_reactivity_q31 = q31_ratio(1u, 8u);
    styled.background.warp_reactivity_q31 = q31_ratio(1u, 12u);
    styled.background.depth_reactivity_q31 = q31_ratio(1u, 7u);
    styled.background.opacity_q31 = q31_ratio(1u, 8u);

    styled.core.shape = ODM_CORE_SHAPE_CIRCLE;
    styled.core.fit = ODM_CORE_FIT_COVER;
    styled.core.width_q31 = q31_ratio(7u, 20u);
    styled.core.height_q31 = q31_ratio(7u, 20u);
    styled.core.corner_radius_q31 = 0u;
    styled.core.border_q16 = 1u << 16;
    styled.core.feather_q16 = 1u << 16;
    styled.core.scale_reactivity_q31 = q31_ratio(1u, 3u);

    styled.field.flags = ODM_FIELD_RADIAL_BARS | ODM_FIELD_PARTICLES | ODM_FIELD_ORBIT_RING;
    styled.field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;
    styled.field.particle_count = 28u;
    styled.field.ring_gap_q16 = 10u << 16;
    styled.field.bar_min_q16 = 2u << 16;
    styled.field.bar_max_q16 = 104u << 16;
    styled.field.bar_width_q16 = 1u << 16;
    styled.field.particle_radius_q16 = 1u << 16;
    styled.field.field_opacity_q31 = (uint32_t)INT32_MAX;
    styled.field.seed = seed;

    styled.hud.flags = ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE;
    styled.hud.margin_q16 = 4u << 16;
    styled.hud.progress_height_q16 = 3u << 16;
    styled.hud.progress_width_q31 = q31_ratio(4u, 5u);
    styled.hud.text_scale_q16 = 3u << 16;
    styled.hud.line_gap_q16 = 2u << 16;
    styled.hud.metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_CENTER;
    styled.hud.progress_style = ODM_HUD_PROGRESS_CAPSULE;
    styled.hud.time_mode = ODM_HUD_TIME_ELAPSED;

    if (!set_route(&styled, ODM_REACTION_TARGET_BACKGROUND_ZOOM,
                   ODM_REACTION_SOURCE_GRID, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&styled, ODM_REACTION_TARGET_BACKGROUND_WARP,
                   ODM_REACTION_SOURCE_FRACTURE, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&styled, ODM_REACTION_TARGET_BACKGROUND_DEPTH,
                   ODM_REACTION_SOURCE_GRID, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&styled, ODM_REACTION_TARGET_CORE_SCALE,
                   ODM_REACTION_SOURCE_CORE_BREATH, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&styled, ODM_REACTION_TARGET_FIELD_GAIN,
                   ODM_REACTION_SOURCE_RADIAL_GAIN, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;
    if (!set_route(&styled, ODM_REACTION_TARGET_PARTICLE_DENSITY,
                   ODM_REACTION_SOURCE_PARTICLES, ODM_REACTION_SOURCE_NONE,
                   ODM_REACTION_COMBINE_A_ONLY)) return 0;

    if (odm_layered_config_validate(&styled) != ODM_STATUS_OK) return 0;
    *out_config = styled;
    return 1;
}

static int run_ffmpeg_bound(const odm_export_recipe *recipe,
                            const char *video_path,
                            const char *audio_path,
                            const char *output_path,
                            const char *argv_dump_path) {
    char argb[ODM_EXPORT_ARGV_MAX_BYTES];
    uint32_t offs[ODM_EXPORT_ARGV_MAX_TOKENS];
    uint64_t need = 0u;
    uint32_t count = 0u;
    char *argvv[ODM_EXPORT_ARGV_MAX_TOKENS + 1u];
    FILE *dump = NULL;
    pid_t pid;
    int status;
    uint32_t i;
    if (odm_export_ffmpeg_argv_bind(recipe, video_path, audio_path, output_path,
                                    argb, sizeof(argb), &need, &count,
                                    offs, ODM_EXPORT_ARGV_MAX_TOKENS) != ODM_STATUS_OK) {
        return 0;
    }
    if (argv_dump_path) {
        dump = fopen(argv_dump_path, "wb");
        if (dump) {
            for (i = 0u; i < count; ++i) {
                fputs(argb + offs[i], dump);
                fputc('\n', dump);
            }
            fclose(dump);
        }
    }
    for (i = 0u; i < count; ++i) argvv[i] = argb + offs[i];
    argvv[count] = NULL;
    pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        execvp(argvv[0], argvv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(int argc, char **argv) {
    const char *wav_path, *core_raw_path, *out_mp4, *work_dir;
    uint32_t out_size = 540u;
    int32_t fps = 60;
    int dynamics_enabled = 1;   /* Visual Dynamics activo por defecto */
    int positional = 0;
    uint32_t source_w = 256u, source_h = 256u;
    odm_pcm_stereo_q31 *pcm = NULL;
    uint64_t pcm_frames_required = 0u;
    odm_media_facts audio_facts;
    int32_t *window_q31 = NULL;
    odm_music_complex_q31 *twiddles = NULL;
    odm_music_analysis_plan analysis_plan;
    odm_music_analysis_state analysis_state;
    odm_music_analysis_scratch analysis_scratch;
    uint64_t tick_count = 0u;
    odm_music_analysis_tick *ticks = NULL;
    odm_music_reaction_tick *reaction_ticks = NULL;
    odm_music_reaction_profile reaction_profile;
    odm_music_reaction_state reaction_state;
    odm_music_reaction_frame *reaction_frames = NULL;
    odm_music_reaction_frame *refined_frames = NULL;
    odm_music_reaction_event *events = NULL;
    uint64_t event_count = 0u;
    uint32_t macro_gate_q31 = 0u;
    odm_music_reaction_event_projection *event_projections = NULL;
    odm_director_state dir_state;
    odm_composition_frame_state *comp_frames = NULL;
    odm_director_frame_state *dir_frames = NULL;
    odm_composition_frame_state *presentation_comp = NULL;
    odm_director_frame_state *presentation_dir = NULL;
    odm_music_reaction_projection *projections = NULL;
    uint64_t *presentation_samples = NULL;
    state_ctx sctx;
    core_ctx cctx;
    uint8_t *core_rgba = NULL;
    uint64_t core_raw_size = 0u;
    odm_layered_config config;
    odm_export_plan export_plan;
    odm_export_profile export_profile;
    odm_export_recipe recipe;
    odm_export_run_request run_request;
    odm_export_sink sink;
    file_sink fsink;
    uint64_t frame_bytes = 0u, scratch_bytes = 0u, work_units = 0u;
    uint8_t *frame = NULL, *scratch = NULL;
    uint8_t receipt_buf[ODM_EXPORT_RUN_RECEIPT_BYTES];
    uint64_t receipt_required = 0u;
    odm_export_run_receipt receipt;
    uint64_t i;
    uint64_t seed = 0u;
    uint64_t preview_start_sec = 0u, preview_duration_sec = 0u;
    uint64_t preview_start_frame = 0u, preview_pcm_frames = 0u;
    const odm_pcm_stereo_q31 *preview_pcm = NULL;
    char final_dir[1024], receipt_copy[1024], argv_dump[1024], final_video[1024], final_audio[1024];

    if (argc < 5) {
        fprintf(stderr,
            "uso: %s <audio> <imagen|core.rgba> <salida.mp4> <dir_trabajo>\n"
            "        [inicio_seg duracion_seg] [--no-dynamics] [--fps N] [--size N]\n"
            "\n"
            "  audio  : wav, mp3, m4a/aac, flac, ogg, opus, aiff, mp4...\n"
            "  imagen : png, jpeg, webp... o un volcado RGBA8 crudo\n"
            "  --no-dynamics : control A/B, desactiva Visual Dynamics\n",
            argv[0]);
        return 2;
    }
    {
        int a;
        for (a = 5; a < argc; ++a) {
            if (strcmp(argv[a], "--no-dynamics") == 0) { dynamics_enabled = 0; }
            else if (strcmp(argv[a], "--fps") == 0 && a + 1 < argc) { fps = (int32_t)atoi(argv[++a]); }
            else if (strcmp(argv[a], "--size") == 0 && a + 1 < argc) { out_size = (uint32_t)atoi(argv[++a]); }
            else if (positional == 0) { preview_start_sec = strtoull(argv[a], NULL, 10); positional = 1; }
            else if (positional == 1) { preview_duration_sec = strtoull(argv[a], NULL, 10); positional = 2; }
        }
        if (positional == 2 && preview_duration_sec == 0u) {
            fprintf(stderr, "duration must be positive\n"); return 2;
        }
    }
    wav_path = argv[1];
    core_raw_path = argv[2];
    out_mp4 = argv[3];
    work_dir = argv[4];


    memset(&audio_facts, 0, sizeof(audio_facts));
    memset(&analysis_plan, 0, sizeof(analysis_plan));
    memset(&analysis_state, 0, sizeof(analysis_state));
    memset(&analysis_scratch, 0, sizeof(analysis_scratch));
    memset(&reaction_profile, 0, sizeof(reaction_profile));
    memset(&reaction_state, 0, sizeof(reaction_state));
    memset(&dir_state, 0, sizeof(dir_state));
    memset(&sctx, 0, sizeof(sctx));
    memset(&cctx, 0, sizeof(cctx));
    memset(&config, 0, sizeof(config));
    memset(&export_plan, 0, sizeof(export_plan));
    memset(&export_profile, 0, sizeof(export_profile));
    memset(&recipe, 0, sizeof(recipe));
    memset(&run_request, 0, sizeof(run_request));
    memset(&sink, 0, sizeof(sink));
    memset(&fsink, 0, sizeof(fsink));
    memset(&receipt, 0, sizeof(receipt));

    /* Cualquier contenedor soportado entra por la frontera de canonicalizacion.
     * WAV va por el camino nativo; el resto pasa por el adaptador y vuelve a
     * ser verificado por el mismo decodificador nativo. */
    {
        odm_media_adapter_provenance audio_prov;
        odm_status st = odm_media_load_audio_q31(wav_path, NULL, 0u,
                                                 &pcm_frames_required, &audio_facts,
                                                 &audio_prov);
        if (st != ODM_STATUS_BUFFER_TOO_SMALL && st != ODM_STATUS_OK) {
            fprintf(stderr, "audio sizing failed st=%d\n", (int)st); return 4;
        }
        pcm = (odm_pcm_stereo_q31 *)malloc((size_t)pcm_frames_required * sizeof(*pcm));
        if (!pcm) { fprintf(stderr, "oom pcm\n"); return 5; }
        st = odm_media_load_audio_q31(wav_path, pcm, pcm_frames_required,
                                      &pcm_frames_required, &audio_facts, &audio_prov);
        if (st != ODM_STATUS_OK) {
            fprintf(stderr, "audio decode failed st=%d\n", (int)st); return 6;
        }
        if (audio_prov.available && audio_prov.output_bytes) {
            fprintf(stderr, "media adapter: %s (%llu -> %llu bytes)\n",
                    audio_prov.identity,
                    (unsigned long long)audio_prov.input_bytes,
                    (unsigned long long)audio_prov.output_bytes);
        }
        fprintf(stderr, "audio: %llu frames @48kHz (%.2f s)\n",
                (unsigned long long)pcm_frames_required,
                (double)pcm_frames_required / 48000.0);
    }

    window_q31 = (int32_t *)malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES * sizeof(*window_q31));
    twiddles = (odm_music_complex_q31 *)malloc((size_t)ODM_MUSIC_FFT_TWIDDLES * sizeof(*twiddles));
    if (!window_q31 || !twiddles) { fprintf(stderr, "oom analysis plan\n"); return 7; }
    if (odm_music_analysis_plan_build(window_q31, ODM_MUSIC_WINDOW_SAMPLES,
                                      twiddles, ODM_MUSIC_FFT_TWIDDLES,
                                      &analysis_plan) != ODM_STATUS_OK) {
        fprintf(stderr, "analysis plan build failed\n"); return 8;
    }
    if (odm_music_tick_count(pcm_frames_required, &tick_count) != ODM_STATUS_OK || tick_count == 0u) {
        fprintf(stderr, "tick count failed\n"); return 9;
    }
    ticks = (odm_music_analysis_tick *)calloc((size_t)tick_count, sizeof(*ticks));
    reaction_ticks = (odm_music_reaction_tick *)calloc((size_t)tick_count, sizeof(*reaction_ticks));
    reaction_frames = (odm_music_reaction_frame *)calloc((size_t)tick_count, sizeof(*reaction_frames));
    refined_frames = (odm_music_reaction_frame *)calloc((size_t)tick_count, sizeof(*refined_frames));
    comp_frames = (odm_composition_frame_state *)calloc((size_t)tick_count, sizeof(*comp_frames));
    dir_frames = (odm_director_frame_state *)calloc((size_t)tick_count, sizeof(*dir_frames));
    if (!ticks || !reaction_ticks || !reaction_frames || !refined_frames || !comp_frames || !dir_frames) {
        fprintf(stderr, "oom tick arrays\n"); return 10;
    }
    if (odm_music_analysis_state_init(&analysis_state) != ODM_STATUS_OK) { fprintf(stderr, "analysis state init failed\n"); return 11; }
    for (i = 0u; i < tick_count; ++i) {
        if (odm_music_analyze_tick(&analysis_plan, window_q31, twiddles,
                                   pcm, pcm_frames_required, i,
                                   &analysis_state, &analysis_scratch, &ticks[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "music analyze failed at tick %llu\n", (unsigned long long)i); return 12;
        }
        if (odm_music_reaction_extract_tick(&ticks[i], &analysis_scratch, &reaction_ticks[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "reaction extract failed at tick %llu\n", (unsigned long long)i); return 13;
        }
    }
    if (odm_music_reaction_profile_build(reaction_ticks, tick_count, &reaction_profile) != ODM_STATUS_OK) {
        fprintf(stderr, "reaction profile failed\n"); return 14;
    }
    seed = digest_seed64(&audio_facts.decoded_sha256);
    if (odm_music_reaction_state_init(&reaction_state) != ODM_STATUS_OK) { fprintf(stderr, "reaction init failed\n"); return 15; }
    for (i = 0u; i < tick_count; ++i) {
        if (odm_music_reaction_resolve_tick(&reaction_state, &reaction_profile,
                                            &reaction_ticks[i], &reaction_frames[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "reaction resolve failed at tick %llu\n", (unsigned long long)i); return 17;
        }
    }
    if (odm_music_reaction_refine_events_offline(reaction_frames, tick_count,
                                                 refined_frames, tick_count) != ODM_STATUS_OK) {
        fprintf(stderr, "offline reaction refinement failed\n"); return 17;
    }
    if (odm_music_reaction_build_events_offline(reaction_frames, tick_count, NULL, 0u,
                                                &event_count, &macro_gate_q31) != ODM_STATUS_OK) {
        fprintf(stderr, "offline event sizing failed\n"); return 17;
    }
    if (event_count != 0u) {
        events = (odm_music_reaction_event *)calloc((size_t)event_count, sizeof(*events));
        if (!events) { fprintf(stderr, "oom event stream\n"); return 17; }
        if (odm_music_reaction_build_events_offline(reaction_frames, tick_count, events, event_count,
                                                    &event_count, &macro_gate_q31) != ODM_STATUS_OK) {
            fprintf(stderr, "offline event build failed\n"); return 17;
        }
    }
    if (odm_director_init(&dir_state, seed ^ UINT64_C(0xa5a55eed01401401)) != ODM_STATUS_OK) { fprintf(stderr, "director init failed\n"); return 16; }
    for (i = 0u; i < tick_count; ++i) {
        if (odm_composition_resolve_music_reaction(&ticks[i], &refined_frames[i],
                                                   &comp_frames[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "music-reaction composition failed at tick %llu\n", (unsigned long long)i); return 18;
        }
        if (odm_director_resolve_tick(&dir_state, &comp_frames[i], &dir_frames[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "director resolve failed at tick %llu\n", (unsigned long long)i); return 19;
        }
    }

    if (preview_start_sec > UINT64_MAX / ODM_MUSIC_SAMPLE_RATE) return 20;
    preview_start_frame = preview_start_sec * ODM_MUSIC_SAMPLE_RATE;
    if (preview_start_frame >= pcm_frames_required) { fprintf(stderr, "preview start beyond audio\n"); return 20; }
    if (preview_duration_sec == 0u) preview_pcm_frames = pcm_frames_required - preview_start_frame;
    else {
        uint64_t requested = preview_duration_sec > UINT64_MAX / ODM_MUSIC_SAMPLE_RATE
                                 ? UINT64_MAX
                                 : preview_duration_sec * ODM_MUSIC_SAMPLE_RATE;
        uint64_t available = pcm_frames_required - preview_start_frame;
        preview_pcm_frames = requested < available ? requested : available;
    }
    preview_pcm = pcm + preview_start_frame;

    /* El nucleo acepta una imagen real (PNG/JPEG/WebP/...) o un volcado .rgba
     * crudo. La imagen se canonicaliza a RGBA8 por la misma frontera. */
    {
        odm_media_facts core_facts;
        odm_media_adapter_provenance core_prov;
        uint64_t need = 0u;
        odm_status st;
        memset(&core_facts, 0, sizeof(core_facts));
        st = odm_media_load_image_rgba8(core_raw_path, NULL, 0u, &need, &core_facts, &core_prov);
        if (st == ODM_STATUS_BUFFER_TOO_SMALL || st == ODM_STATUS_OK) {
            core_rgba = (uint8_t *)malloc((size_t)need);
            if (!core_rgba) { fprintf(stderr, "oom core image\n"); return 18; }
            st = odm_media_load_image_rgba8(core_raw_path, core_rgba, need, &need,
                                            &core_facts, &core_prov);
            if (st != ODM_STATUS_OK) { fprintf(stderr, "core image decode failed st=%d\n", (int)st); return 18; }
            source_w = core_facts.width;
            source_h = core_facts.height;
            core_raw_size = need;
            fprintf(stderr, "core: %ux%u imagen\n", source_w, source_h);
        } else if (!read_file_all(core_raw_path, &core_rgba, &core_raw_size)) {
            fprintf(stderr, "failed to read core input\n"); return 18;
        }
    }
    cctx.width = source_w;
    cctx.height = source_h;
    cctx.bytes_per_frame = (uint64_t)source_w * source_h * 4u;
    if (cctx.bytes_per_frame == 0u || core_raw_size % cctx.bytes_per_frame != 0u) {
        fprintf(stderr, "invalid core raw size\n"); return 19;
    }
    cctx.frame_count = core_raw_size / cctx.bytes_per_frame;
    cctx.rgba = core_rgba;
    cctx.samples_per_frame = (int64_t)ODM_MUSIC_SAMPLE_RATE / (int64_t)fps;

    if (!build_config(&config, out_size, out_size, fps, seed ^ UINT64_C(0x55aa55aa11223344))) {
        fprintf(stderr, "build config failed\n"); return 20;
    }
    if (odm_export_plan_build(&config, (int64_t)preview_pcm_frames, preview_pcm_frames,
                              ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                              &export_plan) != ODM_STATUS_OK) {
        fprintf(stderr, "export plan failed\n"); return 21;
    }
    if (odm_export_profile_init_h264_aac(&export_profile, 28u,
                                         ODM_EXPORT_PRESET_ULTRAFAST,
                                         1u, 96u,
                                         ODM_EXPORT_FLAG_FASTSTART) != ODM_STATUS_OK) {
        fprintf(stderr, "export profile init failed\n"); return 22;
    }
    if (odm_export_recipe_build(&export_plan, &export_profile, &recipe) != ODM_STATUS_OK) {
        fprintf(stderr, "export recipe build failed\n"); return 23;
    }

    /* Presentation-time authority. The canonical music timeline remains 100 Hz,
     * but every raster frame receives all instantaneous evidence in the exact
     * causal interval since the previous raster. This prevents 24/30/60 fps
     * export from silently dropping 10 ms attacks between video frames. */
    presentation_samples = (uint64_t *)malloc((size_t)recipe.frame_count * sizeof(*presentation_samples));
    projections = (odm_music_reaction_projection *)calloc((size_t)recipe.frame_count, sizeof(*projections));
    event_projections = (odm_music_reaction_event_projection *)calloc((size_t)recipe.frame_count, sizeof(*event_projections));
    presentation_comp = (odm_composition_frame_state *)calloc((size_t)recipe.frame_count, sizeof(*presentation_comp));
    presentation_dir = (odm_director_frame_state *)calloc((size_t)recipe.frame_count, sizeof(*presentation_dir));
    if (!presentation_samples || !projections || !event_projections || !presentation_comp || !presentation_dir) {
        fprintf(stderr, "oom presentation timeline\n"); return 23;
    }
    for (i = 0u; i < recipe.frame_count; ++i) {
        int64_t local_sample = 0;
        if (odm_export_frame_sample(&recipe, i, &local_sample) != ODM_STATUS_OK || local_sample < 0 ||
            preview_start_frame > UINT64_MAX - (uint64_t)local_sample) {
            fprintf(stderr, "presentation sample build failed\n"); return 23;
        }
        presentation_samples[i] = preview_start_frame + (uint64_t)local_sample;
    }
    if (odm_music_reaction_project_timeline(refined_frames, tick_count,
                                            presentation_samples, recipe.frame_count,
                                            projections, recipe.frame_count) != ODM_STATUS_OK) {
        fprintf(stderr, "presentation projection failed\n"); return 23;
    }
    if (odm_music_reaction_project_events_offline(events, event_count,
                                                   presentation_samples, recipe.frame_count,
                                                   event_projections, recipe.frame_count) != ODM_STATUS_OK) {
        fprintf(stderr, "sample-domain event projection failed\n"); return 23;
    }
    for (i = 0u; i < recipe.frame_count; ++i) {
        uint64_t absolute_tick = projections[i].source_tick_index;
        uint64_t local_tick;
        int64_t local_sample = 0;
        if (absolute_tick >= tick_count ||
            odm_export_frame_sample(&recipe, i, &local_sample) != ODM_STATUS_OK || local_sample < 0) {
            fprintf(stderr, "presentation source mapping failed\n"); return 23;
        }
        if (odm_composition_resolve_music_reaction_sample_presentation(
                &ticks[absolute_tick], &projections[i], &event_projections[i],
                &presentation_comp[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "sample-accurate projected composition failed at frame %llu sample=%llu prev=%lld cap=%u flags=0x%x sal=%u pulse=%u dom=%llu eff=%llu owner=%llu own_eff=%llu own_sal=%u strength=%u fam=%u/%u\n",
                    (unsigned long long)i,
                    (unsigned long long)event_projections[i].presentation_sample,
                    (long long)event_projections[i].previous_presentation_sample,
                    event_projections[i].captured_event_count, event_projections[i].event_flags,
                    event_projections[i].event_salience_q31, event_projections[i].event_pulse_q31,
                    (unsigned long long)event_projections[i].dominant_event_index,
                    (unsigned long long)event_projections[i].dominant_effective_sample,
                    (unsigned long long)event_projections[i].pulse_owner_event_index,
                    (unsigned long long)event_projections[i].pulse_owner_effective_sample,
                    event_projections[i].pulse_owner_salience_q31,
                    event_projections[i].strength_basis_q31,
                    event_projections[i].strongest_family, event_projections[i].second_family);
            return 23;
        }
        presentation_dir[i] = dir_frames[absolute_tick];
        local_tick = (uint64_t)local_sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
        presentation_comp[i].tick_index = local_tick;
        presentation_comp[i].center_sample = local_tick * (uint64_t)ODM_MUSIC_TICK_SAMPLES;
        presentation_dir[i].tick_index = local_tick;
    }

    /* Volcado de la secuencia de composicion cruda: es la entrada que consume
     * tools/flicker_metrics para comparar objetivamente con y sin dinamica. */
    {
        char dump_path[1200];
        if (snprintf(dump_path, sizeof(dump_path), "%s/composition_frames.bin", work_dir) > 0) {
            FILE *dh = fopen(dump_path, "wb");
            if (dh) {
                (void)fwrite(presentation_comp, sizeof(*presentation_comp),
                             (size_t)recipe.frame_count, dh);
                fclose(dh);
            }
        }
    }

    /* Visual Dynamics. Sin esta pasada, un valor visual existe exactamente
     * mientras su evidencia esta alta. Con ella, la evidencia dispara una
     * respuesta que tiene ataque, cuerpo y caida propios en dominio de muestras,
     * asi que un golpe sigue viendose despues de que su evidencia desaparezca y
     * la rejilla no puede latir a la velocidad del halo.
     *
     * La coordenada que se usa es la muestra exacta de presentacion del
     * fotograma, no su indice: por eso cambiar los FPS reencuadra la misma
     * envolvente en vez de producir otra reaccion. */
    if (dynamics_enabled) {
        odm_visual_scene scene;
        if (odm_visual_scene_init(&scene, 0u) != ODM_STATUS_OK) {
            fprintf(stderr, "visual scene init failed\n"); return 24;
        }
        for (i = 0u; i < recipe.frame_count; ++i) {
            int64_t sample = 0;
            if (odm_export_frame_sample(&recipe, i, &sample) != ODM_STATUS_OK || sample < 0) {
                fprintf(stderr, "frame sample mapping failed\n"); return 24;
            }
            if (odm_visual_scene_apply(&scene, &presentation_comp[i], (uint64_t)sample,
                                       &presentation_comp[i]) != ODM_STATUS_OK) {
                fprintf(stderr, "visual scene apply failed at frame %llu\n",
                        (unsigned long long)i);
                return 24;
            }
        }
        fprintf(stderr, "visual dynamics: aplicado a %llu fotogramas\n",
                (unsigned long long)recipe.frame_count);
    } else {
        fprintf(stderr, "visual dynamics: DESACTIVADO (control A/B)\n");
    }
    sctx.composition = presentation_comp;
    sctx.director = presentation_dir;
    sctx.frame_count = recipe.frame_count;

    if (snprintf(final_dir, sizeof(final_dir), "%s/export.final", work_dir) < 0 ||
        snprintf(receipt_copy, sizeof(receipt_copy), "%s/export.receipt.bin", work_dir) < 0 ||
        snprintf(argv_dump, sizeof(argv_dump), "%s/ffmpeg_argv.txt", work_dir) < 0 ||
        snprintf(final_video, sizeof(final_video), "%s/video.rgba", final_dir) < 0 ||
        snprintf(final_audio, sizeof(final_audio), "%s/audio.s32le", final_dir) < 0 ||
        snprintf(fsink.stage_dir, sizeof(fsink.stage_dir), "%s/export.part", work_dir) < 0 ||
        snprintf(fsink.final_dir, sizeof(fsink.final_dir), "%s/export.final", work_dir) < 0 ||
        snprintf(fsink.video_path, sizeof(fsink.video_path), "%s/video.rgba", fsink.stage_dir) < 0 ||
        snprintf(fsink.audio_path, sizeof(fsink.audio_path), "%s/audio.s32le", fsink.stage_dir) < 0 ||
        snprintf(fsink.receipt_path, sizeof(fsink.receipt_path), "%s/receipt.bin", fsink.stage_dir) < 0) {
        fprintf(stderr, "path build failed\n"); return 24;
    }

    run_request.schema_version = ODM_EXPORT_RUN_SCHEMA_VERSION;
    run_request.flags = ODM_EXPORT_RUN_FLAG_DIRECT_STAGING | ODM_EXPORT_RUN_FLAG_ROW_PARALLEL_4;
    run_request.config = &config;
    run_request.recipe = &recipe;
    run_request.state_provider = state_provider;
    run_request.state_user = &sctx;
    run_request.core_provider = core_provider;
    run_request.core_user = &cctx;
    run_request.canonical_pcm = preview_pcm;
    run_request.canonical_pcm_frames = preview_pcm_frames;
    run_request.job_ticket = NULL;

    if (odm_export_run_requirements(&run_request, &frame_bytes, &scratch_bytes, &work_units) != ODM_STATUS_OK) {
        fprintf(stderr, "run requirements failed\n"); return 25;
    }
    frame = (uint8_t *)malloc((size_t)frame_bytes);
    scratch = (uint8_t *)malloc((size_t)scratch_bytes);
    if (!frame || !scratch) { fprintf(stderr, "oom render scratch\n"); return 26; }

    sink.user = &fsink;
    sink.begin = sink_begin;
    sink.write_video = sink_write_video;
    sink.write_audio = sink_write_audio;
    sink.commit = sink_commit;
    sink.abort = sink_abort;

    {
        odm_status run_st = odm_export_run(&run_request, &sink,
                       scratch, scratch_bytes,
                       frame, frame_bytes,
                       receipt_buf, sizeof(receipt_buf),
                       &receipt_required, &receipt);
        if (run_st != ODM_STATUS_OK) {
            fprintf(stderr, "export run failed status=%d\n", (int)run_st); return 27;
        }
    }
    if (!run_ffmpeg_bound(&recipe, final_video, final_audio, out_mp4, argv_dump)) {
        fprintf(stderr, "ffmpeg encode failed\n"); return 28;
    }
    {
        char receipt_src[1024];
        if (snprintf(receipt_src, sizeof(receipt_src), "%s/receipt.bin", final_dir) > 0) {
            copy_file(receipt_src, receipt_copy);
        }
    }
    remove_tree(final_dir);

    fprintf(stdout,
            "ok\nframes=%llu\naudio_frames=%llu\nticks=%llu\nevents=%llu\nmacro_gate_q31=%u\nstart_sec=%llu\nduration_sec=%.3f\nwork_units=%llu\noutput=%s\n",
            (unsigned long long)recipe.frame_count,
            (unsigned long long)recipe.audio_source_frames,
            (unsigned long long)tick_count,
            (unsigned long long)event_count,
            macro_gate_q31,
            (unsigned long long)preview_start_sec,
            (double)preview_pcm_frames / (double)ODM_MUSIC_SAMPLE_RATE,
            (unsigned long long)work_units,
            out_mp4);

    free(frame);
    free(scratch);
    free(core_rgba);
    free(presentation_samples);
    free(projections);
    free(event_projections);
    free(presentation_dir);
    free(presentation_comp);
    free(dir_frames);
    free(comp_frames);
    free(refined_frames);
    free(events);
    free(reaction_frames);
    free(reaction_ticks);
    free(ticks);
    free(twiddles);
    free(window_q31);
    free(pcm);
    return 0;
}

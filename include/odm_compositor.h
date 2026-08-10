#ifndef ODM_COMPOSITOR_H
#define ODM_COMPOSITOR_H

#include "odm_hash.h"
#include "odm_job.h"
#include "odm_renderer.h"
#include "odm_status.h"
#include "odm_visual.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_LAYERED_SCHEMA_VERSION UINT32_C(1)
#define ODM_LAYERED_POLICY_VERSION UINT32_C(13)
#define ODM_LAYERED_POLICY_BYTES UINT32_C(1536)
#define ODM_LAYERED_CONFIG_BYTES UINT32_C(1024)

#define ODM_CANVAS_ASPECT_CUSTOM          UINT32_C(0)
#define ODM_CANVAS_ASPECT_HORIZONTAL_16_9 UINT32_C(1)
#define ODM_CANVAS_ASPECT_SQUARE_1_1      UINT32_C(2)
#define ODM_CANVAS_ASPECT_VERTICAL_9_16   UINT32_C(3)
#define ODM_CANVAS_ASPECT_HORIZONTAL_4_3  UINT32_C(4)
#define ODM_CANVAS_ASPECT_VERTICAL_4_5    UINT32_C(5)

#define ODM_BACKGROUND_NONE       UINT32_C(0)
#define ODM_BACKGROUND_SOLID      UINT32_C(1)
#define ODM_BACKGROUND_GRID             UINT32_C(2)
#define ODM_BACKGROUND_PERSPECTIVE_GRID UINT32_C(3)
/* Campo de profundidad: gradiente radial con difuminado ordenado. Existe porque
 * un fondo plano no es un fondo, es la ausencia de uno, y porque un degradado
 * oscuro sin difuminar produce bandas visibles a 8 bits. */
#define ODM_BACKGROUND_DEPTH_FIELD      UINT32_C(4)
/* Horizonte: degradado vertical con una linea nitida a la altura del centro.
 * Da suelo y cielo, que es lo que hace que una composicion centrada deje de
 * flotar en el vacio. */
#define ODM_BACKGROUND_HORIZON          UINT32_C(5)
/* Anillos concentricos atenuados hacia el borde. */
#define ODM_BACKGROUND_CONCENTRIC       UINT32_C(6)
/* Matriz de puntos: estructura sin el peso visual de una rejilla de lineas. */
#define ODM_BACKGROUND_DOT_MATRIX       UINT32_C(7)
/* Degradado lineal diagonal, difuminado con Bayer para no producir bandas. */
#define ODM_BACKGROUND_GRADIENT         UINT32_C(8)
#define ODM_BACKGROUND_STYLE_MAX        ODM_BACKGROUND_GRADIENT

#define ODM_CORE_SHAPE_CIRCLE       UINT32_C(1)
#define ODM_CORE_SHAPE_SQUARE       UINT32_C(2)
#define ODM_CORE_SHAPE_ROUNDED_RECT UINT32_C(3)
#define ODM_CORE_FIT_COVER          UINT32_C(1)
#define ODM_CORE_FIT_CONTAIN        UINT32_C(2)
#define ODM_CORE_FIT_STRETCH        UINT32_C(3)

#define ODM_FIELD_RADIAL_BARS UINT32_C(1)
#define ODM_FIELD_PARTICLES   UINT32_C(2)
#define ODM_FIELD_ORBIT_RING  UINT32_C(4)

#define ODM_HUD_PROGRESS_BAR UINT32_C(1)
#define ODM_HUD_TIME_CODE    UINT32_C(2)
#define ODM_HUD_TITLE        UINT32_C(4)
#define ODM_HUD_ARTIST       UINT32_C(8)

#define ODM_HUD_ANCHOR_TOP_LEFT      UINT32_C(0)
#define ODM_HUD_ANCHOR_TOP_CENTER    UINT32_C(1)
#define ODM_HUD_ANCHOR_TOP_RIGHT     UINT32_C(2)
#define ODM_HUD_ANCHOR_BOTTOM_LEFT   UINT32_C(3)
#define ODM_HUD_ANCHOR_BOTTOM_CENTER UINT32_C(4)
#define ODM_HUD_ANCHOR_BOTTOM_RIGHT  UINT32_C(5)

#define ODM_HUD_PROGRESS_RECT     UINT32_C(0)
#define ODM_HUD_PROGRESS_CAPSULE  UINT32_C(1)
#define ODM_HUD_PROGRESS_HAIRLINE UINT32_C(2)

#define ODM_HUD_TIME_ELAPSED_TOTAL UINT32_C(0)
#define ODM_HUD_TIME_ELAPSED       UINT32_C(1)
#define ODM_HUD_TIME_REMAINING     UINT32_C(2)

#define ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL UINT32_C(1)
#define ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE       UINT32_C(2)

#define ODM_LAYERED_LAYER_BACKGROUND UINT32_C(1)
#define ODM_LAYERED_LAYER_CORE       UINT32_C(2)
#define ODM_LAYERED_LAYER_FIELD      UINT32_C(3)
#define ODM_LAYERED_LAYER_HUD        UINT32_C(4)
#define ODM_LAYERED_LAYER_MASK_BACKGROUND UINT32_C(1)
#define ODM_LAYERED_LAYER_MASK_CORE       UINT32_C(2)
#define ODM_LAYERED_LAYER_MASK_FIELD      UINT32_C(4)
#define ODM_LAYERED_LAYER_MASK_HUD        UINT32_C(8)
#define ODM_LAYERED_LAYER_MASK_ALL \
    (ODM_LAYERED_LAYER_MASK_BACKGROUND | ODM_LAYERED_LAYER_MASK_CORE | \
     ODM_LAYERED_LAYER_MASK_FIELD | ODM_LAYERED_LAYER_MASK_HUD)
#define ODM_LAYERED_MAX_PIXELS UINT64_C(67108864)
#define ODM_LAYERED_MAX_PARTICLES UINT32_C(512)
#define ODM_LAYERED_METADATA_BYTES UINT32_C(96)


/* -------------------------------------------------------------------------
 * Layered Style Profile v1
 *
 * Authoring-only visual style record. Applying a profile materializes an
 * ordinary odm_layered_config; no hidden style state participates in render.
 * This keeps config_sha256 / Export Recipe as the sole pixel authority while
 * allowing reusable professional design systems and per-layer application.
 * ------------------------------------------------------------------------- */
#define ODM_LAYERED_STYLE_SCHEMA_VERSION UINT32_C(1)
#define ODM_LAYERED_STYLE_BYTES UINT32_C(512)

#define ODM_LAYERED_STYLE_LAYER_BACKGROUND UINT32_C(1)
#define ODM_LAYERED_STYLE_LAYER_CORE       UINT32_C(2)
#define ODM_LAYERED_STYLE_LAYER_FIELD      UINT32_C(4)
#define ODM_LAYERED_STYLE_LAYER_HUD        UINT32_C(8)
#define ODM_LAYERED_STYLE_LAYER_ALL \
    (ODM_LAYERED_STYLE_LAYER_BACKGROUND | ODM_LAYERED_STYLE_LAYER_CORE | \
     ODM_LAYERED_STYLE_LAYER_FIELD | ODM_LAYERED_STYLE_LAYER_HUD)

#define ODM_LAYERED_STYLE_PRESET_MONO_PRECISE    UINT32_C(1)
#define ODM_LAYERED_STYLE_PRESET_VOID_CINEMATIC  UINT32_C(2)
#define ODM_LAYERED_STYLE_PRESET_DEEP_GRID       UINT32_C(3)
#define ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL UINT32_C(4)

#define ODM_REACTION_SCHEMA_VERSION UINT32_C(1)

/* Reaction sources are normalized Q1.31 values already resolved by
 * Composition/Director; Reaction Matrix never reads audio or Music Map. */
#define ODM_REACTION_SOURCE_NONE                 UINT32_C(0)
#define ODM_REACTION_SOURCE_CORE_BREATH          UINT32_C(1)
#define ODM_REACTION_SOURCE_GRID                 UINT32_C(2)
#define ODM_REACTION_SOURCE_PARTICLES            UINT32_C(3)
#define ODM_REACTION_SOURCE_RADIAL_GAIN          UINT32_C(4)
#define ODM_REACTION_SOURCE_FRACTURE             UINT32_C(5)
#define ODM_REACTION_SOURCE_MEMORY_ECHO          UINT32_C(6)
#define ODM_REACTION_SOURCE_HALO                 UINT32_C(7)
#define ODM_REACTION_SOURCE_LATERAL_FIELD        UINT32_C(8)
#define ODM_REACTION_SOURCE_ACCENT               UINT32_C(9)
#define ODM_REACTION_SOURCE_BAND_0                UINT32_C(10)
#define ODM_REACTION_SOURCE_BAND_1                UINT32_C(11)
#define ODM_REACTION_SOURCE_BAND_2                UINT32_C(12)
#define ODM_REACTION_SOURCE_BAND_3                UINT32_C(13)
#define ODM_REACTION_SOURCE_BAND_4                UINT32_C(14)
#define ODM_REACTION_SOURCE_BAND_5                UINT32_C(15)
#define ODM_REACTION_SOURCE_DIRECTOR_ENERGY       UINT32_C(16)
#define ODM_REACTION_SOURCE_DIRECTOR_NOVELTY      UINT32_C(17)
#define ODM_REACTION_SOURCE_DIRECTOR_DEPTH        UINT32_C(18)
#define ODM_REACTION_SOURCE_DIRECTOR_EDGE         UINT32_C(19)
#define ODM_REACTION_SOURCE_DIRECTOR_ORBIT        UINT32_C(20)
#define ODM_REACTION_SOURCE_DIRECTOR_WINGS        UINT32_C(21)
#define ODM_REACTION_SOURCE_DIRECTOR_MEMORY       UINT32_C(22)
#define ODM_REACTION_SOURCE_DIRECTOR_TUNNEL       UINT32_C(23)
#define ODM_REACTION_SOURCE_DIRECTOR_BACKDROP     UINT32_C(24)
#define ODM_REACTION_SOURCE_DIRECTOR_ARCHITECTURE UINT32_C(25)
#define ODM_REACTION_SOURCE_MAX ODM_REACTION_SOURCE_DIRECTOR_ARCHITECTURE

#define ODM_REACTION_COMBINE_A_ONLY   UINT32_C(0)
#define ODM_REACTION_COMBINE_MAX      UINT32_C(1)
#define ODM_REACTION_COMBINE_MIN      UINT32_C(2)
#define ODM_REACTION_COMBINE_AVERAGE  UINT32_C(3)
#define ODM_REACTION_COMBINE_MUL      UINT32_C(4)
#define ODM_REACTION_COMBINE_ABS_DIFF UINT32_C(5)

#define ODM_REACTION_TARGET_BACKGROUND_ZOOM  UINT32_C(1)
#define ODM_REACTION_TARGET_BACKGROUND_WARP  UINT32_C(2)
#define ODM_REACTION_TARGET_BACKGROUND_DEPTH UINT32_C(3)
#define ODM_REACTION_TARGET_CORE_SCALE       UINT32_C(4)
#define ODM_REACTION_TARGET_FIELD_GAIN       UINT32_C(5)
#define ODM_REACTION_TARGET_PARTICLE_DENSITY UINT32_C(6)

typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t a;
} odm_rgba16;


typedef struct {
    uint32_t schema_version;
    uint32_t preset_id;
    uint32_t layer_mask;
    uint32_t flags;
    odm_rgba16 background_solid;
    odm_rgba16 background_grid;
    odm_rgba16 core_border;
    odm_rgba16 field_primary;
    odm_rgba16 field_secondary;
    odm_rgba16 hud_foreground;
    odm_rgba16 hud_background;
    uint32_t background_opacity_q31;
    uint32_t field_opacity_q31;
    uint32_t hud_opacity_q31;
    uint32_t grid_spacing_scale_q16;
    uint32_t grid_line_scale_q16;
    uint32_t grid_feather_scale_q16;
    uint32_t background_zoom_scale_q16;
    uint32_t background_warp_scale_q16;
    uint32_t background_depth_scale_q16;
    uint32_t core_size_scale_q16;
    uint32_t core_border_scale_q16;
    uint32_t core_feather_scale_q16;
    uint32_t core_reactivity_scale_q16;
    uint32_t ring_gap_scale_q16;
    uint32_t bar_min_scale_q16;
    uint32_t bar_max_scale_q16;
    uint32_t bar_width_scale_q16;
    uint32_t particle_count_scale_q16;
    uint32_t particle_radius_scale_q16;
    uint32_t hud_margin_scale_q16;
    uint32_t hud_progress_scale_q16;
    uint32_t hud_text_scale_q16;
    uint32_t hud_line_gap_scale_q16;
    uint32_t progress_style;
    uint32_t metadata_anchor;
    uint32_t time_mode;
    uint32_t reserved[20];
} odm_layered_style_profile;

typedef struct {
    uint32_t schema_version;
    uint32_t aspect;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t flags;
    uint32_t safe_left_q31;
    uint32_t safe_top_q31;
    uint32_t safe_right_q31;
    uint32_t safe_bottom_q31;
    uint32_t reserved[2];
} odm_canvas_config;

typedef struct {
    uint32_t schema_version;
    uint32_t style;
    odm_rgba16 solid_color;
    odm_rgba16 grid_color;
    uint32_t grid_spacing_q16;
    uint32_t grid_line_q16;
    uint32_t grid_feather_q16;
    uint32_t zoom_reactivity_q31;
    uint32_t warp_reactivity_q31;
    uint32_t depth_reactivity_q31;
    uint32_t opacity_q31;
    uint32_t reserved[6];
} odm_background_config;

typedef struct {
    uint32_t schema_version;
    uint32_t shape;
    uint32_t fit;
    uint32_t center_x_q31;
    uint32_t center_y_q31;
    uint32_t width_q31;
    uint32_t height_q31;
    uint32_t corner_radius_q31;
    uint32_t border_q16;
    uint32_t feather_q16;
    uint32_t scale_reactivity_q31;
    uint32_t opacity_q31;
    odm_rgba16 border_color;
    uint32_t reserved[6];
} odm_core_config;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint32_t radial_segments;
    uint32_t particle_count;
    uint32_t ring_gap_q16;
    uint32_t bar_min_q16;
    uint32_t bar_max_q16;
    uint32_t bar_width_q16;
    uint32_t particle_radius_q16;
    uint32_t field_opacity_q31;
    odm_rgba16 primary_color;
    odm_rgba16 secondary_color;
    /* Las particulas tienen color propio. Antes reutilizaban primary_color, de
     * modo que retocar el acento de las barras cambiaba tambien las particulas:
     * dos categorias distintas atadas al mismo control. */
    odm_rgba16 particle_color;
    uint64_t seed;
    uint32_t reserved[4];
} odm_field_config;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint32_t margin_q16;
    uint32_t progress_height_q16;
    uint32_t progress_width_q31;
    uint32_t text_scale_q16;
    uint32_t line_gap_q16;
    uint32_t opacity_q31;
    odm_rgba16 foreground_color;
    odm_rgba16 background_color;
    char title[ODM_LAYERED_METADATA_BYTES];
    char artist[ODM_LAYERED_METADATA_BYTES];
    uint32_t metadata_anchor;
    uint32_t progress_style;
    uint32_t time_mode;
    /* Cada elemento del HUD lleva su color. `foreground_color` sigue siendo el
     * del codigo de tiempo y el valor del que parten los demas por defecto, de
     * modo que una configuracion que no los toca se dibuja exactamente igual
     * que antes; pero titulo, autoria y barra ya son controlables por separado,
     * que es lo que pide poder editarlos como categorias distintas. */
    odm_rgba16 title_color;
    odm_rgba16 artist_color;
    odm_rgba16 progress_color;
    odm_rgba16 progress_track_color;
    uint32_t reserved0;
} odm_hud_config;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint32_t background_zoom_route;
    uint32_t background_warp_route;
    uint32_t background_depth_route;
    uint32_t core_scale_route;
    uint32_t field_gain_route;
    uint32_t particle_density_route;
} odm_reaction_config;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    odm_canvas_config canvas;
    odm_background_config background;
    odm_core_config core;
    odm_field_config field;
    odm_hud_config hud;
    odm_reaction_config reaction;
} odm_layered_config;

typedef struct {
    uint32_t schema_version;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    int32_t center_x_q16;
    int32_t center_y_q16;
    int32_t core_half_w_q16;
    int32_t core_half_h_q16;
    int32_t core_radius_q16;
    int32_t core_corner_q16;
    int32_t ring_inner_radius_q16;
    int32_t grid_spacing_q16;
    int32_t grid_line_q16;
    int32_t grid_feather_q16;
    int32_t grid_offset_x_q16;
    int32_t grid_offset_y_q16;
    int32_t grid_warp_q16;
    int32_t grid_depth_q16;
    int32_t safe_left_q16;
    int32_t safe_top_q16;
    int32_t safe_right_q16;
    int32_t safe_bottom_q16;
    uint64_t tick_index;
    int64_t sample;
    int64_t duration_samples;
    uint32_t core_opacity_q31;
    uint32_t field_opacity_q31;
    uint32_t progress_q31;
    uint32_t particle_count;
    uint32_t ring_phase;
    uint32_t layout;
    uint32_t flags;
    uint32_t radial_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    /* Coherent per-sector provenance selected at the compositor boundary.
     * These arrays are meaningful only with RADIAL_PROVENANCE. */
    uint32_t radial_body_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    uint32_t radial_release_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    uint32_t radial_attack_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    odm_sha256_digest config_sha256;
    uint32_t reserved[4];
} odm_layered_frame_plan;

#define ODM_LAYERED_RADIAL_TRACE_SCHEMA_VERSION UINT32_C(1)
#define ODM_LAYERED_RADIAL_TRACE_FLAG_NATIVE_96 UINT32_C(1)
#define ODM_LAYERED_RADIAL_TRACE_FLAG_REDUCED_48 UINT32_C(2)
#define ODM_LAYERED_RADIAL_TRACE_FLAG_STRICT_CAUSAL UINT32_C(4)
#define ODM_LAYERED_RADIAL_TRACE_FLAG_TIMESCALE UINT32_C(8)

/* Pixel-authority provenance for one strict causal radial after compositor
 * resolution. Unlike the reaction-side 48-pair summary, this trace identifies
 * the exact source lane selected by the compositor and proves the complete
 * final/body/release/attack tuple against both Composition and Frame Plan. */
typedef struct {
    uint32_t schema_version;
    uint32_t radial_index;
    uint32_t output_radial_count;
    uint32_t source_lane;
    uint32_t candidate_lane_a;
    uint32_t candidate_lane_b;
    uint32_t flags;
    uint32_t final_q31;
    uint32_t body_q31;
    uint32_t release_q31;
    uint32_t attack_q31;
    uint32_t reserved[1];
} odm_layered_radial_trace;

typedef struct {
    uint32_t schema_version;
    uint32_t output_pixel_format;
    uint32_t width;
    uint32_t height;
    int32_t fps;
    uint32_t audio_sample_rate;
    uint32_t audio_channels;
    uint32_t reserved0;
    int64_t project_end_sample;
    int64_t samples_per_frame;
    uint64_t frame_count;
    int64_t output_end_sample;
    uint64_t audio_source_frames;
    uint64_t audio_pad_frames;
    uint64_t video_bytes_per_frame;
    uint64_t video_total_bytes;
    odm_sha256_digest layered_policy_sha256;
    odm_sha256_digest config_sha256;
    uint32_t reserved[4];
} odm_export_plan;

/* Canonical reusable visual styles. Profiles are authoring inputs only;
 * odm_layered_style_apply() materializes all selected values into a normal
 * Layered Config. Transactional: outputs remain unchanged on any error. */
odm_status odm_layered_style_init(odm_layered_style_profile *out_style,
                                  uint32_t preset_id);
odm_status odm_layered_style_validate(const odm_layered_style_profile *style);
odm_status odm_layered_style_bytes(const odm_layered_style_profile *style,
                                   uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required);
odm_status odm_layered_style_sha256(const odm_layered_style_profile *style,
                                    odm_sha256_digest *out_hash);
odm_status odm_layered_style_apply(const odm_layered_config *base_config,
                                   const odm_layered_style_profile *style,
                                   odm_layered_config *out_config);

/* Canonical convenience constructors. They only materialize an ordinary
 * odm_layered_config; the resulting 1024-byte config remains the sole render
 * authority and is hashed exactly like a manually authored config. */
odm_status odm_layered_config_init_default(
    odm_layered_config *out_config,
    uint32_t aspect,
    uint32_t width,
    uint32_t height,
    int32_t fps);

/* Transactional convenience setters: on error config is unchanged. */
odm_status odm_layered_config_set_core_shape(
    odm_layered_config *config,
    uint32_t shape);

odm_status odm_layered_config_set_metadata(
    odm_layered_config *config,
    const char *title,
    const char *artist);

/* Reaction Matrix authoring. Routes are packed canonically as
 * source_a | source_b<<8 | combine<<16; all higher bits must be zero.
 * Legacy all-zero reaction records preserve the exact 0.13 routing semantics.
 * Setters are transactional and materialize explicit v1 routes. */
odm_status odm_reaction_route_make(uint32_t source_a,
                                   uint32_t source_b,
                                   uint32_t combine,
                                   uint32_t *out_route);
odm_status odm_layered_reaction_init_default(odm_reaction_config *out_reaction);
odm_status odm_layered_config_set_reaction_route(odm_layered_config *config,
                                                  uint32_t target,
                                                  uint32_t route);

odm_status odm_layered_config_validate(const odm_layered_config *config);

odm_status odm_layered_resolve_frame_plan(
    const odm_layered_config *config,
    const odm_composition_frame_state *composition,
    const odm_director_frame_state *director,
    int64_t sample,
    int64_t duration_samples,
    odm_layered_frame_plan *out_plan);

/* Trace one published strict-causal radial back through the exact compositor
 * selection boundary. `composition` is required deliberately: source-lane
 * identity is re-derived and cross-checked rather than trusted from mutable
 * diagnostic metadata stored in the Frame Plan. */
odm_status odm_layered_trace_radial(
    const odm_layered_config *config,
    const odm_composition_frame_state *composition,
    const odm_layered_frame_plan *plan,
    uint32_t radial_index,
    odm_layered_radial_trace *out_trace);

odm_status odm_layered_render_requirements(
    const odm_layered_config *config,
    uint32_t output_pixel_format,
    uint64_t *out_frame_bytes,
    uint64_t *out_scratch_bytes);

/* Canonical independent layer output. Each layer is rendered against
 * transparent black as RGBA16LE linear-premultiplied using the same production
 * raster semantics as the full compositor with all other layer authorities
 * disabled. Because integer premultiplied alpha compositing is quantized, this
 * API does NOT claim that alpha-over recomposition of already-flattened layer
 * buffers is associative or byte-identical to the single-pass full compositor. */
odm_status odm_layered_render_layer_requirements(
    const odm_layered_config *config,
    uint64_t *out_frame_bytes,
    uint64_t *out_scratch_bytes);

odm_status odm_layered_render_layer_rgba16(
    const odm_layered_config *config,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    uint32_t layer_id,
    const odm_job_ticket *job_ticket,
    void *scratch, uint64_t scratch_bytes,
    uint8_t *frame, uint64_t frame_capacity,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes,
    odm_sha256_digest *out_pixel_sha256);

/* Canonical authority-stack composition. This is the exact way to combine
 * independently selectable layers: the mask selects original rendering
 * authorities, which are executed in constitutional order Background -> Core
 * -> Field -> HUD. It deliberately does not alpha-over already-flattened
 * Layer Output buffers, because integer-premultiplied alpha is quantized and
 * such host-side recomposition is not generally associative.
 *
 * ALL must be byte/SHA-identical to odm_layered_render_frame(...RGBA16...).
 * A single-bit mask must be byte/SHA-identical to the corresponding canonical
 * Layer Output. core_surface may be NULL iff CORE is not selected. */
odm_status odm_layered_render_stack_rgba16(
    const odm_layered_config *config,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    uint32_t layer_mask,
    const odm_job_ticket *job_ticket,
    void *scratch, uint64_t scratch_bytes,
    uint8_t *frame, uint64_t frame_capacity,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes,
    odm_sha256_digest *out_pixel_sha256);

odm_status odm_layered_render_frame(
    const odm_layered_config *config,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    uint32_t output_pixel_format,
    void *scratch,
    uint64_t scratch_bytes,
    uint8_t *frame,
    uint64_t frame_capacity,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes,
    odm_sha256_digest *out_pixel_sha256);

odm_status odm_export_plan_build(
    const odm_layered_config *config,
    int64_t project_end_sample,
    uint64_t audio_source_frames,
    uint32_t output_pixel_format,
    odm_export_plan *out_plan);

odm_status odm_layered_config_bytes(const odm_layered_config *config,
                                    uint8_t *buffer, uint64_t capacity,
                                    uint64_t *out_required);
odm_status odm_layered_config_sha256(const odm_layered_config *config,
                                    odm_sha256_digest *out_hash);
odm_status odm_layered_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                    uint64_t *out_required);
odm_status odm_layered_policy_current_sha256(odm_sha256_digest *out_hash);

#ifdef __cplusplus
}
#endif

#endif /* ODM_COMPOSITOR_H */

#ifndef ODM_COMPOSITOR_INTERNAL_H
#define ODM_COMPOSITOR_INTERNAL_H

#include "odm_compositor.h"
#include "renderer_internal.h"

#include <stddef.h>
#include <stdint.h>

#define ODM_Q16_ONE INT32_C(65536)

#define ODM_LAYERED_WORKER_POOL_BYTES UINT32_C(4096)
typedef union {
    max_align_t align;
    uint8_t bytes[ODM_LAYERED_WORKER_POOL_BYTES];
} odm_layered_worker_pool;

odm_status odm_layered_worker_pool_init(odm_layered_worker_pool *pool,
                                        uint32_t worker_count);
void odm_layered_worker_pool_destroy(odm_layered_worker_pool *pool);

uint8_t odm_layered_linear_u16_to_srgb8_fast(uint16_t value);

typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t a;
} odm_layered_pixel16;

typedef struct {
    int32_t index;
    uint32_t frac_q16;
} odm_layered_core_map_entry;

typedef struct {
    uint32_t output_pixel_format;
    uint32_t reserved0;
    uint64_t frame_bytes;
    uint64_t scratch_bytes;
    odm_sha256_digest config_sha256;
} odm_layered_render_admission;

typedef struct {
    int64_t box_w_q16;
    int64_t box_h_q16;
    int64_t crop_x_q16;
    int64_t crop_y_q16;
    int64_t crop_w_q16;
    int64_t crop_h_q16;
    int64_t disp_x_q16;
    int64_t disp_y_q16;
    int64_t disp_w_q16;
    int64_t disp_h_q16;
    int64_t min_x;
    int64_t max_x;
    int64_t min_y;
    int64_t max_y;
} odm_layered_core_raster_plan;

uint64_t odm_layered_isqrt_u64(uint64_t x);
int32_t odm_layered_mul_q16_q31(int32_t value_q16, uint32_t factor_q31);
int odm_layered_reaction_validate(const odm_reaction_config *reaction);
odm_status odm_layered_reaction_eval(uint32_t route,
                                      const odm_composition_frame_state *composition,
                                      const odm_director_frame_state *director,
                                      uint32_t *out_value);
void odm_layered_reaction_effective(const odm_layered_config *config,
                                    odm_reaction_config *out_reaction);
uint32_t odm_layered_coverage_q31(int64_t distance_q16, int64_t half_width_q16,
                                  int64_t feather_q16);
void odm_layered_blend16(odm_layered_pixel16 *dst, const odm_rgba16 *src,
                         uint32_t coverage_q31, uint32_t opacity_q31);
void odm_layered_blend_premul16(odm_layered_pixel16 *dst,
                                const odm_layered_pixel16 *src,
                                uint32_t opacity_q31);
odm_status odm_layered_background_pixel(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        uint32_t x, uint32_t y,
                                        odm_layered_pixel16 *out);
void odm_layered_background_render_rows(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        uint32_t y_begin, uint32_t y_end,
                                        odm_layered_pixel16 *frame);
void odm_layered_background_prepare_columns(const odm_layered_frame_plan *plan,
                                            int32_t *col_shifts,
                                            uint32_t count);
void odm_layered_background_render_rows_cached(const odm_layered_config *config,
                                               const odm_layered_frame_plan *plan,
                                               const int32_t *col_shifts,
                                               uint32_t y_begin, uint32_t y_end,
                                               odm_layered_pixel16 *frame);
void odm_layered_background_render(const odm_layered_config *config,
                                   const odm_layered_frame_plan *plan,
                                   odm_layered_pixel16 *frame);
int64_t odm_layered_core_sdf_q16(const odm_layered_config *config,
                                  const odm_layered_frame_plan *plan,
                                  int64_t dx_q16, int64_t dy_q16);
odm_status odm_layered_core_surface_validate(const odm_render_surface_frame *surface);
odm_status odm_layered_core_pixel_prevalidated(const odm_layered_config *config,
                                                const odm_layered_frame_plan *plan,
                                                const odm_render_surface_frame *surface,
                                                uint32_t x, uint32_t y,
                                                odm_layered_pixel16 *out);
odm_status odm_layered_core_pixel(const odm_layered_config *config,
                                  const odm_layered_frame_plan *plan,
                                  const odm_render_surface_frame *surface,
                                  uint32_t x, uint32_t y,
                                  odm_layered_pixel16 *out);
odm_status odm_layered_core_render_rows(const odm_layered_config *config,
                                        const odm_layered_frame_plan *plan,
                                        const odm_render_surface_frame *surface,
                                        uint32_t y_begin, uint32_t y_end,
                                        odm_layered_pixel16 *frame);
odm_status odm_layered_core_prepare_raster(const odm_layered_config *config,
                                           const odm_layered_frame_plan *plan,
                                           const odm_render_surface_frame *surface,
                                           odm_layered_core_raster_plan *out_raster,
                                           odm_layered_core_map_entry *xmap,
                                           uint32_t xmap_count);
odm_status odm_layered_core_render_rows_cached(const odm_layered_config *config,
                                               const odm_layered_frame_plan *plan,
                                               const odm_render_surface_frame *surface,
                                               const odm_layered_core_raster_plan *raster,
                                               const odm_layered_core_map_entry *xmap,
                                               uint32_t y_begin, uint32_t y_end,
                                               odm_layered_pixel16 *frame);
void odm_layered_field_render(const odm_layered_config *config,
                              const odm_layered_frame_plan *plan,
                              odm_layered_pixel16 *frame);
void odm_layered_hud_render(const odm_layered_config *config,
                            const odm_layered_frame_plan *plan,
                            odm_layered_pixel16 *frame);

/* Native streaming path: identical pixels and transactional publication, but
 * deliberately omits the per-frame SHA.  Callers must bind the returned bytes
 * into a stronger enclosing identity (Export does so with video_stream_sha256). */
odm_status odm_layered_render_frame_stream_unhashed(
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
    uint64_t *out_required_scratch_bytes);

/* Export-only zero-copy publication boundary. On success *out_stage points into
 * caller-owned scratch and remains valid only until scratch is reused. No public
 * frame buffer is written. The returned bytes are identical to the public
 * renderer and are intended for immediate hashing/sink consumption. */
odm_status odm_layered_render_frame_stream_stage_unhashed(
    const odm_layered_config *config,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    uint32_t output_pixel_format,
    void *scratch,
    uint64_t scratch_bytes,
    const uint8_t **out_stage,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes);

/* Same scratch-resident stream path with deterministic disjoint-row workers.
 * worker_count is implementation-only (1..4) and cannot change output bytes. */
/* Export/internal fast path. Admission performs the full immutable-config
 * validation and hash once; admitted frame rendering still validates all
 * frame-variable structure, core surface, cancellation, aliasing and buffers. */
odm_status odm_layered_render_admit(const odm_layered_config *config,
                                    uint32_t output_pixel_format,
                                    odm_layered_render_admission *out_admission);
odm_status odm_layered_render_frame_stream_stage_admitted_workers(
    const odm_layered_config *config,
    const odm_layered_render_admission *admission,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    void *scratch,
    uint64_t scratch_bytes,
    uint32_t worker_count,
    const uint8_t **out_stage,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes);

odm_status odm_layered_render_frame_stream_stage_unhashed_workers(
    const odm_layered_config *config,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    uint32_t output_pixel_format,
    void *scratch,
    uint64_t scratch_bytes,
    uint32_t worker_count,
    const uint8_t **out_stage,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes);

/* Persistent-session form used by Native Export. Threads are created once
 * before sink.begin() and reused for every frame; scheduling remains
 * non-semantic and byte-identical to worker_count=1. */
odm_status odm_layered_render_frame_stream_stage_admitted_pool(
    const odm_layered_config *config,
    const odm_layered_render_admission *admission,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    void *scratch,
    uint64_t scratch_bytes,
    odm_layered_worker_pool *pool,
    const uint8_t **out_stage,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes);

odm_status odm_layered_render_frame_stream_stage_unhashed_pool(
    const odm_layered_config *config,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    uint32_t output_pixel_format,
    void *scratch,
    uint64_t scratch_bytes,
    odm_layered_worker_pool *pool,
    const uint8_t **out_stage,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes);

/* Radial raster optics v2 (Layered Policy v11).
 * Normative definition: docs/RADIAL_MORPHOLOGY_V2.md section 4. Shared by the
 * policy encoder (layout.c) and the field rasterizer (field.c) so the two
 * cannot drift apart again. */
#define LAYER_RADIAL_AUTHORITY_FLOOR UINT32_C(322122547)  /* 0.15 */
#define LAYER_RADIAL_TAIL_WEIGHT     UINT32_C(1395864371) /* 0.65 */

#endif

#include "compositor_internal.h"
#include "srgb16_lut.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

static int ranges_overlap(const void *a, uint64_t a_bytes,
                          const void *b, uint64_t b_bytes) {
    uintptr_t as, bs, ae, be;
    if (a_bytes == 0u || b_bytes == 0u) return 0;
    if (!a || !b || a_bytes > (uint64_t)UINTPTR_MAX ||
        b_bytes > (uint64_t)UINTPTR_MAX) return 1;
    as = (uintptr_t)a;
    bs = (uintptr_t)b;
    if ((uintptr_t)a_bytes > UINTPTR_MAX - as ||
        (uintptr_t)b_bytes > UINTPTR_MAX - bs) return 1;
    ae = as + (uintptr_t)a_bytes;
    be = bs + (uintptr_t)b_bytes;
    return as < be && bs < ae;
}

static int zero_u32(const uint32_t *v, uint32_t n) {
    uint32_t i;
    for (i = 0u; i < n; ++i) if (v[i] != 0u) return 0;
    return 1;
}

static uint32_t provenance_mul_q31(uint32_t a, uint32_t b) {
    uint64_t product = (uint64_t)a * (uint64_t)b + (uint64_t)INT32_MAX / 2u;
    product /= (uint64_t)INT32_MAX;
    return product > (uint64_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)product;
}

static odm_status checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (a != 0u && b > UINT64_MAX / a) return ODM_STATUS_OVERFLOW;
    *out = a * b;
    return ODM_STATUS_OK;
}

static odm_status checked_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (b > UINT64_MAX - a) return ODM_STATUS_OVERFLOW;
    *out = a + b;
    return ODM_STATUS_OK;
}

static odm_status cancel_check(const odm_job_ticket *ticket) {
    int cancelled = 0;
    odm_status st;
    if (!ticket) return ODM_STATUS_OK;
    st = odm_job_should_cancel(*ticket, &cancelled);
    if (st != ODM_STATUS_OK) return st;
    return cancelled ? ODM_STATUS_CANCELLED : ODM_STATUS_OK;
}

static odm_status validate_plan_bound(const odm_layered_config *c,
                                      const odm_layered_frame_plan *p,
                                      const odm_sha256_digest *admitted_hash) {
    uint32_t i;
    odm_sha256_digest config_hash;
    odm_status st;
    if (!c || !p) return ODM_STATUS_INVALID_ARGUMENT;
    if (p->schema_version != ODM_LAYERED_SCHEMA_VERSION ||
        p->width != c->canvas.width || p->height != c->canvas.height ||
        p->fps != c->canvas.fps || p->sample < 0 || p->duration_samples <= 0 ||
        p->sample > p->duration_samples || p->layout > ODM_DIRECTOR_LAYOUT_FRACTURE ||
        p->core_half_w_q16 < 0 || p->core_half_h_q16 < 0 ||
        p->core_radius_q16 < 0 || p->core_corner_q16 < 0 ||
        p->ring_inner_radius_q16 < 0 || p->grid_spacing_q16 < 0 ||
        p->grid_line_q16 < 0 || p->grid_feather_q16 < 0 ||
        p->core_opacity_q31 > (uint32_t)INT32_MAX ||
        p->field_opacity_q31 > (uint32_t)INT32_MAX ||
        p->progress_q31 > (uint32_t)INT32_MAX ||
        p->particle_count > c->field.particle_count ||
        (p->flags & ~(ODM_COMPOSITION_FLAG_RADIAL_HIRES |
                      ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                      ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE |
                      ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE)) != 0u ||
        p->safe_left_q16 < 0 || p->safe_top_q16 < 0 ||
        p->safe_right_q16 > ((int32_t)p->width << 16) ||
        p->safe_bottom_q16 > ((int32_t)p->height << 16) ||
        p->safe_left_q16 >= p->safe_right_q16 || p->safe_top_q16 >= p->safe_bottom_q16 ||
        !zero_u32(p->reserved, 4u)) return ODM_STATUS_INVALID_DATA;
    if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u &&
        (p->flags & (ODM_COMPOSITION_FLAG_RADIAL_HIRES |
                     ODM_COMPOSITION_FLAG_STRICT_CAUSAL)) !=
            (ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL))
        return ODM_STATUS_INVALID_DATA;
    if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE) != 0u &&
        (p->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) == 0u)
        return ODM_STATUS_INVALID_DATA;
    /* STRICT_CAUSAL is a render-boundary promise, not merely a hint emitted
     * by the normal resolver. A forged/direct frame plan must not be able to
     * smuggle clock/phase motion back into a path that advertises causal
     * isolation. These are the only plan-local procedural authorities used by
     * Background/Field geometry. */
    if ((p->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u &&
        (p->ring_phase != 0u || p->grid_offset_x_q16 != 0 ||
         p->grid_offset_y_q16 != 0))
        return ODM_STATUS_INVALID_DATA;
    for (i = 0u; i < c->field.radial_segments; ++i) {
        if (p->radial_q31[i] > (uint32_t)INT32_MAX ||
            p->radial_body_q31[i] > (uint32_t)INT32_MAX ||
            p->radial_release_q31[i] > (uint32_t)INT32_MAX ||
            p->radial_attack_q31[i] > (uint32_t)INT32_MAX)
            return ODM_STATUS_INVALID_DATA;
        if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u) {
            uint32_t body = p->radial_body_q31[i];
            uint32_t base = body;
            uint32_t expected;
            if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE) != 0u) {
                const uint32_t release_weight_q31 = UINT32_C(751619277); /* 0.35 */
                uint32_t release_mix = provenance_mul_q31(p->radial_release_q31[i],
                                                          release_weight_q31);
                base = body + provenance_mul_q31((uint32_t)INT32_MAX - body,
                                                 release_mix);
            } else if (p->radial_release_q31[i] != 0u) {
                return ODM_STATUS_INVALID_DATA;
            }
            expected = base + provenance_mul_q31((uint32_t)INT32_MAX - base,
                                                 p->radial_attack_q31[i]);
            if (expected != p->radial_q31[i]) return ODM_STATUS_INVALID_DATA;
        } else if (p->radial_body_q31[i] != 0u || p->radial_release_q31[i] != 0u ||
                   p->radial_attack_q31[i] != 0u) {
            return ODM_STATUS_INVALID_DATA;
        }
    }
    for (; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
        if (p->radial_q31[i] != 0u || p->radial_body_q31[i] != 0u ||
            p->radial_release_q31[i] != 0u || p->radial_attack_q31[i] != 0u)
            return ODM_STATUS_INVALID_DATA;
    }
    if (admitted_hash) {
        if (!odm_sha256_equal(admitted_hash, &p->config_sha256))
            return ODM_STATUS_INTEGRITY_ERROR;
    } else {
        st = odm_layered_config_sha256(c, &config_hash);
        if (st != ODM_STATUS_OK) return st;
        if (!odm_sha256_equal(&config_hash, &p->config_sha256))
            return ODM_STATUS_INTEGRITY_ERROR;
    }
    return ODM_STATUS_OK;
}

static odm_status requirements(const odm_layered_config *c,
                               uint32_t output_pixel_format,
                               uint64_t *out_frame_bytes,
                               uint64_t *out_scratch_bytes) {
    uint64_t pixels, working, out_bytes, xmap_bytes, col_shift_bytes, scratch;
    uint64_t bpp;
    odm_status st;
    if (!out_frame_bytes || !out_scratch_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    *out_frame_bytes = 0u;
    *out_scratch_bytes = 0u;
    st = odm_layered_config_validate(c);
    if (st != ODM_STATUS_OK) return st;
    if (output_pixel_format == ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL) bpp = 8u;
    else if (output_pixel_format == ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE) bpp = 4u;
    else return ODM_STATUS_UNSUPPORTED;
    st = checked_mul_u64(c->canvas.width, c->canvas.height, &pixels);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(pixels, sizeof(odm_layered_pixel16), &working);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(pixels, bpp, &out_bytes);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(c->canvas.width, sizeof(odm_layered_core_map_entry),
                         &xmap_bytes);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(c->canvas.width, sizeof(int32_t), &col_shift_bytes);
    if (st != ODM_STATUS_OK) return st;
    /* Transactional output: scratch owns the canonical premultiplied working
     * frame, the fully encoded staging frame and a per-frame Core x-map. The
     * map removes invariant crop/fit division from the pixel hot loop. */
    st = checked_add_u64(working, out_bytes, &scratch);
    if (st != ODM_STATUS_OK) return st;
    st = checked_add_u64(scratch, xmap_bytes, &scratch);
    if (st != ODM_STATUS_OK) return st;
    st = checked_add_u64(scratch, col_shift_bytes, &scratch);
    if (st != ODM_STATUS_OK) return st;
    *out_frame_bytes = out_bytes;
    *out_scratch_bytes = scratch;
    return ODM_STATUS_OK;
}

odm_status odm_layered_render_requirements(const odm_layered_config *config,
                                           uint32_t output_pixel_format,
                                           uint64_t *out_frame_bytes,
                                           uint64_t *out_scratch_bytes) {
    return requirements(config, output_pixel_format,
                        out_frame_bytes, out_scratch_bytes);
}

odm_status odm_layered_render_admit(const odm_layered_config *config,
                                    uint32_t output_pixel_format,
                                    odm_layered_render_admission *out_admission) {
    odm_layered_render_admission a;
    odm_status st;
    if (!config || !out_admission) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&a, 0, sizeof(a));
    st = requirements(config, output_pixel_format, &a.frame_bytes, &a.scratch_bytes);
    if (st != ODM_STATUS_OK) return st;
    st = odm_layered_config_sha256(config, &a.config_sha256);
    if (st != ODM_STATUS_OK) return st;
    a.output_pixel_format = output_pixel_format;
    *out_admission = a;
    return ODM_STATUS_OK;
}

uint8_t odm_layered_linear_u16_to_srgb8_fast(uint16_t value) {
    return odm_layered_srgb16_to_srgb8_lut[value];
}

uint8_t odm_layered_linear_u16_to_srgb8_dither(uint16_t value, uint32_t x, uint32_t y) {
    /* La tabla 8.8 dice DONDE cae el valor entre dos codigos, no solo cual es
     * el mas cercano. Con esa fraccion el desplazamiento de Bayer decide el
     * lado, de modo que un degradado que solo recorre siete codigos se
     * distribuye en lugar de escalonarse. Centrado en cero: el valor medio de
     * la imagen no cambia. */
    int32_t q8 = (int32_t)odm_layered_srgb16_to_srgb8_q8_lut[value];
    int32_t b = (int32_t)odm_layered_bayer8[((y & 7u) << 3) | (x & 7u)];
    int32_t code = (q8 + 128 + (2 * b + 1 - 64) * 2) >> 8;
    if (code < 0) code = 0;
    if (code > 255) code = 255;
    return (uint8_t)code;
}

static void write_u16le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void encode_pixel16le(const odm_layered_pixel16 *p, uint8_t *out) {
    write_u16le(out + 0u, p->r);
    write_u16le(out + 2u, p->g);
    write_u16le(out + 4u, p->b);
    write_u16le(out + 6u, p->a);
}

static void encode_pixel8_black_dither(const odm_layered_pixel16 *p, uint8_t *out,
                                       uint32_t x, uint32_t y) {
    out[0] = odm_layered_linear_u16_to_srgb8_dither(p->r, x, y);
    out[1] = odm_layered_linear_u16_to_srgb8_dither(p->g, x, y);
    out[2] = odm_layered_linear_u16_to_srgb8_dither(p->b, x, y);
    out[3] = 255u;
}

static void encode_pixel8_black(const odm_layered_pixel16 *p, uint8_t *out) {
    /* The working RGB is already linear-premultiplied. Compositing over black
     * therefore leaves RGB unchanged; delivery alpha becomes opaque. */
    out[0] = odm_layered_linear_u16_to_srgb8_fast(p->r);
    out[1] = odm_layered_linear_u16_to_srgb8_fast(p->g);
    out[2] = odm_layered_linear_u16_to_srgb8_fast(p->b);
    out[3] = UINT8_MAX;
}

enum odm_layered_row_phase {
    ODM_LAYERED_ROW_BACKGROUND = 1,
    ODM_LAYERED_ROW_CORE = 2,
    ODM_LAYERED_ROW_ENCODE16 = 3,
    ODM_LAYERED_ROW_ENCODE8 = 4
};

typedef struct {
    enum odm_layered_row_phase phase;
    uint32_t worker_count;
    uint32_t width;
    uint32_t height;
    const odm_layered_config *config;
    const odm_layered_frame_plan *plan;
    const odm_render_surface_frame *surface;
    const odm_layered_core_raster_plan *raster;
    const odm_layered_core_map_entry *xmap;
    const int32_t *background_col_shifts;
    /* La fase de codificacion no recibe la configuracion -- no la necesita --
     * asi que la decision de difuminar viaja como dato, no como puntero a algo
     * que en esta fase es nulo. */
    uint32_t dither_output;
    odm_layered_pixel16 *work;
    uint8_t *stage;
    const odm_job_ticket *job_ticket;
} odm_layered_row_context;

typedef struct {
    const odm_layered_row_context *context;
    uint32_t worker_id;
    odm_status status;
} odm_layered_row_job;

typedef struct odm_layered_worker_pool_impl odm_layered_worker_pool_impl;

typedef struct {
    odm_layered_worker_pool_impl *pool;
    uint32_t worker_id;
} odm_layered_pool_arg;

struct odm_layered_worker_pool_impl {
    pthread_t threads[3];
    pthread_mutex_t mutex;
    pthread_cond_t work_cond;
    pthread_cond_t done_cond;
    uint32_t worker_count;
    uint32_t created_threads;
    uint32_t finished_threads;
    uint32_t stop;
    uint64_t generation;
    odm_layered_row_context context;
    odm_status worker_status[4];
    odm_layered_pool_arg args[3];
};

_Static_assert(sizeof(odm_layered_worker_pool_impl) <= ODM_LAYERED_WORKER_POOL_BYTES,
               "worker pool storage too small");
_Static_assert(_Alignof(odm_layered_worker_pool) >= _Alignof(odm_layered_worker_pool_impl),
               "worker pool alignment too small");

static odm_layered_worker_pool_impl *layered_pool_impl(odm_layered_worker_pool *pool) {
    return pool ? (odm_layered_worker_pool_impl *)(void *)pool->bytes : NULL;
}

static void layered_row_execute(odm_layered_row_job *job) {
    const odm_layered_row_context *c;
    uint64_t y, stride;
    if (!job || !job->context) return;
    c = job->context;
    job->status = ODM_STATUS_OK;
    if (c->worker_count == 0u || job->worker_id >= c->worker_count) {
        job->status = ODM_STATUS_INVALID_ARGUMENT;
        return;
    }
    stride = (uint64_t)c->worker_count * UINT64_C(64);
    y = (uint64_t)job->worker_id * UINT64_C(64);
    while (y < c->height) {
        uint32_t y_begin = (uint32_t)y;
        uint32_t y_end = y + UINT64_C(64) > c->height
                       ? c->height : (uint32_t)(y + UINT64_C(64));
        if (c->phase == ODM_LAYERED_ROW_BACKGROUND) {
            odm_layered_background_render_rows_cached(c->config, c->plan,
                                                       c->background_col_shifts,
                                                       y_begin, y_end, c->work);
        } else if (c->phase == ODM_LAYERED_ROW_CORE) {
            job->status = odm_layered_core_render_rows_cached(
                c->config, c->plan, c->surface, c->raster, c->xmap,
                y_begin, y_end, c->work);
            if (job->status != ODM_STATUS_OK) return;
        } else {
            uint64_t i = (uint64_t)y_begin * c->width;
            uint64_t end = (uint64_t)y_end * c->width;
            if (c->phase == ODM_LAYERED_ROW_ENCODE16) {
                for (; i < end; ++i) encode_pixel16le(&c->work[i], c->stage + i * 8u);
            } else if (c->phase == ODM_LAYERED_ROW_ENCODE8) {
                if (c->dither_output != 0u) {
                    /* Recorrido por filas: la posicion en la matriz de Bayer
                     * sale del indice, sin division por pixel. */
                    uint32_t yy, xx;
                    for (yy = y_begin; yy < y_end; ++yy) {
                        uint64_t base = (uint64_t)yy * c->width;
                        for (xx = 0u; xx < c->width; ++xx)
                            encode_pixel8_black_dither(&c->work[base + xx],
                                                       c->stage + (base + xx) * 4u, xx, yy);
                    }
                } else {
                    for (; i < end; ++i) encode_pixel8_black(&c->work[i], c->stage + i * 4u);
                }
            } else {
                job->status = ODM_STATUS_INVALID_ARGUMENT;
                return;
            }
        }
        job->status = cancel_check(c->job_ticket);
        if (job->status != ODM_STATUS_OK) return;
        if (stride > UINT64_MAX - y) return;
        y += stride;
    }
}

static void *layered_row_thread(void *opaque) {
    layered_row_execute((odm_layered_row_job *)opaque);
    return NULL;
}

static void *layered_pool_thread(void *opaque) {
    odm_layered_pool_arg *arg = (odm_layered_pool_arg *)opaque;
    odm_layered_worker_pool_impl *pool;
    uint64_t seen_generation = 0u;
    if (!arg || !arg->pool) return NULL;
    pool = arg->pool;
    if (pthread_mutex_lock(&pool->mutex) != 0) return NULL;
    for (;;) {
        odm_layered_row_job job;
        while (!pool->stop && pool->generation == seen_generation) {
            if (pthread_cond_wait(&pool->work_cond, &pool->mutex) != 0) {
                pool->stop = 1u;
                break;
            }
        }
        if (pool->stop) {
            (void)pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }
        seen_generation = pool->generation;
        memset(&job, 0, sizeof(job));
        job.context = &pool->context;
        job.worker_id = arg->worker_id;
        job.status = ODM_STATUS_OK;
        if (pthread_mutex_unlock(&pool->mutex) != 0) return NULL;
        layered_row_execute(&job);
        if (pthread_mutex_lock(&pool->mutex) != 0) return NULL;
        pool->worker_status[arg->worker_id] = job.status;
        pool->finished_threads++;
        if (pool->finished_threads == pool->worker_count - 1u)
            (void)pthread_cond_signal(&pool->done_cond);
    }
}

odm_status odm_layered_worker_pool_init(odm_layered_worker_pool *storage,
                                        uint32_t worker_count) {
    odm_layered_worker_pool_impl *pool;
    uint32_t i;
    if (!storage || worker_count < 2u || worker_count > 4u)
        return ODM_STATUS_INVALID_ARGUMENT;
    memset(storage, 0, sizeof(*storage));
    pool = layered_pool_impl(storage);
    pool->worker_count = worker_count;
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) return ODM_STATUS_INTERNAL_ERROR;
    if (pthread_cond_init(&pool->work_cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&pool->mutex);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    if (pthread_cond_init(&pool->done_cond, NULL) != 0) {
        (void)pthread_cond_destroy(&pool->work_cond);
        (void)pthread_mutex_destroy(&pool->mutex);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    for (i = 1u; i < worker_count; ++i) {
        pool->args[i - 1u].pool = pool;
        pool->args[i - 1u].worker_id = i;
        if (pthread_create(&pool->threads[i - 1u], NULL, layered_pool_thread,
                           &pool->args[i - 1u]) != 0) {
            uint32_t j;
            if (pthread_mutex_lock(&pool->mutex) == 0) {
                pool->stop = 1u;
                pool->generation++;
                (void)pthread_cond_broadcast(&pool->work_cond);
                (void)pthread_mutex_unlock(&pool->mutex);
            }
            for (j = 0u; j < pool->created_threads; ++j)
                (void)pthread_join(pool->threads[j], NULL);
            (void)pthread_cond_destroy(&pool->done_cond);
            (void)pthread_cond_destroy(&pool->work_cond);
            (void)pthread_mutex_destroy(&pool->mutex);
            memset(storage, 0, sizeof(*storage));
            return ODM_STATUS_INTERNAL_ERROR;
        }
        pool->created_threads++;
    }
    return ODM_STATUS_OK;
}

void odm_layered_worker_pool_destroy(odm_layered_worker_pool *storage) {
    odm_layered_worker_pool_impl *pool;
    uint32_t i;
    if (!storage) return;
    pool = layered_pool_impl(storage);
    if (pool->worker_count < 2u || pool->worker_count > 4u) {
        memset(storage, 0, sizeof(*storage));
        return;
    }
    if (pthread_mutex_lock(&pool->mutex) == 0) {
        pool->stop = 1u;
        pool->generation++;
        (void)pthread_cond_broadcast(&pool->work_cond);
        (void)pthread_mutex_unlock(&pool->mutex);
    }
    for (i = 0u; i < pool->created_threads; ++i)
        (void)pthread_join(pool->threads[i], NULL);
    (void)pthread_cond_destroy(&pool->done_cond);
    (void)pthread_cond_destroy(&pool->work_cond);
    (void)pthread_mutex_destroy(&pool->mutex);
    memset(storage, 0, sizeof(*storage));
}

static odm_status layered_pool_run(odm_layered_worker_pool *storage,
                                   const odm_layered_row_context *context) {
    odm_layered_worker_pool_impl *pool = layered_pool_impl(storage);
    odm_layered_row_job main_job;
    odm_status result = ODM_STATUS_OK;
    uint32_t i;
    if (!pool || !context || pool->worker_count != context->worker_count ||
        pool->worker_count < 2u || pool->worker_count > 4u)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (pthread_mutex_lock(&pool->mutex) != 0) return ODM_STATUS_INTERNAL_ERROR;
    pool->context = *context;
    pool->finished_threads = 0u;
    for (i = 0u; i < pool->worker_count; ++i) pool->worker_status[i] = ODM_STATUS_OK;
    pool->generation++;
    (void)pthread_cond_broadcast(&pool->work_cond);
    if (pthread_mutex_unlock(&pool->mutex) != 0) return ODM_STATUS_INTERNAL_ERROR;

    memset(&main_job, 0, sizeof(main_job));
    main_job.context = context;
    main_job.worker_id = 0u;
    main_job.status = ODM_STATUS_OK;
    layered_row_execute(&main_job);

    if (pthread_mutex_lock(&pool->mutex) != 0) return ODM_STATUS_INTERNAL_ERROR;
    while (pool->finished_threads < pool->worker_count - 1u && !pool->stop) {
        if (pthread_cond_wait(&pool->done_cond, &pool->mutex) != 0) {
            pool->stop = 1u;
            result = ODM_STATUS_INTERNAL_ERROR;
            break;
        }
    }
    pool->worker_status[0] = main_job.status;
    if (pool->stop && result == ODM_STATUS_OK) result = ODM_STATUS_INTERNAL_ERROR;
    if (result == ODM_STATUS_OK) {
        for (i = 0u; i < pool->worker_count; ++i) {
            if (pool->worker_status[i] != ODM_STATUS_OK) {
                result = pool->worker_status[i];
                break;
            }
        }
    }
    if (pthread_mutex_unlock(&pool->mutex) != 0 && result == ODM_STATUS_OK)
        result = ODM_STATUS_INTERNAL_ERROR;
    return result;
}

static odm_status layered_run_row_phase(const odm_layered_row_context *context) {
    odm_layered_row_job jobs[4];
    pthread_t threads[3];
    uint32_t i, created = 0u;
    int create_error = 0;
    if (!context || context->worker_count == 0u || context->worker_count > 4u)
        return ODM_STATUS_INVALID_ARGUMENT;
    memset(jobs, 0, sizeof(jobs));
    for (i = 0u; i < context->worker_count; ++i) {
        jobs[i].context = context;
        jobs[i].worker_id = i;
        jobs[i].status = ODM_STATUS_OK;
    }
    for (i = 1u; i < context->worker_count; ++i) {
        if (pthread_create(&threads[i - 1u], NULL, layered_row_thread, &jobs[i]) != 0) {
            create_error = 1;
            break;
        }
        created++;
    }
    layered_row_execute(&jobs[0]);
    for (i = 0u; i < created; ++i) {
        if (pthread_join(threads[i], NULL) != 0) create_error = 1;
    }
    if (create_error) return ODM_STATUS_INTERNAL_ERROR;
    for (i = 0u; i < context->worker_count; ++i) {
        if (jobs[i].status != ODM_STATUS_OK) return jobs[i].status;
    }
    return ODM_STATUS_OK;
}

static odm_status layered_dispatch_row_phase(const odm_layered_row_context *context,
                                             odm_layered_worker_pool *pool) {
    if (pool && context && context->worker_count > 1u)
        return layered_pool_run(pool, context);
    return layered_run_row_phase(context);
}

static odm_status render_frame_impl(
    const odm_layered_config *config,
    const odm_layered_render_admission *admission,
    const odm_layered_frame_plan *plan,
    const odm_render_surface_frame *core_surface,
    const odm_job_ticket *job_ticket,
    uint32_t output_pixel_format,
    uint32_t layer_mask,
    void *scratch,
    uint64_t scratch_bytes,
    uint8_t *frame,
    uint64_t frame_capacity,
    uint64_t *out_required_frame_bytes,
    uint64_t *out_required_scratch_bytes,
    odm_sha256_digest *out_pixel_sha256,
    int compute_pixel_sha256,
    int publish_frame,
    uint32_t worker_count,
    odm_layered_worker_pool *worker_pool,
    const uint8_t **out_stage) {
    uint64_t frame_bytes = 0u, required_scratch = 0u, pixels = 0u;
    uint64_t working_bytes = 0u, i;
    odm_layered_pixel16 *work;
    uint8_t *stage;
    odm_layered_core_map_entry *xmap;
    int32_t *background_col_shifts;
    odm_layered_core_raster_plan raster;
    odm_status st;
    if (!out_required_frame_bytes || !out_required_scratch_bytes)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_required_frame_bytes = 0u;
    *out_required_scratch_bytes = 0u;
    if (out_stage) *out_stage = NULL;
    if (worker_count == 0u || worker_count > 4u) return ODM_STATUS_INVALID_ARGUMENT;
    if (layer_mask == 0u || (layer_mask & ~ODM_LAYERED_LAYER_MASK_ALL) != 0u)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (admission) {
        if (!config || admission->output_pixel_format != output_pixel_format ||
            admission->reserved0 != 0u || admission->frame_bytes == 0u ||
            admission->scratch_bytes == 0u) return ODM_STATUS_INVALID_DATA;
        frame_bytes = admission->frame_bytes;
        required_scratch = admission->scratch_bytes;
        st = validate_plan_bound(config, plan, &admission->config_sha256);
    } else {
        st = requirements(config, output_pixel_format, &frame_bytes, &required_scratch);
        if (st == ODM_STATUS_OK) st = validate_plan_bound(config, plan, NULL);
    }
    *out_required_frame_bytes = frame_bytes;
    *out_required_scratch_bytes = required_scratch;
    if (st != ODM_STATUS_OK) return st;
    if (!scratch ||
        (publish_frame && !frame) || (!publish_frame && !out_stage) ||
        (compute_pixel_sha256 && !out_pixel_sha256) ||
        (((layer_mask & ODM_LAYERED_LAYER_MASK_CORE) != 0u) && !core_surface))
        return ODM_STATUS_INVALID_ARGUMENT;
    if (scratch_bytes < required_scratch ||
        (publish_frame && frame_capacity < frame_bytes))
        return ODM_STATUS_BUFFER_TOO_SMALL;
    if (((uintptr_t)scratch & (uintptr_t)7u) != 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if ((publish_frame && ranges_overlap(scratch, required_scratch, frame, frame_bytes)) ||
        ranges_overlap(scratch, required_scratch, config, sizeof(*config)) ||
        ranges_overlap(scratch, required_scratch, plan, sizeof(*plan)) ||
        (publish_frame && ranges_overlap(frame, frame_bytes, config, sizeof(*config))) ||
        (publish_frame && ranges_overlap(frame, frame_bytes, plan, sizeof(*plan))))
        return ODM_STATUS_INVALID_ARGUMENT;
    if ((layer_mask & ODM_LAYERED_LAYER_MASK_CORE) != 0u) {
        if (ranges_overlap(scratch, required_scratch, core_surface->pixels,
                           core_surface->pixel_bytes) ||
            (publish_frame && ranges_overlap(frame, frame_bytes, core_surface->pixels,
                                             core_surface->pixel_bytes)) ||
            ranges_overlap(scratch, required_scratch, core_surface, sizeof(*core_surface)) ||
            (publish_frame && ranges_overlap(frame, frame_bytes, core_surface,
                                             sizeof(*core_surface))))
            return ODM_STATUS_INVALID_ARGUMENT;
        st = odm_layered_core_surface_validate(core_surface);
        if (st != ODM_STATUS_OK) return st;
    }
    st = checked_mul_u64(config->canvas.width, config->canvas.height, &pixels);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(pixels, sizeof(odm_layered_pixel16), &working_bytes);
    if (st != ODM_STATUS_OK) return st;
    work = (odm_layered_pixel16 *)scratch;
    stage = (uint8_t *)scratch + working_bytes;
    xmap = (odm_layered_core_map_entry *)(stage + frame_bytes);
    background_col_shifts = (int32_t *)(xmap + plan->width);
    /* Canonical authority stack. Background is the only authority guaranteed
     * to initialize every pixel; when omitted, transparent black is the exact
     * starting state. Flattened Layer Output buffers are never fed back here. */
    if ((layer_mask & ODM_LAYERED_LAYER_MASK_BACKGROUND) != 0u) {
        odm_layered_background_prepare_columns(plan, background_col_shifts, plan->width);
    } else {
        memset(work, 0, (size_t)working_bytes);
    }
    if ((layer_mask & ODM_LAYERED_LAYER_MASK_CORE) != 0u) {
        st = odm_layered_core_prepare_raster(config, plan, core_surface, &raster,
                                             xmap, plan->width);
        if (st != ODM_STATUS_OK) return st;
    } else {
        memset(&raster, 0, sizeof(raster));
    }

    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;

    /* Background/Core are independent row authorities. Runtime worker count
     * changes scheduling only; layer selection changes authority, not raster
     * mathematics. */
    {
        odm_layered_row_context rows;
        memset(&rows, 0, sizeof(rows));
        rows.worker_count = worker_count;
        rows.width = plan->width;
        rows.height = plan->height;
        rows.config = config;
        rows.plan = plan;
        rows.surface = core_surface;
        rows.raster = &raster;
        rows.xmap = xmap;
        rows.background_col_shifts = background_col_shifts;
        rows.work = work;
        rows.stage = stage;
        rows.job_ticket = job_ticket;
        if ((layer_mask & ODM_LAYERED_LAYER_MASK_BACKGROUND) != 0u) {
            rows.phase = ODM_LAYERED_ROW_BACKGROUND;
            st = layered_dispatch_row_phase(&rows, worker_pool);
            if (st != ODM_STATUS_OK) return st;
        }
        if ((layer_mask & ODM_LAYERED_LAYER_MASK_CORE) != 0u) {
            rows.phase = ODM_LAYERED_ROW_CORE;
            st = layered_dispatch_row_phase(&rows, worker_pool);
            if (st != ODM_STATUS_OK) return st;
        }
    }

    /* Ordered overlay authorities: Field never owns metadata; HUD never owns
     * music interpretation. Both consume only the already-resolved frame plan. */
    if ((layer_mask & ODM_LAYERED_LAYER_MASK_FIELD) != 0u)
        odm_layered_field_render(config, plan, work);
    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;
    if ((layer_mask & ODM_LAYERED_LAYER_MASK_HUD) != 0u)
        odm_layered_hud_render(config, plan, work);
    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;

    {
        odm_layered_row_context rows;
        (void)i;
        memset(&rows, 0, sizeof(rows));
        rows.worker_count = worker_count;
        rows.width = plan->width;
        rows.height = plan->height;
        rows.work = work;
        rows.stage = stage;
        rows.job_ticket = job_ticket;
        rows.phase = output_pixel_format == ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL
                   ? ODM_LAYERED_ROW_ENCODE16 : ODM_LAYERED_ROW_ENCODE8;
        rows.dither_output = (config->flags & ODM_LAYERED_FLAG_DITHER_OUTPUT) != 0u ? 1u : 0u;
        st = layered_dispatch_row_phase(&rows, worker_pool);
        if (st != ODM_STATUS_OK) return st;
    }
    if (compute_pixel_sha256) {
        st = odm_sha256(stage, frame_bytes, out_pixel_sha256);
        if (st != ODM_STATUS_OK) return st;
    }
    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;

    if (publish_frame) memcpy(frame, stage, (size_t)frame_bytes);
    else *out_stage = stage;
    return ODM_STATUS_OK;
}

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
    odm_sha256_digest *out_pixel_sha256) {
    return render_frame_impl(config, NULL, plan, core_surface, job_ticket,
                             output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             frame, frame_capacity, out_required_frame_bytes,
                             out_required_scratch_bytes, out_pixel_sha256, 1, 1, 1u, NULL, NULL);
}

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
    odm_sha256_digest *out_pixel_sha256) {
    return render_frame_impl(config, NULL, plan, core_surface, job_ticket,
                             ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL, layer_mask,
                             scratch, scratch_bytes, frame, frame_capacity,
                             out_required_frame_bytes, out_required_scratch_bytes,
                             out_pixel_sha256, 1, 1, 1u, NULL, NULL);
}

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
    uint64_t *out_required_scratch_bytes) {
    return render_frame_impl(config, NULL, plan, core_surface, job_ticket,
                             output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             frame, frame_capacity, out_required_frame_bytes,
                             out_required_scratch_bytes, NULL, 0, 1, 1u, NULL, NULL);
}

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
    uint64_t *out_required_scratch_bytes) {
    return render_frame_impl(config, NULL, plan, core_surface, job_ticket,
                             output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             NULL, 0u, out_required_frame_bytes,
                             out_required_scratch_bytes, NULL, 0, 0, 1u, NULL, out_stage);
}

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
    uint64_t *out_required_scratch_bytes) {
    if (!admission) return ODM_STATUS_INVALID_ARGUMENT;
    return render_frame_impl(config, admission, plan, core_surface, job_ticket,
                             admission->output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             NULL, 0u, out_required_frame_bytes,
                             out_required_scratch_bytes, NULL, 0, 0,
                             worker_count, NULL, out_stage);
}

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
    uint64_t *out_required_scratch_bytes) {
    return render_frame_impl(config, NULL, plan, core_surface, job_ticket,
                             output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             NULL, 0u, out_required_frame_bytes,
                             out_required_scratch_bytes, NULL, 0, 0,
                             worker_count, NULL, out_stage);
}

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
    uint64_t *out_required_scratch_bytes) {
    odm_layered_worker_pool_impl *impl = layered_pool_impl(pool);
    if (!admission || !impl) return ODM_STATUS_INVALID_ARGUMENT;
    return render_frame_impl(config, admission, plan, core_surface, job_ticket,
                             admission->output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             NULL, 0u, out_required_frame_bytes,
                             out_required_scratch_bytes, NULL, 0, 0,
                             impl->worker_count, pool, out_stage);
}

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
    uint64_t *out_required_scratch_bytes) {
    odm_layered_worker_pool_impl *impl = layered_pool_impl(pool);
    if (!impl) return ODM_STATUS_INVALID_ARGUMENT;
    return render_frame_impl(config, NULL, plan, core_surface, job_ticket,
                             output_pixel_format, ODM_LAYERED_LAYER_MASK_ALL,
                             scratch, scratch_bytes,
                             NULL, 0u, out_required_frame_bytes,
                             out_required_scratch_bytes, NULL, 0, 0,
                             impl->worker_count, pool, out_stage);
}

odm_status odm_layered_render_layer_requirements(
    const odm_layered_config *config,
    uint64_t *out_frame_bytes,
    uint64_t *out_scratch_bytes) {
    uint64_t pixels, bytes;
    odm_status st;
    if (!out_frame_bytes || !out_scratch_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    *out_frame_bytes = 0u;
    *out_scratch_bytes = 0u;
    st = odm_layered_config_validate(config);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(config->canvas.width, config->canvas.height, &pixels);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(pixels, UINT64_C(8), &bytes);
    if (st != ODM_STATUS_OK) return st;
    if (bytes > UINT64_MAX / UINT64_C(2)) return ODM_STATUS_OVERFLOW;
    *out_frame_bytes = bytes;
    *out_scratch_bytes = bytes * UINT64_C(2);
    return ODM_STATUS_OK;
}

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
    odm_sha256_digest *out_pixel_sha256) {
    uint64_t frame_bytes = 0u, required_scratch = 0u, pixels = 0u, i;
    odm_layered_pixel16 *work;
    uint8_t *stage;
    odm_status st;
    if (!out_required_frame_bytes || !out_required_scratch_bytes)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_required_frame_bytes = 0u;
    *out_required_scratch_bytes = 0u;
    st = odm_layered_render_layer_requirements(config, &frame_bytes, &required_scratch);
    *out_required_frame_bytes = frame_bytes;
    *out_required_scratch_bytes = required_scratch;
    if (st != ODM_STATUS_OK) return st;
    st = validate_plan_bound(config, plan, NULL);
    if (st != ODM_STATUS_OK) return st;
    if (layer_id < ODM_LAYERED_LAYER_BACKGROUND || layer_id > ODM_LAYERED_LAYER_HUD)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (!scratch || !frame || !out_pixel_sha256) return ODM_STATUS_INVALID_ARGUMENT;
    if (scratch_bytes < required_scratch || frame_capacity < frame_bytes)
        return ODM_STATUS_BUFFER_TOO_SMALL;
    if (((uintptr_t)scratch & (uintptr_t)7u) != 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if (ranges_overlap(scratch, required_scratch, frame, frame_bytes) ||
        ranges_overlap(scratch, required_scratch, config, sizeof(*config)) ||
        ranges_overlap(scratch, required_scratch, plan, sizeof(*plan)) ||
        ranges_overlap(frame, frame_bytes, config, sizeof(*config)) ||
        ranges_overlap(frame, frame_bytes, plan, sizeof(*plan)))
        return ODM_STATUS_INVALID_ARGUMENT;
    if (layer_id == ODM_LAYERED_LAYER_CORE) {
        if (!core_surface) return ODM_STATUS_INVALID_ARGUMENT;
        if (ranges_overlap(scratch, required_scratch, core_surface->pixels,
                           core_surface->pixel_bytes) ||
            ranges_overlap(frame, frame_bytes, core_surface->pixels,
                           core_surface->pixel_bytes) ||
            ranges_overlap(scratch, required_scratch, core_surface, sizeof(*core_surface)) ||
            ranges_overlap(frame, frame_bytes, core_surface, sizeof(*core_surface)))
            return ODM_STATUS_INVALID_ARGUMENT;
        st = odm_layered_core_surface_validate(core_surface);
        if (st != ODM_STATUS_OK) return st;
    }
    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;
    st = checked_mul_u64(config->canvas.width, config->canvas.height, &pixels);
    if (st != ODM_STATUS_OK) return st;
    work = (odm_layered_pixel16 *)scratch;
    stage = (uint8_t *)scratch + frame_bytes;
    memset(work, 0, (size_t)frame_bytes);
    switch (layer_id) {
        case ODM_LAYERED_LAYER_BACKGROUND:
            odm_layered_background_render(config, plan, work);
            break;
        case ODM_LAYERED_LAYER_CORE:
            st = odm_layered_core_render_rows(config, plan, core_surface, 0u,
                                              plan->height, work);
            if (st != ODM_STATUS_OK) return st;
            break;
        case ODM_LAYERED_LAYER_FIELD:
            odm_layered_field_render(config, plan, work);
            break;
        case ODM_LAYERED_LAYER_HUD:
            odm_layered_hud_render(config, plan, work);
            break;
        default:
            return ODM_STATUS_INVALID_ARGUMENT;
    }
    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;
    for (i = 0u; i < pixels; ++i) encode_pixel16le(&work[i], stage + i * UINT64_C(8));
    st = odm_sha256(stage, frame_bytes, out_pixel_sha256);
    if (st != ODM_STATUS_OK) return st;
    st = cancel_check(job_ticket);
    if (st != ODM_STATUS_OK) return st;
    memcpy(frame, stage, (size_t)frame_bytes);
    return ODM_STATUS_OK;
}

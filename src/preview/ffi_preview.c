#include "odm_ffi.h"

#include "odm_job.h"
#include "odm_preview.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(odm_ffi_preview_abi_info) == 128u,
               "preview ABI discovery structure must remain 128 bytes");
_Static_assert(offsetof(odm_ffi_preview_abi_info, music_analysis_tick_size) == 44u &&
               offsetof(odm_ffi_preview_abi_info, job_storage_bytes) == 48u,
               "preview ABI discovery dependent-layout offsets changed");
_Static_assert(sizeof(odm_preview_config) == 32u &&
               sizeof(odm_preview_asset_resource) == 64u &&
               sizeof(odm_preview_asset_frame) == 80u &&
               sizeof(odm_preview_plan) == 128u &&
               sizeof(odm_preview_frame_info) == 128u &&
               sizeof(odm_music_analysis_tick) == 128u &&
               sizeof(odm_progress_snapshot) == 56u,
               "preview ABI layout changed");

static odm_status odm_ffi_job_storage_mut(void *storage, uint64_t storage_bytes,
                                          odm_job **out_job) {
    if (!storage || !out_job || storage_bytes < (uint64_t)sizeof(odm_job)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (((uintptr_t)storage % (uintptr_t)_Alignof(odm_job)) != 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    *out_job = (odm_job *)storage;
    return ODM_STATUS_OK;
}

static odm_status odm_ffi_job_storage_const(const void *storage,
                                            uint64_t storage_bytes,
                                            const odm_job **out_job) {
    if (!storage || !out_job || storage_bytes < (uint64_t)sizeof(odm_job)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (((uintptr_t)storage % (uintptr_t)_Alignof(odm_job)) != 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    *out_job = (const odm_job *)storage;
    return ODM_STATUS_OK;
}

odm_status odm_ffi_preview_abi_query(uint32_t requested_schema,
                                     odm_ffi_preview_abi_info *out_info,
                                     uint64_t capacity,
                                     uint64_t *out_required) {
    odm_ffi_preview_abi_info info;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = (uint64_t)sizeof(info);
    if (requested_schema != ODM_FFI_PREVIEW_ABI_SCHEMA_VERSION) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    if (!out_info || capacity < (uint64_t)sizeof(info)) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.schema_version = ODM_FFI_PREVIEW_ABI_SCHEMA_VERSION;
    info.ffi_abi_version = ODM_FFI_ABI_VERSION;
    info.endian_marker = ODM_FFI_ENDIAN_MARKER;
    info.preview_config_size = (uint32_t)sizeof(odm_preview_config);
    info.preview_asset_resource_size = (uint32_t)sizeof(odm_preview_asset_resource);
    info.preview_asset_frame_size = (uint32_t)sizeof(odm_preview_asset_frame);
    info.preview_plan_size = (uint32_t)sizeof(odm_preview_plan);
    info.preview_frame_info_size = (uint32_t)sizeof(odm_preview_frame_info);
    info.progress_snapshot_size = (uint32_t)sizeof(odm_progress_snapshot);
    info.job_storage_alignment = (uint32_t)_Alignof(odm_job);
    info.music_analysis_tick_size = (uint32_t)sizeof(odm_music_analysis_tick);
    info.job_storage_bytes = (uint64_t)sizeof(odm_job);
    info.feature_bits = ODM_FFI_PREVIEW_FEATURE_EXACT_SEMANTICS |
                        ODM_FFI_PREVIEW_FEATURE_RASTER_CLASSES |
                        ODM_FFI_PREVIEW_FEATURE_JOB_PROGRESS |
                        ODM_FFI_PREVIEW_FEATURE_CANCELLATION |
                        ODM_FFI_PREVIEW_FEATURE_FLAT_ASSETS |
                        ODM_FFI_PREVIEW_FEATURE_RGBA8_SRGB;
    *out_info = info;
    return ODM_STATUS_OK;
}

odm_status odm_ffi_job_init(void *storage, uint64_t storage_bytes) {
    odm_job *job;
    odm_status status = odm_ffi_job_storage_mut(storage, storage_bytes, &job);
    return status == ODM_STATUS_OK ? odm_job_init(job) : status;
}

odm_status odm_ffi_job_begin(void *storage, uint64_t storage_bytes,
                             uint64_t work_total, uint64_t *out_generation) {
    odm_job *job;
    odm_job_ticket ticket;
    odm_status status;
    if (!out_generation) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_ffi_job_storage_mut(storage, storage_bytes, &job);
    if (status != ODM_STATUS_OK) return status;
    status = odm_job_begin(job, work_total, &ticket);
    if (status != ODM_STATUS_OK) return status;
    *out_generation = ticket.generation;
    return ODM_STATUS_OK;
}

odm_status odm_ffi_job_request_cancel(void *storage, uint64_t storage_bytes) {
    odm_job *job;
    odm_status status = odm_ffi_job_storage_mut(storage, storage_bytes, &job);
    return status == ODM_STATUS_OK ? odm_job_request_cancel(job) : status;
}

odm_status odm_ffi_job_snapshot(const void *storage, uint64_t storage_bytes,
                                odm_progress_snapshot *out_snapshot) {
    const odm_job *job;
    odm_status status = odm_ffi_job_storage_const(storage, storage_bytes, &job);
    return status == ODM_STATUS_OK ? odm_job_snapshot(job, out_snapshot) : status;
}

odm_status odm_ffi_job_finish(void *storage, uint64_t storage_bytes,
                              uint64_t generation, odm_status result_status) {
    odm_job *job;
    odm_job_ticket ticket;
    odm_status status = odm_ffi_job_storage_mut(storage, storage_bytes, &job);
    if (status != ODM_STATUS_OK) return status;
    ticket.job = job;
    ticket.generation = generation;
    return odm_job_finish(ticket, result_status);
}

odm_status odm_ffi_job_destroy(void *storage, uint64_t storage_bytes) {
    odm_job *job;
    odm_status status = odm_ffi_job_storage_mut(storage, storage_bytes, &job);
    return status == ODM_STATUS_OK ? odm_job_destroy(job) : status;
}

odm_status odm_ffi_preview_plan_frame(
    const uint8_t *render_ir, uint64_t render_ir_bytes, int64_t frame_index,
    const odm_music_analysis_tick *music_tick, const odm_preview_config *config,
    uint64_t resource_count, uint64_t frame_count, odm_preview_plan *out_plan) {
    return odm_preview_plan_frame(render_ir, render_ir_bytes, frame_index,
                                  music_tick, config, resource_count, frame_count,
                                  out_plan);
}

odm_status odm_ffi_preview_render_frame(
    const uint8_t *render_ir, uint64_t render_ir_bytes, int64_t frame_index,
    const odm_music_analysis_tick *music_tick, const odm_preview_config *config,
    const odm_preview_asset_resource *resources, uint64_t resource_count,
    const odm_preview_asset_frame *frames, uint64_t frame_count,
    const uint8_t *pixel_blob, uint64_t pixel_blob_bytes,
    void *job_storage, uint64_t job_storage_bytes, uint64_t job_generation,
    void *scratch, uint64_t scratch_bytes, uint8_t *rgba8,
    uint64_t rgba8_capacity, uint64_t *out_required_rgba8,
    uint64_t *out_required_scratch, odm_preview_frame_info *out_info) {
    odm_job *job = NULL;
    odm_job_ticket ticket;
    const odm_job_ticket *ticket_ptr = NULL;
    odm_status status;
    if (!job_storage) {
        if (job_storage_bytes != 0u || job_generation != 0u) {
            return ODM_STATUS_INVALID_ARGUMENT;
        }
    } else {
        status = odm_ffi_job_storage_mut(job_storage, job_storage_bytes, &job);
        if (status != ODM_STATUS_OK) return status;
        ticket.job = job;
        ticket.generation = job_generation;
        ticket_ptr = &ticket;
    }
    return odm_preview_render_frame(
        render_ir, render_ir_bytes, frame_index, music_tick, config,
        resources, resource_count, frames, frame_count, pixel_blob,
        pixel_blob_bytes, ticket_ptr, scratch, scratch_bytes, rgba8,
        rgba8_capacity, out_required_rgba8, out_required_scratch, out_info);
}

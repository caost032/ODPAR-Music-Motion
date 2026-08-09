#ifndef ODM_FFI_H
#define ODM_FFI_H

#include "odm_preview.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_FFI_ABI_VERSION 1u
#define ODM_FFI_ENDIAN_MARKER UINT32_C(0x01020304)
#define ODM_FFI_FEATURE_SHA256       (UINT64_C(1) << 0)
#define ODM_FFI_FEATURE_WIRE_RECORDS (UINT64_C(1) << 1)
#define ODM_FFI_FEATURE_ADMISSION    (UINT64_C(1) << 2)
#define ODM_FFI_FEATURE_SPINE_JSON   (UINT64_C(1) << 3)
#define ODM_FFI_FEATURE_EXECUTOR     (UINT64_C(1) << 4)
#define ODM_FFI_FEATURE_PREVIEW      (UINT64_C(1) << 5)
#define ODM_FFI_FEATURE_JOB_STORAGE  (UINT64_C(1) << 6)
#define ODM_FFI_FEATURE_SHARED_LIB   (UINT64_C(1) << 7)

/* ABI v1 freezes this structure at 64 bytes.  All output is copied; it contains
 * no internal pointers, native enums, size_t or floating-point fields. */
typedef struct {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t endian_marker;
    uint32_t status_size;
    uint32_t digest_size;
    uint32_t progress_snapshot_size;
    uint32_t wire_record_info_size;
    uint32_t resource_limits_size;
    uint32_t resource_request_size;
    uint32_t admission_result_size;
    uint32_t executor_config_size;
    uint32_t executor_task_handle_size;
    uint64_t feature_bits;
    uint64_t reserved_u64_0;
} odm_ffi_abi_info;

#define ODM_FFI_PREVIEW_ABI_SCHEMA_VERSION 1u
#define ODM_FFI_PREVIEW_FEATURE_EXACT_SEMANTICS (UINT64_C(1) << 0)
#define ODM_FFI_PREVIEW_FEATURE_RASTER_CLASSES  (UINT64_C(1) << 1)
#define ODM_FFI_PREVIEW_FEATURE_JOB_PROGRESS    (UINT64_C(1) << 2)
#define ODM_FFI_PREVIEW_FEATURE_CANCELLATION    (UINT64_C(1) << 3)
#define ODM_FFI_PREVIEW_FEATURE_FLAT_ASSETS     (UINT64_C(1) << 4)
#define ODM_FFI_PREVIEW_FEATURE_RGBA8_SRGB      (UINT64_C(1) << 5)

typedef struct {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t ffi_abi_version;
    uint32_t endian_marker;
    uint32_t preview_config_size;
    uint32_t preview_asset_resource_size;
    uint32_t preview_asset_frame_size;
    uint32_t preview_plan_size;
    uint32_t preview_frame_info_size;
    uint32_t progress_snapshot_size;
    uint32_t job_storage_alignment;
    uint32_t music_analysis_tick_size;
    uint64_t job_storage_bytes;
    uint64_t feature_bits;
    uint64_t reserved_u64[8];
} odm_ffi_preview_abi_info;

odm_status odm_ffi_abi_query(uint32_t requested_abi,
                             odm_ffi_abi_info *out_info,
                             uint64_t capacity,
                             uint64_t *out_required);
odm_status odm_ffi_status_name(odm_status status, char *buffer,
                               uint64_t capacity, uint64_t *out_required);
odm_status odm_ffi_sha256(const uint8_t *data, uint64_t length,
                          uint8_t *buffer, uint64_t capacity,
                          uint64_t *out_required);
odm_status odm_ffi_spine_report_json(char *buffer, uint64_t capacity,
                                     uint64_t *out_required);
odm_status odm_ffi_spine_summary_json(char *buffer, uint64_t capacity,
                                      uint64_t *out_required);

odm_status odm_ffi_preview_abi_query(uint32_t requested_schema,
                                     odm_ffi_preview_abi_info *out_info,
                                     uint64_t capacity,
                                     uint64_t *out_required);
odm_status odm_ffi_job_init(void *storage, uint64_t storage_bytes);
odm_status odm_ffi_job_begin(void *storage, uint64_t storage_bytes,
                             uint64_t work_total, uint64_t *out_generation);
odm_status odm_ffi_job_request_cancel(void *storage, uint64_t storage_bytes);
odm_status odm_ffi_job_snapshot(const void *storage, uint64_t storage_bytes,
                                odm_progress_snapshot *out_snapshot);
odm_status odm_ffi_job_finish(void *storage, uint64_t storage_bytes,
                              uint64_t generation, odm_status result_status);
odm_status odm_ffi_job_destroy(void *storage, uint64_t storage_bytes);
odm_status odm_ffi_preview_plan_frame(
    const uint8_t *render_ir, uint64_t render_ir_bytes, int64_t frame_index,
    const odm_music_analysis_tick *music_tick, const odm_preview_config *config,
    uint64_t resource_count, uint64_t frame_count, odm_preview_plan *out_plan);
odm_status odm_ffi_preview_render_frame(
    const uint8_t *render_ir, uint64_t render_ir_bytes, int64_t frame_index,
    const odm_music_analysis_tick *music_tick, const odm_preview_config *config,
    const odm_preview_asset_resource *resources, uint64_t resource_count,
    const odm_preview_asset_frame *frames, uint64_t frame_count,
    const uint8_t *pixel_blob, uint64_t pixel_blob_bytes,
    void *job_storage, uint64_t job_storage_bytes, uint64_t job_generation,
    void *scratch, uint64_t scratch_bytes, uint8_t *rgba8,
    uint64_t rgba8_capacity, uint64_t *out_required_rgba8,
    uint64_t *out_required_scratch, odm_preview_frame_info *out_info);

#ifdef __cplusplus
}
#endif

#endif /* ODM_FFI_H */

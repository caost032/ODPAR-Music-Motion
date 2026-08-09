#include "odm_ffi.h"

#include "odm_hash.h"
#include "odm_executor.h"
#include "odm_limits.h"
#include "odm_progress.h"
#include "odm_spine.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(odm_status) == 4u, "odm_status ABI must be 32-bit");
_Static_assert(sizeof(odm_sha256_digest) == 32u,
               "SHA-256 digest ABI must be 32 bytes");
_Static_assert(sizeof(odm_ffi_abi_info) == 64u,
               "ABI discovery structure must remain 64 bytes");
_Static_assert(offsetof(odm_ffi_abi_info, feature_bits) == 48u,
               "ABI discovery feature offset changed");
_Static_assert(sizeof(odm_progress_snapshot) == 56u,
               "progress snapshot ABI changed");
_Static_assert(offsetof(odm_progress_snapshot, generation) == 32u,
               "progress snapshot ABI alignment changed");
_Static_assert(sizeof(odm_wire_record_info) == 64u,
               "wire info ABI changed");
_Static_assert(offsetof(odm_wire_record_info, payload_bytes) == 24u &&
               offsetof(odm_wire_record_info, payload_sha256) == 32u,
               "wire info ABI offsets changed");
_Static_assert(sizeof(odm_resource_limits) == 56u &&
               sizeof(odm_resource_request) == 56u &&
               sizeof(odm_admission_result) == 32u,
               "admission ABI changed");
_Static_assert(sizeof(odm_executor_config) == 16u &&
               sizeof(odm_executor_task_handle) == 16u &&
               sizeof(odm_executor_snapshot) == 64u &&
               sizeof(odm_executor_task_snapshot) == 32u,
               "executor ABI changed");

static int odm_ffi_u64_to_size(uint64_t value, size_t *out_value) {
#if SIZE_MAX < UINT64_MAX
    if (value > (uint64_t)SIZE_MAX) return 0;
#endif
    *out_value = (size_t)value;
    return 1;
}

odm_status odm_ffi_abi_query(uint32_t requested_abi,
                             odm_ffi_abi_info *out_info,
                             uint64_t capacity,
                             uint64_t *out_required) {
    odm_ffi_abi_info info;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = (uint64_t)sizeof(info);
    if (requested_abi != ODM_FFI_ABI_VERSION) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    if (!out_info || capacity < (uint64_t)sizeof(info)) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.abi_version = ODM_FFI_ABI_VERSION;
    info.endian_marker = ODM_FFI_ENDIAN_MARKER;
    info.status_size = (uint32_t)sizeof(odm_status);
    info.digest_size = (uint32_t)sizeof(odm_sha256_digest);
    info.progress_snapshot_size = (uint32_t)sizeof(odm_progress_snapshot);
    info.wire_record_info_size = (uint32_t)sizeof(odm_wire_record_info);
    info.resource_limits_size = (uint32_t)sizeof(odm_resource_limits);
    info.resource_request_size = (uint32_t)sizeof(odm_resource_request);
    info.admission_result_size = (uint32_t)sizeof(odm_admission_result);
    info.executor_config_size = (uint32_t)sizeof(odm_executor_config);
    info.executor_task_handle_size =
        (uint32_t)sizeof(odm_executor_task_handle);
    info.feature_bits = ODM_FFI_FEATURE_SHA256 |
                        ODM_FFI_FEATURE_WIRE_RECORDS |
                        ODM_FFI_FEATURE_ADMISSION |
                        ODM_FFI_FEATURE_SPINE_JSON |
                        ODM_FFI_FEATURE_EXECUTOR;
    *out_info = info;
    return ODM_STATUS_OK;
}

odm_status odm_ffi_status_name(odm_status status, char *buffer,
                               uint64_t capacity, uint64_t *out_required) {
    const char *name;
    size_t length;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    name = odm_status_name(status);
    length = strlen(name) + 1u;
    *out_required = (uint64_t)length;
    if (!buffer || capacity < (uint64_t)length) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, name, length);
    return ODM_STATUS_OK;
}

odm_status odm_ffi_sha256(const uint8_t *data, uint64_t length,
                          uint8_t *buffer, uint64_t capacity,
                          uint64_t *out_required) {
    odm_sha256_digest digest;
    odm_status status;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_SHA256_DIGEST_BYTES;
    if (length != 0u && !data) return ODM_STATUS_INVALID_ARGUMENT;
    if (!buffer || capacity < ODM_SHA256_DIGEST_BYTES) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    status = odm_sha256(data, length, &digest);
    if (status != ODM_STATUS_OK) return status;
    memcpy(buffer, digest.bytes, sizeof(digest.bytes));
    return ODM_STATUS_OK;
}

static odm_status odm_ffi_spine_json(int summary, char *buffer,
                                     uint64_t capacity,
                                     uint64_t *out_required) {
    size_t native_capacity;
    size_t native_required = 0u;
    odm_status status;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_ffi_u64_to_size(capacity, &native_capacity)) {
        return ODM_STATUS_OVERFLOW;
    }
    status = summary ?
        odm_spine_summary_json(buffer, native_capacity, &native_required) :
        odm_spine_report_json(buffer, native_capacity, &native_required);
    *out_required = (uint64_t)native_required;
    return status;
}

odm_status odm_ffi_spine_report_json(char *buffer, uint64_t capacity,
                                     uint64_t *out_required) {
    return odm_ffi_spine_json(0, buffer, capacity, out_required);
}

odm_status odm_ffi_spine_summary_json(char *buffer, uint64_t capacity,
                                      uint64_t *out_required) {
    return odm_ffi_spine_json(1, buffer, capacity, out_required);
}

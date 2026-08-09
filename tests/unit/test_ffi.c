#include "test_harness.h"

#include "odm_ffi.h"
#include "odm_hash.h"

#include <stdlib.h>
#include <string.h>

void odm_test_ffi(odm_test_context *context) {
    {
        odm_ffi_abi_info info;
        odm_ffi_abi_info sentinel;
        uint64_t required = 0u;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        info = sentinel;
        ODM_TEST_CHECK(context,
                       odm_ffi_abi_query(ODM_FFI_ABI_VERSION, NULL, 0u,
                                         &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == 64u);
        ODM_TEST_CHECK(context,
                       odm_ffi_abi_query(ODM_FFI_ABI_VERSION, &info,
                                         required - 1u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context,
                       memcmp(&info, &sentinel, sizeof(info)) == 0);
        ODM_TEST_CHECK(context,
                       odm_ffi_abi_query(ODM_FFI_ABI_VERSION + 1u, &info,
                                         sizeof(info), &required) ==
                           ODM_STATUS_VERSION_MISMATCH);
        ODM_TEST_CHECK(context,
                       odm_ffi_abi_query(ODM_FFI_ABI_VERSION, &info,
                                         sizeof(info), &required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, info.struct_size == 64u);
        ODM_TEST_CHECK(context, info.abi_version == ODM_FFI_ABI_VERSION);
        ODM_TEST_CHECK(context, info.endian_marker == ODM_FFI_ENDIAN_MARKER);
        ODM_TEST_CHECK(context, info.status_size == 4u);
        ODM_TEST_CHECK(context, info.digest_size == ODM_SHA256_DIGEST_BYTES);
        ODM_TEST_CHECK(context,
                       (info.feature_bits & ODM_FFI_FEATURE_SHA256) != 0u);
        ODM_TEST_CHECK(context, info.executor_config_size == 16u &&
                                info.executor_task_handle_size == 16u &&
                                info.reserved_u64_0 == 0u);
        ODM_TEST_CHECK(context,
                       (info.feature_bits & ODM_FFI_FEATURE_EXECUTOR) != 0u);
    }
    {
        char name[32];
        uint64_t required = 0u;
        memset(name, 'x', sizeof(name));
        ODM_TEST_CHECK(context,
                       odm_ffi_status_name(ODM_STATUS_INTEGRITY_ERROR, NULL, 0u,
                                           &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == strlen("integrity_error") + 1u);
        ODM_TEST_CHECK(context,
                       odm_ffi_status_name(ODM_STATUS_INTEGRITY_ERROR, name,
                                           required - 1u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, name[0] == 'x');
        ODM_TEST_CHECK(context,
                       odm_ffi_status_name(ODM_STATUS_INTEGRITY_ERROR, name,
                                           sizeof(name), &required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, strcmp(name, "integrity_error") == 0);
    }
    {
        uint8_t digest[ODM_SHA256_DIGEST_BYTES];
        uint8_t sentinel[ODM_SHA256_DIGEST_BYTES];
        odm_sha256_digest core_digest;
        uint64_t required = 0u;
        memset(sentinel, 0xa5, sizeof(sentinel));
        memcpy(digest, sentinel, sizeof(digest));
        ODM_TEST_CHECK(context,
                       odm_ffi_sha256((const uint8_t *)"abc", 3u, digest,
                                      sizeof(digest) - 1u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context,
                       memcmp(digest, sentinel, sizeof(digest)) == 0);
        ODM_TEST_CHECK(context,
                       odm_ffi_sha256(NULL, 1u, digest, sizeof(digest),
                                      &required) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_ffi_sha256((const uint8_t *)"abc", 3u, digest,
                                      sizeof(digest), &required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_sha256("abc", 3u, &core_digest) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       memcmp(digest, core_digest.bytes, sizeof(digest)) == 0);
    }
    {
        uint64_t required = 0u;
        char *json;
        ODM_TEST_CHECK(context,
                       odm_ffi_spine_summary_json(NULL, 0u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required > 100u);
        json = (char *)malloc((size_t)required);
        ODM_TEST_CHECK(context, json != NULL);
        if (json) {
            uint64_t second_required = 0u;
            ODM_TEST_CHECK(context,
                           odm_ffi_spine_summary_json(json, required,
                                                      &second_required) ==
                               ODM_STATUS_OK);
            ODM_TEST_CHECK(context, second_required == required);
            ODM_TEST_CHECK(context,
                           strstr(json, "odm_spine_summary_v1") != NULL);
            free(json);
        }
    }
}

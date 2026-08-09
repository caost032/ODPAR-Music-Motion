#include "selftest.h"

#include "odm_fixed.h"
#include "odm_executor.h"
#include "odm_ffi.h"
#include "odm_hash.h"
#include "odm_job.h"
#include "odm_limits.h"
#include "odm_memory.h"
#include "odm_rng.h"
#include "odm_spine.h"
#include "odm_time.h"
#include "odm_wire.h"

#include <stdint.h>
#include <string.h>

static odm_status odm_selftest_fail(odm_selftest_result *result,
                                    const char *name, odm_status status) {
    result->failed_check = name;
    result->failure_status = status;
    return status == ODM_STATUS_OK ? ODM_STATUS_INVARIANT_BROKEN : status;
}

#define ODM_SELFTEST_CHECK(result, name, expression) do { \
    odm_status odm_selftest_status_ = (expression); \
    (result)->checks++; \
    if (odm_selftest_status_ != ODM_STATUS_OK) \
        return odm_selftest_fail((result), (name), odm_selftest_status_); \
    (result)->passed++; \
} while (0)

static odm_status odm_selftest_status_contract(void) {
    if (!odm_status_is_ok(ODM_STATUS_OK) ||
        strcmp(odm_status_name(ODM_STATUS_OVERFLOW), "overflow") != 0 ||
        strcmp(odm_status_name((odm_status)9999), "unknown_status") != 0) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_time_contract(void) {
    odm_timeline timeline;
    int64_t sample = 0;
    if (odm_time_frame_start(30, 30, &sample) != ODM_STATUS_OK ||
        sample != INT64_C(48000)) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    if (odm_time_resolve_default(INT64_C(48000), INT64_C(1601), 30,
                                 &timeline) != ODM_STATUS_OK ||
        timeline.frame_count != INT64_C(32) ||
        timeline.output_end_sample != INT64_C(51200) ||
        timeline.silence_pad_samples != INT64_C(3200)) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_fixed_contract(void) {
    odm_q32_32 value = 0;
    odm_microunit round_trip = 0;
    if (odm_q32_32_from_microunits(INT64_C(500000), &value) != ODM_STATUS_OK ||
        value != INT64_C(2147483648) ||
        odm_q32_32_to_microunits(value, &round_trip) != ODM_STATUS_OK ||
        round_trip != INT64_C(500000)) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_rng_contract(void) {
    odm_rng rng;
    uint32_t first = 0u;
    uint32_t second = 0u;
    if (odm_rng_seed(&rng, UINT64_C(42), UINT64_C(54)) != ODM_STATUS_OK ||
        odm_rng_next_u32(&rng, &first) != ODM_STATUS_OK ||
        odm_rng_next_u32(&rng, &second) != ODM_STATUS_OK ||
        first != UINT32_C(0xa15c02b7) ||
        second != UINT32_C(0x7b47f409)) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_memory_contract(void) {
    odm_memory_budget budget;
    odm_memory_budget_snapshot snapshot;
    odm_arena arena = ODM_ARENA_INITIALIZER;
    odm_allocator allocator = odm_allocator_default();
    void *memory = NULL;
    odm_memory_budget_init(&budget, UINT64_C(64));
    if (odm_arena_init(&arena, 32u, allocator, &budget) != ODM_STATUS_OK ||
        odm_arena_allocate(&arena, 16u, 8u, &memory) != ODM_STATUS_OK ||
        !memory || odm_arena_destroy(&arena) != ODM_STATUS_OK ||
        odm_memory_budget_read(&budget, &snapshot) != ODM_STATUS_OK ||
        snapshot.current_bytes != 0u || snapshot.peak_bytes != 32u) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_job_contract(void) {
    odm_job job;
    odm_job_ticket ticket;
    odm_progress_snapshot snapshot;
    if (odm_job_init(&job) != ODM_STATUS_OK ||
        odm_job_begin(&job, UINT64_C(100), &ticket) != ODM_STATUS_OK ||
        odm_job_publish(ticket, ODM_PHASE_EXECUTE, UINT64_C(25)) != ODM_STATUS_OK ||
        odm_job_finish(ticket, ODM_STATUS_OK) != ODM_STATUS_OK ||
        odm_job_snapshot(&job, &snapshot) != ODM_STATUS_OK ||
        snapshot.state != ODM_JOB_SUCCEEDED ||
        snapshot.progress_millionths != ODM_PROGRESS_ONE ||
        odm_job_destroy(&job) != ODM_STATUS_OK) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_hash_contract(void) {
    static const uint8_t expected[ODM_SHA256_DIGEST_BYTES] = {
        0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
        0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
        0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
        0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu
    };
    odm_sha256_digest digest;
    if (odm_sha256("abc", 3u, &digest) != ODM_STATUS_OK ||
        memcmp(digest.bytes, expected, sizeof(expected)) != 0) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_wire_contract(void) {
    uint8_t record[67];
    uint64_t required = 0u;
    uint64_t offset = 0u;
    odm_wire_record_info info;
    if (odm_wire_record_write(1u, 1u, 0u, (const uint8_t *)"abc", 3u,
                              record, sizeof(record), &required, NULL) !=
            ODM_STATUS_OK ||
        required != sizeof(record) ||
        odm_wire_record_read(record, required, &info, &offset, NULL) !=
            ODM_STATUS_OK ||
        info.payload_bytes != 3u || offset != ODM_WIRE_RECORD_HEADER_BYTES) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_limits_contract(void) {
    odm_resource_limits limits;
    odm_resource_request request;
    odm_admission_result result;
    memset(&request, 0, sizeof(request));
    request.schema_version = ODM_RESOURCE_LIMITS_SCHEMA_VERSION;
    request.workers = 1u;
    request.jobs = 1u;
    if (odm_resource_limits_default(&limits) != ODM_STATUS_OK ||
        odm_resource_admit(&limits, &request, &result) != ODM_STATUS_OK ||
        result.verdict != ODM_ADMISSION_ACCEPT) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_ffi_contract(void) {
    odm_ffi_abi_info info;
    uint64_t required = 0u;
    if (odm_ffi_abi_query(ODM_FFI_ABI_VERSION, &info, sizeof(info),
                          &required) != ODM_STATUS_OK ||
        required != sizeof(info) || info.struct_size != sizeof(info) ||
        info.abi_version != ODM_FFI_ABI_VERSION) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_selftest_executor_task(void *context,
                                             odm_job_ticket ticket) {
    uint32_t *called = (uint32_t *)context;
    *called = 1u;
    return odm_job_publish(ticket, ODM_PHASE_EXECUTE, 1u);
}

static odm_status odm_selftest_executor_contract(void) {
    odm_executor_config config = {
        ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 1u, 1u
    };
    odm_executor *executor = NULL;
    odm_executor_task_handle handle;
    odm_job job;
    odm_status task_status = ODM_STATUS_INTERNAL_ERROR;
    uint32_t called = 0u;
    int job_initialized = 0;
    if (odm_job_init(&job) != ODM_STATUS_OK) goto fail;
    job_initialized = 1;
    if (odm_executor_create(&config, odm_allocator_default(), NULL,
                            &executor) != ODM_STATUS_OK) goto fail;
    if (odm_executor_submit(executor, odm_selftest_executor_task, &called,
                            &job, 1u, &handle) != ODM_STATUS_OK) goto fail;
    if (odm_executor_wait(executor, handle, &task_status) != ODM_STATUS_OK ||
        task_status != ODM_STATUS_OK || called != 1u) goto fail;
    {
        odm_status destroy_status = odm_executor_destroy(executor, NULL);
        executor = NULL;
        if (destroy_status != ODM_STATUS_OK) goto fail;
    }
    if (odm_job_destroy(&job) != ODM_STATUS_OK) goto fail;
    return ODM_STATUS_OK;
fail:
    if (executor) (void)odm_executor_destroy(executor, NULL);
    if (job_initialized) (void)odm_job_destroy(&job);
    return ODM_STATUS_INVARIANT_BROKEN;
}

static odm_status odm_selftest_spine_contract(void) {
    odm_spine_summary summary;
    if (odm_spine_validate(&summary) != ODM_STATUS_OK ||
        summary.module_count == 0u || summary.invariant_count == 0u ||
        summary.graph_acyclic != 1u) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

odm_status odm_selftest_run(odm_selftest_result *out_result) {
    if (!out_result) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out_result, 0, sizeof(*out_result));
    ODM_SELFTEST_CHECK(out_result, "status", odm_selftest_status_contract());
    ODM_SELFTEST_CHECK(out_result, "time", odm_selftest_time_contract());
    ODM_SELFTEST_CHECK(out_result, "fixed", odm_selftest_fixed_contract());
    ODM_SELFTEST_CHECK(out_result, "rng", odm_selftest_rng_contract());
    ODM_SELFTEST_CHECK(out_result, "memory", odm_selftest_memory_contract());
    ODM_SELFTEST_CHECK(out_result, "job", odm_selftest_job_contract());
    ODM_SELFTEST_CHECK(out_result, "hash", odm_selftest_hash_contract());
    ODM_SELFTEST_CHECK(out_result, "wire", odm_selftest_wire_contract());
    ODM_SELFTEST_CHECK(out_result, "limits", odm_selftest_limits_contract());
    ODM_SELFTEST_CHECK(out_result, "ffi", odm_selftest_ffi_contract());
    ODM_SELFTEST_CHECK(out_result, "executor", odm_selftest_executor_contract());
    ODM_SELFTEST_CHECK(out_result, "spine", odm_selftest_spine_contract());
    return ODM_STATUS_OK;
}

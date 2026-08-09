#define _POSIX_C_SOURCE 200809L

#include "odm_executor.h"
#include "odm_hash.h"
#include "odm_wire.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ODM_BENCH_HASH_BYTES (UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024))
#define ODM_BENCH_HASH_ROUNDS UINT64_C(16)
#define ODM_BENCH_WIRE_BYTES (UINT64_C(8) * UINT64_C(1024) * UINT64_C(1024))
#define ODM_BENCH_WIRE_ROUNDS UINT64_C(8)
#define ODM_BENCH_TASK_CAPACITY UINT32_C(4096)
#define ODM_BENCH_TASK_TOTAL UINT64_C(100000)
#define ODM_BENCH_MIN_SHA_BYTES_PER_SECOND UINT64_C(100000000)
#define ODM_BENCH_MIN_WIRE_BYTES_PER_SECOND UINT64_C(50000000)
#define ODM_BENCH_MIN_TASKS_PER_SECOND UINT64_C(100000)

static int odm_bench_now(uint64_t *out_nanoseconds) {
    struct timespec value;
    uint64_t seconds;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 ||
        value.tv_sec < 0 || value.tv_nsec < 0 ||
        value.tv_nsec >= 1000000000L) {
        return 0;
    }
    seconds = (uint64_t)value.tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)value.tv_nsec) /
                      UINT64_C(1000000000)) {
        return 0;
    }
    *out_nanoseconds = seconds * UINT64_C(1000000000) +
                       (uint64_t)value.tv_nsec;
    return 1;
}

static uint64_t odm_bench_rate(uint64_t units, uint64_t nanoseconds) {
    if (nanoseconds == 0u || units > UINT64_MAX / UINT64_C(1000000000)) {
        return 0u;
    }
    return units * UINT64_C(1000000000) / nanoseconds;
}

static odm_status odm_bench_noop(void *context, odm_job_ticket ticket) {
    (void)context;
    (void)ticket;
    return ODM_STATUS_OK;
}

int main(void) {
    uint8_t *hash_data = NULL;
    uint8_t *wire_record = NULL;
    odm_sha256_digest digest;
    odm_executor *executor = NULL;
    odm_job *jobs = NULL;
    odm_executor_task_handle *handles = NULL;
    odm_executor_config config = {
        ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 4u,
        ODM_BENCH_TASK_CAPACITY
    };
    odm_executor_snapshot executor_snapshot;
    uint64_t hash_start;
    uint64_t hash_end;
    uint64_t wire_start;
    uint64_t wire_end;
    uint64_t executor_start;
    uint64_t executor_end;
    uint64_t required = 0u;
    uint64_t round;
    uint64_t submitted = 0u;
    uint64_t sha_rate;
    uint64_t wire_rate;
    uint64_t task_rate;
    uint32_t index;
    int exit_code = 1;

    hash_data = (uint8_t *)malloc((size_t)ODM_BENCH_HASH_BYTES);
    wire_record = (uint8_t *)malloc((size_t)(ODM_BENCH_WIRE_BYTES +
                                             ODM_WIRE_RECORD_HEADER_BYTES));
    jobs = (odm_job *)calloc((size_t)ODM_BENCH_TASK_CAPACITY, sizeof(*jobs));
    handles = (odm_executor_task_handle *)malloc(
        (size_t)ODM_BENCH_TASK_CAPACITY * sizeof(*handles));
    if (!hash_data || !wire_record || !jobs || !handles) goto cleanup;
    for (round = 0u; round < ODM_BENCH_HASH_BYTES; round++) {
        hash_data[round] = (uint8_t)(round * UINT64_C(1315423911) +
                                     UINT64_C(0x5a));
    }

    if (!odm_bench_now(&hash_start)) goto cleanup;
    for (round = 0u; round < ODM_BENCH_HASH_ROUNDS; round++) {
        if (odm_sha256(hash_data, ODM_BENCH_HASH_BYTES, &digest) !=
            ODM_STATUS_OK) goto cleanup;
    }
    if (!odm_bench_now(&hash_end) || hash_end <= hash_start) goto cleanup;

    if (!odm_bench_now(&wire_start)) goto cleanup;
    for (round = 0u; round < ODM_BENCH_WIRE_ROUNDS; round++) {
        if (odm_wire_record_write(1u, 1u, 0u, hash_data,
                                  ODM_BENCH_WIRE_BYTES, wire_record,
                                  ODM_BENCH_WIRE_BYTES +
                                      ODM_WIRE_RECORD_HEADER_BYTES,
                                  &required, &digest) != ODM_STATUS_OK ||
            required != ODM_BENCH_WIRE_BYTES +
                            ODM_WIRE_RECORD_HEADER_BYTES) goto cleanup;
    }
    if (!odm_bench_now(&wire_end) || wire_end <= wire_start) goto cleanup;

    for (index = 0u; index < ODM_BENCH_TASK_CAPACITY; index++) {
        if (odm_job_init(&jobs[index]) != ODM_STATUS_OK) goto cleanup;
    }
    if (odm_executor_create(&config, odm_allocator_default(), NULL,
                            &executor) != ODM_STATUS_OK) goto cleanup;
    if (!odm_bench_now(&executor_start)) goto cleanup;
    while (submitted < ODM_BENCH_TASK_TOTAL) {
        uint64_t remaining = ODM_BENCH_TASK_TOTAL - submitted;
        uint32_t batch = remaining < ODM_BENCH_TASK_CAPACITY ?
            (uint32_t)remaining : ODM_BENCH_TASK_CAPACITY;
        for (index = 0u; index < batch; index++) {
            if (odm_executor_submit(executor, odm_bench_noop, NULL,
                                    &jobs[index], 0u, &handles[index]) !=
                ODM_STATUS_OK) goto cleanup;
        }
        for (index = 0u; index < batch; index++) {
            odm_status task_status = ODM_STATUS_INTERNAL_ERROR;
            if (odm_executor_wait(executor, handles[index], &task_status) !=
                    ODM_STATUS_OK || task_status != ODM_STATUS_OK) goto cleanup;
        }
        submitted += batch;
    }
    if (!odm_bench_now(&executor_end) || executor_end <= executor_start ||
        odm_executor_read(executor, &executor_snapshot) != ODM_STATUS_OK) {
        goto cleanup;
    }
    sha_rate = odm_bench_rate(ODM_BENCH_HASH_BYTES * ODM_BENCH_HASH_ROUNDS,
                              hash_end - hash_start);
    wire_rate = odm_bench_rate(ODM_BENCH_WIRE_BYTES * ODM_BENCH_WIRE_ROUNDS,
                               wire_end - wire_start);
    task_rate = odm_bench_rate(ODM_BENCH_TASK_TOTAL,
                               executor_end - executor_start);
    printf(
        "{\"schema\":\"odm_core_benchmark_v1\","
        "\"measurement_only\":false,"
        "\"thresholds\":{\"sha256_bytes_per_second\":%" PRIu64
        ",\"wire_payload_bytes_per_second\":%" PRIu64
        ",\"executor_tasks_per_second\":%" PRIu64 "},"
        "\"sha256\":{\"bytes\":%" PRIu64 ",\"nanoseconds\":%" PRIu64
        ",\"bytes_per_second\":%" PRIu64 "},"
        "\"wire_record\":{\"payload_bytes\":%" PRIu64
        ",\"nanoseconds\":%" PRIu64 ",\"payload_bytes_per_second\":%" PRIu64
        "},\"executor\":{\"workers\":%" PRIu32
        ",\"tasks\":%" PRIu64 ",\"nanoseconds\":%" PRIu64
        ",\"tasks_per_second\":%" PRIu64
        ",\"allocation_bytes\":%" PRIu64 "},\"sink\":%u,\"status\":\"%s\"}\n",
        ODM_BENCH_MIN_SHA_BYTES_PER_SECOND,
        ODM_BENCH_MIN_WIRE_BYTES_PER_SECOND,
        ODM_BENCH_MIN_TASKS_PER_SECOND,
        ODM_BENCH_HASH_BYTES * ODM_BENCH_HASH_ROUNDS,
        hash_end - hash_start,
        sha_rate,
        ODM_BENCH_WIRE_BYTES * ODM_BENCH_WIRE_ROUNDS,
        wire_end - wire_start,
        wire_rate,
        config.worker_count, ODM_BENCH_TASK_TOTAL,
        executor_end - executor_start,
        task_rate,
        executor_snapshot.allocation_bytes,
        (unsigned)digest.bytes[0],
        sha_rate >= ODM_BENCH_MIN_SHA_BYTES_PER_SECOND &&
        wire_rate >= ODM_BENCH_MIN_WIRE_BYTES_PER_SECOND &&
        task_rate >= ODM_BENCH_MIN_TASKS_PER_SECOND ? "pass" : "fail");
    exit_code = sha_rate >= ODM_BENCH_MIN_SHA_BYTES_PER_SECOND &&
                wire_rate >= ODM_BENCH_MIN_WIRE_BYTES_PER_SECOND &&
                task_rate >= ODM_BENCH_MIN_TASKS_PER_SECOND ? 0 : 2;

cleanup:
    if (executor) (void)odm_executor_destroy(executor, NULL);
    if (jobs) {
        for (index = 0u; index < ODM_BENCH_TASK_CAPACITY; index++) {
            (void)odm_job_destroy(&jobs[index]);
        }
    }
    free(handles);
    free(jobs);
    free(wire_record);
    free(hash_data);
    return exit_code;
}

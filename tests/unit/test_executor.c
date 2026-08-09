#include "test_harness.h"

#include "odm_executor.h"

#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t input;
    uint64_t *output;
} odm_test_compute_task;

typedef struct {
    _Atomic uint32_t *started;
    _Atomic uint32_t *release;
    _Atomic uint32_t *executed;
} odm_test_block_task;

static odm_status odm_test_executor_compute(void *opaque,
                                            odm_job_ticket ticket) {
    odm_test_compute_task *task = (odm_test_compute_task *)opaque;
    *task->output = task->input * UINT64_C(6364136223846793005) +
                    UINT64_C(1442695040888963407);
    return odm_job_publish(ticket, ODM_PHASE_EXECUTE, 1u);
}

static odm_status odm_test_executor_fail(void *opaque,
                                         odm_job_ticket ticket) {
    (void)opaque;
    (void)ticket;
    return ODM_STATUS_INVALID_DATA;
}

static odm_status odm_test_executor_block(void *opaque,
                                          odm_job_ticket ticket) {
    odm_test_block_task *task = (odm_test_block_task *)opaque;
    int cancelled = 0;
    atomic_fetch_add_explicit(task->executed, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(task->started, 1u, memory_order_release);
    while (atomic_load_explicit(task->release, memory_order_acquire) == 0u) {
        odm_status status = odm_job_should_cancel(ticket, &cancelled);
        if (status != ODM_STATUS_OK) return status;
        if (cancelled) return ODM_STATUS_CANCELLED;
        (void)sched_yield();
    }
    return odm_job_publish(ticket, ODM_PHASE_EXECUTE, 1u);
}

static int odm_test_wait_at_least(_Atomic uint32_t *value, uint32_t expected) {
    uint32_t attempt;
    for (attempt = 0u; attempt < UINT32_C(10000000); attempt++) {
        if (atomic_load_explicit(value, memory_order_acquire) >= expected) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

void odm_test_executor(odm_test_context *context) {
    {
        enum { TASK_COUNT = 128 };
        odm_executor_config config = {
            ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 4u, TASK_COUNT
        };
        odm_resource_limits limits;
        odm_admission_result admission;
        odm_memory_budget budget;
        odm_memory_budget_snapshot budget_snapshot;
        odm_executor *executor = NULL;
        odm_executor_snapshot executor_snapshot;
        odm_executor_task_handle handles[TASK_COUNT];
        odm_test_compute_task tasks[TASK_COUNT];
        odm_job jobs[TASK_COUNT];
        uint64_t outputs[TASK_COUNT];
        uint64_t required_bytes = 0u;
        uint32_t index;
        ODM_TEST_CHECK(context,
                       odm_resource_limits_default(&limits) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_executor_config_admit(&config, &limits, &admission) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, admission.verdict == ODM_ADMISSION_ACCEPT);
        ODM_TEST_CHECK(context,
                       odm_executor_required_bytes(&config, &required_bytes) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, required_bytes > 0u);
        odm_memory_budget_init(&budget, required_bytes);
        ODM_TEST_CHECK(context,
                       odm_executor_create(&config, odm_allocator_default(),
                                           &budget, &executor) == ODM_STATUS_OK);
        for (index = 0u; index < TASK_COUNT; index++) {
            outputs[index] = 0u;
            tasks[index].input = index;
            tasks[index].output = &outputs[index];
            ODM_TEST_CHECK(context, odm_job_init(&jobs[index]) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_executor_submit(executor,
                                               odm_test_executor_compute,
                                               &tasks[index], &jobs[index], 1u,
                                               &handles[index]) == ODM_STATUS_OK);
        }
        for (index = 0u; index < TASK_COUNT; index++) {
            odm_status task_status = ODM_STATUS_BUSY;
            odm_progress_snapshot job_snapshot;
            ODM_TEST_CHECK(context,
                           odm_executor_wait(executor, handles[index],
                                             &task_status) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, task_status == ODM_STATUS_OK);
            ODM_TEST_CHECK(
                context,
                outputs[index] ==
                    (uint64_t)index * UINT64_C(6364136223846793005) +
                        UINT64_C(1442695040888963407));
            ODM_TEST_CHECK(context,
                           odm_job_snapshot(&jobs[index], &job_snapshot) ==
                               ODM_STATUS_OK);
            ODM_TEST_CHECK(context, job_snapshot.state == ODM_JOB_SUCCEEDED);
            ODM_TEST_CHECK(context,
                           odm_job_destroy(&jobs[index]) == ODM_STATUS_OK);
        }
        ODM_TEST_CHECK(context,
                       odm_executor_read(executor, &executor_snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, executor_snapshot.outstanding_tasks == 0u);
        ODM_TEST_CHECK(context, executor_snapshot.submitted_total == TASK_COUNT);
        ODM_TEST_CHECK(context, executor_snapshot.completed_total == TASK_COUNT);
        ODM_TEST_CHECK(context, executor_snapshot.collected_total == TASK_COUNT);
        ODM_TEST_CHECK(context,
                       executor_snapshot.allocation_bytes == required_bytes);
        ODM_TEST_CHECK(context,
                       odm_executor_destroy(executor, &executor_snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, executor_snapshot.accepting == 0u);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &budget_snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, budget_snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, budget_snapshot.peak_bytes == required_bytes);
    }
    {
        odm_executor_config config = {
            ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 1u, 2u
        };
        odm_executor *executor = NULL;
        odm_job jobs[3];
        odm_executor_task_handle handles[3];
        odm_executor_task_snapshot task_snapshot;
        _Atomic uint32_t started = 0u;
        _Atomic uint32_t release = 0u;
        _Atomic uint32_t executed_first = 0u;
        _Atomic uint32_t executed_second = 0u;
        odm_test_block_task first = {&started, &release, &executed_first};
        odm_test_block_task second = {&started, &release, &executed_second};
        odm_status task_status = ODM_STATUS_INTERNAL_ERROR;
        int started_ok;
        ODM_TEST_CHECK(context,
                       odm_executor_create(&config, odm_allocator_default(),
                                           NULL, &executor) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_init(&jobs[0]) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_init(&jobs[1]) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_init(&jobs[2]) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_executor_submit(executor, odm_test_executor_block,
                                           &first, &jobs[0], 1u,
                                           &handles[0]) == ODM_STATUS_OK);
        started_ok = odm_test_wait_at_least(&started, 1u);
        ODM_TEST_CHECK(context, started_ok);
        ODM_TEST_CHECK(context,
                       odm_executor_try_collect(executor, handles[0],
                                                &task_status) ==
                           ODM_STATUS_BUSY);
        ODM_TEST_CHECK(context,
                       odm_executor_submit(executor, odm_test_executor_block,
                                           &second, &jobs[1], 1u,
                                           &handles[1]) == ODM_STATUS_OK);
        memset(&handles[2], 0xa5, sizeof(handles[2]));
        ODM_TEST_CHECK(context,
                       odm_executor_submit(executor, odm_test_executor_fail,
                                           NULL, &jobs[2], 1u,
                                           &handles[2]) == ODM_STATUS_BUSY);
        ODM_TEST_CHECK(context, handles[2].id == UINT64_C(0xa5a5a5a5a5a5a5a5));
        ODM_TEST_CHECK(context,
                       odm_executor_request_cancel(executor, handles[1]) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_executor_task_read(executor, handles[1],
                                              &task_snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       task_snapshot.state == ODM_EXECUTOR_TASK_QUEUED);
        atomic_store_explicit(&release, 1u, memory_order_release);
        ODM_TEST_CHECK(context,
                       odm_executor_wait(executor, handles[0], &task_status) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, task_status == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_executor_wait(executor, handles[1], &task_status) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, task_status == ODM_STATUS_CANCELLED);
        ODM_TEST_CHECK(context,
                       atomic_load_explicit(&executed_second,
                                            memory_order_relaxed) == 0u);
        ODM_TEST_CHECK(context,
                       odm_executor_task_read(executor, handles[1],
                                              &task_snapshot) ==
                           ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context,
                       odm_executor_destroy(executor, NULL) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_destroy(&jobs[0]) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_destroy(&jobs[1]) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_destroy(&jobs[2]) == ODM_STATUS_OK);
    }
    {
        odm_executor_config config = {
            ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 2u, 4u
        };
        odm_executor *executor = NULL;
        odm_job jobs[4];
        odm_executor_task_handle handles[4];
        odm_executor_snapshot final_snapshot;
        _Atomic uint32_t started = 0u;
        _Atomic uint32_t release = 0u;
        _Atomic uint32_t executed = 0u;
        odm_test_block_task task = {&started, &release, &executed};
        uint32_t index;
        ODM_TEST_CHECK(context,
                       odm_executor_create(&config, odm_allocator_default(),
                                           NULL, &executor) == ODM_STATUS_OK);
        for (index = 0u; index < 4u; index++) {
            ODM_TEST_CHECK(context, odm_job_init(&jobs[index]) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_executor_submit(executor,
                                               odm_test_executor_block, &task,
                                               &jobs[index], 1u,
                                               &handles[index]) == ODM_STATUS_OK);
        }
        ODM_TEST_CHECK(context, odm_test_wait_at_least(&started, 1u));
        ODM_TEST_CHECK(context,
                       odm_executor_destroy(executor, &final_snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, final_snapshot.accepting == 0u);
        ODM_TEST_CHECK(context, final_snapshot.outstanding_tasks == 4u);
        ODM_TEST_CHECK(context, final_snapshot.completed_tasks == 4u);
        for (index = 0u; index < 4u; index++) {
            odm_progress_snapshot snapshot;
            ODM_TEST_CHECK(context,
                           odm_job_snapshot(&jobs[index], &snapshot) ==
                               ODM_STATUS_OK);
            ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_CANCELLED);
            ODM_TEST_CHECK(context,
                           odm_job_destroy(&jobs[index]) == ODM_STATUS_OK);
        }
    }
    {
        odm_executor_config config = {
            ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 1u, 1u
        };
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_executor *executor = (odm_executor *)(uintptr_t)UINTPTR_MAX;
        uint64_t required = 0u;
        ODM_TEST_CHECK(context,
                       odm_executor_required_bytes(&config, &required) ==
                           ODM_STATUS_OK);
        odm_memory_budget_init(&budget, required - 1u);
        ODM_TEST_CHECK(context,
                       odm_executor_create(&config, odm_allocator_default(),
                                           &budget, &executor) ==
                           ODM_STATUS_BUDGET_EXCEEDED);
        ODM_TEST_CHECK(context,
                       executor == (odm_executor *)(uintptr_t)UINTPTR_MAX);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        config.worker_count = 0u;
        ODM_TEST_CHECK(context,
                       odm_executor_required_bytes(&config, &required) ==
                           ODM_STATUS_INVALID_ARGUMENT);
    }
    {
        odm_executor_config config = {
            ODM_EXECUTOR_CONFIG_SCHEMA_VERSION, 0u, 1u, 1u
        };
        odm_executor *executor = NULL;
        odm_job job;
        odm_executor_task_handle handle;
        odm_status task_status = ODM_STATUS_OK;
        odm_progress_snapshot snapshot;
        ODM_TEST_CHECK(context,
                       odm_executor_create(&config, odm_allocator_default(),
                                           NULL, &executor) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_executor_submit(executor, odm_test_executor_fail,
                                           NULL, &job, 0u, &handle) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_executor_wait(executor, handle, &task_status) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, task_status == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_FAILED);
        ODM_TEST_CHECK(context,
                       snapshot.error_code == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context,
                       odm_executor_destroy(executor, NULL) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
    }
}

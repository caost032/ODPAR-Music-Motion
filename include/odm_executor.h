#ifndef ODM_EXECUTOR_H
#define ODM_EXECUTOR_H

#include "odm_job.h"
#include "odm_limits.h"
#include "odm_memory.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_EXECUTOR_CONFIG_SCHEMA_VERSION 1u
#define ODM_EXECUTOR_SNAPSHOT_SCHEMA_VERSION 1u
#define ODM_EXECUTOR_TASK_SNAPSHOT_SCHEMA_VERSION 1u
#define ODM_EXECUTOR_MAX_WORKERS 256u
#define ODM_EXECUTOR_MAX_TASKS 1048576u

#define ODM_EXECUTOR_TASK_QUEUED    ((uint32_t)1)
#define ODM_EXECUTOR_TASK_RUNNING   ((uint32_t)2)
#define ODM_EXECUTOR_TASK_COMPLETED ((uint32_t)3)

typedef struct odm_executor odm_executor;

typedef odm_status (*odm_executor_task_fn)(void *context,
                                            odm_job_ticket ticket);

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint32_t worker_count;
    uint32_t task_capacity;
} odm_executor_config;

typedef struct {
    uint64_t id;
    uint32_t slot;
    uint32_t reserved;
} odm_executor_task_handle;

typedef struct {
    uint32_t schema_version;
    uint32_t accepting;
    uint32_t worker_count;
    uint32_t task_capacity;
    uint32_t outstanding_tasks;
    uint32_t queued_tasks;
    uint32_t running_tasks;
    uint32_t completed_tasks;
    uint64_t submitted_total;
    uint64_t completed_total;
    uint64_t collected_total;
    uint64_t allocation_bytes;
} odm_executor_snapshot;

typedef struct {
    uint32_t schema_version;
    uint32_t state;
    odm_status result_status;
    uint32_t reserved;
    uint64_t id;
    uint64_t job_generation;
} odm_executor_task_snapshot;

/* The returned byte count is the one-allocation executor footprint and is also
 * the exact amount charged to budget by create. */
odm_status odm_executor_required_bytes(const odm_executor_config *config,
                                       uint64_t *out_bytes);
odm_status odm_executor_config_admit(const odm_executor_config *config,
                                     const odm_resource_limits *limits,
                                     odm_admission_result *out_result);
odm_status odm_executor_create(const odm_executor_config *config,
                               odm_allocator allocator,
                               odm_memory_budget *budget,
                               odm_executor **out_executor);
/* On successful submit, function, context and job remain caller-owned and must
 * stay alive until that handle is collected or executor_destroy returns.  A
 * task may publish through its ticket, but must not finish or destroy the job;
 * the executor alone publishes the terminal state exactly once. */
odm_status odm_executor_submit(odm_executor *executor,
                               odm_executor_task_fn function,
                               void *context,
                               odm_job *job,
                               uint64_t work_total,
                               odm_executor_task_handle *out_handle);
odm_status odm_executor_request_cancel(odm_executor *executor,
                                       odm_executor_task_handle handle);
odm_status odm_executor_task_read(odm_executor *executor,
                                  odm_executor_task_handle handle,
                                  odm_executor_task_snapshot *out_snapshot);
odm_status odm_executor_try_collect(odm_executor *executor,
                                    odm_executor_task_handle handle,
                                    odm_status *out_task_status);
odm_status odm_executor_wait(odm_executor *executor,
                             odm_executor_task_handle handle,
                             odm_status *out_task_status);
odm_status odm_executor_read(odm_executor *executor,
                             odm_executor_snapshot *out_snapshot);

/* Destroy stops admission, requests cancellation for every outstanding job,
 * drains/joins workers, publishes terminal job states and releases storage.
 * No other executor call may race with destroy. */
odm_status odm_executor_destroy(odm_executor *executor,
                                odm_executor_snapshot *out_final_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* ODM_EXECUTOR_H */

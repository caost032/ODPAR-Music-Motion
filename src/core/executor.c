#include "odm_executor.h"

#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ODM_EXECUTOR_COOKIE UINT64_C(0x4f444d4558454331)
#define ODM_EXECUTOR_SLOT_FREE UINT32_C(0)

typedef struct {
    uint64_t id;
    odm_executor_task_fn function;
    void *context;
    odm_job_ticket ticket;
    odm_status result_status;
    uint32_t state;
    uint32_t reserved;
} odm_executor_slot;

struct odm_executor {
    uint64_t cookie;
    odm_allocator allocator;
    odm_memory_budget *budget;
    uint64_t allocation_bytes;
    pthread_mutex_t mutex;
    pthread_cond_t work_available;
    pthread_cond_t task_completed;
    pthread_t *workers;
    odm_executor_slot *slots;
    uint32_t *queue;
    uint32_t worker_count;
    uint32_t task_capacity;
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
    uint32_t running_count;
    uint32_t completed_count;
    uint32_t free_head;
    uint32_t free_count;
    uint32_t shutdown;
    uint32_t initialized;
    uint64_t next_id;
    uint64_t submitted_total;
    uint64_t completed_total;
    uint64_t collected_total;
};

typedef struct {
    size_t total_bytes;
    size_t workers_offset;
    size_t slots_offset;
    size_t queue_offset;
} odm_executor_layout;

static int odm_executor_allocator_valid(odm_allocator allocator) {
    return allocator.allocate != NULL && allocator.deallocate != NULL;
}

static int odm_executor_add_size(size_t left, size_t right,
                                 size_t *out_value) {
    if (right > SIZE_MAX - left) return 0;
    *out_value = left + right;
    return 1;
}

static int odm_executor_mul_size(size_t left, size_t right,
                                 size_t *out_value) {
    if (left != 0u && right > SIZE_MAX / left) return 0;
    *out_value = left * right;
    return 1;
}

static int odm_executor_align_size(size_t value, size_t alignment,
                                   size_t *out_value) {
    size_t remainder;
    size_t padding;
    if (alignment == 0u) return 0;
    remainder = value % alignment;
    padding = remainder == 0u ? 0u : alignment - remainder;
    return odm_executor_add_size(value, padding, out_value);
}

static odm_status odm_executor_config_validate(
    const odm_executor_config *config) {
    if (!config) return ODM_STATUS_INVALID_ARGUMENT;
    if (config->schema_version != ODM_EXECUTOR_CONFIG_SCHEMA_VERSION) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    if (config->flags != 0u || config->worker_count == 0u ||
        config->task_capacity == 0u ||
        config->worker_count > ODM_EXECUTOR_MAX_WORKERS ||
        config->task_capacity > ODM_EXECUTOR_MAX_TASKS ||
        config->worker_count > config->task_capacity) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_executor_layout_for(const odm_executor_config *config,
                                          odm_executor_layout *out_layout) {
    odm_executor_layout layout;
    size_t bytes;
    size_t part;
    odm_status status = odm_executor_config_validate(config);
    if (status != ODM_STATUS_OK || !out_layout) {
        return status != ODM_STATUS_OK ? status : ODM_STATUS_INVALID_ARGUMENT;
    }
    if (!odm_executor_align_size(sizeof(odm_executor), _Alignof(pthread_t),
                                 &layout.workers_offset) ||
        !odm_executor_mul_size((size_t)config->worker_count,
                               sizeof(pthread_t), &part) ||
        !odm_executor_add_size(layout.workers_offset, part, &bytes) ||
        !odm_executor_align_size(bytes, _Alignof(odm_executor_slot),
                                 &layout.slots_offset) ||
        !odm_executor_mul_size((size_t)config->task_capacity,
                               sizeof(odm_executor_slot), &part) ||
        !odm_executor_add_size(layout.slots_offset, part, &bytes) ||
        !odm_executor_align_size(bytes, _Alignof(uint32_t),
                                 &layout.queue_offset) ||
        !odm_executor_mul_size((size_t)config->task_capacity,
                               sizeof(uint32_t), &part) ||
        !odm_executor_add_size(layout.queue_offset, part,
                               &layout.total_bytes)) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_layout = layout;
    return ODM_STATUS_OK;
}

odm_status odm_executor_required_bytes(const odm_executor_config *config,
                                       uint64_t *out_bytes) {
    odm_executor_layout layout;
    odm_status status;
    if (!out_bytes) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_executor_layout_for(config, &layout);
    if (status != ODM_STATUS_OK) return status;
#if SIZE_MAX > UINT64_MAX
    if (layout.total_bytes > (size_t)UINT64_MAX) return ODM_STATUS_OVERFLOW;
#endif
    *out_bytes = (uint64_t)layout.total_bytes;
    return ODM_STATUS_OK;
}

odm_status odm_executor_config_admit(const odm_executor_config *config,
                                     const odm_resource_limits *limits,
                                     odm_admission_result *out_result) {
    odm_resource_request request;
    odm_status status = odm_executor_config_validate(config);
    if (status != ODM_STATUS_OK) return status;
    if (!out_result) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&request, 0, sizeof(request));
    request.schema_version = ODM_RESOURCE_LIMITS_SCHEMA_VERSION;
    request.queue_items = config->task_capacity;
    request.workers = config->worker_count;
    request.jobs = config->task_capacity;
    return odm_resource_admit(limits, &request, out_result);
}

static int odm_executor_valid(const odm_executor *executor) {
    return executor != NULL && executor->cookie == ODM_EXECUTOR_COOKIE &&
           executor->initialized != 0u;
}

static odm_executor_slot *odm_executor_find_locked(
    odm_executor *executor, odm_executor_task_handle handle) {
    odm_executor_slot *slot;
    if (handle.id == 0u || handle.reserved != 0u ||
        handle.slot >= executor->task_capacity) {
        return NULL;
    }
    slot = &executor->slots[handle.slot];
    return slot->state != ODM_EXECUTOR_SLOT_FREE && slot->id == handle.id ?
               slot : NULL;
}

static void odm_executor_snapshot_locked(const odm_executor *executor,
                                         odm_executor_snapshot *out_snapshot) {
    out_snapshot->schema_version = ODM_EXECUTOR_SNAPSHOT_SCHEMA_VERSION;
    out_snapshot->accepting = executor->shutdown == 0u ? 1u : 0u;
    out_snapshot->worker_count = executor->worker_count;
    out_snapshot->task_capacity = executor->task_capacity;
    out_snapshot->outstanding_tasks = executor->queue_count +
                                      executor->running_count +
                                      executor->completed_count;
    out_snapshot->queued_tasks = executor->queue_count;
    out_snapshot->running_tasks = executor->running_count;
    out_snapshot->completed_tasks = executor->completed_count;
    out_snapshot->submitted_total = executor->submitted_total;
    out_snapshot->completed_total = executor->completed_total;
    out_snapshot->collected_total = executor->collected_total;
    out_snapshot->allocation_bytes = executor->allocation_bytes;
}

static void *odm_executor_worker(void *opaque) {
    odm_executor *executor = (odm_executor *)opaque;
    for (;;) {
        uint32_t slot_index;
        odm_executor_slot *slot;
        odm_executor_task_fn function;
        void *task_context;
        odm_job_ticket ticket;
        odm_status task_status;
        odm_status finish_status;
        odm_progress_snapshot job_snapshot;
        int cancelled = 0;
        if (pthread_mutex_lock(&executor->mutex) != 0) return NULL;
        while (executor->queue_count == 0u && executor->shutdown == 0u) {
            if (pthread_cond_wait(&executor->work_available,
                                  &executor->mutex) != 0) {
                (void)pthread_mutex_unlock(&executor->mutex);
                return NULL;
            }
        }
        if (executor->queue_count == 0u && executor->shutdown != 0u) {
            (void)pthread_mutex_unlock(&executor->mutex);
            return NULL;
        }
        slot_index = executor->queue[executor->queue_head];
        executor->queue_head = (executor->queue_head + 1u) %
                               executor->task_capacity;
        executor->queue_count--;
        slot = &executor->slots[slot_index];
        if (slot->state != ODM_EXECUTOR_TASK_QUEUED) {
            (void)pthread_mutex_unlock(&executor->mutex);
            return NULL;
        }
        slot->state = ODM_EXECUTOR_TASK_RUNNING;
        executor->running_count++;
        function = slot->function;
        task_context = slot->context;
        ticket = slot->ticket;
        (void)pthread_mutex_unlock(&executor->mutex);

        task_status = odm_job_should_cancel(ticket, &cancelled);
        if (task_status == ODM_STATUS_OK) {
            task_status = cancelled ? ODM_STATUS_CANCELLED :
                                      function(task_context, ticket);
        }
        finish_status = odm_job_finish(ticket, task_status);
        if (finish_status == ODM_STATUS_OK &&
            odm_job_snapshot(ticket.job, &job_snapshot) == ODM_STATUS_OK) {
            task_status = job_snapshot.error_code;
        } else if (finish_status != ODM_STATUS_OK) {
            task_status = finish_status;
        } else {
            task_status = ODM_STATUS_INVARIANT_BROKEN;
        }

        if (pthread_mutex_lock(&executor->mutex) != 0) return NULL;
        slot = &executor->slots[slot_index];
        if (slot->state != ODM_EXECUTOR_TASK_RUNNING ||
            slot->id == 0u || executor->running_count == 0u) {
            (void)pthread_mutex_unlock(&executor->mutex);
            return NULL;
        }
        slot->result_status = task_status;
        slot->state = ODM_EXECUTOR_TASK_COMPLETED;
        executor->running_count--;
        executor->completed_count++;
        executor->completed_total++;
        (void)pthread_cond_broadcast(&executor->task_completed);
        (void)pthread_mutex_unlock(&executor->mutex);
    }
}

static odm_status odm_executor_release_allocation(odm_executor *executor) {
    odm_allocator allocator = executor->allocator;
    odm_memory_budget *budget = executor->budget;
    uint64_t allocation_bytes = executor->allocation_bytes;
    size_t native_bytes = (size_t)allocation_bytes;
    volatile uint8_t *cursor = (volatile uint8_t *)executor;
    odm_status status = budget ?
        odm_memory_budget_release(budget, allocation_bytes) : ODM_STATUS_OK;
    while (native_bytes != 0u) {
        *cursor++ = 0u;
        native_bytes--;
    }
    allocator.deallocate(allocator.context, executor);
    return status;
}

odm_status odm_executor_create(const odm_executor_config *config,
                               odm_allocator allocator,
                               odm_memory_budget *budget,
                               odm_executor **out_executor) {
    odm_executor_layout layout;
    odm_executor *executor;
    uint64_t allocation_bytes;
    uint32_t created = 0u;
    odm_status status;
    if (!out_executor || !odm_executor_allocator_valid(allocator)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_executor_layout_for(config, &layout);
    if (status != ODM_STATUS_OK) return status;
    allocation_bytes = (uint64_t)layout.total_bytes;
    if (budget) {
        status = odm_memory_budget_reserve(budget, allocation_bytes);
        if (status != ODM_STATUS_OK) return status;
    }
    executor = (odm_executor *)allocator.allocate(allocator.context,
                                                  layout.total_bytes);
    if (!executor) {
        if (budget) (void)odm_memory_budget_release(budget, allocation_bytes);
        return ODM_STATUS_OUT_OF_MEMORY;
    }
    memset(executor, 0, layout.total_bytes);
    executor->allocator = allocator;
    executor->budget = budget;
    executor->allocation_bytes = allocation_bytes;
    executor->worker_count = config->worker_count;
    executor->task_capacity = config->task_capacity;
    executor->workers = (pthread_t *)((uint8_t *)executor +
                                      layout.workers_offset);
    executor->slots = (odm_executor_slot *)((uint8_t *)executor +
                                            layout.slots_offset);
    executor->queue = (uint32_t *)((uint8_t *)executor + layout.queue_offset);
    executor->next_id = 1u;
    executor->free_head = 0u;
    executor->free_count = executor->task_capacity;
    for (created = 0u; created < executor->task_capacity; created++) {
        executor->slots[created].reserved =
            created + 1u < executor->task_capacity ? created + 1u : UINT32_MAX;
    }
    created = 0u;
    if (pthread_mutex_init(&executor->mutex, NULL) != 0) {
        (void)odm_executor_release_allocation(executor);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    if (pthread_cond_init(&executor->work_available, NULL) != 0) {
        (void)pthread_mutex_destroy(&executor->mutex);
        (void)odm_executor_release_allocation(executor);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    if (pthread_cond_init(&executor->task_completed, NULL) != 0) {
        (void)pthread_cond_destroy(&executor->work_available);
        (void)pthread_mutex_destroy(&executor->mutex);
        (void)odm_executor_release_allocation(executor);
        return ODM_STATUS_INTERNAL_ERROR;
    }
    executor->cookie = ODM_EXECUTOR_COOKIE;
    executor->initialized = 1u;
    while (created < executor->worker_count) {
        if (pthread_create(&executor->workers[created], NULL,
                           odm_executor_worker, executor) != 0) {
            uint32_t joined;
            if (pthread_mutex_lock(&executor->mutex) == 0) {
                executor->shutdown = 1u;
                (void)pthread_cond_broadcast(&executor->work_available);
                (void)pthread_mutex_unlock(&executor->mutex);
            }
            for (joined = 0u; joined < created; joined++) {
                (void)pthread_join(executor->workers[joined], NULL);
            }
            (void)pthread_cond_destroy(&executor->task_completed);
            (void)pthread_cond_destroy(&executor->work_available);
            (void)pthread_mutex_destroy(&executor->mutex);
            (void)odm_executor_release_allocation(executor);
            return ODM_STATUS_INTERNAL_ERROR;
        }
        created++;
    }
    *out_executor = executor;
    return ODM_STATUS_OK;
}

odm_status odm_executor_submit(odm_executor *executor,
                               odm_executor_task_fn function,
                               void *context,
                               odm_job *job,
                               uint64_t work_total,
                               odm_executor_task_handle *out_handle) {
    uint32_t slot_index;
    odm_executor_slot *slot = NULL;
    odm_job_ticket ticket;
    odm_executor_task_handle handle;
    odm_status status;
    if (!odm_executor_valid(executor) || !function || !job || !out_handle) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    if (executor->shutdown != 0u) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_INVALID_STATE;
    }
    if (executor->next_id == 0u ||
        executor->submitted_total == UINT64_MAX) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_OVERFLOW;
    }
    if (executor->free_count == 0u || executor->free_head == UINT32_MAX) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_BUSY;
    }
    slot_index = executor->free_head;
    slot = &executor->slots[slot_index];
    if (slot->state != ODM_EXECUTOR_SLOT_FREE) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    status = odm_job_begin(job, work_total, &ticket);
    if (status != ODM_STATUS_OK) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return status;
    }
    executor->free_head = slot->reserved;
    executor->free_count--;
    slot->id = executor->next_id;
    slot->function = function;
    slot->context = context;
    slot->ticket = ticket;
    slot->result_status = ODM_STATUS_BUSY;
    slot->state = ODM_EXECUTOR_TASK_QUEUED;
    handle.id = slot->id;
    handle.slot = slot_index;
    handle.reserved = 0u;
    executor->next_id++;
    executor->submitted_total++;
    executor->queue[executor->queue_tail] = slot_index;
    executor->queue_tail = (executor->queue_tail + 1u) %
                           executor->task_capacity;
    executor->queue_count++;
    (void)pthread_cond_signal(&executor->work_available);
    (void)pthread_mutex_unlock(&executor->mutex);
    *out_handle = handle;
    return ODM_STATUS_OK;
}

odm_status odm_executor_request_cancel(odm_executor *executor,
                                       odm_executor_task_handle handle) {
    odm_executor_slot *slot;
    odm_status status;
    if (!odm_executor_valid(executor)) return ODM_STATUS_INVALID_ARGUMENT;
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    slot = odm_executor_find_locked(executor, handle);
    if (!slot) status = ODM_STATUS_INVALID_STATE;
    else if (slot->state == ODM_EXECUTOR_TASK_COMPLETED) {
        status = ODM_STATUS_INVALID_STATE;
    } else status = odm_job_request_cancel(slot->ticket.job);
    (void)pthread_mutex_unlock(&executor->mutex);
    return status;
}

odm_status odm_executor_task_read(odm_executor *executor,
                                  odm_executor_task_handle handle,
                                  odm_executor_task_snapshot *out_snapshot) {
    odm_executor_slot *slot;
    odm_executor_task_snapshot snapshot;
    if (!odm_executor_valid(executor) || !out_snapshot) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    slot = odm_executor_find_locked(executor, handle);
    if (!slot) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_INVALID_STATE;
    }
    snapshot.schema_version = ODM_EXECUTOR_TASK_SNAPSHOT_SCHEMA_VERSION;
    snapshot.state = slot->state;
    snapshot.result_status = slot->result_status;
    snapshot.reserved = 0u;
    snapshot.id = slot->id;
    snapshot.job_generation = slot->ticket.generation;
    (void)pthread_mutex_unlock(&executor->mutex);
    *out_snapshot = snapshot;
    return ODM_STATUS_OK;
}

static odm_status odm_executor_collect_locked(odm_executor *executor,
                                              odm_executor_slot *slot,
                                              odm_status *out_task_status) {
    odm_status result;
    size_t slot_index;
    if (slot->state != ODM_EXECUTOR_TASK_COMPLETED) return ODM_STATUS_BUSY;
    if (executor->completed_count == 0u ||
        executor->collected_total == UINT64_MAX) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    result = slot->result_status;
    slot_index = (size_t)(slot - executor->slots);
    memset(slot, 0, sizeof(*slot));
    slot->reserved = executor->free_head;
    executor->free_head = (uint32_t)slot_index;
    executor->free_count++;
    executor->completed_count--;
    executor->collected_total++;
    *out_task_status = result;
    return ODM_STATUS_OK;
}

odm_status odm_executor_try_collect(odm_executor *executor,
                                    odm_executor_task_handle handle,
                                    odm_status *out_task_status) {
    odm_executor_slot *slot;
    odm_status status;
    if (!odm_executor_valid(executor) || !out_task_status) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    slot = odm_executor_find_locked(executor, handle);
    status = slot ? odm_executor_collect_locked(executor, slot,
                                                 out_task_status) :
                    ODM_STATUS_INVALID_STATE;
    (void)pthread_mutex_unlock(&executor->mutex);
    return status;
}

odm_status odm_executor_wait(odm_executor *executor,
                             odm_executor_task_handle handle,
                             odm_status *out_task_status) {
    odm_executor_slot *slot;
    odm_status status;
    if (!odm_executor_valid(executor) || !out_task_status) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    slot = odm_executor_find_locked(executor, handle);
    if (!slot) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_INVALID_STATE;
    }
    while (slot->state != ODM_EXECUTOR_TASK_COMPLETED) {
        if (pthread_cond_wait(&executor->task_completed,
                              &executor->mutex) != 0) {
            (void)pthread_mutex_unlock(&executor->mutex);
            return ODM_STATUS_INTERNAL_ERROR;
        }
        slot = odm_executor_find_locked(executor, handle);
        if (!slot) {
            (void)pthread_mutex_unlock(&executor->mutex);
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    status = odm_executor_collect_locked(executor, slot, out_task_status);
    (void)pthread_mutex_unlock(&executor->mutex);
    return status;
}

odm_status odm_executor_read(odm_executor *executor,
                             odm_executor_snapshot *out_snapshot) {
    odm_executor_snapshot snapshot;
    if (!odm_executor_valid(executor) || !out_snapshot) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    odm_executor_snapshot_locked(executor, &snapshot);
    (void)pthread_mutex_unlock(&executor->mutex);
    *out_snapshot = snapshot;
    return ODM_STATUS_OK;
}

odm_status odm_executor_destroy(odm_executor *executor,
                                odm_executor_snapshot *out_final_snapshot) {
    odm_executor_snapshot snapshot;
    uint32_t index;
    odm_status status = ODM_STATUS_OK;
    if (!odm_executor_valid(executor)) return ODM_STATUS_INVALID_ARGUMENT;
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    if (executor->shutdown != 0u) {
        (void)pthread_mutex_unlock(&executor->mutex);
        return ODM_STATUS_INVALID_STATE;
    }
    executor->shutdown = 1u;
    for (index = 0u; index < executor->task_capacity; index++) {
        odm_executor_slot *slot = &executor->slots[index];
        if (slot->state == ODM_EXECUTOR_TASK_QUEUED ||
            slot->state == ODM_EXECUTOR_TASK_RUNNING) {
            odm_status cancel_status = odm_job_request_cancel(slot->ticket.job);
            if (cancel_status != ODM_STATUS_OK &&
                cancel_status != ODM_STATUS_INVALID_STATE) {
                status = cancel_status;
            }
        }
    }
    (void)pthread_cond_broadcast(&executor->work_available);
    (void)pthread_mutex_unlock(&executor->mutex);
    for (index = 0u; index < executor->worker_count; index++) {
        if (pthread_join(executor->workers[index], NULL) != 0) {
            status = ODM_STATUS_INTERNAL_ERROR;
        }
    }
    if (pthread_mutex_lock(&executor->mutex) != 0) {
        return ODM_STATUS_INTERNAL_ERROR;
    }
    odm_executor_snapshot_locked(executor, &snapshot);
    (void)pthread_mutex_unlock(&executor->mutex);
    if (pthread_cond_destroy(&executor->task_completed) != 0) {
        status = ODM_STATUS_INTERNAL_ERROR;
    }
    if (pthread_cond_destroy(&executor->work_available) != 0) {
        status = ODM_STATUS_INTERNAL_ERROR;
    }
    if (pthread_mutex_destroy(&executor->mutex) != 0) {
        status = ODM_STATUS_INTERNAL_ERROR;
    }
    if (out_final_snapshot) *out_final_snapshot = snapshot;
    if (odm_executor_release_allocation(executor) != ODM_STATUS_OK) {
        status = ODM_STATUS_INVARIANT_BROKEN;
    }
    return status;
}

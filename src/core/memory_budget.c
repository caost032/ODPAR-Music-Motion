#include "odm_memory.h"

#include <limits.h>
#include <stdlib.h>

static void *odm_system_allocate(void *context, size_t bytes) {
    (void)context;
    return malloc(bytes);
}

static void odm_system_deallocate(void *context, void *memory) {
    (void)context;
    free(memory);
}

odm_allocator odm_allocator_default(void) {
    odm_allocator allocator;
    allocator.context = NULL;
    allocator.allocate = odm_system_allocate;
    allocator.deallocate = odm_system_deallocate;
    return allocator;
}

void odm_memory_budget_init(odm_memory_budget *budget, uint64_t limit_bytes) {
    if (!budget) return;
    budget->limit_bytes = limit_bytes;
    atomic_init(&budget->current_bytes, UINT64_C(0));
    atomic_init(&budget->peak_bytes, UINT64_C(0));
    atomic_init(&budget->reservation_count, UINT64_C(0));
}

odm_status odm_memory_budget_reserve(odm_memory_budget *budget, uint64_t bytes) {
    uint64_t current;
    uint64_t desired;
    uint64_t peak;
    uint64_t reservation_count;
    if (!budget) return ODM_STATUS_INVALID_ARGUMENT;
    if (bytes == 0u) return ODM_STATUS_OK;
    current = atomic_load_explicit(&budget->current_bytes, memory_order_relaxed);
    for (;;) {
        if (bytes > UINT64_MAX - current) return ODM_STATUS_OVERFLOW;
        desired = current + bytes;
        if (budget->limit_bytes != 0u && desired > budget->limit_bytes) {
            return ODM_STATUS_BUDGET_EXCEEDED;
        }
        if (atomic_compare_exchange_weak_explicit(
                &budget->current_bytes, &current, desired,
                memory_order_acq_rel, memory_order_relaxed)) {
            break;
        }
    }
    reservation_count = atomic_load_explicit(&budget->reservation_count,
                                              memory_order_relaxed);
    for (;;) {
        if (reservation_count == UINT64_MAX) {
            (void)odm_memory_budget_release(budget, bytes);
            return ODM_STATUS_OVERFLOW;
        }
        if (atomic_compare_exchange_weak_explicit(
                &budget->reservation_count, &reservation_count,
                reservation_count + UINT64_C(1),
                memory_order_relaxed, memory_order_relaxed)) {
            break;
        }
    }
    peak = atomic_load_explicit(&budget->peak_bytes, memory_order_relaxed);
    while (desired > peak &&
           !atomic_compare_exchange_weak_explicit(
               &budget->peak_bytes, &peak, desired,
               memory_order_release, memory_order_relaxed)) {
    }
    return ODM_STATUS_OK;
}

odm_status odm_memory_budget_release(odm_memory_budget *budget, uint64_t bytes) {
    uint64_t current;
    if (!budget) return ODM_STATUS_INVALID_ARGUMENT;
    if (bytes == 0u) return ODM_STATUS_OK;
    current = atomic_load_explicit(&budget->current_bytes, memory_order_relaxed);
    for (;;) {
        if (bytes > current) return ODM_STATUS_INVARIANT_BROKEN;
        if (atomic_compare_exchange_weak_explicit(
                &budget->current_bytes, &current, current - bytes,
                memory_order_acq_rel, memory_order_relaxed)) {
            return ODM_STATUS_OK;
        }
    }
}

odm_status odm_memory_budget_read(const odm_memory_budget *budget,
                                  odm_memory_budget_snapshot *out_snapshot) {
    if (!budget || !out_snapshot) return ODM_STATUS_INVALID_ARGUMENT;
    out_snapshot->limit_bytes = budget->limit_bytes;
    out_snapshot->current_bytes = atomic_load_explicit(
        &budget->current_bytes, memory_order_acquire);
    out_snapshot->peak_bytes = atomic_load_explicit(
        &budget->peak_bytes, memory_order_acquire);
    out_snapshot->reservation_count = atomic_load_explicit(
        &budget->reservation_count, memory_order_acquire);
    return ODM_STATUS_OK;
}

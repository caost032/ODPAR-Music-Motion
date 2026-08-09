#include "test_harness.h"

#include "odm_memory.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    size_t calls;
    size_t frees;
    size_t live_blocks;
    size_t fail_on_call;
} odm_fault_allocator;

static void *odm_fault_allocate(void *opaque, size_t bytes) {
    odm_fault_allocator *state = (odm_fault_allocator *)opaque;
    void *memory;
    state->calls++;
    if (state->fail_on_call != 0u && state->calls == state->fail_on_call) {
        return NULL;
    }
    memory = malloc(bytes);
    if (memory) state->live_blocks++;
    return memory;
}

static void odm_fault_deallocate(void *opaque, void *memory) {
    odm_fault_allocator *state = (odm_fault_allocator *)opaque;
    if (memory) {
        state->frees++;
        state->live_blocks--;
        free(memory);
    }
}

typedef struct {
    odm_memory_budget *budget;
    _Atomic int *failed;
} odm_budget_worker_args;

static void *odm_budget_worker(void *opaque) {
    odm_budget_worker_args *args = (odm_budget_worker_args *)opaque;
    unsigned iteration;
    for (iteration = 0u; iteration < 10000u; iteration++) {
        odm_status status;
        do {
            status = odm_memory_budget_reserve(args->budget, UINT64_C(1));
        } while (status == ODM_STATUS_BUDGET_EXCEEDED);
        if (status != ODM_STATUS_OK) {
            atomic_store_explicit(args->failed, 1, memory_order_relaxed);
            return NULL;
        }
        {
            odm_memory_budget_snapshot snapshot;
            if (odm_memory_budget_read(args->budget, &snapshot) != ODM_STATUS_OK ||
                snapshot.current_bytes > snapshot.limit_bytes) {
                atomic_store_explicit(args->failed, 1, memory_order_relaxed);
            }
        }
        if (odm_memory_budget_release(args->budget, UINT64_C(1)) != ODM_STATUS_OK) {
            atomic_store_explicit(args->failed, 1, memory_order_relaxed);
            return NULL;
        }
    }
    return NULL;
}

void odm_test_memory(odm_test_context *context) {
    {
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_memory_budget_init(&budget, UINT64_C(100));
        ODM_TEST_CHECK(context,
                       odm_memory_budget_reserve(&budget, UINT64_C(60)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_reserve(&budget, UINT64_C(41)) ==
                           ODM_STATUS_BUDGET_EXCEEDED);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 60u);
        ODM_TEST_CHECK(context, snapshot.peak_bytes == 60u);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_release(&budget, UINT64_C(61)) ==
                           ODM_STATUS_INVARIANT_BROKEN);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_release(&budget, UINT64_C(60)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
    }
    {
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_memory_budget_init(&budget, 0u);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_reserve(&budget, UINT64_MAX) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_reserve(&budget, UINT64_C(1)) ==
                           ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_release(&budget, UINT64_MAX) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, snapshot.peak_bytes == UINT64_MAX);
    }
    {
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_memory_budget_init(&budget, 0u);
        atomic_store_explicit(&budget.reservation_count, UINT64_MAX,
                              memory_order_relaxed);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_reserve(&budget, UINT64_C(1)) ==
                           ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, snapshot.reservation_count == UINT64_MAX);
    }
    {
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_arena arena = ODM_ARENA_INITIALIZER;
        odm_arena_mark mark;
        void *first = NULL;
        void *second = NULL;
        odm_memory_budget_init(&budget, UINT64_C(64));
        ODM_TEST_CHECK(context,
                       odm_arena_init(&arena, 64u, odm_allocator_default(), &budget) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_arena_allocate(&arena, 1u, 1u, &first) == ODM_STATUS_OK);
        mark = odm_arena_get_mark(&arena);
        ODM_TEST_CHECK(context,
                       odm_arena_allocate(&arena, 16u, 16u, &second) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, ((uintptr_t)second & (uintptr_t)15u) == 0u);
        ODM_TEST_CHECK(context,
                       odm_arena_allocate(&arena, 128u, 1u, &second) ==
                           ODM_STATUS_BUDGET_EXCEEDED);
        ODM_TEST_CHECK(context,
                       odm_arena_rewind(&arena, mark, 1) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, arena.offset == mark.offset);
        mark.offset = arena.offset + 1u;
        ODM_TEST_CHECK(context,
                       odm_arena_rewind(&arena, mark, 0) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, first != NULL);
        ODM_TEST_CHECK(context, odm_arena_destroy(&arena) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, snapshot.peak_bytes == 64u);
        ODM_TEST_CHECK(context,
                       odm_arena_destroy(&arena) == ODM_STATUS_INVALID_STATE);
    }
    {
        odm_fault_allocator fault = {0};
        odm_allocator allocator = {&fault, odm_fault_allocate, odm_fault_deallocate};
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_arena arena = ODM_ARENA_INITIALIZER;
        odm_memory_budget_init(&budget, UINT64_C(64));
        fault.fail_on_call = 1u;
        ODM_TEST_CHECK(context,
                       odm_arena_init(&arena, 64u, allocator, &budget) ==
                           ODM_STATUS_OUT_OF_MEMORY);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, fault.live_blocks == 0u);
    }
    {
        odm_memory_budget source_budget;
        odm_memory_budget destination_budget;
        odm_memory_budget_snapshot source_memory;
        odm_memory_budget_snapshot destination_memory;
        odm_owner source = ODM_OWNER_INITIALIZER;
        odm_owner destination = ODM_OWNER_INITIALIZER;
        odm_owner_snapshot source_snapshot;
        odm_owner_snapshot destination_snapshot;
        odm_allocator allocator = odm_allocator_default();
        void *first = NULL;
        void *second = NULL;
        odm_memory_budget_init(&source_budget, UINT64_C(4096));
        odm_memory_budget_init(&destination_budget, UINT64_C(1));
        ODM_TEST_CHECK(context,
                       odm_owner_init(&source, allocator, &source_budget) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_init(&destination, allocator, &destination_budget) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_allocate(&source, 16u, &first) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_allocate(&source, 32u, &second) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_take(&destination, &source, first) ==
                           ODM_STATUS_BUDGET_EXCEEDED);
        ODM_TEST_CHECK(context,
                       odm_owner_move_all(&destination, &source) ==
                           ODM_STATUS_BUDGET_EXCEEDED);
        ODM_TEST_CHECK(context,
                       odm_owner_read(&source, &source_snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, source_snapshot.allocation_count == 2u);
        odm_memory_budget_init(&destination_budget, UINT64_C(4096));
        atomic_store_explicit(&source_budget.current_bytes, UINT64_C(0),
                              memory_order_relaxed);
        ODM_TEST_CHECK(context,
                       odm_owner_take(&destination, &source, first) ==
                           ODM_STATUS_INVARIANT_BROKEN);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&destination_budget,
                                              &destination_memory) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, destination_memory.current_bytes == 0u);
        ODM_TEST_CHECK(context,
                       odm_owner_read(&source, &source_snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, source_snapshot.allocation_count == 2u);
        atomic_store_explicit(&source_budget.current_bytes,
                              source_snapshot.physical_bytes,
                              memory_order_relaxed);
        ODM_TEST_CHECK(context,
                       odm_owner_take(&destination, &source, first) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_read(&source, &source_snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_read(&destination, &destination_snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, source_snapshot.allocation_count == 1u);
        ODM_TEST_CHECK(context, destination_snapshot.allocation_count == 1u);
        ODM_TEST_CHECK(context,
                       odm_owner_move_all(&destination, &source) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_read(&source, &source_snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_read(&destination, &destination_snapshot) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, source_snapshot.allocation_count == 0u);
        ODM_TEST_CHECK(context, destination_snapshot.allocation_count == 2u);
        ODM_TEST_CHECK(context,
                       odm_owner_release(&destination, first) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_release(&destination, first) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, odm_owner_destroy(&source) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_owner_destroy(&destination) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&source_budget, &source_memory) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&destination_budget,
                                              &destination_memory) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, source_memory.current_bytes == 0u);
        ODM_TEST_CHECK(context, destination_memory.current_bytes == 0u);
        ODM_TEST_CHECK(context, second != NULL);
    }
    {
        odm_fault_allocator fault = {0};
        odm_allocator allocator = {&fault, odm_fault_allocate, odm_fault_deallocate};
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        odm_owner owner = ODM_OWNER_INITIALIZER;
        void *memory = NULL;
        odm_memory_budget_init(&budget, UINT64_C(1024));
        fault.fail_on_call = 1u;
        ODM_TEST_CHECK(context,
                       odm_owner_init(&owner, allocator, &budget) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_owner_allocate(&owner, 32u, &memory) ==
                           ODM_STATUS_OUT_OF_MEMORY);
        ODM_TEST_CHECK(context, memory == NULL);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, fault.live_blocks == 0u);
        ODM_TEST_CHECK(context, odm_owner_destroy(&owner) == ODM_STATUS_OK);
    }
    {
        odm_memory_budget budget;
        odm_memory_budget_snapshot snapshot;
        pthread_t threads[4];
        odm_budget_worker_args args;
        _Atomic int failed;
        size_t index;
        atomic_init(&failed, 0);
        odm_memory_budget_init(&budget, UINT64_C(2));
        args.budget = &budget;
        args.failed = &failed;
        for (index = 0u; index < 4u; index++) {
            ODM_TEST_CHECK(context,
                           pthread_create(&threads[index], NULL, odm_budget_worker,
                                          &args) == 0);
        }
        for (index = 0u; index < 4u; index++) {
            ODM_TEST_CHECK(context, pthread_join(threads[index], NULL) == 0);
        }
        ODM_TEST_CHECK(context,
                       atomic_load_explicit(&failed, memory_order_relaxed) == 0);
        ODM_TEST_CHECK(context,
                       odm_memory_budget_read(&budget, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.current_bytes == 0u);
        ODM_TEST_CHECK(context, snapshot.peak_bytes <= 2u);
    }
}

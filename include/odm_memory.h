#ifndef ODM_MEMORY_H
#define ODM_MEMORY_H

#include "odm_status.h"

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*odm_allocate_fn)(void *context, size_t bytes);
typedef void (*odm_deallocate_fn)(void *context, void *memory);

typedef struct {
    void *context;
    odm_allocate_fn allocate;
    odm_deallocate_fn deallocate;
} odm_allocator;

typedef struct {
    uint64_t limit_bytes; /* zero means unlimited but still measured */
    _Atomic uint64_t current_bytes;
    _Atomic uint64_t peak_bytes;
    _Atomic uint64_t reservation_count;
} odm_memory_budget;

typedef struct {
    uint64_t limit_bytes;
    uint64_t current_bytes;
    uint64_t peak_bytes;
    uint64_t reservation_count;
} odm_memory_budget_snapshot;

typedef struct {
    uint8_t *base;
    size_t capacity;
    size_t offset;
    size_t high_water;
    uint64_t charged_bytes;
    odm_allocator allocator;
    odm_memory_budget *budget;
    uint32_t initialized;
    uint32_t reserved;
} odm_arena;

typedef struct {
    size_t offset;
} odm_arena_mark;

typedef struct {
    void *head;
    odm_allocator allocator;
    odm_memory_budget *budget;
    uint64_t payload_bytes;
    uint64_t physical_bytes;
    uint64_t allocation_count;
    uint32_t initialized;
    uint32_t reserved;
} odm_owner;

typedef struct {
    uint64_t payload_bytes;
    uint64_t physical_bytes;
    uint64_t allocation_count;
} odm_owner_snapshot;

/* Arena and owner initialization deliberately reject re-initialization because it
 * could orphan live storage.  Caller-owned objects must therefore start from one
 * of these zero initializers before the first odm_*_init call. */
#define ODM_ARENA_INITIALIZER {0}
#define ODM_OWNER_INITIALIZER {0}

odm_allocator odm_allocator_default(void);

/* A budget is initialized exactly once before concurrent use; limit_bytes is
 * immutable until every user has stopped. */
void odm_memory_budget_init(odm_memory_budget *budget, uint64_t limit_bytes);
odm_status odm_memory_budget_reserve(odm_memory_budget *budget, uint64_t bytes);
odm_status odm_memory_budget_release(odm_memory_budget *budget, uint64_t bytes);
odm_status odm_memory_budget_read(const odm_memory_budget *budget,
                                  odm_memory_budget_snapshot *out_snapshot);

odm_status odm_arena_init(odm_arena *arena, size_t capacity,
                          odm_allocator allocator, odm_memory_budget *budget);
odm_status odm_arena_allocate(odm_arena *arena, size_t bytes, size_t alignment,
                              void **out_memory);
odm_arena_mark odm_arena_get_mark(const odm_arena *arena);
odm_status odm_arena_rewind(odm_arena *arena, odm_arena_mark mark,
                            int clear_rewound_bytes);
odm_status odm_arena_destroy(odm_arena *arena);

odm_status odm_owner_init(odm_owner *owner, odm_allocator allocator,
                          odm_memory_budget *budget);
odm_status odm_owner_allocate(odm_owner *owner, size_t bytes, void **out_memory);
odm_status odm_owner_release(odm_owner *owner, void *memory);
odm_status odm_owner_take(odm_owner *destination, odm_owner *source,
                          void *memory);
odm_status odm_owner_move_all(odm_owner *destination, odm_owner *source);
odm_status odm_owner_read(const odm_owner *owner,
                          odm_owner_snapshot *out_snapshot);
odm_status odm_owner_destroy(odm_owner *owner);

#ifdef __cplusplus
}
#endif

#endif /* ODM_MEMORY_H */

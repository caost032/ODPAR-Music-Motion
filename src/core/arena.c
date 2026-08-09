#include "odm_memory.h"

#include <stdint.h>
#include <string.h>

static int odm_allocator_valid(odm_allocator allocator) {
    return allocator.allocate != NULL && allocator.deallocate != NULL;
}

static odm_status odm_size_to_u64(size_t value, uint64_t *out_value) {
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
#if SIZE_MAX > UINT64_MAX
    if (value > (size_t)UINT64_MAX) return ODM_STATUS_OVERFLOW;
#endif
    *out_value = (uint64_t)value;
    return ODM_STATUS_OK;
}

static void odm_secure_clear(void *memory, size_t bytes) {
    volatile uint8_t *cursor = (volatile uint8_t *)memory;
    while (bytes != 0u) {
        *cursor++ = 0u;
        bytes--;
    }
}

odm_status odm_arena_init(odm_arena *arena, size_t capacity,
                          odm_allocator allocator, odm_memory_budget *budget) {
    uint64_t charged_bytes;
    odm_status status;
    if (!arena || !odm_allocator_valid(allocator)) return ODM_STATUS_INVALID_ARGUMENT;
    if (arena->initialized != 0u) return ODM_STATUS_INVALID_STATE;
    status = odm_size_to_u64(capacity, &charged_bytes);
    if (status != ODM_STATUS_OK) return status;
    if (budget) {
        status = odm_memory_budget_reserve(budget, charged_bytes);
        if (status != ODM_STATUS_OK) return status;
    }
    arena->base = NULL;
    if (capacity != 0u) {
        arena->base = (uint8_t *)allocator.allocate(allocator.context, capacity);
        if (!arena->base) {
            if (budget) (void)odm_memory_budget_release(budget, charged_bytes);
            return ODM_STATUS_OUT_OF_MEMORY;
        }
    }
    arena->capacity = capacity;
    arena->offset = 0u;
    arena->high_water = 0u;
    arena->charged_bytes = charged_bytes;
    arena->allocator = allocator;
    arena->budget = budget;
    arena->reserved = 0u;
    arena->initialized = 1u;
    return ODM_STATUS_OK;
}

odm_status odm_arena_allocate(odm_arena *arena, size_t bytes, size_t alignment,
                              void **out_memory) {
    uintptr_t address;
    size_t padding;
    size_t start;
    size_t end;
    if (!arena || !out_memory || arena->initialized == 0u || bytes == 0u ||
        alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    *out_memory = NULL;
    if (alignment > _Alignof(max_align_t)) return ODM_STATUS_UNSUPPORTED;
    if (arena->offset > arena->capacity || !arena->base) {
        return ODM_STATUS_BUDGET_EXCEEDED;
    }
    address = (uintptr_t)(arena->base + arena->offset);
    padding = (size_t)((0u - address) & (uintptr_t)(alignment - 1u));
    if (padding > SIZE_MAX - arena->offset) return ODM_STATUS_OVERFLOW;
    start = arena->offset + padding;
    if (bytes > SIZE_MAX - start) return ODM_STATUS_OVERFLOW;
    end = start + bytes;
    if (end > arena->capacity) return ODM_STATUS_BUDGET_EXCEEDED;
    *out_memory = arena->base + start;
    arena->offset = end;
    if (end > arena->high_water) arena->high_water = end;
    return ODM_STATUS_OK;
}

odm_arena_mark odm_arena_get_mark(const odm_arena *arena) {
    odm_arena_mark mark;
    mark.offset = (arena && arena->initialized != 0u) ? arena->offset : SIZE_MAX;
    return mark;
}

odm_status odm_arena_rewind(odm_arena *arena, odm_arena_mark mark,
                            int clear_rewound_bytes) {
    if (!arena || arena->initialized == 0u || mark.offset > arena->offset) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (clear_rewound_bytes && arena->base && mark.offset < arena->offset) {
        odm_secure_clear(arena->base + mark.offset, arena->offset - mark.offset);
    }
    arena->offset = mark.offset;
    return ODM_STATUS_OK;
}

odm_status odm_arena_destroy(odm_arena *arena) {
    odm_status status = ODM_STATUS_OK;
    if (!arena) return ODM_STATUS_INVALID_ARGUMENT;
    if (arena->initialized == 0u) return ODM_STATUS_INVALID_STATE;
    if (arena->base) {
        odm_secure_clear(arena->base, arena->capacity);
        arena->allocator.deallocate(arena->allocator.context, arena->base);
    }
    if (arena->budget) {
        status = odm_memory_budget_release(arena->budget, arena->charged_bytes);
    }
    memset(arena, 0, sizeof(*arena));
    return status;
}

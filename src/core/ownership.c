#include "odm_memory.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ODM_OWNER_BLOCK_MAGIC UINT64_C(0x4f444d4f574e4552)

typedef union odm_owner_block odm_owner_block;
union odm_owner_block {
    struct {
        odm_owner_block *previous;
        odm_owner_block *next;
        odm_owner *owner;
        size_t payload_bytes;
        size_t physical_bytes;
        uint64_t magic;
    } fields;
    max_align_t alignment;
};

static int odm_allocator_valid(odm_allocator allocator) {
    return allocator.allocate != NULL && allocator.deallocate != NULL;
}

static int odm_allocator_same(odm_allocator left, odm_allocator right) {
    return left.context == right.context && left.allocate == right.allocate &&
           left.deallocate == right.deallocate;
}

static odm_status odm_size_to_u64(size_t value, uint64_t *out_value) {
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
#if SIZE_MAX > UINT64_MAX
    if (value > (size_t)UINT64_MAX) return ODM_STATUS_OVERFLOW;
#endif
    *out_value = (uint64_t)value;
    return ODM_STATUS_OK;
}

static int odm_u64_add_overflows(uint64_t left, uint64_t right) {
    return right > UINT64_MAX - left;
}

static void odm_secure_clear(void *memory, size_t bytes) {
    volatile uint8_t *cursor = (volatile uint8_t *)memory;
    while (bytes != 0u) {
        *cursor++ = 0u;
        bytes--;
    }
}

static odm_status odm_owner_validate_ledger(const odm_owner *owner) {
    const odm_owner_block *cursor;
    const odm_owner_block *previous = NULL;
    uint64_t payload_bytes = 0u;
    uint64_t physical_bytes = 0u;
    uint64_t allocation_count = 0u;
    if (!owner || owner->initialized == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    cursor = (const odm_owner_block *)owner->head;
    while (cursor) {
        uint64_t payload;
        uint64_t physical;
        if (allocation_count >= owner->allocation_count ||
            cursor->fields.magic != ODM_OWNER_BLOCK_MAGIC ||
            cursor->fields.owner != owner ||
            cursor->fields.previous != previous ||
            cursor->fields.payload_bytes == 0u ||
            cursor->fields.payload_bytes > SIZE_MAX - sizeof(*cursor) ||
            cursor->fields.physical_bytes !=
                sizeof(*cursor) + cursor->fields.payload_bytes) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        if (odm_size_to_u64(cursor->fields.payload_bytes, &payload) !=
                ODM_STATUS_OK ||
            odm_size_to_u64(cursor->fields.physical_bytes, &physical) !=
                ODM_STATUS_OK ||
            odm_u64_add_overflows(payload_bytes, payload) ||
            odm_u64_add_overflows(physical_bytes, physical)) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        payload_bytes += payload;
        physical_bytes += physical;
        allocation_count++;
        previous = cursor;
        cursor = cursor->fields.next;
    }
    if (payload_bytes != owner->payload_bytes ||
        physical_bytes != owner->physical_bytes ||
        allocation_count != owner->allocation_count) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    return ODM_STATUS_OK;
}

static odm_owner_block *odm_owner_find(const odm_owner *owner, const void *memory) {
    odm_owner_block *cursor;
    uint64_t visited = 0u;
    if (!owner || !memory) return NULL;
    cursor = (odm_owner_block *)owner->head;
    while (cursor && visited < owner->allocation_count) {
        if (cursor->fields.magic != ODM_OWNER_BLOCK_MAGIC ||
            cursor->fields.owner != owner) {
            return NULL;
        }
        if ((const void *)(cursor + 1) == memory) return cursor;
        cursor = cursor->fields.next;
        visited++;
    }
    return NULL;
}

static void odm_owner_unlink(odm_owner *owner, odm_owner_block *block) {
    if (block->fields.previous) block->fields.previous->fields.next = block->fields.next;
    else owner->head = block->fields.next;
    if (block->fields.next) block->fields.next->fields.previous = block->fields.previous;
    block->fields.previous = NULL;
    block->fields.next = NULL;
}

static void odm_owner_link_front(odm_owner *owner, odm_owner_block *block) {
    block->fields.previous = NULL;
    block->fields.next = (odm_owner_block *)owner->head;
    if (block->fields.next) block->fields.next->fields.previous = block;
    block->fields.owner = owner;
    owner->head = block;
}

odm_status odm_owner_init(odm_owner *owner, odm_allocator allocator,
                          odm_memory_budget *budget) {
    if (!owner || !odm_allocator_valid(allocator)) return ODM_STATUS_INVALID_ARGUMENT;
    if (owner->initialized != 0u) return ODM_STATUS_INVALID_STATE;
    owner->head = NULL;
    owner->allocator = allocator;
    owner->budget = budget;
    owner->payload_bytes = 0u;
    owner->physical_bytes = 0u;
    owner->allocation_count = 0u;
    owner->reserved = 0u;
    owner->initialized = 1u;
    return ODM_STATUS_OK;
}

odm_status odm_owner_allocate(odm_owner *owner, size_t bytes, void **out_memory) {
    size_t physical_size;
    uint64_t payload_u64;
    uint64_t physical_u64;
    odm_owner_block *block;
    odm_status status;
    if (!owner || !out_memory || owner->initialized == 0u || bytes == 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    *out_memory = NULL;
    status = odm_owner_validate_ledger(owner);
    if (status != ODM_STATUS_OK) return status;
    if (bytes > SIZE_MAX - sizeof(*block)) return ODM_STATUS_OVERFLOW;
    physical_size = sizeof(*block) + bytes;
    status = odm_size_to_u64(bytes, &payload_u64);
    if (status != ODM_STATUS_OK) return status;
    status = odm_size_to_u64(physical_size, &physical_u64);
    if (status != ODM_STATUS_OK) return status;
    if (odm_u64_add_overflows(owner->payload_bytes, payload_u64) ||
        odm_u64_add_overflows(owner->physical_bytes, physical_u64) ||
        owner->allocation_count == UINT64_MAX) {
        return ODM_STATUS_OVERFLOW;
    }
    if (owner->budget) {
        status = odm_memory_budget_reserve(owner->budget, physical_u64);
        if (status != ODM_STATUS_OK) return status;
    }
    block = (odm_owner_block *)owner->allocator.allocate(owner->allocator.context,
                                                         physical_size);
    if (!block) {
        if (owner->budget) (void)odm_memory_budget_release(owner->budget, physical_u64);
        return ODM_STATUS_OUT_OF_MEMORY;
    }
    block->fields.payload_bytes = bytes;
    block->fields.physical_bytes = physical_size;
    block->fields.magic = ODM_OWNER_BLOCK_MAGIC;
    odm_owner_link_front(owner, block);
    owner->payload_bytes += payload_u64;
    owner->physical_bytes += physical_u64;
    owner->allocation_count++;
    *out_memory = block + 1;
    return ODM_STATUS_OK;
}

odm_status odm_owner_release(odm_owner *owner, void *memory) {
    odm_owner_block *block;
    uint64_t payload;
    uint64_t physical;
    odm_status status;
    if (!owner || owner->initialized == 0u || !memory) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_owner_validate_ledger(owner);
    if (status != ODM_STATUS_OK) return status;
    block = odm_owner_find(owner, memory);
    if (!block) return ODM_STATUS_INVALID_ARGUMENT;
    payload = (uint64_t)block->fields.payload_bytes;
    physical = (uint64_t)block->fields.physical_bytes;
    if (payload > owner->payload_bytes || physical > owner->physical_bytes ||
        owner->allocation_count == 0u) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    if (owner->budget) {
        status = odm_memory_budget_release(owner->budget, physical);
        if (status != ODM_STATUS_OK) return status;
    }
    odm_owner_unlink(owner, block);
    owner->payload_bytes -= payload;
    owner->physical_bytes -= physical;
    owner->allocation_count--;
    odm_secure_clear(block + 1, block->fields.payload_bytes);
    block->fields.magic = 0u;
    owner->allocator.deallocate(owner->allocator.context, block);
    return ODM_STATUS_OK;
}

odm_status odm_owner_take(odm_owner *destination, odm_owner *source,
                          void *memory) {
    odm_owner_block *block;
    uint64_t payload;
    uint64_t physical;
    odm_status status;
    if (!destination || !source || destination == source || !memory ||
        destination->initialized == 0u || source->initialized == 0u ||
        !odm_allocator_same(destination->allocator, source->allocator)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_owner_validate_ledger(destination);
    if (status != ODM_STATUS_OK) return status;
    status = odm_owner_validate_ledger(source);
    if (status != ODM_STATUS_OK) return status;
    block = odm_owner_find(source, memory);
    if (!block) return ODM_STATUS_INVALID_ARGUMENT;
    payload = (uint64_t)block->fields.payload_bytes;
    physical = (uint64_t)block->fields.physical_bytes;
    if (odm_u64_add_overflows(destination->payload_bytes, payload) ||
        odm_u64_add_overflows(destination->physical_bytes, physical) ||
        destination->allocation_count == UINT64_MAX) {
        return ODM_STATUS_OVERFLOW;
    }
    if (destination->budget != source->budget && destination->budget) {
        status = odm_memory_budget_reserve(destination->budget, physical);
        if (status != ODM_STATUS_OK) return status;
    }
    if (destination->budget != source->budget && source->budget) {
        status = odm_memory_budget_release(source->budget, physical);
        if (status != ODM_STATUS_OK) {
            if (destination->budget) {
                (void)odm_memory_budget_release(destination->budget, physical);
            }
            return status;
        }
    }
    odm_owner_unlink(source, block);
    odm_owner_link_front(destination, block);
    source->payload_bytes -= payload;
    source->physical_bytes -= physical;
    source->allocation_count--;
    destination->payload_bytes += payload;
    destination->physical_bytes += physical;
    destination->allocation_count++;
    return ODM_STATUS_OK;
}

odm_status odm_owner_move_all(odm_owner *destination, odm_owner *source) {
    odm_owner_block *cursor;
    odm_owner_block *tail;
    odm_status status;
    if (!destination || !source || destination == source ||
        destination->initialized == 0u || source->initialized == 0u ||
        !odm_allocator_same(destination->allocator, source->allocator)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_owner_validate_ledger(destination);
    if (status != ODM_STATUS_OK) return status;
    status = odm_owner_validate_ledger(source);
    if (status != ODM_STATUS_OK) return status;
    if (source->allocation_count == 0u) return ODM_STATUS_OK;
    if (odm_u64_add_overflows(destination->payload_bytes, source->payload_bytes) ||
        odm_u64_add_overflows(destination->physical_bytes, source->physical_bytes) ||
        odm_u64_add_overflows(destination->allocation_count,
                              source->allocation_count)) {
        return ODM_STATUS_OVERFLOW;
    }
    if (destination->budget != source->budget && destination->budget) {
        status = odm_memory_budget_reserve(destination->budget,
                                           source->physical_bytes);
        if (status != ODM_STATUS_OK) return status;
    }
    if (destination->budget != source->budget && source->budget) {
        status = odm_memory_budget_release(source->budget,
                                           source->physical_bytes);
        if (status != ODM_STATUS_OK) {
            if (destination->budget) {
                (void)odm_memory_budget_release(destination->budget,
                                                source->physical_bytes);
            }
            return status;
        }
    }
    cursor = (odm_owner_block *)source->head;
    tail = cursor;
    while (cursor) {
        if (cursor->fields.magic != ODM_OWNER_BLOCK_MAGIC ||
            cursor->fields.owner != source) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        cursor->fields.owner = destination;
        tail = cursor;
        cursor = cursor->fields.next;
    }
    if (tail) {
        tail->fields.next = (odm_owner_block *)destination->head;
        if (tail->fields.next) tail->fields.next->fields.previous = tail;
        ((odm_owner_block *)source->head)->fields.previous = NULL;
        destination->head = source->head;
    }
    destination->payload_bytes += source->payload_bytes;
    destination->physical_bytes += source->physical_bytes;
    destination->allocation_count += source->allocation_count;
    source->head = NULL;
    source->payload_bytes = 0u;
    source->physical_bytes = 0u;
    source->allocation_count = 0u;
    return ODM_STATUS_OK;
}

odm_status odm_owner_read(const odm_owner *owner,
                          odm_owner_snapshot *out_snapshot) {
    if (!owner || !out_snapshot || owner->initialized == 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    out_snapshot->payload_bytes = owner->payload_bytes;
    out_snapshot->physical_bytes = owner->physical_bytes;
    out_snapshot->allocation_count = owner->allocation_count;
    return ODM_STATUS_OK;
}

odm_status odm_owner_destroy(odm_owner *owner) {
    odm_owner_block *cursor;
    odm_status status = ODM_STATUS_OK;
    if (!owner) return ODM_STATUS_INVALID_ARGUMENT;
    if (owner->initialized == 0u) return ODM_STATUS_INVALID_STATE;
    status = odm_owner_validate_ledger(owner);
    if (status != ODM_STATUS_OK) return status;
    cursor = (odm_owner_block *)owner->head;
    while (cursor) {
        odm_owner_block *next = cursor->fields.next;
        uint64_t physical = (uint64_t)cursor->fields.physical_bytes;
        if (cursor->fields.magic != ODM_OWNER_BLOCK_MAGIC ||
            cursor->fields.owner != owner) {
            status = ODM_STATUS_INVARIANT_BROKEN;
            break;
        }
        odm_secure_clear(cursor + 1, cursor->fields.payload_bytes);
        cursor->fields.magic = 0u;
        owner->allocator.deallocate(owner->allocator.context, cursor);
        if (owner->budget &&
            odm_memory_budget_release(owner->budget, physical) != ODM_STATUS_OK) {
            status = ODM_STATUS_INVARIANT_BROKEN;
        }
        cursor = next;
    }
    memset(owner, 0, sizeof(*owner));
    return status;
}

#include "odm_spine.h"

#include <stdio.h>
#include <string.h>

static int odm_spine_module_index_by_id(const odm_spine_module *modules,
                                        size_t count, uint32_t id) {
    size_t index;
    for (index = 0u; index < count; index++) {
        if (modules[index].id == id) return (int)index;
    }
    return -1;
}

static int odm_spine_text_is_truncated(const char *buffer) {
    size_t length;
    if (!buffer) return 0;
    length = strlen(buffer);
    return length >= 3u && buffer[length - 3u] == '.' &&
           buffer[length - 2u] == '.' && buffer[length - 1u] == '.';
}

/* Append one CSV item without making impact traversal fail when the public
 * diagnostic text buffer is full. Counts remain authoritative; only the
 * human-readable list is deterministically truncated with a trailing "...".
 * The fixed public struct therefore keeps its ABI while the DAG can grow. */
static void odm_spine_append_csv_bounded(char *buffer, size_t capacity,
                                         const char *text) {
    size_t used;
    size_t text_length;
    size_t separator;
    const size_t marker_length = 3u;
    if (!buffer || capacity == 0u || !text) return;
    if (odm_spine_text_is_truncated(buffer)) return;
    used = strlen(buffer);
    text_length = strlen(text);
    separator = used == 0u ? 0u : 1u;
    if (used < capacity && separator + text_length < capacity - used) {
        if (separator != 0u) buffer[used++] = ',';
        memcpy(buffer + used, text, text_length + 1u);
        return;
    }
    if (capacity <= marker_length) {
        if (capacity > 0u) buffer[capacity - 1u] = '\0';
        return;
    }
    /* Preserve as much already-written diagnostic text as possible while
     * guaranteeing a NUL-terminated explicit truncation marker. */
    used = strlen(buffer);
    if (used > capacity - marker_length - 1u) {
        used = capacity - marker_length - 1u;
    }
    memcpy(buffer + used, "...", marker_length);
    buffer[used + marker_length] = '\0';
}

odm_status odm_spine_impact_of(const char *module_name,
                               odm_spine_impact *out_impact) {
    const odm_spine_module *modules;
    const odm_spine_dependency *dependencies;
    const odm_spine_invariant *invariants;
    size_t module_count;
    size_t dependency_count;
    size_t invariant_count;
    size_t target_index = SIZE_MAX;
    size_t queue[ODM_SPINE_MAX_MODULES];
    uint8_t visited[ODM_SPINE_MAX_MODULES] = {0};
    size_t queue_head = 0u;
    size_t queue_tail = 0u;
    size_t index;
    if (!module_name || !out_impact) return ODM_STATUS_INVALID_ARGUMENT;
    modules = odm_spine_modules(&module_count);
    dependencies = odm_spine_dependencies(&dependency_count);
    invariants = odm_spine_invariants(&invariant_count);
    if (module_count > ODM_SPINE_MAX_MODULES) return ODM_STATUS_INVARIANT_BROKEN;
    for (index = 0u; index < module_count; index++) {
        if (strcmp(modules[index].name, module_name) == 0 ||
            strcmp(modules[index].path, module_name) == 0) {
            target_index = index;
            break;
        }
    }
    if (target_index == SIZE_MAX) return ODM_STATUS_UNSUPPORTED;
    memset(out_impact, 0, sizeof(*out_impact));
    out_impact->module_id = modules[target_index].id;
    (void)snprintf(out_impact->module, sizeof(out_impact->module), "%s",
                   modules[target_index].name);
    visited[target_index] = 1u;
    queue[queue_tail++] = target_index;
    while (queue_head < queue_tail) {
        size_t current = queue[queue_head++];
        for (index = 0u; index < dependency_count; index++) {
            int dependent_index;
            if (dependencies[index].dependency != modules[current].id) continue;
            dependent_index = odm_spine_module_index_by_id(
                modules, module_count, dependencies[index].module);
            if (dependent_index < 0) return ODM_STATUS_INVARIANT_BROKEN;
            if (current == target_index) out_impact->direct_dependents++;
            if (visited[(size_t)dependent_index] == 0u) {
                visited[(size_t)dependent_index] = 1u;
                queue[queue_tail++] = (size_t)dependent_index;
                out_impact->transitive_dependents++;
                odm_spine_append_csv_bounded(out_impact->dependents,
                                             sizeof(out_impact->dependents),
                                             modules[(size_t)dependent_index].name);
            }
        }
    }
    for (index = 0u; index < invariant_count; index++) {
        int module_index = odm_spine_module_index_by_id(
            modules, module_count, invariants[index].module);
        if (module_index < 0) return ODM_STATUS_INVARIANT_BROKEN;
        if (visited[(size_t)module_index] != 0u) {
            odm_spine_append_csv_bounded(out_impact->invariants_at_stake,
                                         sizeof(out_impact->invariants_at_stake),
                                         invariants[index].id);
        }
    }
    return ODM_STATUS_OK;
}

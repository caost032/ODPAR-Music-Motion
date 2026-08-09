#include "odm_spine.h"
#include "odm_fixed.h"
#include "odm_rng.h"
#include "odm_time.h"
#include "odm_version.h"
#include "spine_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    int overflow;
} odm_json_writer;

static int odm_spine_module_index(const odm_spine_module *modules, size_t count,
                                  uint32_t id) {
    size_t index;
    for (index = 0u; index < count; index++) {
        if (modules[index].id == id) return (int)index;
    }
    return -1;
}

static int odm_spine_string_unique(const char *value, size_t before,
                                   const char *const *values) {
    size_t index;
    for (index = 0u; index < before; index++) {
        if (strcmp(value, values[index]) == 0) return 0;
    }
    return 1;
}

odm_status odm_spine_validate(odm_spine_summary *out_summary) {
    const odm_spine_module *modules;
    const odm_spine_dependency *dependencies;
    const odm_spine_invariant *invariants;
    const odm_spine_ownership *ownerships;
    const odm_spine_guardian *guardians;
    const odm_spine_capability *capabilities;
    const odm_spine_state_model *state_models;
    const odm_spine_backend *renderer_backends;
    const odm_spine_backend *media_adapters;
    const odm_spine_backend *output_profiles;
    const odm_spine_parity *parity_contracts;
    const odm_spine_bug *known_bugs;
    const odm_spine_investigation *security_investigations;
    size_t module_count;
    size_t dependency_count;
    size_t invariant_count;
    size_t ownership_count;
    size_t guardian_count;
    size_t capability_count;
    size_t state_model_count;
    size_t renderer_backend_count;
    size_t media_adapter_count;
    size_t output_profile_count;
    size_t parity_contract_count;
    size_t known_bug_count;
    size_t security_investigation_count;
    uint32_t indegree[ODM_SPINE_MAX_MODULES] = {0};
    size_t queue[ODM_SPINE_MAX_MODULES];
    const char *names[ODM_SPINE_MAX_MODULES];
    const char *paths[ODM_SPINE_MAX_MODULES];
    size_t queue_head = 0u;
    size_t queue_tail = 0u;
    size_t visited = 0u;
    size_t index;

    modules = odm_spine_modules(&module_count);
    dependencies = odm_spine_dependencies(&dependency_count);
    invariants = odm_spine_invariants(&invariant_count);
    ownerships = odm_spine_ownerships(&ownership_count);
    guardians = odm_spine_guardians(&guardian_count);
    capabilities = odm_spine_capabilities(&capability_count);
    state_models = odm_spine_state_models(&state_model_count);
    renderer_backends = odm_spine_renderer_backends(&renderer_backend_count);
    media_adapters = odm_spine_media_adapters(&media_adapter_count);
    output_profiles = odm_spine_output_profiles(&output_profile_count);
    parity_contracts = odm_spine_parity_contracts(&parity_contract_count);
    known_bugs = odm_spine_known_bugs(&known_bug_count);
    security_investigations = odm_spine_security_investigations(
        &security_investigation_count);
    if (!out_summary || module_count == 0u || module_count > ODM_SPINE_MAX_MODULES ||
        module_count > UINT32_MAX || dependency_count > UINT32_MAX ||
        invariant_count > UINT32_MAX || ownership_count > UINT32_MAX ||
        guardian_count > UINT32_MAX || capability_count > UINT32_MAX ||
        state_model_count > UINT32_MAX || renderer_backend_count > UINT32_MAX ||
        media_adapter_count > UINT32_MAX || output_profile_count > UINT32_MAX ||
        parity_contract_count > UINT32_MAX || known_bug_count > UINT32_MAX ||
        security_investigation_count > UINT32_MAX ||
        (state_model_count != 0u && !state_models) ||
        (renderer_backend_count != 0u && !renderer_backends) ||
        (media_adapter_count != 0u && !media_adapters) ||
        (output_profile_count != 0u && !output_profiles) ||
        (parity_contract_count != 0u && !parity_contracts) ||
        (known_bug_count != 0u && !known_bugs) ||
        (security_investigation_count != 0u && !security_investigations)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    memset(out_summary, 0, sizeof(*out_summary));

    for (index = 0u; index < module_count; index++) {
        size_t earlier;
        if (modules[index].id == 0u || !modules[index].name || !modules[index].path ||
            !modules[index].role || modules[index].name[0] == '\0' ||
            modules[index].path[0] == '\0' || modules[index].role[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        names[index] = modules[index].name;
        paths[index] = modules[index].path;
        if (!odm_spine_string_unique(modules[index].name, index, names) ||
            !odm_spine_string_unique(modules[index].path, index, paths)) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (modules[earlier].id == modules[index].id) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
    }

    for (index = 0u; index < dependency_count; index++) {
        int module_index = odm_spine_module_index(modules, module_count,
                                                  dependencies[index].module);
        int dependency_index = odm_spine_module_index(
            modules, module_count, dependencies[index].dependency);
        size_t earlier;
        if (module_index < 0 || dependency_index < 0 ||
            module_index == dependency_index ||
            modules[(size_t)module_index].layer <=
                modules[(size_t)dependency_index].layer) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (dependencies[earlier].module == dependencies[index].module &&
                dependencies[earlier].dependency == dependencies[index].dependency) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
        if (indegree[(size_t)dependency_index] == UINT32_MAX) {
            return ODM_STATUS_OVERFLOW;
        }
        indegree[(size_t)dependency_index]++;
    }

    for (index = 0u; index < module_count; index++) {
        uint32_t expected_layer = 0u;
        int has_dependency = 0;
        size_t edge;
        for (edge = 0u; edge < dependency_count; edge++) {
            if (dependencies[edge].module == modules[index].id) {
                int dependency_index = odm_spine_module_index(
                    modules, module_count, dependencies[edge].dependency);
                uint32_t candidate;
                if (dependency_index < 0) return ODM_STATUS_INVARIANT_BROKEN;
                candidate = modules[(size_t)dependency_index].layer + 1u;
                if (candidate > expected_layer) expected_layer = candidate;
                has_dependency = 1;
            }
        }
        if ((!has_dependency && modules[index].layer != 0u) ||
            (has_dependency && modules[index].layer != expected_layer)) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        if (indegree[index] == 0u) queue[queue_tail++] = index;
    }

    while (queue_head < queue_tail) {
        size_t current = queue[queue_head++];
        size_t edge;
        visited++;
        for (edge = 0u; edge < dependency_count; edge++) {
            if (dependencies[edge].module == modules[current].id) {
                int dependency_index = odm_spine_module_index(
                    modules, module_count, dependencies[edge].dependency);
                if (dependency_index < 0 || indegree[(size_t)dependency_index] == 0u) {
                    return ODM_STATUS_INVARIANT_BROKEN;
                }
                indegree[(size_t)dependency_index]--;
                if (indegree[(size_t)dependency_index] == 0u) {
                    queue[queue_tail++] = (size_t)dependency_index;
                }
            }
        }
    }
    if (visited != module_count) return ODM_STATUS_INVARIANT_BROKEN;

    for (index = 0u; index < invariant_count; index++) {
        size_t earlier;
        if (!invariants[index].id || !invariants[index].guarantee ||
            !invariants[index].mechanism || !invariants[index].evidence ||
            invariants[index].id[0] == '\0' || invariants[index].evidence[0] == '\0' ||
            odm_spine_module_index(modules, module_count,
                                   invariants[index].module) < 0) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (strcmp(invariants[earlier].id, invariants[index].id) == 0) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
    }
    for (index = 0u; index < ownership_count; index++) {
        size_t earlier;
        if (!ownerships[index].resource || !ownerships[index].acquire ||
            !ownerships[index].release || !ownerships[index].transfer ||
            ownerships[index].resource[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (strcmp(ownerships[earlier].resource,
                       ownerships[index].resource) == 0) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
    }
    for (index = 0u; index < guardian_count; index++) {
        size_t earlier;
        if (!guardians[index].id || !guardians[index].path ||
            !guardians[index].scope || guardians[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (strcmp(guardians[earlier].id, guardians[index].id) == 0) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
    }
    for (index = 0u; index < capability_count; index++) {
        size_t earlier;
        if (!capabilities[index].id || !capabilities[index].state ||
            !capabilities[index].evidence || capabilities[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (strcmp(capabilities[earlier].id, capabilities[index].id) == 0) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
    }
    for (index = 0u; index < state_model_count; index++) {
        if (!state_models[index].capability || !state_models[index].model ||
            !state_models[index].evidence ||
            state_models[index].capability[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    for (index = 0u; index < renderer_backend_count; index++) {
        if (!renderer_backends[index].id || !renderer_backends[index].state ||
            !renderer_backends[index].evidence ||
            renderer_backends[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    for (index = 0u; index < media_adapter_count; index++) {
        if (!media_adapters[index].id || !media_adapters[index].state ||
            !media_adapters[index].evidence || media_adapters[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    for (index = 0u; index < output_profile_count; index++) {
        if (!output_profiles[index].id || !output_profiles[index].state ||
            !output_profiles[index].evidence ||
            output_profiles[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    for (index = 0u; index < parity_contract_count; index++) {
        if (!parity_contracts[index].id ||
            !parity_contracts[index].left_semantics ||
            !parity_contracts[index].right_semantics ||
            !parity_contracts[index].state || !parity_contracts[index].evidence ||
            parity_contracts[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    for (index = 0u; index < known_bug_count; index++) {
        if (!known_bugs[index].id || !known_bugs[index].severity ||
            !known_bugs[index].state || !known_bugs[index].evidence ||
            known_bugs[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
    for (index = 0u; index < security_investigation_count; index++) {
        size_t earlier;
        if (!security_investigations[index].id ||
            !security_investigations[index].topic ||
            !security_investigations[index].state ||
            !security_investigations[index].evidence ||
            security_investigations[index].id[0] == '\0') {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        for (earlier = 0u; earlier < index; earlier++) {
            if (strcmp(security_investigations[earlier].id,
                       security_investigations[index].id) == 0) {
                return ODM_STATUS_INVARIANT_BROKEN;
            }
        }
    }

    out_summary->module_count = (uint32_t)module_count;
    out_summary->dependency_count = (uint32_t)dependency_count;
    out_summary->invariant_count = (uint32_t)invariant_count;
    out_summary->ownership_count = (uint32_t)ownership_count;
    out_summary->guardian_count = (uint32_t)guardian_count;
    out_summary->capability_count = (uint32_t)capability_count;
    out_summary->state_model_count = (uint32_t)state_model_count;
    out_summary->renderer_backend_count = (uint32_t)renderer_backend_count;
    out_summary->media_adapter_count = (uint32_t)media_adapter_count;
    out_summary->output_profile_count = (uint32_t)output_profile_count;
    out_summary->parity_contract_count = (uint32_t)parity_contract_count;
    out_summary->known_bug_count = (uint32_t)known_bug_count;
    out_summary->security_investigation_count =
        (uint32_t)security_investigation_count;
    out_summary->source_file_count = odm_spine_registry_source_file_count();
    out_summary->source_line_count = odm_spine_registry_source_line_count();
    out_summary->graph_acyclic = 1u;
    return ODM_STATUS_OK;
}

static void odm_json_write_bytes(odm_json_writer *writer, const char *bytes,
                                 size_t length) {
    if (length > SIZE_MAX - writer->length) {
        writer->overflow = 1;
        return;
    }
    if (writer->buffer && writer->length + length < writer->capacity) {
        memcpy(writer->buffer + writer->length, bytes, length);
    }
    writer->length += length;
}

static void odm_json_write_literal(odm_json_writer *writer, const char *literal) {
    odm_json_write_bytes(writer, literal, strlen(literal));
}

static void odm_json_write_u64(odm_json_writer *writer, uint64_t value) {
    char text[32];
    int length = snprintf(text, sizeof(text), "%" PRIu64 "", value);
    if (length < 0 || (size_t)length >= sizeof(text)) {
        writer->overflow = 1;
        return;
    }
    odm_json_write_bytes(writer, text, (size_t)length);
}

static void odm_json_write_i64(odm_json_writer *writer, int64_t value) {
    char text[32];
    int length = snprintf(text, sizeof(text), "%" PRId64 "", value);
    if (length < 0 || (size_t)length >= sizeof(text)) {
        writer->overflow = 1;
        return;
    }
    odm_json_write_bytes(writer, text, (size_t)length);
}

static void odm_json_write_string(odm_json_writer *writer, const char *value) {
    static const char hexadecimal[] = "0123456789abcdef";
    const unsigned char *cursor;
    if (!value) {
        odm_json_write_literal(writer, "null");
        return;
    }
    odm_json_write_literal(writer, "\"");
    cursor = (const unsigned char *)value;
    while (*cursor != 0u) {
        unsigned char byte = *cursor++;
        if (byte == '"' || byte == '\\') {
            char escaped[2] = {'\\', (char)byte};
            odm_json_write_bytes(writer, escaped, sizeof(escaped));
        } else if (byte == '\b') odm_json_write_literal(writer, "\\b");
        else if (byte == '\f') odm_json_write_literal(writer, "\\f");
        else if (byte == '\n') odm_json_write_literal(writer, "\\n");
        else if (byte == '\r') odm_json_write_literal(writer, "\\r");
        else if (byte == '\t') odm_json_write_literal(writer, "\\t");
        else if (byte < 0x20u) {
            char escaped[6] = {'\\', 'u', '0', '0',
                               hexadecimal[byte >> 4], hexadecimal[byte & 0x0fu]};
            odm_json_write_bytes(writer, escaped, sizeof(escaped));
        } else {
            char plain = (char)byte;
            odm_json_write_bytes(writer, &plain, 1u);
        }
    }
    odm_json_write_literal(writer, "\"");
}

static odm_status odm_json_finish(odm_json_writer *writer, size_t *out_required) {
    size_t required;
    if (!out_required || writer->overflow || writer->length == SIZE_MAX) {
        if (writer->buffer && writer->capacity != 0u) writer->buffer[0] = '\0';
        return writer->overflow ? ODM_STATUS_OVERFLOW : ODM_STATUS_INVALID_ARGUMENT;
    }
    required = writer->length + 1u;
    *out_required = required;
    if (!writer->buffer || writer->capacity < required) {
        if (writer->buffer && writer->capacity != 0u) writer->buffer[0] = '\0';
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    writer->buffer[writer->length] = '\0';
    return ODM_STATUS_OK;
}

odm_status odm_spine_summary_json(char *buffer, size_t capacity,
                                  size_t *out_required) {
    odm_spine_summary summary;
    odm_json_writer writer = {buffer, capacity, 0u, 0};
    odm_status status = odm_spine_validate(&summary);
    if (status != ODM_STATUS_OK) return status;
    odm_json_write_literal(&writer, "{\"schema\":\"odm_spine_summary_v1\",\"version\":");
    odm_json_write_string(&writer, odm_version_string());
    odm_json_write_literal(&writer, ",\"source_id\":");
    odm_json_write_string(&writer, odm_source_id());
    odm_json_write_literal(&writer, ",\"compiler\":");
    odm_json_write_string(&writer, odm_compiler_id());
    odm_json_write_literal(&writer, ",\"modules\":");
    odm_json_write_u64(&writer, summary.module_count);
    odm_json_write_literal(&writer, ",\"dependencies\":");
    odm_json_write_u64(&writer, summary.dependency_count);
    odm_json_write_literal(&writer, ",\"invariants\":");
    odm_json_write_u64(&writer, summary.invariant_count);
    odm_json_write_literal(&writer, ",\"ownership_pairs\":");
    odm_json_write_u64(&writer, summary.ownership_count);
    odm_json_write_literal(&writer, ",\"guardians\":");
    odm_json_write_u64(&writer, summary.guardian_count);
    odm_json_write_literal(&writer, ",\"capabilities\":");
    odm_json_write_u64(&writer, summary.capability_count);
    odm_json_write_literal(&writer, ",\"state_models\":");
    odm_json_write_u64(&writer, summary.state_model_count);
    odm_json_write_literal(&writer, ",\"renderer_backends\":");
    odm_json_write_u64(&writer, summary.renderer_backend_count);
    odm_json_write_literal(&writer, ",\"media_adapters\":");
    odm_json_write_u64(&writer, summary.media_adapter_count);
    odm_json_write_literal(&writer, ",\"output_profiles\":");
    odm_json_write_u64(&writer, summary.output_profile_count);
    odm_json_write_literal(&writer, ",\"parity_contracts\":");
    odm_json_write_u64(&writer, summary.parity_contract_count);
    odm_json_write_literal(&writer, ",\"known_bugs\":");
    odm_json_write_u64(&writer, summary.known_bug_count);
    odm_json_write_literal(&writer, ",\"security_investigations\":");
    odm_json_write_u64(&writer, summary.security_investigation_count);
    odm_json_write_literal(&writer, ",\"source_files\":");
    odm_json_write_u64(&writer, summary.source_file_count);
    odm_json_write_literal(&writer, ",\"source_lines\":");
    odm_json_write_u64(&writer, summary.source_line_count);
    odm_json_write_literal(&writer, ",\"graph_acyclic\":true,\"gate0_claim\":\"requires_external_gate_evidence\",\"gate1_claim\":\"requires_external_gate_evidence\"}");
    return odm_json_finish(&writer, out_required);
}

odm_status odm_spine_report_json(char *buffer, size_t capacity,
                                 size_t *out_required) {
    const odm_spine_module *modules;
    const odm_spine_dependency *dependencies;
    const odm_spine_invariant *invariants;
    const odm_spine_ownership *ownerships;
    const odm_spine_guardian *guardians;
    const odm_spine_capability *capabilities;
    const odm_spine_state_model *state_models;
    const odm_spine_backend *renderer_backends;
    const odm_spine_backend *media_adapters;
    const odm_spine_backend *output_profiles;
    const odm_spine_parity *parity_contracts;
    const odm_spine_bug *known_bugs;
    const odm_spine_investigation *security_investigations;
    size_t module_count;
    size_t dependency_count;
    size_t invariant_count;
    size_t ownership_count;
    size_t guardian_count;
    size_t capability_count;
    size_t state_model_count;
    size_t renderer_backend_count;
    size_t media_adapter_count;
    size_t output_profile_count;
    size_t parity_contract_count;
    size_t known_bug_count;
    size_t security_investigation_count;
    odm_spine_summary summary;
    odm_json_writer writer = {buffer, capacity, 0u, 0};
    odm_status status = odm_spine_validate(&summary);
    size_t index;
    if (status != ODM_STATUS_OK) return status;
    modules = odm_spine_modules(&module_count);
    dependencies = odm_spine_dependencies(&dependency_count);
    invariants = odm_spine_invariants(&invariant_count);
    ownerships = odm_spine_ownerships(&ownership_count);
    guardians = odm_spine_guardians(&guardian_count);
    capabilities = odm_spine_capabilities(&capability_count);
    state_models = odm_spine_state_models(&state_model_count);
    renderer_backends = odm_spine_renderer_backends(&renderer_backend_count);
    media_adapters = odm_spine_media_adapters(&media_adapter_count);
    output_profiles = odm_spine_output_profiles(&output_profile_count);
    parity_contracts = odm_spine_parity_contracts(&parity_contract_count);
    known_bugs = odm_spine_known_bugs(&known_bug_count);
    security_investigations = odm_spine_security_investigations(
        &security_investigation_count);

    odm_json_write_literal(&writer, "{\"schema\":\"odm_music_spine_v1\",\"identity\":{\"name\":\"ODPAR Music\",\"kind\":\"verifiable audiovisual compiler/renderer\"},\"build\":{\"version\":");
    odm_json_write_string(&writer, odm_version_string());
    odm_json_write_literal(&writer, ",\"abi_version\":");
    odm_json_write_u64(&writer, odm_abi_version());
    odm_json_write_literal(&writer, ",\"source_id\":");
    odm_json_write_string(&writer, odm_source_id());
    odm_json_write_literal(&writer, ",\"compiler\":");
    odm_json_write_string(&writer, odm_compiler_id());
    odm_json_write_literal(&writer, ",\"c_standard\":201112,\"wallclock_free_identity\":true},\"time_contract\":{\"version\":0,\"sample_rate\":");
    odm_json_write_i64(&writer, ODM_TIMELINE_RATE);
    odm_json_write_literal(&writer, ",\"supported_fps\":[24,25,30,50,60],\"fractional_ntsc_supported\":false,\"music_map_rate\":");
    odm_json_write_i64(&writer, ODM_MUSIC_MAP_RATE);
    odm_json_write_literal(&writer, ",\"music_map_tick_samples\":");
    odm_json_write_i64(&writer, ODM_MUSIC_MAP_TICK_SAMPLES);
    odm_json_write_literal(&writer, "},\"numeric_contract\":{\"pcm\":\"Q1.31 int32\",\"authored_parameters\":\"integer microunits\",\"geometry\":\"Q32.32 int64\",\"phase\":\"uint32 full turn\",\"rounding\":");
    odm_json_write_string(&writer, ODM_FIXED_ROUNDING);
    odm_json_write_literal(&writer, ",\"rng\":");
    odm_json_write_string(&writer, ODM_RNG_ALGORITHM);
    odm_json_write_literal(&writer, "},\"totals\":{\"modules\":");
    odm_json_write_u64(&writer, summary.module_count);
    odm_json_write_literal(&writer, ",\"dependencies\":");
    odm_json_write_u64(&writer, summary.dependency_count);
    odm_json_write_literal(&writer, ",\"invariants\":");
    odm_json_write_u64(&writer, summary.invariant_count);
    odm_json_write_literal(&writer, ",\"ownership_pairs\":");
    odm_json_write_u64(&writer, summary.ownership_count);
    odm_json_write_literal(&writer, ",\"guardians\":");
    odm_json_write_u64(&writer, summary.guardian_count);
    odm_json_write_literal(&writer, ",\"capabilities\":");
    odm_json_write_u64(&writer, summary.capability_count);
    odm_json_write_literal(&writer, ",\"state_models\":");
    odm_json_write_u64(&writer, summary.state_model_count);
    odm_json_write_literal(&writer, ",\"renderer_backends\":");
    odm_json_write_u64(&writer, summary.renderer_backend_count);
    odm_json_write_literal(&writer, ",\"media_adapters\":");
    odm_json_write_u64(&writer, summary.media_adapter_count);
    odm_json_write_literal(&writer, ",\"output_profiles\":");
    odm_json_write_u64(&writer, summary.output_profile_count);
    odm_json_write_literal(&writer, ",\"parity_contracts\":");
    odm_json_write_u64(&writer, summary.parity_contract_count);
    odm_json_write_literal(&writer, ",\"known_bugs\":");
    odm_json_write_u64(&writer, summary.known_bug_count);
    odm_json_write_literal(&writer, ",\"security_investigations\":");
    odm_json_write_u64(&writer, summary.security_investigation_count);
    odm_json_write_literal(&writer, ",\"source_files\":");
    odm_json_write_u64(&writer, summary.source_file_count);
    odm_json_write_literal(&writer, ",\"source_lines\":");
    odm_json_write_u64(&writer, summary.source_line_count);
    odm_json_write_literal(&writer, "},\"graph_acyclic\":true,\"maturity_policy\":\"external evidence required; registry presence is not a passing test\",\"modules\":[");
    for (index = 0u; index < module_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_u64(&writer, modules[index].id);
        odm_json_write_literal(&writer, ",\"name\":");
        odm_json_write_string(&writer, modules[index].name);
        odm_json_write_literal(&writer, ",\"path\":");
        odm_json_write_string(&writer, modules[index].path);
        odm_json_write_literal(&writer, ",\"layer\":");
        odm_json_write_u64(&writer, modules[index].layer);
        odm_json_write_literal(&writer, ",\"source_lines\":");
        odm_json_write_u64(&writer, modules[index].source_lines);
        odm_json_write_literal(&writer, ",\"role\":");
        odm_json_write_string(&writer, modules[index].role);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"dependencies\":[");
    for (index = 0u; index < dependency_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"module\":");
        odm_json_write_u64(&writer, dependencies[index].module);
        odm_json_write_literal(&writer, ",\"dependency\":");
        odm_json_write_u64(&writer, dependencies[index].dependency);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"invariants\":[");
    for (index = 0u; index < invariant_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, invariants[index].id);
        odm_json_write_literal(&writer, ",\"module\":");
        odm_json_write_u64(&writer, invariants[index].module);
        odm_json_write_literal(&writer, ",\"guarantee\":");
        odm_json_write_string(&writer, invariants[index].guarantee);
        odm_json_write_literal(&writer, ",\"mechanism\":");
        odm_json_write_string(&writer, invariants[index].mechanism);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, invariants[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"ownership\":[");
    for (index = 0u; index < ownership_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"resource\":");
        odm_json_write_string(&writer, ownerships[index].resource);
        odm_json_write_literal(&writer, ",\"acquire\":");
        odm_json_write_string(&writer, ownerships[index].acquire);
        odm_json_write_literal(&writer, ",\"release\":");
        odm_json_write_string(&writer, ownerships[index].release);
        odm_json_write_literal(&writer, ",\"transfer\":");
        odm_json_write_string(&writer, ownerships[index].transfer);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"guardians\":[");
    for (index = 0u; index < guardian_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, guardians[index].id);
        odm_json_write_literal(&writer, ",\"path\":");
        odm_json_write_string(&writer, guardians[index].path);
        odm_json_write_literal(&writer, ",\"scope\":");
        odm_json_write_string(&writer, guardians[index].scope);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"capabilities\":[");
    for (index = 0u; index < capability_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, capabilities[index].id);
        odm_json_write_literal(&writer, ",\"version_major\":");
        odm_json_write_u64(&writer, capabilities[index].version_major);
        odm_json_write_literal(&writer, ",\"version_minor\":");
        odm_json_write_u64(&writer, capabilities[index].version_minor);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, capabilities[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, capabilities[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"state_models\":[");
    for (index = 0u; index < state_model_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"capability\":");
        odm_json_write_string(&writer, state_models[index].capability);
        odm_json_write_literal(&writer, ",\"model\":");
        odm_json_write_string(&writer, state_models[index].model);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, state_models[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"renderer_backends\":[");
    for (index = 0u; index < renderer_backend_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, renderer_backends[index].id);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, renderer_backends[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, renderer_backends[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"media_adapters\":[");
    for (index = 0u; index < media_adapter_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, media_adapters[index].id);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, media_adapters[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, media_adapters[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"output_profiles\":[");
    for (index = 0u; index < output_profile_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, output_profiles[index].id);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, output_profiles[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, output_profiles[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"parity_contracts\":[");
    for (index = 0u; index < parity_contract_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, parity_contracts[index].id);
        odm_json_write_literal(&writer, ",\"left_semantics\":");
        odm_json_write_string(&writer, parity_contracts[index].left_semantics);
        odm_json_write_literal(&writer, ",\"right_semantics\":");
        odm_json_write_string(&writer, parity_contracts[index].right_semantics);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, parity_contracts[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, parity_contracts[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"known_bugs\":[");
    for (index = 0u; index < known_bug_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, known_bugs[index].id);
        odm_json_write_literal(&writer, ",\"severity\":");
        odm_json_write_string(&writer, known_bugs[index].severity);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, known_bugs[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, known_bugs[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"security_investigations\":[");
    for (index = 0u; index < security_investigation_count; index++) {
        if (index != 0u) odm_json_write_literal(&writer, ",");
        odm_json_write_literal(&writer, "{\"id\":");
        odm_json_write_string(&writer, security_investigations[index].id);
        odm_json_write_literal(&writer, ",\"topic\":");
        odm_json_write_string(&writer, security_investigations[index].topic);
        odm_json_write_literal(&writer, ",\"state\":");
        odm_json_write_string(&writer, security_investigations[index].state);
        odm_json_write_literal(&writer, ",\"evidence\":");
        odm_json_write_string(&writer, security_investigations[index].evidence);
        odm_json_write_literal(&writer, "}");
    }
    odm_json_write_literal(&writer, "],\"known_limits\":[\"no Media Truth adapter or decoding before Gate 2\",\"no Music Map implementation before Gate 3\",\"no Visual Score, Render IR, renderer, Halo, Grid or effects before their owning gates\",\"no state models, renderer backends, media adapters, output profiles or parity contracts are registered before implementation evidence\",\"fractional NTSC rates are outside time contract v0\"]}");
    return odm_json_finish(&writer, out_required);
}

odm_status odm_spine_impact_json(const odm_spine_impact *impact,
                                 char *buffer, size_t capacity,
                                 size_t *out_required) {
    odm_json_writer writer = {buffer, capacity, 0u, 0};
    if (!impact) return ODM_STATUS_INVALID_ARGUMENT;
    odm_json_write_literal(&writer, "{\"schema\":\"odm_spine_impact_v1\",\"module_id\":");
    odm_json_write_u64(&writer, impact->module_id);
    odm_json_write_literal(&writer, ",\"module\":");
    odm_json_write_string(&writer, impact->module);
    odm_json_write_literal(&writer, ",\"direct_dependents\":");
    odm_json_write_u64(&writer, impact->direct_dependents);
    odm_json_write_literal(&writer, ",\"transitive_dependents\":");
    odm_json_write_u64(&writer, impact->transitive_dependents);
    odm_json_write_literal(&writer, ",\"dependents\":");
    odm_json_write_string(&writer,
                          impact->dependents[0] != '\0' ? impact->dependents : NULL);
    odm_json_write_literal(&writer, ",\"invariants_at_stake\":");
    odm_json_write_string(&writer, impact->invariants_at_stake[0] != '\0'
                                   ? impact->invariants_at_stake : NULL);
    odm_json_write_literal(&writer, ",\"dependents_text_truncated\":");
    odm_json_write_literal(&writer,
        strstr(impact->dependents, "...") != NULL ? "true" : "false");
    odm_json_write_literal(&writer, ",\"invariants_text_truncated\":");
    odm_json_write_literal(&writer,
        strstr(impact->invariants_at_stake, "...") != NULL ? "true" : "false");
    odm_json_write_literal(&writer, "}");
    return odm_json_finish(&writer, out_required);
}

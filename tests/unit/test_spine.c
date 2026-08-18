#include "test_harness.h"

#include "odm_spine.h"
#include "odm_version.h"

#include <stdlib.h>
#include <string.h>

static size_t spine_module_index_for_id(const odm_spine_module *modules, size_t count,
                                        uint32_t id) {
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (modules[i].id == id) return i;
    }
    return SIZE_MAX;
}

static int spine_expected_dependents(const char *name, uint32_t *out_direct,
                                     uint32_t *out_transitive) {
    size_t module_count = 0u, dependency_count = 0u, target_index = SIZE_MAX, i;
    const odm_spine_module *modules = odm_spine_modules(&module_count);
    const odm_spine_dependency *dependencies = odm_spine_dependencies(&dependency_count);
    uint8_t seen[ODM_SPINE_MAX_MODULES];
    uint32_t direct = 0u, transitive = 0u;
    int changed;
    if (!name || !out_direct || !out_transitive || !modules || !dependencies ||
        module_count > ODM_SPINE_MAX_MODULES) return 0;
    for (i = 0u; i < module_count; ++i) {
        if (strcmp(modules[i].name, name) == 0) { target_index = i; break; }
    }
    if (target_index == SIZE_MAX) return 0;
    memset(seen, 0, sizeof(seen));
    for (i = 0u; i < dependency_count; ++i) {
        if (dependencies[i].dependency == modules[target_index].id) {
            size_t index = spine_module_index_for_id(modules, module_count, dependencies[i].module);
            if (index == SIZE_MAX) return 0;
            if (seen[index] == 0u) { seen[index] = 1u; ++direct; }
        }
    }
    do {
        changed = 0;
        for (i = 0u; i < dependency_count; ++i) {
            size_t dependency_index = spine_module_index_for_id(
                modules, module_count, dependencies[i].dependency);
            size_t module_index = spine_module_index_for_id(
                modules, module_count, dependencies[i].module);
            if (dependency_index == SIZE_MAX || module_index == SIZE_MAX) return 0;
            if (seen[dependency_index] != 0u && seen[module_index] == 0u) {
                seen[module_index] = 1u;
                changed = 1;
            }
        }
    } while (changed != 0);
    for (i = 0u; i < module_count; ++i) {
        if (seen[i] != 0u) ++transitive;
    }
    *out_direct = direct;
    *out_transitive = transitive;
    return 1;
}

void odm_test_spine(odm_test_context *context) {
    odm_spine_summary summary;
    size_t required = 0u;
    char *first;
    char *second;
    {
        size_t modules_n = 0u, dependencies_n = 0u, invariants_n = 0u;
        size_t capabilities_n = 0u, states_n = 0u, renderers_n = 0u;
        size_t adapters_n = 0u, outputs_n = 0u, parities_n = 0u;
        size_t bugs_n = 0u, investigations_n = 0u;
        (void)odm_spine_modules(&modules_n);
        (void)odm_spine_dependencies(&dependencies_n);
        (void)odm_spine_invariants(&invariants_n);
        (void)odm_spine_capabilities(&capabilities_n);
        (void)odm_spine_state_models(&states_n);
        (void)odm_spine_renderer_backends(&renderers_n);
        (void)odm_spine_media_adapters(&adapters_n);
        (void)odm_spine_output_profiles(&outputs_n);
        (void)odm_spine_parity_contracts(&parities_n);
        (void)odm_spine_known_bugs(&bugs_n);
        (void)odm_spine_security_investigations(&investigations_n);
        ODM_TEST_CHECK(context, odm_spine_validate(&summary) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, summary.module_count == (uint32_t)modules_n);
        ODM_TEST_CHECK(context, summary.dependency_count == (uint32_t)dependencies_n);
        ODM_TEST_CHECK(context, summary.invariant_count == (uint32_t)invariants_n);
        ODM_TEST_CHECK(context, summary.capability_count == (uint32_t)capabilities_n);
        ODM_TEST_CHECK(context, summary.state_model_count == (uint32_t)states_n);
        ODM_TEST_CHECK(context, summary.renderer_backend_count == (uint32_t)renderers_n);
        ODM_TEST_CHECK(context, summary.media_adapter_count == (uint32_t)adapters_n);
        ODM_TEST_CHECK(context, summary.output_profile_count == (uint32_t)outputs_n);
        ODM_TEST_CHECK(context, summary.parity_contract_count == (uint32_t)parities_n);
        ODM_TEST_CHECK(context, summary.known_bug_count == (uint32_t)bugs_n);
        ODM_TEST_CHECK(context, summary.security_investigation_count == (uint32_t)investigations_n);
    }
    ODM_TEST_CHECK(context, summary.graph_acyclic == 1u);
    ODM_TEST_CHECK(context, summary.source_file_count == summary.module_count);
    ODM_TEST_CHECK(context, summary.source_line_count > 0u);
    ODM_TEST_CHECK(context,
                   strncmp(odm_source_id(), "sha256:", 7u) == 0);

    ODM_TEST_CHECK(context,
                   odm_spine_report_json(NULL, 0u, &required) ==
                       ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, required > 1000u);
    first = (char *)malloc(required);
    second = (char *)malloc(required);
    ODM_TEST_CHECK(context, first != NULL);
    ODM_TEST_CHECK(context, second != NULL);
    if (first && second) {
        size_t second_required = 0u;
        ODM_TEST_CHECK(context,
                       odm_spine_report_json(first, required, &second_required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, second_required == required);
        ODM_TEST_CHECK(context,
                       odm_spine_report_json(second, required, &second_required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, strcmp(first, second) == 0);
        ODM_TEST_CHECK(context,
                       strstr(first, "\"graph_acyclic\":true") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "\"state\":\"implemented_uncertified\"") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "\"state\":\"certified\"") == NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "\"sample_rate\":48000") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "cpu_reference_v1") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "\"known_bugs\":[]") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "SEC-G0-MEMORY") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "delivery.mp4_h264_aac_sdr_bt709_v1") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(first, "observed_local_adapter_only") != NULL);
    }
    if (required > 1u) {
        char *small = (char *)malloc(required - 1u);
        ODM_TEST_CHECK(context, small != NULL);
        if (small) {
            small[0] = 'x';
            ODM_TEST_CHECK(context,
                           odm_spine_report_json(small, required - 1u, &required) ==
                               ODM_STATUS_BUFFER_TOO_SMALL);
            ODM_TEST_CHECK(context, small[0] == '\0');
            free(small);
        }
    }
    free(first);
    free(second);

    {
        odm_spine_impact impact;
        ODM_TEST_CHECK(context,
                       odm_spine_impact_of("time", &impact) == ODM_STATUS_OK);
        {
            uint32_t expected_direct = 0u, expected_transitive = 0u;
            ODM_TEST_CHECK(context, spine_expected_dependents(
                "time", &expected_direct, &expected_transitive) != 0);
            ODM_TEST_CHECK(context, impact.direct_dependents == expected_direct);
            ODM_TEST_CHECK(context, impact.transitive_dependents == expected_transitive);
        }
        ODM_TEST_CHECK(context, strstr(impact.dependents, "selftest") != NULL);
        ODM_TEST_CHECK(context, strstr(impact.dependents, "cli") != NULL);
        ODM_TEST_CHECK(context,
                       strstr(impact.invariants_at_stake, "TIME-1") != NULL);
        ODM_TEST_CHECK(context,
                       odm_spine_impact_of("does-not-exist", &impact) ==
                           ODM_STATUS_UNSUPPORTED);
        /* Root-like modules may have more textual impact than the fixed ABI
         * diagnostic buffers can hold. Traversal/counts must still succeed;
         * only the human-readable CSV is allowed to truncate explicitly. */
        ODM_TEST_CHECK(context, ODM_SPINE_MAX_MODULES >= 256u);
        ODM_TEST_CHECK(context,
                       odm_spine_impact_of("status", &impact) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, impact.transitive_dependents > 32u);
        ODM_TEST_CHECK(context, impact.dependents[0] != '\0');
        ODM_TEST_CHECK(context,
                       strstr(impact.dependents, "...") != NULL ||
                       strlen(impact.dependents) < ODM_SPINE_IMPACT_TEXT_CAPACITY - 1u);
    }
    {
        size_t module_count = 0u;
        size_t ownership_count = 0u;
        const odm_spine_module *modules = odm_spine_modules(&module_count);
        const odm_spine_ownership *ownerships =
            odm_spine_ownerships(&ownership_count);
        size_t empty_count = 1u;
        size_t investigation_count = 0u;
        const odm_spine_investigation *investigations =
            odm_spine_security_investigations(&investigation_count);
        size_t index;
        ODM_TEST_CHECK(context, module_count == summary.module_count);
        for (index = 0u; index < module_count; index++) {
            ODM_TEST_CHECK(context, modules[index].source_lines > 0u);
        }
        /* The registry currently carries seven foundational ownership
         * contracts plus the three ProjectSession lifetime/handle contracts. */
        ODM_TEST_CHECK(context, ownership_count == 10u);
        for (index = 0u; index < ownership_count; index++) {
            ODM_TEST_CHECK(context, ownerships[index].acquire[0] != '\0');
            ODM_TEST_CHECK(context, ownerships[index].release[0] != '\0');
        }
        ODM_TEST_CHECK(context,
                       odm_spine_state_models(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == summary.state_model_count);
        empty_count = 0u;
        ODM_TEST_CHECK(context,
                       odm_spine_renderer_backends(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == 2u);
        empty_count = 0u;
        ODM_TEST_CHECK(context,
                       odm_spine_media_adapters(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == 8u);
        empty_count = 0u;
        ODM_TEST_CHECK(context,
                       odm_spine_output_profiles(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == 2u);
        empty_count = 1u;
        ODM_TEST_CHECK(context,
                       odm_spine_parity_contracts(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == 1u);
        empty_count = 1u;
        ODM_TEST_CHECK(context,
                       odm_spine_known_bugs(&empty_count) == NULL);
        ODM_TEST_CHECK(context, empty_count == 0u);
        ODM_TEST_CHECK(context, investigations != NULL);
        ODM_TEST_CHECK(context, investigation_count == summary.security_investigation_count);
    }
}

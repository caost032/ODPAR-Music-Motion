#include "test_harness.h"

#include "odm_spine.h"
#include "odm_version.h"

#include <stdlib.h>
#include <string.h>

void odm_test_spine(odm_test_context *context) {
    odm_spine_summary summary;
    size_t required = 0u;
    char *first;
    char *second;
    ODM_TEST_CHECK(context, odm_spine_validate(&summary) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, summary.module_count == 68u);
    ODM_TEST_CHECK(context, summary.dependency_count == 254u);
    ODM_TEST_CHECK(context, summary.invariant_count == 141u);
    ODM_TEST_CHECK(context, summary.capability_count == 128u);
    ODM_TEST_CHECK(context, summary.state_model_count == 11u);
    ODM_TEST_CHECK(context, summary.renderer_backend_count == 1u);
    ODM_TEST_CHECK(context, summary.media_adapter_count == 8u);
    ODM_TEST_CHECK(context, summary.output_profile_count == 2u);
    ODM_TEST_CHECK(context, summary.parity_contract_count == 1u);
    ODM_TEST_CHECK(context, summary.known_bug_count == 0u);
    ODM_TEST_CHECK(context, summary.security_investigation_count == 96u);
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
        ODM_TEST_CHECK(context, impact.direct_dependents == 12u);
        ODM_TEST_CHECK(context, impact.transitive_dependents == 30u);
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
        ODM_TEST_CHECK(context, ownership_count == 7u);
        for (index = 0u; index < ownership_count; index++) {
            ODM_TEST_CHECK(context, ownerships[index].acquire[0] != '\0');
            ODM_TEST_CHECK(context, ownerships[index].release[0] != '\0');
        }
        ODM_TEST_CHECK(context,
                       odm_spine_state_models(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == 11u);
        empty_count = 0u;
        ODM_TEST_CHECK(context,
                       odm_spine_renderer_backends(&empty_count) != NULL);
        ODM_TEST_CHECK(context, empty_count == 1u);
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
        ODM_TEST_CHECK(context, investigation_count == 96u);
    }
}

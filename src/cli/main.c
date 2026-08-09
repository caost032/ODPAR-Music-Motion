#include "odm_spine.h"
#include "odm_status.h"
#include "odm_version.h"
#include "selftest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef odm_status (*odm_json_report_fn)(char *, size_t, size_t *);

static int odm_cli_emit_json(odm_json_report_fn report) {
    size_t required = 0u;
    char *buffer;
    odm_status status = report(NULL, 0u, &required);
    if (status != ODM_STATUS_BUFFER_TOO_SMALL || required == 0u) {
        fprintf(stderr, "spine sizing failed: %s\n", odm_status_name(status));
        return 3;
    }
    buffer = (char *)malloc(required);
    if (!buffer) {
        fputs("out of memory while emitting JSON\n", stderr);
        return 3;
    }
    status = report(buffer, required, &required);
    if (status != ODM_STATUS_OK) {
        fprintf(stderr, "spine serialization failed: %s\n", odm_status_name(status));
        free(buffer);
        return 3;
    }
    puts(buffer);
    free(buffer);
    return 0;
}

static int odm_cli_emit_impact(const char *module_name) {
    odm_spine_impact impact;
    size_t required = 0u;
    char *buffer;
    odm_status status = odm_spine_impact_of(module_name, &impact);
    if (status != ODM_STATUS_OK) {
        fprintf(stderr, "impact query failed for '%s': %s\n", module_name,
                odm_status_name(status));
        return status == ODM_STATUS_UNSUPPORTED ? 2 : 3;
    }
    status = odm_spine_impact_json(&impact, NULL, 0u, &required);
    if (status != ODM_STATUS_BUFFER_TOO_SMALL || required == 0u) return 3;
    buffer = (char *)malloc(required);
    if (!buffer) return 3;
    status = odm_spine_impact_json(&impact, buffer, required, &required);
    if (status == ODM_STATUS_OK) puts(buffer);
    free(buffer);
    return status == ODM_STATUS_OK ? 0 : 3;
}

static int odm_cli_selftest(void) {
    odm_selftest_result result;
    odm_status status = odm_selftest_run(&result);
    printf("{\"schema\":\"odm_selftest_v1\",\"checks\":%u,\"passed\":%u,"
           "\"failed\":%u,\"status\":\"%s\",\"failed_check\":",
           result.checks, result.passed, result.checks - result.passed,
           status == ODM_STATUS_OK ? "pass" : "fail");
    if (result.failed_check) printf("\"%s\"", result.failed_check);
    else fputs("null", stdout);
    printf(",\"error\":\"%s\"}\n",
           odm_status_name(status == ODM_STATUS_OK ? ODM_STATUS_OK
                                                   : result.failure_status));
    return status == ODM_STATUS_OK ? 0 : 3;
}

static void odm_cli_usage(FILE *stream) {
    fputs("usage: odpar-music --version | --spine | --spine-summary | "
          "--spine-impact=<module> | selftest\n", stream);
}

int main(int argc, char **argv) {
    const char impact_prefix[] = "--spine-impact=";
    if (argc != 2) {
        odm_cli_usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "--version") == 0) {
        printf("odpar-music %s abi=%u source=%s compiler=%s\n",
               odm_version_string(), odm_abi_version(), odm_source_id(),
               odm_compiler_id());
        return 0;
    }
    if (strcmp(argv[1], "--spine") == 0) {
        return odm_cli_emit_json(odm_spine_report_json);
    }
    if (strcmp(argv[1], "--spine-summary") == 0) {
        return odm_cli_emit_json(odm_spine_summary_json);
    }
    if (strncmp(argv[1], impact_prefix, sizeof(impact_prefix) - 1u) == 0) {
        const char *module_name = argv[1] + sizeof(impact_prefix) - 1u;
        if (module_name[0] == '\0') {
            odm_cli_usage(stderr);
            return 2;
        }
        return odm_cli_emit_impact(module_name);
    }
    if (strcmp(argv[1], "selftest") == 0) return odm_cli_selftest();
    odm_cli_usage(stderr);
    return 2;
}

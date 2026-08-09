#include "test_harness.h"

#include <inttypes.h>
#include <stdio.h>

void odm_test_check(odm_test_context *context, int condition,
                    const char *file, int line, const char *expression) {
    context->checks++;
    if (condition) {
        context->passed++;
    } else {
        context->failed++;
        printf("FAIL %s:%d: %s\n", file, line, expression);
    }
}

int main(void) {
    odm_test_context context = {0};
    odm_test_status(&context);
    odm_test_time(&context);
    odm_test_fixed(&context);
    odm_test_rng(&context);
    odm_test_memory(&context);
    odm_test_job(&context);
    odm_test_hash(&context);
    odm_test_wire(&context);
    odm_test_limits(&context);
    odm_test_ffi(&context);
    odm_test_executor(&context);
    odm_test_spine(&context);
    odm_test_media(&context);
    odm_test_resample(&context);
    odm_test_music_analysis(&context);
    odm_test_music_reaction(&context);
    odm_test_music_map_wire(&context);
    odm_test_score_ir(&context);
    odm_test_package(&context);
    odm_test_renderer(&context);
    odm_test_visual(&context);
    odm_test_visual_dynamics(&context);
    odm_test_visual_scene(&context);
    odm_test_preview(&context);
    odm_test_master(&context);
    odm_test_studio(&context);
    odm_test_delivery(&context);
    odm_test_compositor(&context);
    odm_test_export_engine(&context);
    odm_test_export_run(&context);
    odm_test_properties(&context);
    printf("ODM TESTS checks=%" PRIu64 " passed=%" PRIu64 " failed=%" PRIu64 "\n",
           context.checks, context.passed, context.failed);
    return context.failed == 0u ? 0 : 1;
}

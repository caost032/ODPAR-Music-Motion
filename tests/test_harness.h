#ifndef ODM_TEST_HARNESS_H
#define ODM_TEST_HARNESS_H

#include <stdint.h>

typedef struct {
    uint64_t checks;
    uint64_t passed;
    uint64_t failed;
} odm_test_context;

void odm_test_check(odm_test_context *context, int condition,
                    const char *file, int line, const char *expression);
void odm_test_status(odm_test_context *context);
void odm_test_time(odm_test_context *context);
void odm_test_fixed(odm_test_context *context);
void odm_test_rng(odm_test_context *context);
void odm_test_memory(odm_test_context *context);
void odm_test_job(odm_test_context *context);
void odm_test_hash(odm_test_context *context);
void odm_test_wire(odm_test_context *context);
void odm_test_limits(odm_test_context *context);
void odm_test_ffi(odm_test_context *context);
void odm_test_executor(odm_test_context *context);
void odm_test_spine(odm_test_context *context);
void odm_test_media(odm_test_context *context);
void odm_test_resample(odm_test_context *context);
void odm_test_music_analysis(odm_test_context *context);
void odm_test_music_reaction(odm_test_context *context);
void odm_test_music_map_wire(odm_test_context *context);
void odm_test_score_ir(odm_test_context *context);
void odm_test_package(odm_test_context *context);
void odm_test_renderer(odm_test_context *context);
void odm_test_visual(odm_test_context *context);
void odm_test_visual_dynamics(odm_test_context *context);
void odm_test_preview(odm_test_context *context);
void odm_test_master(odm_test_context *context);
void odm_test_studio(odm_test_context *context);
void odm_test_delivery(odm_test_context *context);
void odm_test_compositor(odm_test_context *context);
void odm_test_export_engine(odm_test_context *context);
void odm_test_export_run(odm_test_context *context);
void odm_test_properties(odm_test_context *context);

#define ODM_TEST_CHECK(context, expression) \
    odm_test_check((context), (expression) ? 1 : 0, __FILE__, __LINE__, #expression)

#endif /* ODM_TEST_HARNESS_H */

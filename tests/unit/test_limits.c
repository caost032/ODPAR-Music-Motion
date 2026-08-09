#include "test_harness.h"

#include "odm_limits.h"

#include <limits.h>
#include <string.h>

void odm_test_limits(odm_test_context *context) {
    odm_resource_limits limits;
    odm_resource_request request;
    odm_admission_result result;
    ODM_TEST_CHECK(context,
                   odm_resource_limits_default(&limits) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_resource_limits_validate(&limits) == ODM_STATUS_OK);
    memset(&request, 0, sizeof(request));
    request.schema_version = ODM_RESOURCE_LIMITS_SCHEMA_VERSION;
    request.input_bytes = limits.max_input_bytes;
    request.output_bytes = limits.max_output_bytes;
    request.memory_bytes = limits.max_memory_bytes;
    request.work_units = limits.max_work_units;
    request.queue_items = limits.max_queue_items;
    request.workers = limits.max_workers;
    request.jobs = limits.max_jobs;
    request.recursion_depth = limits.max_recursion_depth;
    ODM_TEST_CHECK(context,
                   odm_resource_admit(&limits, &request, &result) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, result.verdict == ODM_ADMISSION_ACCEPT);
    ODM_TEST_CHECK(context, result.dimension == ODM_LIMIT_DIMENSION_NONE);
    ODM_TEST_CHECK(context, result.schema_version ==
                                ODM_ADMISSION_RESULT_SCHEMA_VERSION);
#define ODM_TEST_LIMIT(member, dimension_id) \
    do { \
        request.member = limits.max_##member + 1u; \
        ODM_TEST_CHECK(context, \
                       odm_resource_admit(&limits, &request, &result) == \
                           ODM_STATUS_BUDGET_EXCEEDED); \
        ODM_TEST_CHECK(context, result.verdict == ODM_ADMISSION_REJECT); \
        ODM_TEST_CHECK(context, result.dimension == (dimension_id)); \
        ODM_TEST_CHECK(context, result.observed == \
                                    (uint64_t)request.member); \
        ODM_TEST_CHECK(context, result.limit == \
                                    (uint64_t)limits.max_##member); \
        request.member = limits.max_##member; \
    } while (0)
    ODM_TEST_LIMIT(input_bytes, ODM_LIMIT_DIMENSION_INPUT_BYTES);
    ODM_TEST_LIMIT(output_bytes, ODM_LIMIT_DIMENSION_OUTPUT_BYTES);
    ODM_TEST_LIMIT(memory_bytes, ODM_LIMIT_DIMENSION_MEMORY_BYTES);
    ODM_TEST_LIMIT(work_units, ODM_LIMIT_DIMENSION_WORK_UNITS);
    ODM_TEST_LIMIT(queue_items, ODM_LIMIT_DIMENSION_QUEUE_ITEMS);
    ODM_TEST_LIMIT(workers, ODM_LIMIT_DIMENSION_WORKERS);
    ODM_TEST_LIMIT(jobs, ODM_LIMIT_DIMENSION_JOBS);
    ODM_TEST_LIMIT(recursion_depth, ODM_LIMIT_DIMENSION_RECURSION_DEPTH);
#undef ODM_TEST_LIMIT
    request.input_bytes = limits.max_input_bytes + 1u;
    request.output_bytes = limits.max_output_bytes + 1u;
    ODM_TEST_CHECK(context,
                   odm_resource_admit(&limits, &request, &result) ==
                       ODM_STATUS_BUDGET_EXCEEDED);
    ODM_TEST_CHECK(context,
                   result.dimension == ODM_LIMIT_DIMENSION_INPUT_BYTES);
    request.input_bytes = limits.max_input_bytes;
    request.output_bytes = limits.max_output_bytes;
    {
        odm_admission_result sentinel;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        result = sentinel;
        request.schema_version++;
        ODM_TEST_CHECK(context,
                       odm_resource_admit(&limits, &request, &result) ==
                           ODM_STATUS_VERSION_MISMATCH);
        ODM_TEST_CHECK(context,
                       memcmp(&result, &sentinel, sizeof(result)) == 0);
        request.schema_version = ODM_RESOURCE_LIMITS_SCHEMA_VERSION;
        limits.flags = 1u;
        ODM_TEST_CHECK(context,
                       odm_resource_limits_validate(&limits) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        limits.flags = 0u;
        limits.max_workers = 0u;
        ODM_TEST_CHECK(context,
                       odm_resource_limits_validate(&limits) ==
                           ODM_STATUS_INVALID_ARGUMENT);
    }
    {
        odm_work_quote left = {10u, 200u, 30u};
        odm_work_quote right = {20u, 150u, 40u};
        odm_work_quote output = {0u, 0u, 0u};
        odm_work_quote sentinel = {1u, 2u, 3u};
        ODM_TEST_CHECK(context,
                       odm_work_quote_add(&left, &right, &output) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, output.work_units == 30u);
        ODM_TEST_CHECK(context, output.peak_memory_bytes == 200u);
        ODM_TEST_CHECK(context, output.output_bytes == 70u);
        ODM_TEST_CHECK(context,
                       odm_work_quote_scale(&left, 4u, &output) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, output.work_units == 40u);
        ODM_TEST_CHECK(context, output.peak_memory_bytes == 800u);
        ODM_TEST_CHECK(context, output.output_bytes == 120u);
        left.work_units = UINT64_MAX;
        output = sentinel;
        ODM_TEST_CHECK(context,
                       odm_work_quote_add(&left, &right, &output) ==
                           ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,
                       memcmp(&output, &sentinel, sizeof(output)) == 0);
        output = sentinel;
        ODM_TEST_CHECK(context,
                       odm_work_quote_scale(&left, 2u, &output) ==
                           ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(context,
                       memcmp(&output, &sentinel, sizeof(output)) == 0);
    }
}

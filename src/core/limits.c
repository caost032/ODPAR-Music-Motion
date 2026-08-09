#include "odm_limits.h"

#include <limits.h>

#define ODM_DEFAULT_MAX_INPUT_BYTES    UINT64_C(1073741824)
#define ODM_DEFAULT_MAX_OUTPUT_BYTES   UINT64_C(8589934592)
#define ODM_DEFAULT_MAX_MEMORY_BYTES   UINT64_C(2147483648)
#define ODM_DEFAULT_MAX_WORK_UNITS     UINT64_C(281474976710656)
#define ODM_DEFAULT_MAX_QUEUE_ITEMS    UINT32_C(65536)
#define ODM_DEFAULT_MAX_WORKERS        UINT32_C(64)
#define ODM_DEFAULT_MAX_JOBS           UINT32_C(4096)
#define ODM_DEFAULT_MAX_RECURSION      UINT32_C(1024)

static int odm_u64_add_checked(uint64_t left, uint64_t right,
                               uint64_t *out_value) {
    if (right > UINT64_MAX - left) return 0;
    *out_value = left + right;
    return 1;
}

static int odm_u64_mul_checked(uint64_t left, uint64_t right,
                               uint64_t *out_value) {
    if (left != 0u && right > UINT64_MAX / left) return 0;
    *out_value = left * right;
    return 1;
}

odm_status odm_resource_limits_default(odm_resource_limits *out_limits) {
    odm_resource_limits limits;
    if (!out_limits) return ODM_STATUS_INVALID_ARGUMENT;
    limits.schema_version = ODM_RESOURCE_LIMITS_SCHEMA_VERSION;
    limits.flags = 0u;
    limits.max_input_bytes = ODM_DEFAULT_MAX_INPUT_BYTES;
    limits.max_output_bytes = ODM_DEFAULT_MAX_OUTPUT_BYTES;
    limits.max_memory_bytes = ODM_DEFAULT_MAX_MEMORY_BYTES;
    limits.max_work_units = ODM_DEFAULT_MAX_WORK_UNITS;
    limits.max_queue_items = ODM_DEFAULT_MAX_QUEUE_ITEMS;
    limits.max_workers = ODM_DEFAULT_MAX_WORKERS;
    limits.max_jobs = ODM_DEFAULT_MAX_JOBS;
    limits.max_recursion_depth = ODM_DEFAULT_MAX_RECURSION;
    *out_limits = limits;
    return ODM_STATUS_OK;
}

odm_status odm_resource_limits_validate(const odm_resource_limits *limits) {
    if (!limits) return ODM_STATUS_INVALID_ARGUMENT;
    if (limits->schema_version != ODM_RESOURCE_LIMITS_SCHEMA_VERSION) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    if (limits->flags != 0u || limits->max_input_bytes == 0u ||
        limits->max_output_bytes == 0u || limits->max_memory_bytes == 0u ||
        limits->max_work_units == 0u || limits->max_queue_items == 0u ||
        limits->max_workers == 0u || limits->max_jobs == 0u ||
        limits->max_recursion_depth == 0u) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    return ODM_STATUS_OK;
}

static odm_status odm_admission_publish(uint32_t dimension,
                                        uint64_t observed,
                                        uint64_t limit,
                                        odm_admission_result *out_result) {
    odm_admission_result result;
    result.schema_version = ODM_ADMISSION_RESULT_SCHEMA_VERSION;
    result.verdict = dimension == ODM_LIMIT_DIMENSION_NONE ?
                         ODM_ADMISSION_ACCEPT : ODM_ADMISSION_REJECT;
    result.dimension = dimension;
    result.reserved = 0u;
    result.observed = observed;
    result.limit = limit;
    *out_result = result;
    return dimension == ODM_LIMIT_DIMENSION_NONE ? ODM_STATUS_OK :
                                                  ODM_STATUS_BUDGET_EXCEEDED;
}

odm_status odm_resource_admit(const odm_resource_limits *limits,
                              const odm_resource_request *request,
                              odm_admission_result *out_result) {
    odm_status status;
    if (!request || !out_result) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_resource_limits_validate(limits);
    if (status != ODM_STATUS_OK) return status;
    if (request->schema_version != ODM_RESOURCE_LIMITS_SCHEMA_VERSION) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    if (request->flags != 0u) return ODM_STATUS_INVALID_ARGUMENT;
#define ODM_REJECT_OVER(member, dimension_id) \
    do { \
        if (request->member > limits->max_##member) { \
            return odm_admission_publish((dimension_id), \
                                         (uint64_t)request->member, \
                                         (uint64_t)limits->max_##member, \
                                         out_result); \
        } \
    } while (0)
    ODM_REJECT_OVER(input_bytes, ODM_LIMIT_DIMENSION_INPUT_BYTES);
    ODM_REJECT_OVER(output_bytes, ODM_LIMIT_DIMENSION_OUTPUT_BYTES);
    ODM_REJECT_OVER(memory_bytes, ODM_LIMIT_DIMENSION_MEMORY_BYTES);
    ODM_REJECT_OVER(work_units, ODM_LIMIT_DIMENSION_WORK_UNITS);
    ODM_REJECT_OVER(queue_items, ODM_LIMIT_DIMENSION_QUEUE_ITEMS);
    ODM_REJECT_OVER(workers, ODM_LIMIT_DIMENSION_WORKERS);
    ODM_REJECT_OVER(jobs, ODM_LIMIT_DIMENSION_JOBS);
    ODM_REJECT_OVER(recursion_depth, ODM_LIMIT_DIMENSION_RECURSION_DEPTH);
#undef ODM_REJECT_OVER
    return odm_admission_publish(ODM_LIMIT_DIMENSION_NONE, 0u, 0u,
                                 out_result);
}

odm_status odm_work_quote_add(const odm_work_quote *left,
                              const odm_work_quote *right,
                              odm_work_quote *out_quote) {
    odm_work_quote result;
    if (!left || !right || !out_quote) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_u64_add_checked(left->work_units, right->work_units,
                             &result.work_units) ||
        !odm_u64_add_checked(left->output_bytes, right->output_bytes,
                             &result.output_bytes)) {
        return ODM_STATUS_OVERFLOW;
    }
    result.peak_memory_bytes = left->peak_memory_bytes > right->peak_memory_bytes ?
                                   left->peak_memory_bytes : right->peak_memory_bytes;
    *out_quote = result;
    return ODM_STATUS_OK;
}

odm_status odm_work_quote_scale(const odm_work_quote *quote, uint64_t count,
                                odm_work_quote *out_quote) {
    odm_work_quote result;
    if (!quote || !out_quote) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_u64_mul_checked(quote->work_units, count, &result.work_units) ||
        !odm_u64_mul_checked(quote->output_bytes, count, &result.output_bytes) ||
        !odm_u64_mul_checked(quote->peak_memory_bytes, count,
                             &result.peak_memory_bytes)) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_quote = result;
    return ODM_STATUS_OK;
}

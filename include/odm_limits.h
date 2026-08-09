#ifndef ODM_LIMITS_H
#define ODM_LIMITS_H

#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_RESOURCE_LIMITS_SCHEMA_VERSION 1u
#define ODM_ADMISSION_RESULT_SCHEMA_VERSION 1u

#define ODM_ADMISSION_ACCEPT ((uint32_t)0)
#define ODM_ADMISSION_REJECT ((uint32_t)1)

#define ODM_LIMIT_DIMENSION_NONE            ((uint32_t)0)
#define ODM_LIMIT_DIMENSION_INPUT_BYTES     ((uint32_t)1)
#define ODM_LIMIT_DIMENSION_OUTPUT_BYTES    ((uint32_t)2)
#define ODM_LIMIT_DIMENSION_MEMORY_BYTES    ((uint32_t)3)
#define ODM_LIMIT_DIMENSION_WORK_UNITS      ((uint32_t)4)
#define ODM_LIMIT_DIMENSION_QUEUE_ITEMS     ((uint32_t)5)
#define ODM_LIMIT_DIMENSION_WORKERS         ((uint32_t)6)
#define ODM_LIMIT_DIMENSION_JOBS            ((uint32_t)7)
#define ODM_LIMIT_DIMENSION_RECURSION_DEPTH ((uint32_t)8)

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t max_input_bytes;
    uint64_t max_output_bytes;
    uint64_t max_memory_bytes;
    uint64_t max_work_units;
    uint32_t max_queue_items;
    uint32_t max_workers;
    uint32_t max_jobs;
    uint32_t max_recursion_depth;
} odm_resource_limits;

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t input_bytes;
    uint64_t output_bytes;
    uint64_t memory_bytes;
    uint64_t work_units;
    uint32_t queue_items;
    uint32_t workers;
    uint32_t jobs;
    uint32_t recursion_depth;
} odm_resource_request;

typedef struct {
    uint32_t schema_version;
    uint32_t verdict;
    uint32_t dimension;
    uint32_t reserved;
    uint64_t observed;
    uint64_t limit;
} odm_admission_result;

typedef struct {
    uint64_t work_units;
    uint64_t peak_memory_bytes;
    uint64_t output_bytes;
} odm_work_quote;

odm_status odm_resource_limits_default(odm_resource_limits *out_limits);
odm_status odm_resource_limits_validate(const odm_resource_limits *limits);
odm_status odm_resource_admit(const odm_resource_limits *limits,
                              const odm_resource_request *request,
                              odm_admission_result *out_result);
odm_status odm_work_quote_add(const odm_work_quote *left,
                              const odm_work_quote *right,
                              odm_work_quote *out_quote);
odm_status odm_work_quote_scale(const odm_work_quote *quote, uint64_t count,
                                odm_work_quote *out_quote);

#ifdef __cplusplus
}
#endif

#endif /* ODM_LIMITS_H */

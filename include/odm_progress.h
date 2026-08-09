#ifndef ODM_PROGRESS_H
#define ODM_PROGRESS_H

#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_PROGRESS_ONE UINT32_C(1000000)

typedef int32_t odm_job_state;
typedef int32_t odm_job_phase;

#define ODM_JOB_IDLE       ((odm_job_state)0)
#define ODM_JOB_STARTING   ((odm_job_state)1)
#define ODM_JOB_RUNNING    ((odm_job_state)2)
#define ODM_JOB_SUCCEEDED  ((odm_job_state)3)
#define ODM_JOB_FAILED     ((odm_job_state)4)
#define ODM_JOB_CANCELLED  ((odm_job_state)5)
#define ODM_JOB_DESTROYED  ((odm_job_state)6)

#define ODM_PHASE_IDLE       ((odm_job_phase)0)
#define ODM_PHASE_PREPARE    ((odm_job_phase)1)
#define ODM_PHASE_VALIDATE   ((odm_job_phase)2)
#define ODM_PHASE_EXECUTE    ((odm_job_phase)3)
#define ODM_PHASE_FINALIZE   ((odm_job_phase)4)
#define ODM_PHASE_COMPLETE   ((odm_job_phase)5)
#define ODM_PHASE_CANCELLED  ((odm_job_phase)6)
#define ODM_PHASE_FAILED     ((odm_job_phase)7)

typedef struct {
    uint32_t schema_version;
    int32_t state;
    int32_t phase;
    int32_t error_code;
    uint32_t cancel_requested;
    uint32_t progress_millionths;
    uint32_t reserved;
    uint64_t generation;
    uint64_t work_done;
    uint64_t work_total;
} odm_progress_snapshot;

const char *odm_job_state_name(odm_job_state state);
const char *odm_job_phase_name(odm_job_phase phase);
odm_status odm_progress_fraction(uint64_t done, uint64_t total,
                                 uint32_t *out_millionths);

#ifdef __cplusplus
}
#endif

#endif /* ODM_PROGRESS_H */

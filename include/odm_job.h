#ifndef ODM_JOB_H
#define ODM_JOB_H

#include "odm_progress.h"

#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    _Atomic uint64_t control;
    _Atomic int32_t phase;
    _Atomic int32_t error_code;
    _Atomic uint32_t progress_millionths;
    _Atomic uint64_t work_done;
    _Atomic uint64_t work_total;
    _Atomic uint64_t revision;
    uint64_t cookie;
} odm_job;

typedef struct {
    odm_job *job;
    uint64_t generation;
} odm_job_ticket;

/* The odm_job object is caller-owned storage.  It must remain alive from init
 * through a successful destroy.  begin/publish/finish/request_cancel/snapshot
 * may execute concurrently; destroy must not race any other operation.
 * Snapshots are coherent across all fields: a reader observes one completed
 * publication revision, never a mixture of two revisions. */
odm_status odm_job_init(odm_job *job);
odm_status odm_job_destroy(odm_job *job);
odm_status odm_job_begin(odm_job *job, uint64_t work_total,
                         odm_job_ticket *out_ticket);
odm_status odm_job_request_cancel(odm_job *job);
odm_status odm_job_should_cancel(odm_job_ticket ticket, int *out_cancelled);
odm_status odm_job_publish(odm_job_ticket ticket, odm_job_phase phase,
                           uint64_t work_done);
odm_status odm_job_finish(odm_job_ticket ticket, odm_status result_status);
odm_status odm_job_snapshot(const odm_job *job,
                            odm_progress_snapshot *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* ODM_JOB_H */

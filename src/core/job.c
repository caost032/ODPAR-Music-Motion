#include "odm_job.h"

#include <limits.h>
#include <stddef.h>

#define ODM_JOB_COOKIE UINT64_C(0x4f444d4a4f423031)
#define ODM_JOB_CONTROL_STATE_MASK UINT64_C(0xff)
#define ODM_JOB_CONTROL_CANCEL     UINT64_C(0x100)
#define ODM_JOB_CONTROL_GEN_SHIFT  16u
#define ODM_JOB_CONTROL_GEN_MAX    UINT64_C(0xffffffffffff)
#define ODM_JOB_INTERNAL_FINISHING ((odm_job_state)7)

static odm_job_state odm_job_control_state(uint64_t control) {
    return (odm_job_state)(control & ODM_JOB_CONTROL_STATE_MASK);
}

static uint64_t odm_job_control_generation(uint64_t control) {
    return control >> ODM_JOB_CONTROL_GEN_SHIFT;
}

static uint64_t odm_job_control_with_state(uint64_t control,
                                           odm_job_state state) {
    return (control & ~ODM_JOB_CONTROL_STATE_MASK) | (uint64_t)(uint32_t)state;
}

static int odm_job_is_terminal(odm_job_state state) {
    return state == ODM_JOB_IDLE || state == ODM_JOB_SUCCEEDED ||
           state == ODM_JOB_FAILED || state == ODM_JOB_CANCELLED;
}

static int odm_job_valid(const odm_job *job) {
    return job != NULL && job->cookie == ODM_JOB_COOKIE;
}

static odm_status odm_job_write_lock(odm_job *job, uint64_t *out_revision) {
    uint64_t revision;
    if (!job || !out_revision) return ODM_STATUS_INVALID_ARGUMENT;
    revision = atomic_load_explicit(&job->revision, memory_order_acquire);
    for (;;) {
        if ((revision & UINT64_C(1)) != 0u) {
            revision = atomic_load_explicit(&job->revision,
                                            memory_order_acquire);
            continue;
        }
        if (revision > UINT64_MAX - UINT64_C(2)) return ODM_STATUS_OVERFLOW;
        if (atomic_compare_exchange_weak_explicit(
                &job->revision, &revision, revision + UINT64_C(1),
                memory_order_acquire, memory_order_relaxed)) {
            *out_revision = revision;
            return ODM_STATUS_OK;
        }
    }
}

static void odm_job_write_unlock(odm_job *job, uint64_t revision) {
    atomic_store_explicit(&job->revision, revision + UINT64_C(2),
                          memory_order_release);
}

odm_status odm_job_init(odm_job *job) {
    if (!job) return ODM_STATUS_INVALID_ARGUMENT;
    atomic_init(&job->control, (uint64_t)ODM_JOB_IDLE);
    atomic_init(&job->phase, ODM_PHASE_IDLE);
    atomic_init(&job->error_code, ODM_STATUS_OK);
    atomic_init(&job->progress_millionths, 0u);
    atomic_init(&job->work_done, UINT64_C(0));
    atomic_init(&job->work_total, UINT64_C(0));
    atomic_init(&job->revision, UINT64_C(0));
    job->cookie = ODM_JOB_COOKIE;
    return ODM_STATUS_OK;
}

odm_status odm_job_destroy(odm_job *job) {
    uint64_t control;
    if (!odm_job_valid(job)) return ODM_STATUS_INVALID_ARGUMENT;
    control = atomic_load_explicit(&job->control, memory_order_acquire);
    for (;;) {
        odm_job_state state = odm_job_control_state(control);
        uint64_t desired;
        if (state == ODM_JOB_STARTING || state == ODM_JOB_RUNNING ||
            state == ODM_JOB_INTERNAL_FINISHING) {
            return ODM_STATUS_BUSY;
        }
        if (state == ODM_JOB_DESTROYED) return ODM_STATUS_INVALID_STATE;
        desired = odm_job_control_with_state(control, ODM_JOB_DESTROYED) &
                  ~ODM_JOB_CONTROL_CANCEL;
        if (atomic_compare_exchange_weak_explicit(
                &job->control, &control, desired,
                memory_order_acq_rel, memory_order_acquire)) {
            job->cookie = 0u;
            return ODM_STATUS_OK;
        }
    }
}

odm_status odm_job_begin(odm_job *job, uint64_t work_total,
                         odm_job_ticket *out_ticket) {
    uint64_t control;
    uint64_t revision;
    odm_status lock_status;
    if (!odm_job_valid(job) || !out_ticket) return ODM_STATUS_INVALID_ARGUMENT;
    lock_status = odm_job_write_lock(job, &revision);
    if (lock_status != ODM_STATUS_OK) return lock_status;
    control = atomic_load_explicit(&job->control, memory_order_acquire);
    for (;;) {
        odm_job_state state = odm_job_control_state(control);
        uint64_t generation;
        uint64_t desired;
        if (!odm_job_is_terminal(state)) {
            odm_job_write_unlock(job, revision);
            return ODM_STATUS_BUSY;
        }
        generation = odm_job_control_generation(control);
        if (generation == ODM_JOB_CONTROL_GEN_MAX) {
            odm_job_write_unlock(job, revision);
            return ODM_STATUS_OVERFLOW;
        }
        generation++;
        desired = (generation << ODM_JOB_CONTROL_GEN_SHIFT) |
                  (uint64_t)ODM_JOB_STARTING;
        if (atomic_compare_exchange_weak_explicit(
                &job->control, &control, desired,
                memory_order_acq_rel, memory_order_acquire)) {
            control = desired;
            break;
        }
    }
    atomic_store_explicit(&job->phase, ODM_PHASE_PREPARE, memory_order_relaxed);
    atomic_store_explicit(&job->error_code, ODM_STATUS_OK, memory_order_relaxed);
    atomic_store_explicit(&job->progress_millionths, 0u, memory_order_relaxed);
    atomic_store_explicit(&job->work_done, UINT64_C(0), memory_order_relaxed);
    atomic_store_explicit(&job->work_total, work_total, memory_order_relaxed);
    for (;;) {
        uint64_t desired = odm_job_control_with_state(control, ODM_JOB_RUNNING);
        if (atomic_compare_exchange_weak_explicit(
                &job->control, &control, desired,
                memory_order_release, memory_order_acquire)) {
            out_ticket->job = job;
            out_ticket->generation = odm_job_control_generation(desired);
            odm_job_write_unlock(job, revision);
            return ODM_STATUS_OK;
        }
        if (odm_job_control_generation(control) !=
                odm_job_control_generation(desired) ||
            odm_job_control_state(control) != ODM_JOB_STARTING) {
            odm_job_write_unlock(job, revision);
            return ODM_STATUS_INVARIANT_BROKEN;
        }
    }
}

odm_status odm_job_request_cancel(odm_job *job) {
    uint64_t control;
    if (!odm_job_valid(job)) return ODM_STATUS_INVALID_ARGUMENT;
    control = atomic_load_explicit(&job->control, memory_order_acquire);
    for (;;) {
        odm_job_state state = odm_job_control_state(control);
        uint64_t desired;
        if (state != ODM_JOB_STARTING && state != ODM_JOB_RUNNING) {
            return ODM_STATUS_INVALID_STATE;
        }
        if ((control & ODM_JOB_CONTROL_CANCEL) != 0u) return ODM_STATUS_OK;
        desired = control | ODM_JOB_CONTROL_CANCEL;
        if (atomic_compare_exchange_weak_explicit(
                &job->control, &control, desired,
                memory_order_acq_rel, memory_order_acquire)) {
            return ODM_STATUS_OK;
        }
    }
}

odm_status odm_job_should_cancel(odm_job_ticket ticket, int *out_cancelled) {
    uint64_t control;
    odm_job_state state;
    if (!odm_job_valid(ticket.job) || !out_cancelled) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    control = atomic_load_explicit(&ticket.job->control, memory_order_acquire);
    state = odm_job_control_state(control);
    if (odm_job_control_generation(control) != ticket.generation ||
        (state != ODM_JOB_STARTING && state != ODM_JOB_RUNNING &&
         state != ODM_JOB_INTERNAL_FINISHING)) {
        return ODM_STATUS_INVALID_STATE;
    }
    *out_cancelled = (control & ODM_JOB_CONTROL_CANCEL) != 0u;
    return ODM_STATUS_OK;
}

static void odm_atomic_u64_max(_Atomic uint64_t *target, uint64_t value) {
    uint64_t current = atomic_load_explicit(target, memory_order_relaxed);
    while (value > current &&
           !atomic_compare_exchange_weak_explicit(
               target, &current, value,
               memory_order_release, memory_order_relaxed)) {
    }
}

static void odm_atomic_u32_max(_Atomic uint32_t *target, uint32_t value) {
    uint32_t current = atomic_load_explicit(target, memory_order_relaxed);
    while (value > current &&
           !atomic_compare_exchange_weak_explicit(
               target, &current, value,
               memory_order_release, memory_order_relaxed)) {
    }
}

odm_status odm_job_publish(odm_job_ticket ticket, odm_job_phase phase,
                           uint64_t work_done) {
    uint64_t control;
    uint64_t revision;
    uint64_t total;
    int32_t current_phase;
    uint32_t progress;
    odm_status status;
    if (!odm_job_valid(ticket.job) || phase < ODM_PHASE_PREPARE ||
        phase > ODM_PHASE_FINALIZE) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_job_write_lock(ticket.job, &revision);
    if (status != ODM_STATUS_OK) return status;
    control = atomic_load_explicit(&ticket.job->control, memory_order_acquire);
    if (odm_job_control_generation(control) != ticket.generation ||
        odm_job_control_state(control) != ODM_JOB_RUNNING) {
        odm_job_write_unlock(ticket.job, revision);
        return ODM_STATUS_INVALID_STATE;
    }
    total = atomic_load_explicit(&ticket.job->work_total, memory_order_acquire);
    if (work_done > total) {
        odm_job_write_unlock(ticket.job, revision);
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    current_phase = atomic_load_explicit(&ticket.job->phase, memory_order_relaxed);
    if (phase < current_phase) {
        odm_job_write_unlock(ticket.job, revision);
        return ODM_STATUS_INVALID_STATE;
    }
    status = odm_progress_fraction(work_done, total, &progress);
    if (status != ODM_STATUS_OK) {
        odm_job_write_unlock(ticket.job, revision);
        return status;
    }
    odm_atomic_u64_max(&ticket.job->work_done, work_done);
    odm_atomic_u32_max(&ticket.job->progress_millionths, progress);
    current_phase = atomic_load_explicit(&ticket.job->phase, memory_order_relaxed);
    while (phase > current_phase &&
           !atomic_compare_exchange_weak_explicit(
               &ticket.job->phase, &current_phase, phase,
               memory_order_release, memory_order_relaxed)) {
    }
    odm_job_write_unlock(ticket.job, revision);
    return ODM_STATUS_OK;
}

odm_status odm_job_finish(odm_job_ticket ticket, odm_status result_status) {
    uint64_t control;
    uint64_t finishing;
    uint64_t terminal_control;
    uint64_t revision;
    odm_job_state terminal_state;
    odm_job_phase terminal_phase;
    odm_status terminal_error;
    odm_status lock_status;
    if (!odm_job_valid(ticket.job)) return ODM_STATUS_INVALID_ARGUMENT;
    lock_status = odm_job_write_lock(ticket.job, &revision);
    if (lock_status != ODM_STATUS_OK) return lock_status;
    control = atomic_load_explicit(&ticket.job->control, memory_order_acquire);
    for (;;) {
        if (odm_job_control_generation(control) != ticket.generation ||
            odm_job_control_state(control) != ODM_JOB_RUNNING) {
            odm_job_write_unlock(ticket.job, revision);
            return ODM_STATUS_INVALID_STATE;
        }
        finishing = odm_job_control_with_state(control, ODM_JOB_INTERNAL_FINISHING);
        if (atomic_compare_exchange_weak_explicit(
                &ticket.job->control, &control, finishing,
                memory_order_acq_rel, memory_order_acquire)) {
            break;
        }
    }
    if ((finishing & ODM_JOB_CONTROL_CANCEL) != 0u ||
        result_status == ODM_STATUS_CANCELLED) {
        terminal_state = ODM_JOB_CANCELLED;
        terminal_phase = ODM_PHASE_CANCELLED;
        terminal_error = ODM_STATUS_CANCELLED;
    } else if (result_status == ODM_STATUS_OK) {
        uint64_t total = atomic_load_explicit(&ticket.job->work_total,
                                              memory_order_relaxed);
        terminal_state = ODM_JOB_SUCCEEDED;
        terminal_phase = ODM_PHASE_COMPLETE;
        terminal_error = ODM_STATUS_OK;
        atomic_store_explicit(&ticket.job->work_done, total, memory_order_relaxed);
        atomic_store_explicit(&ticket.job->progress_millionths, ODM_PROGRESS_ONE,
                              memory_order_relaxed);
    } else {
        terminal_state = ODM_JOB_FAILED;
        terminal_phase = ODM_PHASE_FAILED;
        terminal_error = result_status;
    }
    atomic_store_explicit(&ticket.job->phase, terminal_phase, memory_order_relaxed);
    atomic_store_explicit(&ticket.job->error_code, terminal_error,
                          memory_order_relaxed);
    terminal_control = odm_job_control_with_state(finishing, terminal_state);
    atomic_store_explicit(&ticket.job->control, terminal_control,
                          memory_order_release);
    odm_job_write_unlock(ticket.job, revision);
    return ODM_STATUS_OK;
}

odm_status odm_job_snapshot(const odm_job *job,
                            odm_progress_snapshot *out_snapshot) {
    odm_progress_snapshot snapshot;
    uint64_t control_before;
    uint64_t control_after;
    uint64_t revision_before;
    uint64_t revision_after;
    odm_job_state state;
    if (!odm_job_valid(job) || !out_snapshot) return ODM_STATUS_INVALID_ARGUMENT;
    for (;;) {
        revision_before = atomic_load_explicit(&job->revision,
                                               memory_order_acquire);
        if ((revision_before & UINT64_C(1)) != 0u) continue;
        control_before = atomic_load_explicit(&job->control,
                                              memory_order_acquire);
        state = odm_job_control_state(control_before);
        if (state == ODM_JOB_STARTING ||
            state == ODM_JOB_INTERNAL_FINISHING) continue;
        snapshot.schema_version = 1u;
        snapshot.state = state;
        snapshot.phase = atomic_load_explicit(&job->phase,
                                              memory_order_acquire);
        snapshot.error_code = atomic_load_explicit(&job->error_code,
                                                   memory_order_acquire);
        snapshot.cancel_requested =
            (control_before & ODM_JOB_CONTROL_CANCEL) != 0u ? 1u : 0u;
        snapshot.progress_millionths = atomic_load_explicit(
            &job->progress_millionths, memory_order_acquire);
        snapshot.reserved = 0u;
        snapshot.generation = odm_job_control_generation(control_before);
        snapshot.work_done = atomic_load_explicit(&job->work_done,
                                                  memory_order_acquire);
        snapshot.work_total = atomic_load_explicit(&job->work_total,
                                                   memory_order_acquire);
        control_after = atomic_load_explicit(&job->control,
                                             memory_order_acquire);
        revision_after = atomic_load_explicit(&job->revision,
                                              memory_order_acquire);
        if (revision_before == revision_after &&
            (revision_after & UINT64_C(1)) == 0u &&
            control_before == control_after) {
            *out_snapshot = snapshot;
            return ODM_STATUS_OK;
        }
    }
}

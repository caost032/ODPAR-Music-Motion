#include "test_harness.h"

#include "odm_job.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    odm_job_ticket ticket;
    _Atomic int *ready;
    _Atomic int *go;
    odm_status cancel_status;
    odm_status finish_status;
} odm_job_worker_args;

typedef struct {
    odm_job *job;
    odm_job_ticket ticket;
    _Atomic int *ready_count;
    _Atomic int *go;
    int cancel_worker;
    odm_status status;
} odm_job_finish_cancel_race_args;

typedef struct {
    odm_job *job;
    odm_job_ticket ticket;
    _Atomic int *ready;
    _Atomic int *go;
    _Atomic int *initial_snapshot_done;
    _Atomic int *writer_started;
    uint64_t work_total;
    uint32_t snapshot_count;
    uint32_t observed_running;
    uint32_t observed_terminal;
    odm_status status;
} odm_job_snapshot_stress_args;

static void *odm_job_cancel_worker(void *opaque) {
    odm_job_worker_args *args = (odm_job_worker_args *)opaque;
    int cancelled = 0;
    atomic_store_explicit(args->ready, 1, memory_order_release);
    while (atomic_load_explicit(args->go, memory_order_acquire) == 0) {
    }
    args->cancel_status = odm_job_should_cancel(args->ticket, &cancelled);
    if (args->cancel_status == ODM_STATUS_OK && cancelled == 0) {
        args->cancel_status = ODM_STATUS_INVARIANT_BROKEN;
    }
    args->finish_status = odm_job_finish(args->ticket, ODM_STATUS_OK);
    return NULL;
}

static void *odm_job_finish_cancel_race_worker(void *opaque) {
    odm_job_finish_cancel_race_args *args =
        (odm_job_finish_cancel_race_args *)opaque;
    (void)atomic_fetch_add_explicit(args->ready_count, 1, memory_order_release);
    while (atomic_load_explicit(args->go, memory_order_acquire) == 0) {
    }
    if (args->cancel_worker != 0) {
        args->status = odm_job_request_cancel(args->job);
    } else {
        args->status = odm_job_finish(args->ticket, ODM_STATUS_OK);
    }
    return NULL;
}

static odm_status odm_job_snapshot_validate(
    const odm_job_snapshot_stress_args *args,
    const odm_progress_snapshot *snapshot) {
    uint32_t expected_progress = 0u;
    odm_status status;
    if (snapshot->schema_version != 1u ||
        snapshot->generation != args->ticket.generation ||
        snapshot->cancel_requested != 0u ||
        snapshot->work_total != args->work_total ||
        snapshot->work_done > snapshot->work_total) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    if (snapshot->state == ODM_JOB_RUNNING) {
        if (snapshot->phase < ODM_PHASE_PREPARE ||
            snapshot->phase > ODM_PHASE_FINALIZE ||
            snapshot->error_code != ODM_STATUS_OK) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        status = odm_progress_fraction(snapshot->work_done,
                                       snapshot->work_total,
                                       &expected_progress);
        if (status != ODM_STATUS_OK ||
            snapshot->progress_millionths != expected_progress) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        return ODM_STATUS_OK;
    }
    if (snapshot->state == ODM_JOB_SUCCEEDED) {
        if (snapshot->phase != ODM_PHASE_COMPLETE ||
            snapshot->error_code != ODM_STATUS_OK ||
            snapshot->work_done != snapshot->work_total ||
            snapshot->progress_millionths != ODM_PROGRESS_ONE) {
            return ODM_STATUS_INVARIANT_BROKEN;
        }
        return ODM_STATUS_OK;
    }
    return ODM_STATUS_INVARIANT_BROKEN;
}

static void *odm_job_snapshot_stress_worker(void *opaque) {
    odm_job_snapshot_stress_args *args =
        (odm_job_snapshot_stress_args *)opaque;
    odm_progress_snapshot snapshot;
    uint32_t iteration;
    args->status = ODM_STATUS_OK;
    atomic_store_explicit(args->ready, 1, memory_order_release);
    while (atomic_load_explicit(args->go, memory_order_acquire) == 0) {
        (void)sched_yield();
    }
    args->status = odm_job_snapshot(args->job, &snapshot);
    if (args->status == ODM_STATUS_OK) {
        args->status = odm_job_snapshot_validate(args, &snapshot);
    }
    if (args->status == ODM_STATUS_OK && snapshot.state == ODM_JOB_RUNNING) {
        args->observed_running = 1u;
    }
    atomic_store_explicit(args->initial_snapshot_done, 1,
                          memory_order_release);
    while (args->status == ODM_STATUS_OK &&
           atomic_load_explicit(args->writer_started,
                                memory_order_acquire) == 0) {
        (void)sched_yield();
    }
    for (iteration = 0u;
         args->status == ODM_STATUS_OK && iteration < args->snapshot_count;
         iteration++) {
        args->status = odm_job_snapshot(args->job, &snapshot);
        if (args->status == ODM_STATUS_OK) {
            args->status = odm_job_snapshot_validate(args, &snapshot);
        }
        if (args->status == ODM_STATUS_OK) {
            if (snapshot.state == ODM_JOB_RUNNING) {
                args->observed_running = 1u;
            } else if (snapshot.state == ODM_JOB_SUCCEEDED) {
                args->observed_terminal = 1u;
            }
        }
    }
    return NULL;
}

void odm_test_job(odm_test_context *context) {
    uint32_t progress = 0u;
    ODM_TEST_CHECK(context,
                   odm_progress_fraction(0u, 0u, &progress) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, progress == 0u);
    ODM_TEST_CHECK(context,
                   odm_progress_fraction(1u, 0u, &progress) ==
                       ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(context,
                   odm_progress_fraction(UINT64_MAX / 2u, UINT64_MAX, &progress) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, progress == 499999u);
    ODM_TEST_CHECK(context,
                   odm_progress_fraction(UINT64_MAX / 2u + 1u, UINT64_MAX,
                                         &progress) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, progress == 500000u);
    ODM_TEST_CHECK(context,
                   odm_progress_fraction(UINT64_MAX, UINT64_MAX, &progress) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, progress == ODM_PROGRESS_ONE);
    ODM_TEST_CHECK(context,
                   odm_job_state_name(ODM_JOB_CANCELLED)[0] == 'c');
    ODM_TEST_CHECK(context,
                   odm_job_phase_name(ODM_PHASE_FINALIZE)[0] == 'f');
    ODM_TEST_CHECK(context,
                   odm_job_state_name((odm_job_state)999)[0] == 'u');

    {
        odm_job job;
        odm_job_ticket first_ticket;
        odm_job_ticket second_ticket;
        odm_progress_snapshot snapshot;
        int cancelled = 0;
        ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_IDLE);
        ODM_TEST_CHECK(context,
                       odm_job_begin(&job, UINT64_C(100), &first_ticket) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_begin(&job, UINT64_C(100), &second_ticket) ==
                           ODM_STATUS_BUSY);
        ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_BUSY);
        ODM_TEST_CHECK(context,
                       odm_job_publish(first_ticket, ODM_PHASE_EXECUTE,
                                       UINT64_C(25)) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.progress_millionths == 250000u);
        ODM_TEST_CHECK(context, snapshot.work_done == 25u);
        ODM_TEST_CHECK(context,
                       odm_job_publish(first_ticket, ODM_PHASE_EXECUTE,
                                       UINT64_C(10)) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.progress_millionths == 250000u);
        ODM_TEST_CHECK(context, snapshot.work_done == 25u);
        ODM_TEST_CHECK(context,
                       odm_job_publish(first_ticket, ODM_PHASE_VALIDATE,
                                       UINT64_C(30)) == ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context,
                       odm_job_should_cancel(first_ticket, &cancelled) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, cancelled == 0);
        ODM_TEST_CHECK(context,
                       odm_job_finish(first_ticket, ODM_STATUS_OK) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_SUCCEEDED);
        ODM_TEST_CHECK(context, snapshot.work_done == 100u);
        ODM_TEST_CHECK(context,
                       snapshot.progress_millionths == ODM_PROGRESS_ONE);

        ODM_TEST_CHECK(context,
                       odm_job_begin(&job, UINT64_C(10), &second_ticket) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       second_ticket.generation > first_ticket.generation);
        ODM_TEST_CHECK(context,
                       odm_job_publish(first_ticket, ODM_PHASE_EXECUTE, 1u) ==
                           ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context, odm_job_request_cancel(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_finish(second_ticket, ODM_STATUS_OK) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_CANCELLED);
        ODM_TEST_CHECK(context, snapshot.phase == ODM_PHASE_CANCELLED);
        ODM_TEST_CHECK(context, snapshot.error_code == ODM_STATUS_CANCELLED);
        ODM_TEST_CHECK(context, snapshot.cancel_requested == 1u);
        ODM_TEST_CHECK(context,
                       odm_job_request_cancel(&job) == ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
    }
    {
        odm_job job;
        odm_job_ticket ticket;
        odm_progress_snapshot snapshot;
        pthread_t thread;
        _Atomic int ready;
        _Atomic int go;
        odm_job_worker_args args;
        atomic_init(&ready, 0);
        atomic_init(&go, 0);
        ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_begin(&job, UINT64_C(1000), &ticket) ==
                           ODM_STATUS_OK);
        args.ticket = ticket;
        args.ready = &ready;
        args.go = &go;
        args.cancel_status = ODM_STATUS_INTERNAL_ERROR;
        args.finish_status = ODM_STATUS_INTERNAL_ERROR;
        ODM_TEST_CHECK(context,
                       pthread_create(&thread, NULL, odm_job_cancel_worker, &args) ==
                           0);
        while (atomic_load_explicit(&ready, memory_order_acquire) == 0) {
        }
        ODM_TEST_CHECK(context, odm_job_request_cancel(&job) == ODM_STATUS_OK);
        atomic_store_explicit(&go, 1, memory_order_release);
        ODM_TEST_CHECK(context, pthread_join(thread, NULL) == 0);
        ODM_TEST_CHECK(context, args.cancel_status == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, args.finish_status == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_CANCELLED);
        ODM_TEST_CHECK(context, snapshot.state != ODM_JOB_SUCCEEDED);
        ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
    }
    {
        odm_job job;
        odm_job_ticket ticket;
        odm_progress_snapshot snapshot;
        ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_begin(&job, UINT64_C(10), &ticket) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_finish(ticket, ODM_STATUS_OUT_OF_MEMORY) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, snapshot.state == ODM_JOB_FAILED);
        ODM_TEST_CHECK(context, snapshot.phase == ODM_PHASE_FAILED);
        ODM_TEST_CHECK(context,
                       snapshot.error_code == ODM_STATUS_OUT_OF_MEMORY);
        ODM_TEST_CHECK(context, snapshot.progress_millionths == 0u);
        ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &snapshot) ==
                           ODM_STATUS_INVALID_ARGUMENT);
    }
    {
        unsigned iteration;
        for (iteration = 0u; iteration < 256u; iteration++) {
            odm_job job;
            odm_job_ticket ticket;
            odm_progress_snapshot snapshot;
            pthread_t cancel_thread;
            pthread_t finish_thread;
            _Atomic int ready_count;
            _Atomic int go;
            odm_job_finish_cancel_race_args cancel_args;
            odm_job_finish_cancel_race_args finish_args;
            atomic_init(&ready_count, 0);
            atomic_init(&go, 0);
            ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_job_begin(&job, UINT64_C(1), &ticket) ==
                               ODM_STATUS_OK);
            cancel_args.job = &job;
            cancel_args.ticket = ticket;
            cancel_args.ready_count = &ready_count;
            cancel_args.go = &go;
            cancel_args.cancel_worker = 1;
            cancel_args.status = ODM_STATUS_INTERNAL_ERROR;
            finish_args = cancel_args;
            finish_args.cancel_worker = 0;
            ODM_TEST_CHECK(context,
                           pthread_create(&cancel_thread, NULL,
                                          odm_job_finish_cancel_race_worker,
                                          &cancel_args) == 0);
            ODM_TEST_CHECK(context,
                           pthread_create(&finish_thread, NULL,
                                          odm_job_finish_cancel_race_worker,
                                          &finish_args) == 0);
            while (atomic_load_explicit(&ready_count, memory_order_acquire) != 2) {
            }
            atomic_store_explicit(&go, 1, memory_order_release);
            ODM_TEST_CHECK(context, pthread_join(cancel_thread, NULL) == 0);
            ODM_TEST_CHECK(context, pthread_join(finish_thread, NULL) == 0);
            ODM_TEST_CHECK(context, finish_args.status == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           cancel_args.status == ODM_STATUS_OK ||
                               cancel_args.status == ODM_STATUS_INVALID_STATE);
            ODM_TEST_CHECK(context,
                           odm_job_snapshot(&job, &snapshot) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           cancel_args.status != ODM_STATUS_OK ||
                               snapshot.state == ODM_JOB_CANCELLED);
            ODM_TEST_CHECK(context,
                           cancel_args.status == ODM_STATUS_OK ||
                               snapshot.state == ODM_JOB_SUCCEEDED);
            ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
        }
    }
    {
        enum {
            PUBLISH_COUNT = 50000,
            SNAPSHOT_COUNT = 100000
        };
        odm_job job;
        odm_job_ticket ticket;
        odm_progress_snapshot final_snapshot;
        pthread_t snapshot_thread;
        _Atomic int ready;
        _Atomic int go;
        _Atomic int initial_snapshot_done;
        _Atomic int writer_started;
        odm_job_snapshot_stress_args args;
        uint64_t work_done;
        odm_status publish_status = ODM_STATUS_OK;
        atomic_init(&ready, 0);
        atomic_init(&go, 0);
        atomic_init(&initial_snapshot_done, 0);
        atomic_init(&writer_started, 0);
        ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_begin(&job, (uint64_t)PUBLISH_COUNT, &ticket) ==
                           ODM_STATUS_OK);
        args.job = &job;
        args.ticket = ticket;
        args.ready = &ready;
        args.go = &go;
        args.initial_snapshot_done = &initial_snapshot_done;
        args.writer_started = &writer_started;
        args.work_total = (uint64_t)PUBLISH_COUNT;
        args.snapshot_count = SNAPSHOT_COUNT;
        args.observed_running = 0u;
        args.observed_terminal = 0u;
        args.status = ODM_STATUS_INTERNAL_ERROR;
        ODM_TEST_CHECK(context,
                       pthread_create(&snapshot_thread, NULL,
                                      odm_job_snapshot_stress_worker,
                                      &args) == 0);
        while (atomic_load_explicit(&ready, memory_order_acquire) == 0) {
            (void)sched_yield();
        }
        atomic_store_explicit(&go, 1, memory_order_release);
        while (atomic_load_explicit(&initial_snapshot_done,
                                    memory_order_acquire) == 0) {
            (void)sched_yield();
        }
        ODM_TEST_CHECK(context,
                       odm_job_publish(ticket, ODM_PHASE_EXECUTE, 1u) ==
                           ODM_STATUS_OK);
        atomic_store_explicit(&writer_started, 1, memory_order_release);
        for (work_done = 2u; work_done <= (uint64_t)PUBLISH_COUNT;
             work_done++) {
            if (odm_job_publish(ticket, ODM_PHASE_EXECUTE, work_done) !=
                ODM_STATUS_OK) {
                publish_status = ODM_STATUS_INVARIANT_BROKEN;
                break;
            }
        }
        ODM_TEST_CHECK(context, publish_status == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_job_finish(ticket, ODM_STATUS_OK) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, pthread_join(snapshot_thread, NULL) == 0);
        ODM_TEST_CHECK(context, args.status == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, args.observed_running == 1u);
        ODM_TEST_CHECK(context,
                       odm_job_snapshot(&job, &final_snapshot) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, final_snapshot.state == ODM_JOB_SUCCEEDED);
        ODM_TEST_CHECK(context,
                       final_snapshot.progress_millionths == ODM_PROGRESS_ONE);
        ODM_TEST_CHECK(context,
                       final_snapshot.work_done == (uint64_t)PUBLISH_COUNT);
        ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
    }
}

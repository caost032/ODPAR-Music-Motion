#ifndef ODM_VISUAL_DYNAMICS_H
#define ODM_VISUAL_DYNAMICS_H

/* Visual Dynamics v1 — sample-domain causal response.
 *
 * Normative specification: docs/VISUAL_DYNAMICS_V1.md
 *
 * This layer sits between MUSIC EVIDENCE and VISUAL COMPOSITION. It converts
 * evidence into a response that has attack, hold, optional overshoot and
 * release, evaluated in CLOSED FORM from the sample coordinate of the event
 * that caused it.
 *
 * It never creates evidence, never modifies it, and never produces motion in
 * the absence of an accepted trigger. Silence in, stillness out.
 *
 * Everything is int64 sample coordinates on the canonical 48 kHz timeline.
 * Nothing here is expressed in frames or in 100 Hz map ticks, so a response is
 * identical at every frame rate and exact under seek.
 */

#include "odm_status.h"

#include <stdint.h>

#define ODM_VISUAL_DYNAMICS_POLICY_VERSION UINT32_C(1)
#define ODM_VISUAL_DYNAMICS_SCHEMA_VERSION UINT32_C(1)
#define ODM_VISUAL_DYNAMICS_POLICY_BYTES   UINT32_C(1024)

/* Q1.31 unity, shared with the composition layer. */
#define ODM_VD_Q31_ONE UINT32_C(2147483647)

/* Named response classes. A class fixes the temporal character of a target so
 * that styles can choose parameters without being able to make the Grid behave
 * like a radial tip. Ordering of release half-lives is an invariant: it is what
 * enforces micro/meso/macro separation. */
enum {
    ODM_VD_CLASS_RADIAL_TIP     = 0u,
    ODM_VD_CLASS_CORE_IMPACT    = 1u,
    ODM_VD_CLASS_HALO_BODY      = 2u,
    ODM_VD_CLASS_PARTICLE_SPAWN = 3u,
    ODM_VD_CLASS_MEMORY_FIELD   = 4u,
    ODM_VD_CLASS_GRID_FIELD     = 5u,
    ODM_VD_CLASS_BACKGROUND     = 6u,
    ODM_VD_CLASS_COUNT          = 7u
};

/* Response kernel. All durations are canonical 48 kHz sample counts. */
typedef struct {
    uint32_t schema_version;
    uint32_t reserved0;
    uint64_t attack_samples;           /* 0 = instant                        */
    uint64_t overshoot_samples;        /* 0 = no overshoot phase             */
    uint64_t hold_samples;
    uint64_t release_halflife_samples; /* 0 = instant release                */
    uint64_t refractory_samples;       /* bounds repetition, not dynamics    */
    uint32_t dead_zone_q31;            /* evidence at or below this = no event */
    uint32_t knee_hi_q31;              /* smoothstep admission upper bound   */
    uint32_t gain_q31;                 /* applied after admission            */
    uint32_t max_q31;                  /* hard ceiling on peak               */
    uint32_t overshoot_q31;            /* fraction of peak, 0 = none         */
    uint32_t floor_q31;                /* resting level                      */
} odm_vd_kernel;

/* Complete response state. POD of fixed-width integers: serializable,
 * checkpointable and directly comparable between preview and master. */
typedef struct {
    uint64_t event_sample;   /* where the current response began            */
    uint64_t trigger_sample; /* last accepted trigger, for refractory       */
    uint32_t peak_q31;       /* level the response is heading to            */
    uint32_t base_q31;       /* level it departed from, for continuity      */
} odm_vd_state;

/* Fill `out_kernel` with the frozen parameters of a named class.
 * Returns ODM_STATUS_INVALID_ARGUMENT for an unknown class or NULL output. */
odm_status odm_vd_kernel_class(uint32_t response_class, odm_vd_kernel *out_kernel);

/* Validate a kernel. Rejects unknown schema, out-of-range Q1.31 fields and an
 * inverted admission knee. Fail-closed: an invalid kernel never evaluates. */
odm_status odm_vd_kernel_validate(const odm_vd_kernel *kernel);

/* Reset a response to rest. `floor` applies from `sample` onward. */
odm_status odm_vd_state_init(const odm_vd_kernel *kernel, uint64_t sample,
                             odm_vd_state *out_state);

/* Evaluate the response at an absolute sample coordinate.
 *
 * Closed form: depends only on (kernel, state, sample). Evaluating directly at
 * `sample` is bit-identical to evaluating every sample up to it, which is what
 * makes seek exact and free of historical-attack replay.
 *
 * `sample` before `state->event_sample` yields `base_q31`; time never runs
 * backwards into a response. */
odm_status odm_vd_eval(const odm_vd_kernel *kernel, const odm_vd_state *state,
                       uint64_t sample, uint32_t *out_level_q31);

/* Offer evidence at `sample`. The trigger is accepted only if it would actually
 * raise the response, and either the refractory window has elapsed or the new
 * peak exceeds the current one. A rejected trigger leaves state untouched.
 *
 * `out_accepted` may be NULL. */
odm_status odm_vd_trigger(const odm_vd_kernel *kernel, odm_vd_state *state,
                          uint64_t sample, uint32_t evidence_q31,
                          uint32_t *out_accepted);

/* Admission only: evidence -> peak, without touching state. Exposed so oracles
 * and the Reaction Inspector can separate admission from persistence. */
odm_status odm_vd_admit(const odm_vd_kernel *kernel, uint32_t evidence_q31,
                        uint32_t *out_peak_q31);

/* Canonical policy bytes and their digest, in the same style as the visual and
 * layered policies: the identity must cover the semantics it governs. */
odm_status odm_vd_policy_bytes(uint8_t *buffer, uint64_t capacity,
                               uint64_t *out_required);

#endif /* ODM_VISUAL_DYNAMICS_H */

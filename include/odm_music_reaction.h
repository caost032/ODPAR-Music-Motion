#ifndef ODM_MUSIC_REACTION_H
#define ODM_MUSIC_REACTION_H

#include "odm_hash.h"
#include "odm_music_map.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Music Reaction Spine v1
 *
 * This is a versioned derived/inference stream. It does NOT replace or mutate
 * canonical Music Map v1. It consumes the canonical analyzer's deterministic
 * per-tick spectrum scratch immediately after odm_music_analyze_tick() and
 * turns that spectrum into high-resolution, traceable reaction features.
 * Visual code consumes only the resolved structures below; it never reads PCM
 * or FFT scratch directly.
 */
#define ODM_MUSIC_REACTION_SCHEMA_VERSION UINT32_C(2)
#define ODM_MUSIC_REACTION_POLICY_VERSION UINT32_C(11)
#define ODM_MUSIC_REACTION_LANE_COUNT UINT32_C(96)
#define ODM_MUSIC_REACTION_FAMILY_COUNT UINT32_C(6)
#define ODM_MUSIC_REACTION_PROFILE_BYTES UINT32_C(2048)
#define ODM_MUSIC_REACTION_POLICY_BYTES UINT32_C(576)
#define ODM_MUSIC_REACTION_PROJECTION_SCHEMA_VERSION UINT32_C(2)
#define ODM_MUSIC_REACTION_PROJECTION_MAX_GAP_SAMPLES UINT32_C(48000)
#define ODM_MUSIC_REACTION_EVENT_SCHEMA_VERSION UINT32_C(1)
#define ODM_MUSIC_REACTION_EVENT_RECORD_BYTES UINT32_C(128)
#define ODM_MUSIC_REACTION_EVENT_PROJECTION_SCHEMA_VERSION UINT32_C(1)
#define ODM_MUSIC_REACTION_EVENT_PROJECTION_RECORD_BYTES UINT32_C(128)

#define ODM_MUSIC_REACTION_FAMILY_SUB      UINT32_C(0)
#define ODM_MUSIC_REACTION_FAMILY_BASS     UINT32_C(1)
#define ODM_MUSIC_REACTION_FAMILY_LOW_MID  UINT32_C(2)
#define ODM_MUSIC_REACTION_FAMILY_BODY     UINT32_C(3)
#define ODM_MUSIC_REACTION_FAMILY_PRESENCE UINT32_C(4)
#define ODM_MUSIC_REACTION_FAMILY_AIR      UINT32_C(5)

#define ODM_MUSIC_REACTION_EVENT_SUB      UINT32_C(1)
#define ODM_MUSIC_REACTION_EVENT_BASS     UINT32_C(2)
#define ODM_MUSIC_REACTION_EVENT_LOW_MID  UINT32_C(4)
#define ODM_MUSIC_REACTION_EVENT_BODY     UINT32_C(8)
#define ODM_MUSIC_REACTION_EVENT_PRESENCE UINT32_C(16)
#define ODM_MUSIC_REACTION_EVENT_AIR      UINT32_C(32)
#define ODM_MUSIC_REACTION_EVENT_ANY          UINT32_C(64)
#define ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK UINT32_C(128)
/* Presentation-only trace bit: the dominant event used its evidence-gated
 * sub-tick refinement instead of the canonical 100 Hz tick center. */
#define ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED UINT32_C(256)

#define ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE UINT32_C(1)

/* Raw high-resolution derived tick. Values are unprofiled Q1.31 magnitudes.
 * Lane 0 starts at FFT bin 1; DC is never a reaction source. */
typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t tick_index;
    uint64_t center_sample;
    uint32_t lane_magnitude_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t broadband_magnitude_q31;
    uint32_t reserved[7];
} odm_music_reaction_tick;

/* Robust full-track references. References use a deterministic 99th-percentile
 * exact order statistic rather than a single global maximum, so one extreme hit
 * cannot suppress the rest of a song. */
typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t tick_count;
    uint64_t first_tick_index;
    uint64_t last_tick_index;
    uint32_t lane_magnitude_ref[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_attack_ref[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t family_magnitude_ref[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t family_attack_ref[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t broadband_magnitude_ref;
    uint32_t broadband_attack_ref;
    uint32_t reserved[14];
} odm_music_reaction_profile;

/* Sequential resolver state. Attack is immediate; release is bounded and
 * deterministic. The adaptive event threshold is derived only from past
 * resolved attacks and therefore cannot anticipate or invent an event. */
typedef struct {
    uint32_t schema_version;
    uint32_t initialized;
    uint64_t next_tick_index;
    uint32_t previous_lane_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_envelope_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_fast_envelope_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t previous_family_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t family_envelope_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t previous_broadband_q31;
    uint32_t broadband_envelope_q31;
    uint32_t event_baseline_q31;
    uint32_t event_pulse_q31;
    uint32_t refractory_ticks;
    uint32_t reserved[13];
} odm_music_reaction_state;

/* Fully normalized, visual-safe reaction state. lane_q31 is the slow held
 * envelope, lane_fast_q31 is the faster same-lane envelope, and
 * lane_attack_q31 is same-tick onset authority. Together they retain 96
 * independent spectral causes. event_strength_q31 is same-tick raw
 * causal onset evidence; event_pulse_q31 is its bounded temporal tail. Offline
 * full-track refinement may additionally publish contextual macro_salience_q31
 * and macro_pulse_q31. Those fields are presentation salience, not new audio
 * evidence, and are valid only when FRAME_CONTEXTUAL_SALIENCE is set. */
typedef struct {
    uint32_t schema_version;
    uint32_t event_flags;
    uint64_t tick_index;
    uint64_t center_sample;
    uint32_t lane_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_fast_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_attack_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t family_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t family_attack_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t broadband_q31;
    uint32_t broadband_attack_q31;
    uint32_t event_strength_q31;
    uint32_t event_pulse_q31;
    uint32_t event_threshold_q31;
    uint32_t macro_salience_q31;
    uint32_t macro_pulse_q31;
    uint32_t frame_flags;
    uint32_t reserved[8];
} odm_music_reaction_frame;

typedef struct {
    uint32_t schema_version;
    uint32_t radial_index;
    uint32_t lane_a;
    uint32_t lane_b;
    uint32_t direct_level_q31;
    uint32_t direct_attack_q31;
    uint32_t event_strength_q31;
    uint32_t event_flags;
    uint64_t tick_index;
    uint64_t center_sample;
    uint32_t macro_salience_q31;
    uint32_t macro_pulse_q31;
    uint32_t frame_flags;
    /* Reuses the former trailing reserved u32, preserving the 64-byte native
     * layout. It has the same aggregation domain as direct_level_q31, but for
     * the fast same-lane envelope. Together slow/fast/attack expose the full
     * reaction-side multiscale evidence. Legacy 48-pair traces are candidate
     * summaries; use odm_layered_trace_radial() for the selected pixel tuple. */
    uint32_t direct_fast_level_q31;
} odm_music_reaction_trace;

/* Explainable macro-event provenance. This is a read-only derivation from one
 * resolved reaction frame: it does not classify beats or sources. Family ties
 * are resolved by lower canonical family index so traces are reproducible. */
typedef struct {
    uint32_t schema_version;
    uint32_t event_flags;
    uint64_t tick_index;
    uint64_t center_sample;
    uint32_t event_score_q31;
    uint32_t event_strength_q31;
    uint32_t event_threshold_q31;
    uint32_t broadband_attack_q31;
    uint32_t primary_family;
    uint32_t primary_family_attack_q31;
    uint32_t secondary_family;
    uint32_t secondary_family_attack_q31;
    uint32_t macro_salience_q31;
    uint32_t macro_pulse_q31;
    uint32_t frame_flags;
    uint32_t reserved[5];
} odm_music_reaction_event_trace;

/* Causal projection from the canonical 100 Hz reaction timeline to an output
 * presentation instant. Sustained values (lane/family/broadband levels,
 * event_pulse and event_threshold) are held from the latest reaction tick whose
 * center_sample <= presentation_sample. Instantaneous attacks and event
 * strength/flags are aggregated by max/OR over source ticks in the half-open
 * causal interval (previous_presentation_sample, presentation_sample]. The first
 * project-origin presentation uses previous_presentation_sample = -1 so tick 0
 * is admitted exactly once. No future tick may influence the projection. */
typedef struct {
    uint32_t schema_version;
    uint32_t event_flags;
    int64_t previous_presentation_sample;
    uint64_t presentation_sample;
    uint64_t source_tick_index;
    uint64_t source_center_sample;
    uint64_t first_captured_tick_index;
    uint64_t last_captured_tick_index;
    uint32_t captured_tick_count;
    uint32_t captured_event_tick_count;
    uint32_t lane_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_fast_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t lane_attack_q31[ODM_MUSIC_REACTION_LANE_COUNT];
    uint32_t family_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t family_attack_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t broadband_q31;
    uint32_t broadband_attack_q31;
    uint32_t event_strength_q31;
    uint32_t event_pulse_q31;
    uint32_t event_threshold_q31;
    uint32_t macro_salience_q31;
    uint32_t macro_pulse_q31;
    uint32_t frame_flags;
    uint32_t reserved[6];
} odm_music_reaction_projection;

/* Offline macro-event provenance record. It preserves the exact evidence used
 * to publish a contextual event plus a bounded sub-tick timing refinement.
 * `refined_sample` is an inference from the immediate score neighborhood, not
 * a replacement for the canonical 100 Hz Music-Reaction tick. The record does
 * not claim beat/BPM/source classification. */
typedef struct {
    uint32_t schema_version;
    uint32_t event_flags;
    uint64_t event_index;
    uint64_t peak_tick_index;
    uint64_t peak_center_sample;
    uint64_t refined_sample;
    int32_t refined_offset_samples;
    uint32_t timing_confidence_q31;
    uint32_t score_q31;
    uint32_t strength_basis_q31;
    uint32_t local_threshold_q31;
    uint32_t macro_gate_q31;
    uint32_t contextual_salience_q31;
    uint32_t strongest_family;
    uint32_t second_family;
    uint32_t family_attack_q31[ODM_MUSIC_REACTION_FAMILY_COUNT];
    uint32_t broadband_attack_q31;
    uint32_t frame_flags;
    uint32_t reserved[5];
} odm_music_reaction_event;

/* Offline sample-domain projection of the full-track event stream. This is
 * intentionally distinct from the real-time causal reaction projection:
 * `dominant_effective_sample` may use a bounded offline sub-tick inference,
 * but only when its local curvature confidence passes frozen policy. Weak
 * timing evidence falls back exactly to `peak_center_sample`, preventing false
 * precision. `event_pulse_q31` is evaluated in the canonical 48 kHz sample
 * domain, so accepted timing is not quantized back to output FPS.
 *
 * UINT64_MAX is used for event-index/sample sentinels when no event owns the
 * corresponding field. */
typedef struct {
    uint32_t schema_version;
    uint32_t event_flags;
    int64_t previous_presentation_sample;
    uint64_t presentation_sample;
    uint64_t first_event_index;
    uint64_t last_event_index;
    uint64_t dominant_event_index;
    uint64_t dominant_effective_sample;
    uint64_t pulse_owner_event_index;
    uint64_t pulse_owner_effective_sample;
    uint32_t captured_event_count;
    uint32_t event_salience_q31;
    uint32_t event_pulse_q31;
    uint32_t timing_confidence_q31;
    uint32_t strongest_family;
    uint32_t second_family;
    uint32_t broadband_attack_q31;
    uint32_t score_q31;
    uint32_t strength_basis_q31;
    uint32_t pulse_owner_salience_q31;
    /* Sample-domain strength of the dominant newly captured event at this
     * presentation sample. Unlike event_salience_q31 (the event's immutable
     * contextual rank), this value decays from dominant_effective_sample and
     * is therefore the correct instantaneous global-render authority. */
    uint32_t dominant_pulse_q31;
    uint32_t reserved[3];
} odm_music_reaction_event_projection;

/* Frozen spectral lane plan. `end_bin` is exclusive. */
odm_status odm_music_reaction_lane_bounds(uint32_t lane,
                                          uint32_t *out_start_bin,
                                          uint32_t *out_end_bin);

/* Must be called after a successful odm_music_analyze_tick() for the same
 * base tick while scratch still contains that tick's deterministic spectrum. */
odm_status odm_music_reaction_extract_tick(
    const odm_music_analysis_tick *base_tick,
    const odm_music_analysis_scratch *scratch,
    odm_music_reaction_tick *out_tick);

odm_status odm_music_reaction_profile_build(
    const odm_music_reaction_tick *ticks,
    uint64_t tick_count,
    odm_music_reaction_profile *out_profile);

odm_status odm_music_reaction_profile_bytes(
    const odm_music_reaction_profile *profile,
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *out_required);

odm_status odm_music_reaction_profile_sha256(
    const odm_music_reaction_profile *profile,
    odm_sha256_digest *out_hash);

/* Canonical identity of the complete Music-Reaction policy. Unlike a track
 * profile, this record contains no song-dependent values. It binds spectral
 * topology, robust quantiles, attack headroom, temporal release constants,
 * global-event threshold/refractory semantics, contextual salience and causal
 * presentation-projection semantics. */
odm_status odm_music_reaction_policy_bytes(
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *out_required);

odm_status odm_music_reaction_policy_current_sha256(
    odm_sha256_digest *out_hash);

odm_status odm_music_reaction_state_init(odm_music_reaction_state *state);

odm_status odm_music_reaction_resolve_tick(
    odm_music_reaction_state *state,
    const odm_music_reaction_profile *profile,
    const odm_music_reaction_tick *tick,
    odm_music_reaction_frame *out_frame);

/* Offline full-track event refinement. This never changes spectral lanes,
 * family envelopes, attacks, thresholds, tick timestamps, or canonical Music
 * Map facts. It only re-picks macro impact publication at a deterministic local
 * maximum within a +/-2 tick (20 ms) evidence window, reconstructs the raw
 * evidence pulse, and derives a separate robust full-track quantile-ranked macro-salience lane
 * for global presentation controls. Salience never rewrites local evidence.
 * This is onset/context refinement, NOT a beat/BPM/section claim.
 *
 * Transactional for invalid input: the complete sequence is preflighted before
 * caller output is modified. In-place input/output is supported. */
odm_status odm_music_reaction_refine_events_offline(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    odm_music_reaction_frame *out_frames,
    uint64_t frame_capacity);

/* Build the explicit offline macro-event provenance stream without mutating
 * reaction frames. Query mode uses out_events=NULL,event_capacity=0 and still
 * returns the exact record count and macro gate. Publication uses the same
 * local-peak, candidate-median, refractory and contextual-salience policy as
 * odm_music_reaction_refine_events_offline(). Insufficient nonzero capacity is
 * transactional: caller event storage is not modified. */
odm_status odm_music_reaction_build_events_offline(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    odm_music_reaction_event *out_events,
    uint64_t event_capacity,
    uint64_t *out_event_count,
    uint32_t *out_macro_gate_q31);

/* Project a resolved full-track reaction sequence to one video presentation
 * instant without temporal aliasing. The input sequence must be canonical and
 * consecutive. `previous_presentation_sample` is -1 only for the project-origin
 * first frame; otherwise it must be >=0 and strictly less than
 * `presentation_sample`. A projection gap is bounded to one second so malformed
 * callers cannot turn one render frame into an unbounded scan. */
odm_status odm_music_reaction_project_presentation(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    int64_t previous_presentation_sample,
    uint64_t presentation_sample,
    odm_music_reaction_projection *out_projection);

/* O(N+F) offline batch form for export. Presentation samples must be strictly
 * increasing. The first sample starts a fresh causal view: its instantaneous
 * interval is (sample-1, sample], so seeking into a track cannot replay old
 * attacks. The whole source sequence and sample schedule are preflighted before
 * any caller output is modified. */
odm_status odm_music_reaction_project_timeline(
    const odm_music_reaction_frame *frames,
    uint64_t frame_count,
    const uint64_t *presentation_samples,
    uint64_t presentation_count,
    odm_music_reaction_projection *out_projections,
    uint64_t projection_capacity);

/* Project the offline event stream directly in canonical sample time. The
 * complete event stream and presentation schedule are preflighted before any
 * output is modified. A seek-style first presentation captures only an event
 * that lands exactly on that sample, while pulse state is reconstructed from
 * prior events without replaying them as new impacts. */
odm_status odm_music_reaction_project_events_offline(
    const odm_music_reaction_event *events,
    uint64_t event_count,
    const uint64_t *presentation_samples,
    uint64_t presentation_count,
    odm_music_reaction_event_projection *out_projections,
    uint64_t projection_capacity);

/* Reaction-side provenance summary for a legacy 48-sector pair. It exposes
 * the exact two candidate lanes and aggregate slow/fast/attack evidence, but
 * does not claim which candidate the compositor will select. Post-compositor
 * winner provenance is provided by odm_layered_trace_radial(). */
odm_status odm_music_reaction_trace_radial(
    const odm_music_reaction_frame *frame,
    uint32_t radial_index,
    odm_music_reaction_trace *out_trace);

/* Provenance helper for the high-resolution 96-segment compositor. Each
 * visible radial is bound one-to-one to exactly one reaction lane. */
odm_status odm_music_reaction_trace_hires_radial(
    const odm_music_reaction_frame *frame,
    uint32_t radial_index,
    odm_music_reaction_trace *out_trace);

/* Reconstruct the exact macro-event evidence path used by the resolver. The
 * trace is valid for both streaming and contextual full-track frames. */
odm_status odm_music_reaction_trace_event(
    const odm_music_reaction_frame *frame,
    odm_music_reaction_event_trace *out_trace);

#ifdef __cplusplus
}
#endif

#endif /* ODM_MUSIC_REACTION_H */

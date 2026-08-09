#ifndef ODM_VISUAL_H
#define ODM_VISUAL_H

#include "odm_ir.h"
#include "odm_music_reaction.h"
#include "odm_hash.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_VISUAL_HISTORY_SCHEMA_VERSION UINT32_C(1)
#define ODM_VISUAL_HISTORY_MAX_EVENTS UINT32_C(32)
#define ODM_VISUAL_HISTORY_MAX_TRANSFER_DEPTH UINT32_C(64)

typedef struct {
    uint32_t cue_id;
    uint32_t cue_kind;
    uint32_t scene_id;
    uint32_t reserved0;
    int64_t sample;
    int64_t age_samples;
    uint64_t seed;
} odm_visual_event;

typedef struct {
    uint32_t schema_version;
    uint32_t node_id;
    uint32_t scene_id;
    uint32_t state_slot;
    uint32_t event_count;
    uint32_t flags;
    uint32_t reserved[2];
    int64_t sample;
    uint64_t root_seed;
    odm_visual_event events[ODM_VISUAL_HISTORY_MAX_EVENTS];
} odm_visual_history;

odm_status odm_visual_history_resolve(
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    uint32_t node_id,
    int64_t sample,
    odm_visual_history *out_history);


/* -------------------------------------------------------------------------
 * Advanced Composition Layer v1
 *
 * Post-constitutional visual composition resolver. It never changes Music
 * Map truth, Render IR truth or canonical pixels. It converts an exact Music
 * Map tick stream into a compact deterministic composition state that can be
 * consumed once per frame by a renderer. All math is integer/fixed-point.
 * ------------------------------------------------------------------------- */

#define ODM_VISUAL_POLICY_VERSION UINT32_C(9)
#define ODM_VISUAL_POLICY_BYTES UINT32_C(1024)

/* Canonical semantic identity for Advanced Composition + Visual Director.
 * The byte encoding is fixed little-endian and zero-padded to
 * ODM_VISUAL_POLICY_BYTES. It is independent from compiler ABI/layout. */
odm_status odm_visual_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required);
odm_status odm_visual_policy_current_sha256(odm_sha256_digest *out_hash);

#define ODM_COMPOSITION_SCHEMA_VERSION UINT32_C(1)
#define ODM_COMPOSITION_RADIAL_SEGMENTS UINT32_C(48)
#define ODM_COMPOSITION_RADIAL_SEGMENTS_MAX UINT32_C(96)
#define ODM_COMPOSITION_MODE_VOID       UINT32_C(0)
#define ODM_COMPOSITION_MODE_CALM       UINT32_C(1)
#define ODM_COMPOSITION_MODE_FLOW       UINT32_C(2)
#define ODM_COMPOSITION_MODE_IMPACT     UINT32_C(3)
#define ODM_COMPOSITION_FLAG_GLITCH_GATE UINT32_C(1)
#define ODM_COMPOSITION_FLAG_SILENCE     UINT32_C(2)
#define ODM_COMPOSITION_FLAG_RADIAL_HIRES UINT32_C(4)
#define ODM_COMPOSITION_FLAG_STRICT_CAUSAL UINT32_C(8)
#define ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE UINT32_C(16)
#define ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE UINT32_C(32)

typedef struct {
    uint32_t schema_version;
    uint32_t flags;
    uint64_t tick_count;
    uint64_t first_tick_index;
    uint64_t last_tick_index;
    uint32_t envelope_short_ref;
    uint32_t envelope_medium_ref;
    uint32_t spectral_flux_ref;
    uint32_t reserved0;
    uint64_t positive_delta_ref;
    uint32_t band_ref[ODM_MUSIC_BAND_COUNT];
    uint32_t reserved[8];
} odm_composition_profile;

typedef struct {
    uint32_t schema_version;
    uint32_t initialized;
    uint64_t next_tick_index;
    uint64_t seed;
    uint32_t energy_slow_q31;
    uint32_t impact_hold_q31;
    uint32_t width_slow_q31;
    uint32_t high_slow_q31;
    uint32_t glitch_cooldown_ticks;
    uint32_t phase;
    uint32_t reserved[6];
} odm_composition_resolver_state;

typedef struct {
    uint32_t schema_version;
    uint32_t mode;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t tick_index;
    uint64_t center_sample;
    uint32_t phase;
    uint32_t ring_phase;
    int32_t content_offset_x_q31;
    int32_t content_offset_y_q31;
    uint32_t core_scale_q31;
    uint32_t core_breath_q31;
    uint32_t core_border_q31;
    uint32_t halo_q31;
    uint32_t radial_gain_q31;
    uint32_t radial_aperture_q31;
    uint32_t lateral_field_q31;
    uint32_t grid_q31;
    uint32_t particles_q31;
    uint32_t memory_echo_q31;
    uint32_t fracture_q31;
    uint32_t chroma_split_q31;
    uint32_t scan_q31;
    uint32_t vignette_q31;
    uint32_t void_q31;
    uint32_t accent_q31;
    uint32_t band_q31[ODM_MUSIC_BAND_COUNT];
    uint32_t radial_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    /* Music-Reaction provenance. Valid only when RADIAL_PROVENANCE is set.
     * body is the held local spectral contribution before same-lane attack;
     * attack is the exact normalized same-lane transient authority. */
    uint32_t radial_body_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    uint32_t radial_release_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    uint32_t radial_attack_q31[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    uint32_t reserved[8];
} odm_composition_frame_state;

odm_status odm_composition_profile_build(
    const odm_music_analysis_tick *ticks,
    uint64_t tick_count,
    odm_composition_profile *out_profile);

odm_status odm_composition_resolve_tick_profiled(
    odm_composition_resolver_state *state,
    const odm_composition_profile *profile,
    const odm_music_analysis_tick *tick,
    odm_composition_frame_state *out_frame);

odm_status odm_composition_resolve_batch_profiled(
    odm_composition_resolver_state *state,
    const odm_composition_profile *profile,
    const odm_music_analysis_tick *ticks,
    uint64_t tick_count,
    odm_composition_frame_state *out_frames,
    uint64_t frame_capacity);

odm_status odm_composition_resolver_init(
    odm_composition_resolver_state *state,
    uint64_t seed);

odm_status odm_composition_resolve_tick(
    odm_composition_resolver_state *state,
    const odm_music_analysis_tick *tick,
    odm_composition_frame_state *out_frame);

/* Music-Reaction Spine path. This mapping is intentionally stateless: all
 * temporal audio behavior has already been resolved by
 * odm_music_reaction_resolve_tick(). No procedural phase is allowed to alter
 * radial amplitudes in this path. */
odm_status odm_composition_resolve_music_reaction(
    const odm_music_analysis_tick *base_tick,
    const odm_music_reaction_frame *reaction,
    odm_composition_frame_state *out_frame);

/* Presentation-projected Music-Reaction path. This consumes the causal
 * frame-rate projection rather than a raw 100 Hz reaction tick, preserving
 * sub-frame attacks without admitting future evidence. `base_tick` must be the
 * held canonical Music Map tick at projection.presentation_sample. */
odm_status odm_composition_resolve_music_reaction_projection(
    const odm_music_analysis_tick *base_tick,
    const odm_music_reaction_projection *projection,
    odm_composition_frame_state *out_frame);

/* Offline sample-accurate presentation path. Local spectral body/release/attack
 * comes only from the causal reaction projection. Global macro authority is
 * replaced by the offline event projection at the exact presentation sample:
 * a contextual event is published once in its evidence-gated effective-sample
 * interval, while its macro pulse is evaluated directly in 48 kHz sample time.
 * This prevents the 100 Hz event center from becoming a second, coarse global
 * impact. The event projection does not alter any of the 96 local lanes. */
odm_status odm_composition_resolve_music_reaction_sample_presentation(
    const odm_music_analysis_tick *base_tick,
    const odm_music_reaction_projection *projection,
    const odm_music_reaction_event_projection *event_projection,
    odm_composition_frame_state *out_frame);

/* Transactional batch resolver. `state` and output are published only if every
 * tick is canonical and consecutive. `ticks` may be empty. */
odm_status odm_composition_resolve_batch(
    odm_composition_resolver_state *state,
    const odm_music_analysis_tick *ticks,
    uint64_t tick_count,
    odm_composition_frame_state *out_frames,
    uint64_t frame_capacity);


/* -------------------------------------------------------------------------
 * Visual Director v3
 *
 * Deterministic macro-composition grammar layered above Composition v1.
 * It consumes composition states only; it does not reinterpret audio and it
 * never changes Music Map truth. The director enforces minimum dwell,
 * novelty accumulation and bounded transitions so a long-form visual evolves
 * instead of behaving like a per-frame effect roulette.
 * ------------------------------------------------------------------------- */

#define ODM_DIRECTOR_SCHEMA_VERSION UINT32_C(3)
#define ODM_DIRECTOR_LAYOUT_MONOLITH UINT32_C(0)
#define ODM_DIRECTOR_LAYOUT_ORBIT    UINT32_C(1)
#define ODM_DIRECTOR_LAYOUT_WINGS    UINT32_C(2)
#define ODM_DIRECTOR_LAYOUT_MEMORY   UINT32_C(3)
#define ODM_DIRECTOR_LAYOUT_EXPAND   UINT32_C(4)
#define ODM_DIRECTOR_LAYOUT_VOID     UINT32_C(5)
#define ODM_DIRECTOR_LAYOUT_FRACTURE UINT32_C(6)
#define ODM_DIRECTOR_FLAG_LAYOUT_SHIFT UINT32_C(1)
#define ODM_DIRECTOR_FLAG_RECOVERY     UINT32_C(2)
#define ODM_DIRECTOR_FLAG_IMPACT       UINT32_C(4)

/* The state is caller-owned, allocation-free and sequential. */
typedef struct {
    uint32_t schema_version;
    uint32_t initialized;
    uint64_t next_tick_index;
    uint64_t seed;
    uint32_t current_layout;
    uint32_t previous_layout;
    uint32_t dwell_ticks;
    uint32_t transition_ticks_left;
    uint64_t novelty_accum_q31_ticks;
    uint32_t macro_energy_q31;
    uint32_t macro_width_q31;
    uint32_t macro_memory_q31;
    uint32_t macro_fracture_q31;
    uint32_t phase;
    uint32_t layout_epoch;
    uint32_t transition_total_ticks;
    uint32_t candidate_layout;
    uint32_t candidate_ticks;
    uint32_t reserved0;
} odm_director_state;

/* Fixed 128-byte per-tick macro-layout result. */
typedef struct {
    uint32_t schema_version;
    uint32_t layout;
    uint32_t previous_layout;
    uint32_t flags;
    uint64_t tick_index;
    uint32_t transition_q31;
    uint32_t macro_energy_q31;
    uint32_t macro_novelty_q31;
    uint32_t core_extent_q31;
    uint32_t orbit_q31;
    uint32_t wings_q31;
    uint32_t memory_panels_q31;
    uint32_t tunnel_q31;
    uint32_t architecture_q31;
    uint32_t backdrop_q31;
    uint32_t depth_q31;
    uint32_t edge_activity_q31;
    int32_t asymmetry_x_q31;
    int32_t asymmetry_y_q31;
    uint32_t orbit_phase;
    uint32_t layout_epoch;
    uint32_t layer_count;
    uint32_t pane_count;
    uint32_t reserved[8];
} odm_director_frame_state;

odm_status odm_director_init(odm_director_state *state, uint64_t seed);

odm_status odm_director_resolve_tick(
    odm_director_state *state,
    const odm_composition_frame_state *composition,
    odm_director_frame_state *out_frame);

/* Transactional: neither caller state nor output is published if any input
 * state is non-canonical or non-consecutive. */
odm_status odm_director_resolve_batch(
    odm_director_state *state,
    const odm_composition_frame_state *composition_frames,
    uint64_t frame_count,
    odm_director_frame_state *out_frames,
    uint64_t frame_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ODM_VISUAL_H */

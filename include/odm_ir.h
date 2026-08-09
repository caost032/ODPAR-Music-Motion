#ifndef ODM_IR_H
#define ODM_IR_H

#include "odm_hash.h"
#include "odm_music_map.h"
#include "odm_score.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_RENDER_IR_KIND UINT32_C(0x30524952) /* "RIR0" LE */
#define ODM_RENDER_IR_SCHEMA_MAJOR 1u
#define ODM_RENDER_IR_SCHEMA_MINOR 1u
#define ODM_RENDER_IR_HEADER_BYTES 256u
#define ODM_RENDER_IR_SECTION_HEADER_BYTES 16u
#define ODM_RENDER_IR_SECTION_COUNT 9u

#define ODM_IR_SECTION_CAPABILITIES  UINT32_C(1)
#define ODM_IR_SECTION_RESOURCES     UINT32_C(2)
#define ODM_IR_SECTION_TIMELINE      UINT32_C(3)
#define ODM_IR_SECTION_NODES         UINT32_C(4)
#define ODM_IR_SECTION_MODULATORS    UINT32_C(5)
#define ODM_IR_SECTION_AUTOMATION    UINT32_C(6)
#define ODM_IR_SECTION_TRANSITIONS   UINT32_C(7)
#define ODM_IR_SECTION_STATE_TRANSFER UINT32_C(8)
#define ODM_IR_SECTION_OUTPUT        UINT32_C(9)

#define ODM_IR_CAPABILITY_RECORD_BYTES UINT32_C(32)
#define ODM_IR_RESOURCE_RECORD_BYTES   UINT32_C(64)
#define ODM_IR_TIMELINE_RECORD_BYTES   UINT32_C(64)
#define ODM_IR_NODE_RECORD_BYTES       UINT32_C(128)
#define ODM_IR_MODULATOR_RECORD_BYTES  UINT32_C(64)
#define ODM_IR_AUTOMATION_RECORD_BYTES UINT32_C(64)
#define ODM_IR_TRANSITION_RECORD_BYTES UINT32_C(64)
#define ODM_IR_TRANSFER_RECORD_BYTES   UINT32_C(64)
#define ODM_IR_OUTPUT_RECORD_BYTES     UINT32_C(64)

#define ODM_IR_TIMELINE_ACT   UINT32_C(1)
#define ODM_IR_TIMELINE_SCENE UINT32_C(2)
#define ODM_IR_TIMELINE_CUE   UINT32_C(3)

typedef struct {
    uint64_t canonical_frames;
    odm_sha256_digest music_map_sha256;
    odm_sha256_digest music_policy_sha256;
} odm_music_map_binding;

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    int32_t fps;
    uint32_t width;
    uint32_t height;
    uint32_t section_count;
    int64_t samples_per_frame;
    int64_t project_end_sample;
    int64_t frame_count;
    int64_t output_end_sample;
    uint64_t canonical_music_frames;
    uint64_t project_seed;
    odm_sha256_digest score_sha256;
    odm_sha256_digest capability_sha256;
    odm_sha256_digest music_map_sha256;
    odm_sha256_digest music_policy_sha256;
    odm_sha256_digest record_sha256;
} odm_render_ir_info;

typedef struct {
    uint32_t node_id;
    uint32_t scene_id;
    uint32_t role;
    uint32_t capability_id;
    uint32_t resource_id;
    int32_t z_index;
    odm_q32_32 x_q32_32;
    odm_q32_32 y_q32_32;
    odm_q32_32 scale_x_q32_32;
    odm_q32_32 scale_y_q32_32;
    odm_q1_31 opacity_q31;
    odm_phase_u32 phase;
    uint64_t seed_domain;
    uint32_t state_slot;
    uint32_t entry_kind;
    uint32_t exit_kind;
    uint32_t entry_duration_samples;
    uint32_t exit_duration_samples;
    odm_q1_31 entry_weight_q31;
    odm_q1_31 exit_weight_q31;
    odm_q1_31 scene_weight_q31;
    uint32_t flags;
    uint32_t reserved[3];
} odm_frame_node_state;

typedef struct {
    uint32_t transfer_id;
    uint32_t from_scene_id;
    uint32_t to_scene_id;
    uint32_t state_slot;
    uint32_t mode;
    uint32_t reserved[3];
} odm_frame_transfer_event;

typedef struct {
    uint32_t schema_version;
    int32_t fps;
    int64_t frame_index;
    int64_t sample;
    uint32_t scene_a_id;
    uint32_t scene_b_id;
    uint32_t transition_id;
    odm_q1_31 transition_progress_q31;
    uint64_t expected_music_tick;
    uint32_t node_count;
    uint32_t transfer_count;
    uint32_t flags;
    uint32_t reserved[5];
} odm_frame_state;

odm_status odm_score_compile_render_ir(
    const odm_score_view *score,
    const odm_music_map_binding *music,
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *out_required,
    odm_sha256_digest *out_ir_sha256);

odm_status odm_render_ir_validate(
    const uint8_t *record,
    uint64_t record_bytes,
    const odm_sha256_digest *expected_score_sha256,
    const odm_sha256_digest *expected_music_policy_sha256,
    odm_render_ir_info *out_info);

odm_status odm_frame_state_requirements(
    const uint8_t *record,
    uint64_t record_bytes,
    int64_t frame_index,
    const odm_music_analysis_tick *music_tick,
    uint64_t *out_required_nodes,
    uint64_t *out_required_transfers);

odm_status odm_frame_state_resolve(
    const uint8_t *record,
    uint64_t record_bytes,
    int64_t frame_index,
    const odm_music_analysis_tick *music_tick,
    odm_frame_node_state *nodes,
    uint64_t node_capacity,
    odm_frame_transfer_event *transfers,
    uint64_t transfer_capacity,
    uint64_t *out_required_nodes,
    uint64_t *out_required_transfers,
    odm_frame_state *out_state);

#ifdef __cplusplus
}
#endif

#endif /* ODM_IR_H */

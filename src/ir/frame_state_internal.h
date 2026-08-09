#ifndef ODM_FRAME_STATE_INTERNAL_H
#define ODM_FRAME_STATE_INTERNAL_H

#include "ir_internal.h"

odm_status odm_frame_node_compute_internal(
    const odm_ir_parsed *parsed,
    const uint8_t *node_record,
    int64_t sample,
    uint32_t scene_a,
    uint32_t scene_b,
    odm_q1_31 transition_progress,
    const odm_music_analysis_tick *music_tick,
    int synthetic_music,
    int apply_scene_weight,
    odm_frame_node_state *out_state);

#endif /* ODM_FRAME_STATE_INTERNAL_H */

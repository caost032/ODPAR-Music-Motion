#ifndef ODM_VISUAL_INTERNAL_H
#define ODM_VISUAL_INTERNAL_H

#include "odm_visual.h"
#include "ir_internal.h"

#include <stdint.h>

typedef struct {
    odm_q1_31 r;
    odm_q1_31 g;
    odm_q1_31 b;
    odm_q1_31 a;
} odm_visual_pixel_q31;

odm_status odm_visual_history_resolve_parsed(
    const odm_ir_parsed *parsed,
    const uint8_t *node_record,
    int64_t sample,
    odm_visual_history *out_history);

odm_status odm_visual_procedural_pixel(
    const odm_frame_node_state *node,
    const odm_visual_history *history,
    int64_t sample,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    odm_visual_pixel_q31 *out_pixel);

#endif /* ODM_VISUAL_INTERNAL_H */

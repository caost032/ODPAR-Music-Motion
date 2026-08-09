#ifndef ODM_IR_INTERNAL_H
#define ODM_IR_INTERNAL_H

#include "odm_ir.h"

#include <stdint.h>

typedef struct {
    const uint8_t *records;
    uint32_t count;
    uint32_t record_bytes;
} odm_ir_section_view;

typedef struct {
    odm_render_ir_info info;
    odm_ir_section_view sections[ODM_RENDER_IR_SECTION_COUNT];
} odm_ir_parsed;

odm_status odm_ir_parse_internal(const uint8_t *record, uint64_t record_bytes,
                                 odm_ir_parsed *out_parsed);
uint32_t odm_ir_read_u32(const uint8_t *p);
int32_t odm_ir_read_i32(const uint8_t *p);
uint64_t odm_ir_read_u64(const uint8_t *p);
int64_t odm_ir_read_i64(const uint8_t *p);

#endif /* ODM_IR_INTERNAL_H */

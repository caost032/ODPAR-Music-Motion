#ifndef ODM_MASTER_INTERNAL_H
#define ODM_MASTER_INTERNAL_H
#include "odm_master.h"
odm_status odm_master_render_id_internal(const odm_master_plan *plan,
                                         odm_sha256_digest *out_render_id);

odm_status odm_render_receipt_write_internal(const odm_render_receipt_info *info,
                                             uint8_t *record, uint64_t capacity,
                                             uint64_t *out_required,
                                             odm_sha256_digest *out_sha);
#endif

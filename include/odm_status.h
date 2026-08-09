#ifndef ODM_STATUS_H
#define ODM_STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public status values are fixed-width integers, not native C enums. */
typedef int32_t odm_status;

#define ODM_STATUS_OK                 ((odm_status)0)
#define ODM_STATUS_INVALID_ARGUMENT   ((odm_status)1)
#define ODM_STATUS_UNSUPPORTED        ((odm_status)2)
#define ODM_STATUS_OVERFLOW           ((odm_status)3)
#define ODM_STATUS_OUT_OF_MEMORY      ((odm_status)4)
#define ODM_STATUS_BUDGET_EXCEEDED    ((odm_status)5)
#define ODM_STATUS_CANCELLED          ((odm_status)6)
#define ODM_STATUS_BUSY               ((odm_status)7)
#define ODM_STATUS_INVALID_STATE      ((odm_status)8)
#define ODM_STATUS_BUFFER_TOO_SMALL   ((odm_status)9)
#define ODM_STATUS_INVARIANT_BROKEN   ((odm_status)10)
#define ODM_STATUS_INTERNAL_ERROR     ((odm_status)11)
#define ODM_STATUS_INVALID_DATA       ((odm_status)12)
#define ODM_STATUS_INTEGRITY_ERROR    ((odm_status)13)
#define ODM_STATUS_VERSION_MISMATCH   ((odm_status)14)

const char *odm_status_name(odm_status status);
int odm_status_is_ok(odm_status status);

#ifdef __cplusplus
}
#endif

#endif /* ODM_STATUS_H */

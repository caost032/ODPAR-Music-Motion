#ifndef ODM_SELFTEST_H
#define ODM_SELFTEST_H

#include "odm_status.h"

#include <stdint.h>

typedef struct {
    uint32_t checks;
    uint32_t passed;
    const char *failed_check;
    odm_status failure_status;
} odm_selftest_result;

odm_status odm_selftest_run(odm_selftest_result *out_result);

#endif /* ODM_SELFTEST_H */

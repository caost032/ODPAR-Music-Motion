#include "test_harness.h"

#include "odm_fixed.h"

#include <limits.h>

void odm_test_fixed(odm_test_context *context) {
    int64_t integer = 0;
    odm_q1_31 q31 = 0;
    odm_q32_32 q32 = 0;
    odm_q32_32 other = 0;
    odm_microunit micro = 0;
    odm_phase_u32 phase = 0u;

    ODM_TEST_CHECK(context,
                   odm_i64_add_checked(INT64_MAX, 1, &integer) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_i64_add_checked(INT64_MIN, -1, &integer) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_i64_add_checked(INT64_MIN, INT64_MAX, &integer) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, integer == -1);
    ODM_TEST_CHECK(context,
                   odm_i64_sub_checked(INT64_MIN, 1, &integer) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_i64_sub_checked(INT64_MAX, -1, &integer) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_i64_mul_checked(INT64_MIN, -1, &integer) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_i64_mul_checked(INT64_MIN, 1, &integer) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, integer == INT64_MIN);
    ODM_TEST_CHECK(context,
                   odm_i64_mul_checked(INT64_C(3037000499), INT64_C(3037000499),
                                       &integer) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   integer == INT64_C(9223372030926249001));
    ODM_TEST_CHECK(context,
                   odm_i64_mul_checked(INT64_C(3037000500), INT64_C(3037000500),
                                       &integer) == ODM_STATUS_OVERFLOW);

    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(1, 2, &q31) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == INT32_C(1073741824));
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(-1, 1, &q31) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == INT32_MIN);
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(1, 1, &q31) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == INT32_MAX);
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(2, 1, &q31) ==
                       ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(1, 0, &q31) ==
                       ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(context,
                   odm_q1_31_mul(INT32_C(1073741824), INT32_C(1073741824),
                                 &q31) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == INT32_C(536870912));
    ODM_TEST_CHECK(context,
                   odm_q1_31_mul(1, INT32_C(1073741824), &q31) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == 1);
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(1, INT64_C(4294967296), &q31) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == 1);
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(-1, INT64_C(4294967296), &q31) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == -1);
    ODM_TEST_CHECK(context,
                   odm_q1_31_from_ratio(1, INT64_C(8589934592), &q31) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == 0);
    ODM_TEST_CHECK(context,
                   odm_q1_31_mul(-1, INT32_C(1073741824), &q31) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q31 == -1);

    ODM_TEST_CHECK(context,
                   odm_q32_32_from_microunits(INT64_C(1000000), &q32) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q32 == ODM_Q32_ONE);
    ODM_TEST_CHECK(context,
                   odm_q32_32_from_microunits(INT64_C(-1500000), &other) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, other == -INT64_C(6442450944));
    ODM_TEST_CHECK(context,
                   odm_q32_32_to_microunits(other, &micro) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, micro == -1500000);
    ODM_TEST_CHECK(context,
                   odm_q32_32_from_microunits(INT64_MAX, &other) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_q32_32_add(INT64_MAX, 1, &other) == ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_q32_32_sub(INT64_MIN, 1, &other) == ODM_STATUS_OVERFLOW);

    ODM_TEST_CHECK(context,
                   odm_q32_32_from_microunits(INT64_C(1500000), &q32) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_q32_32_from_microunits(INT64_C(2000000), &other) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_q32_32_mul(q32, other, &q32) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q32 == INT64_C(12884901888));
    ODM_TEST_CHECK(context,
                   odm_q32_32_mul(INT64_MAX, ODM_Q32_ONE, &q32) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q32 == INT64_MAX);
    ODM_TEST_CHECK(context,
                   odm_q32_32_mul(INT64_MAX, ODM_Q32_ONE * 2, &q32) ==
                       ODM_STATUS_OVERFLOW);
    ODM_TEST_CHECK(context,
                   odm_q32_32_mul(1, INT64_C(0x80000000), &q32) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q32 == 1);
    ODM_TEST_CHECK(context,
                   odm_q32_32_mul(-1, INT64_C(0x80000000), &q32) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, q32 == -1);

    ODM_TEST_CHECK(context,
                   odm_phase_from_microturns(0, &phase) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, phase == 0u);
    ODM_TEST_CHECK(context,
                   odm_phase_from_microturns(INT64_C(500000), &phase) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, phase == UINT32_C(0x80000000));
    ODM_TEST_CHECK(context,
                   odm_phase_from_microturns(INT64_C(-500000), &phase) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, phase == UINT32_C(0x80000000));
    ODM_TEST_CHECK(context,
                   odm_phase_from_microturns(INT64_C(1000000), &phase) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context, phase == 0u);
    ODM_TEST_CHECK(context,
                   odm_phase_add(UINT32_MAX, 1u) == 0u);
}

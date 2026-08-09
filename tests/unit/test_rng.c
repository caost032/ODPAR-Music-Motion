#include "test_harness.h"

#include "odm_rng.h"

#include <stddef.h>

void odm_test_rng(odm_test_context *context) {
    static const uint32_t expected[] = {
        UINT32_C(0xa15c02b7), UINT32_C(0x7b47f409),
        UINT32_C(0xba1d3330), UINT32_C(0x83d2f293),
        UINT32_C(0xbfa4784b), UINT32_C(0xcbed606e)
    };
    odm_rng first;
    odm_rng second;
    size_t index;
    ODM_TEST_CHECK(context,
                   odm_rng_seed(&first, UINT64_C(42), UINT64_C(54)) ==
                       ODM_STATUS_OK);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        uint32_t value = 0u;
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&first, &value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, value == expected[index]);
    }
    ODM_TEST_CHECK(context,
                   odm_rng_seed(&first, UINT64_C(1), UINT64_C(2)) ==
                       ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_rng_seed(&second, UINT64_C(1), UINT64_C(2)) ==
                       ODM_STATUS_OK);
    for (index = 0u; index < 1000u; index++) {
        uint32_t first_value = 0u;
        uint32_t second_value = 0u;
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&first, &first_value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&second, &second_value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, first_value == second_value);
    }
    ODM_TEST_CHECK(context,
                   odm_rng_seed_derived(&first, UINT64_C(7), UINT64_C(8),
                                        UINT64_C(9)) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_rng_seed_derived(&second, UINT64_C(7), UINT64_C(8),
                                        UINT64_C(9)) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, first.state == second.state);
    ODM_TEST_CHECK(context, first.stream == second.stream);
    ODM_TEST_CHECK(context,
                   odm_rng_derive_seed(UINT64_C(7), UINT64_C(8), UINT64_C(9)) !=
                       odm_rng_derive_seed(UINT64_C(7), UINT64_C(8), UINT64_C(10)));
    {
        uint64_t value = 0u;
        ODM_TEST_CHECK(context,
                       odm_rng_seed(&first, UINT64_C(42), UINT64_C(54)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_rng_next_u64(&first, &value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       value == UINT64_C(0xa15c02b77b47f409));
    }
    for (index = 0u; index < 10000u; index++) {
        uint32_t value = UINT32_MAX;
        ODM_TEST_CHECK(context,
                       odm_rng_bounded_u32(&first, 37u, &value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, value < 37u);
    }
    {
        uint32_t value = 99u;
        odm_rng unseeded = {0};
        ODM_TEST_CHECK(context,
                       odm_rng_bounded_u32(&first, 1u, &value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, value == 0u);
        ODM_TEST_CHECK(context,
                       odm_rng_bounded_u32(&first, 0u, &value) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_rng_bounded_u32(NULL, 3u, &value) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&unseeded, &value) ==
                           ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context, value == 0u);
        ODM_TEST_CHECK(context,
                       odm_rng_bounded_u32(&unseeded, 3u, &value) ==
                           ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&first, NULL) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_rng_seed(NULL, 1u, 2u) ==
                           ODM_STATUS_INVALID_ARGUMENT);
    }
}

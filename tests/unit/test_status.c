#include "test_harness.h"

#include "odm_status.h"
#include "odm_version.h"

#include <string.h>

void odm_test_status(odm_test_context *context) {
    odm_status status;
    for (status = ODM_STATUS_OK; status <= ODM_STATUS_VERSION_MISMATCH; status++) {
        ODM_TEST_CHECK(context, strcmp(odm_status_name(status), "unknown_status") != 0);
    }
    ODM_TEST_CHECK(context, odm_status_is_ok(ODM_STATUS_OK));
    ODM_TEST_CHECK(context, !odm_status_is_ok(ODM_STATUS_CANCELLED));
    ODM_TEST_CHECK(context,
                   strcmp(odm_status_name((odm_status)-1), "unknown_status") == 0);
    ODM_TEST_CHECK(context,
                   strcmp(odm_status_name((odm_status)999), "unknown_status") == 0);
    ODM_TEST_CHECK(context, ODM_VERSION_MAJOR == 0u);
    ODM_TEST_CHECK(context, ODM_VERSION_MINOR == 33u);
    ODM_TEST_CHECK(context, ODM_VERSION_PATCH == 0u);
    ODM_TEST_CHECK(context, strcmp(ODM_VERSION_LABEL, "radial-morphology-v2-visual-dynamics-v1-wip-local") == 0);
    ODM_TEST_CHECK(context, strcmp(odm_version_string(), ODM_VERSION_STRING) == 0);
    ODM_TEST_CHECK(context, strcmp(ODM_VERSION_STRING, "0.33.0-radial-morphology-v2-visual-dynamics-v1-wip-local") == 0);
}

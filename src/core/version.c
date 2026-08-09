#include "odm_version.h"
#include "odm_build_meta.h"

const char *odm_version_string(void) {
    return ODM_VERSION_STRING;
}

const char *odm_source_id(void) {
    return ODM_BUILD_SOURCE_ID;
}

const char *odm_compiler_id(void) {
    return ODM_BUILD_COMPILER_ID;
}

uint32_t odm_abi_version(void) {
    return ODM_ABI_VERSION;
}

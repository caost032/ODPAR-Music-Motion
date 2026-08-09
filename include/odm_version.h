#ifndef ODM_VERSION_H
#define ODM_VERSION_H

#include <stdint.h>

#define ODM_VERSION_MAJOR 0u
#define ODM_VERSION_MINOR 33u
#define ODM_VERSION_PATCH 0u
#define ODM_VERSION_LABEL "radial-morphology-v2-visual-dynamics-v1-wip-local"
#define ODM_VERSION_STRING "0.33.0-radial-morphology-v2-visual-dynamics-v1-wip-local"
#define ODM_ABI_VERSION   6u
#define ODM_SPINE_SCHEMA_VERSION 1u

const char *odm_version_string(void);
const char *odm_source_id(void);
const char *odm_compiler_id(void);
uint32_t odm_abi_version(void);

#endif /* ODM_VERSION_H */

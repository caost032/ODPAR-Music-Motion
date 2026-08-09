#ifndef ODM_SPINE_H
#define ODM_SPINE_H

#include "odm_status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_SPINE_MAX_MODULES 256u
#define ODM_SPINE_IMPACT_TEXT_CAPACITY 8192u

typedef struct {
    uint32_t id;
    const char *name;
    const char *path;
    uint32_t layer;
    uint32_t source_lines;
    const char *role;
} odm_spine_module;

typedef struct {
    uint32_t module;
    uint32_t dependency;
} odm_spine_dependency;

typedef struct {
    const char *id;
    uint32_t module;
    const char *guarantee;
    const char *mechanism;
    const char *evidence;
} odm_spine_invariant;

typedef struct {
    const char *resource;
    const char *acquire;
    const char *release;
    const char *transfer;
} odm_spine_ownership;

typedef struct {
    const char *id;
    const char *path;
    const char *scope;
} odm_spine_guardian;

typedef struct {
    const char *id;
    uint32_t version_major;
    uint32_t version_minor;
    const char *state;
    const char *evidence;
} odm_spine_capability;

typedef struct {
    const char *capability;
    const char *model;
    const char *evidence;
} odm_spine_state_model;

typedef struct {
    const char *id;
    const char *state;
    const char *evidence;
} odm_spine_backend;

typedef struct {
    const char *id;
    const char *left_semantics;
    const char *right_semantics;
    const char *state;
    const char *evidence;
} odm_spine_parity;

typedef struct {
    const char *id;
    const char *severity;
    const char *state;
    const char *evidence;
} odm_spine_bug;

typedef struct {
    const char *id;
    const char *topic;
    const char *state;
    const char *evidence;
} odm_spine_investigation;

typedef struct {
    uint32_t module_count;
    uint32_t dependency_count;
    uint32_t invariant_count;
    uint32_t ownership_count;
    uint32_t guardian_count;
    uint32_t capability_count;
    uint32_t state_model_count;
    uint32_t renderer_backend_count;
    uint32_t media_adapter_count;
    uint32_t output_profile_count;
    uint32_t parity_contract_count;
    uint32_t known_bug_count;
    uint32_t security_investigation_count;
    uint32_t source_file_count;
    uint32_t source_line_count;
    uint32_t graph_acyclic;
} odm_spine_summary;

typedef struct {
    uint32_t module_id;
    uint32_t direct_dependents;
    uint32_t transitive_dependents;
    char module[64];
    char dependents[ODM_SPINE_IMPACT_TEXT_CAPACITY];
    char invariants_at_stake[ODM_SPINE_IMPACT_TEXT_CAPACITY];
} odm_spine_impact;

const odm_spine_module *odm_spine_modules(size_t *out_count);
const odm_spine_dependency *odm_spine_dependencies(size_t *out_count);
const odm_spine_invariant *odm_spine_invariants(size_t *out_count);
const odm_spine_ownership *odm_spine_ownerships(size_t *out_count);
const odm_spine_guardian *odm_spine_guardians(size_t *out_count);
const odm_spine_capability *odm_spine_capabilities(size_t *out_count);
const odm_spine_state_model *odm_spine_state_models(size_t *out_count);
const odm_spine_backend *odm_spine_renderer_backends(size_t *out_count);
const odm_spine_backend *odm_spine_media_adapters(size_t *out_count);
const odm_spine_backend *odm_spine_output_profiles(size_t *out_count);
const odm_spine_parity *odm_spine_parity_contracts(size_t *out_count);
const odm_spine_bug *odm_spine_known_bugs(size_t *out_count);
const odm_spine_investigation *odm_spine_security_investigations(
    size_t *out_count);

odm_status odm_spine_validate(odm_spine_summary *out_summary);
odm_status odm_spine_impact_of(const char *module_name,
                               odm_spine_impact *out_impact);
odm_status odm_spine_report_json(char *buffer, size_t capacity,
                                 size_t *out_required);
odm_status odm_spine_summary_json(char *buffer, size_t capacity,
                                  size_t *out_required);
odm_status odm_spine_impact_json(const odm_spine_impact *impact,
                                 char *buffer, size_t capacity,
                                 size_t *out_required);

#ifdef __cplusplus
}
#endif

#endif /* ODM_SPINE_H */

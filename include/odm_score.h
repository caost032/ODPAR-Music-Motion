#ifndef ODM_SCORE_H
#define ODM_SCORE_H

#include "odm_fixed.h"
#include "odm_hash.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_SCORE_SCHEMA_MAJOR 1u
#define ODM_SCORE_SCHEMA_MINOR 1u

#define ODM_SCORE_MAX_RESOURCES    UINT32_C(65536)
#define ODM_SCORE_MAX_ACTS         UINT32_C(4096)
#define ODM_SCORE_MAX_SCENES       UINT32_C(16384)
#define ODM_SCORE_MAX_NODES        UINT32_C(65536)
#define ODM_SCORE_MAX_CUES         UINT32_C(262144)
#define ODM_SCORE_MAX_MODULATORS   UINT32_C(65536)
#define ODM_SCORE_MAX_AUTOMATION   UINT32_C(262144)
#define ODM_SCORE_MAX_TRANSITIONS  UINT32_C(16384)
#define ODM_SCORE_MAX_TRANSFERS    UINT32_C(65536)

#define ODM_SCORE_RESOURCE_IMAGE   UINT32_C(1)
#define ODM_SCORE_RESOURCE_VIDEO   UINT32_C(2)

#define ODM_SCORE_NODE_ENVIRONMENT UINT32_C(1)
#define ODM_SCORE_NODE_PRIMARY     UINT32_C(2)
#define ODM_SCORE_NODE_SUBJECT     UINT32_C(3)
#define ODM_SCORE_NODE_LOOK        UINT32_C(4)
#define ODM_SCORE_NODE_EFFECT      UINT32_C(5)
#define ODM_SCORE_NODE_PARTICLE    UINT32_C(6)

#define ODM_STATELESS              UINT32_C(0)
#define ODM_EVENT_HISTORY          UINT32_C(1)
#define ODM_ACCUMULATOR            UINT32_C(2)
#define ODM_SIMULATION             UINT32_C(3)

/* Gate-4 structural capabilities. These IDs are canonical Render-IR inputs. */
#define ODM_CAP_BLACK_VOID         UINT32_C(0x00010001)
#define ODM_CAP_PRIMARY_EMPTY      UINT32_C(0x00010002)
#define ODM_CAP_PRIMARY_IMAGE      UINT32_C(0x00010003)
#define ODM_CAP_SUBJECT_IMAGE      UINT32_C(0x00010004)
#define ODM_CAP_LOOK_IDENTITY      UINT32_C(0x00010005)
#define ODM_CAP_PRIMARY_VIDEO      UINT32_C(0x00010006)
#define ODM_CAP_SUBJECT_VIDEO      UINT32_C(0x00010007)
#define ODM_CAP_AUTOMATION         UINT32_C(0x00020001)
#define ODM_CAP_MUSIC_MODULATOR    UINT32_C(0x00020002)
#define ODM_CAP_CROSSFADE          UINT32_C(0x00020003)
#define ODM_CAP_STATE_TRANSFER     UINT32_C(0x00020004)
#define ODM_CAP_TIMELINE_CUE       UINT32_C(0x00020005)

/* Gate-7 deterministic visual systems. Keep capability IDs globally sorted. */
#define ODM_CAP_GRID_PROOF          UINT32_C(0x00030001)
#define ODM_CAP_HALO                UINT32_C(0x00030002)
#define ODM_CAP_RESIDUAL_EVENT_FIELD UINT32_C(0x00030003)
#define ODM_CAP_DETERMINISTIC_PARTICLES UINT32_C(0x00030004)

#define ODM_ENTRY_NONE              UINT32_C(0)
#define ODM_ENTRY_FADE_SCALE        UINT32_C(1)
#define ODM_EXIT_NONE               UINT32_C(0)
#define ODM_EXIT_FADE_SCALE         UINT32_C(1)
#define ODM_SCORE_MAX_ENVELOPE_SAMPLES UINT32_MAX

#define ODM_PARAM_X                UINT32_C(1)
#define ODM_PARAM_Y                UINT32_C(2)
#define ODM_PARAM_SCALE_X          UINT32_C(3)
#define ODM_PARAM_SCALE_Y          UINT32_C(4)
#define ODM_PARAM_OPACITY          UINT32_C(5)
#define ODM_PARAM_PHASE            UINT32_C(6)

#define ODM_AUTOMATION_STEP        UINT32_C(1)
#define ODM_AUTOMATION_LINEAR      UINT32_C(2)

#define ODM_MOD_RMS_LEFT           UINT32_C(1)
#define ODM_MOD_RMS_RIGHT          UINT32_C(2)
#define ODM_MOD_RMS_MID            UINT32_C(3)
#define ODM_MOD_RMS_SIDE           UINT32_C(4)
#define ODM_MOD_ENVELOPE_SHORT     UINT32_C(5)
#define ODM_MOD_ENVELOPE_MEDIUM    UINT32_C(6)
#define ODM_MOD_ENVELOPE_LONG      UINT32_C(7)
#define ODM_MOD_SPECTRAL_FLUX      UINT32_C(8)
#define ODM_MOD_STEREO_WIDTH       UINT32_C(9)
#define ODM_MOD_STEREO_BALANCE     UINT32_C(10)
#define ODM_MOD_BAND_ENERGY        UINT32_C(11)
#define ODM_MOD_SILENCE            UINT32_C(12)

#define ODM_TRANSITION_CROSSFADE   UINT32_C(1)

#define ODM_TRANSFER_COPY          UINT32_C(1)
#define ODM_TRANSFER_RESET         UINT32_C(2)

#define ODM_SCORE_CUE_GENERIC      UINT32_C(1)
#define ODM_SCORE_CUE_BEAT_AUTHORED UINT32_C(2)
#define ODM_SCORE_CUE_SECTION_AUTHORED UINT32_C(3)

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t flags;
    int32_t fps;
    uint32_t width;
    uint32_t height;
    int64_t project_end_sample;
    uint64_t project_seed;
    uint32_t resource_count;
    uint32_t act_count;
    uint32_t scene_count;
    uint32_t node_count;
    uint32_t cue_count;
    uint32_t modulator_count;
    uint32_t automation_count;
    uint32_t transition_count;
    uint32_t transfer_count;
    uint32_t reserved[3];
} odm_score_header;

typedef struct {
    uint32_t resource_id;
    uint32_t kind;
    uint32_t flags;
    uint32_t reserved;
    odm_sha256_digest content_sha256;
} odm_score_resource;

typedef struct {
    uint32_t act_id;
    uint32_t flags;
    int64_t start_sample;
    int64_t end_sample;
    uint32_t reserved[4];
} odm_score_act;

typedef struct {
    uint32_t scene_id;
    uint32_t act_id;
    uint32_t environment_node_id;
    uint32_t primary_node_id;
    int64_t start_sample;
    int64_t end_sample;
    uint32_t flags;
    uint32_t reserved;
} odm_score_scene;

typedef struct {
    uint32_t node_id;
    uint32_t scene_id;
    uint32_t role;
    uint32_t capability_id;
    uint32_t capability_major;
    uint32_t capability_minor;
    uint32_t resource_id;
    uint32_t state_model;
    int32_t z_index;
    uint32_t flags;
    int64_t start_sample;
    int64_t end_sample;
    odm_microunit x_micro;
    odm_microunit y_micro;
    odm_microunit scale_x_micro;
    odm_microunit scale_y_micro;
    odm_microunit opacity_micro;
    odm_microunit phase_microturns;
    uint64_t seed_domain;
    uint32_t state_slot;
    uint32_t entry_kind;
    uint32_t exit_kind;
    uint32_t entry_duration_samples;
    uint32_t exit_duration_samples;
    uint32_t reserved[2];
} odm_score_node;

typedef struct {
    uint32_t cue_id;
    uint32_t scene_id;
    uint32_t kind;
    uint32_t flags;
    int64_t sample;
    uint32_t reserved[2];
} odm_score_cue;

typedef struct {
    uint32_t modulator_id;
    uint32_t node_id;
    uint32_t target_parameter;
    uint32_t source;
    uint32_t band_index;
    uint32_t flags;
    odm_microunit scale_micro;
    odm_microunit bias_micro;
    uint32_t reserved[4];
} odm_score_modulator;

typedef struct {
    uint32_t automation_id;
    uint32_t node_id;
    uint32_t target_parameter;
    uint32_t interpolation;
    uint32_t flags;
    uint32_t reserved0;
    int64_t start_sample;
    int64_t end_sample;
    odm_microunit from_micro;
    odm_microunit to_micro;
    uint32_t reserved[2];
} odm_score_automation;

typedef struct {
    uint32_t transition_id;
    uint32_t from_scene_id;
    uint32_t to_scene_id;
    uint32_t kind;
    int64_t start_sample;
    int64_t end_sample;
    uint32_t flags;
    uint32_t reserved[3];
} odm_score_transition;

typedef struct {
    uint32_t transfer_id;
    uint32_t from_scene_id;
    uint32_t to_scene_id;
    uint32_t state_slot;
    uint32_t mode;
    uint32_t flags;
    int64_t at_sample;
    uint32_t reserved[4];
} odm_score_state_transfer;

/* Authoring view only. Pointers are never serialized into canonical Score/IR. */
typedef struct {
    const odm_score_header *header;
    const odm_score_resource *resources;
    const odm_score_act *acts;
    const odm_score_scene *scenes;
    const odm_score_node *nodes;
    const odm_score_cue *cues;
    const odm_score_modulator *modulators;
    const odm_score_automation *automation;
    const odm_score_transition *transitions;
    const odm_score_state_transfer *transfers;
} odm_score_view;

typedef struct {
    uint32_t capability_id;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t state_model;
    uint32_t allowed_roles_mask;
    uint32_t resource_kind;
} odm_visual_capability_info;

odm_status odm_score_validate(const odm_score_view *score);
odm_status odm_score_hash(const odm_score_view *score,
                          odm_sha256_digest *out_score_sha256);
odm_status odm_score_capability_hash(const odm_score_view *score,
                                     odm_sha256_digest *out_capability_sha256);
const odm_visual_capability_info *odm_visual_capabilities(uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* ODM_SCORE_H */

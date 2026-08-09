#ifndef ODM_RNG_H
#define ODM_RNG_H

#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_RNG_ALGORITHM "pcg32-xsh-rr-v1"

typedef struct {
    uint64_t state;
    uint64_t stream;
    uint64_t cookie;
} odm_rng;

odm_status odm_rng_seed(odm_rng *rng, uint64_t seed, uint64_t sequence);
odm_status odm_rng_seed_derived(odm_rng *rng, uint64_t root_seed,
                                uint64_t domain_id, uint64_t entity_id);
odm_status odm_rng_next_u32(odm_rng *rng, uint32_t *out_value);
odm_status odm_rng_next_u64(odm_rng *rng, uint64_t *out_value);
odm_status odm_rng_bounded_u32(odm_rng *rng, uint32_t bound,
                               uint32_t *out_value);
uint64_t odm_rng_derive_seed(uint64_t root_seed, uint64_t domain_id,
                             uint64_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* ODM_RNG_H */

#include "odm_rng.h"

#include <stddef.h>

#define ODM_RNG_COOKIE UINT64_C(0x4f444d524e473031)

static uint64_t odm_splitmix64(uint64_t value) {
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static uint32_t odm_rng_step(odm_rng *rng) {
    uint64_t old_state;
    uint32_t mixed;
    uint32_t rotation;
    old_state = rng->state;
    rng->state = old_state * UINT64_C(6364136223846793005) + rng->stream;
    mixed = (uint32_t)(((old_state >> 18u) ^ old_state) >> 27u);
    rotation = (uint32_t)(old_state >> 59u);
    return (mixed >> rotation) | (mixed << ((0u - rotation) & 31u));
}

static int odm_rng_valid(const odm_rng *rng) {
    return rng != NULL && rng->cookie == ODM_RNG_COOKIE &&
           (rng->stream & UINT64_C(1)) != 0u;
}

odm_status odm_rng_seed(odm_rng *rng, uint64_t seed, uint64_t sequence) {
    if (!rng) return ODM_STATUS_INVALID_ARGUMENT;
    rng->state = 0u;
    rng->stream = (sequence << 1u) | UINT64_C(1);
    rng->cookie = ODM_RNG_COOKIE;
    (void)odm_rng_step(rng);
    rng->state += seed;
    (void)odm_rng_step(rng);
    return ODM_STATUS_OK;
}

uint64_t odm_rng_derive_seed(uint64_t root_seed, uint64_t domain_id,
                             uint64_t entity_id) {
    uint64_t domain = odm_splitmix64(domain_id + UINT64_C(0x9e3779b97f4a7c15));
    uint64_t entity = odm_splitmix64(entity_id + UINT64_C(0xd1b54a32d192ed03));
    return odm_splitmix64(root_seed ^ domain ^ entity);
}

odm_status odm_rng_seed_derived(odm_rng *rng, uint64_t root_seed,
                                uint64_t domain_id, uint64_t entity_id) {
    uint64_t seed = odm_rng_derive_seed(root_seed, domain_id, entity_id);
    uint64_t sequence = odm_splitmix64(domain_id ^ (entity_id << 1u));
    return odm_rng_seed(rng, seed, sequence);
}

odm_status odm_rng_next_u32(odm_rng *rng, uint32_t *out_value) {
    if (!rng || !out_value) return ODM_STATUS_INVALID_ARGUMENT;
    *out_value = 0u;
    if (!odm_rng_valid(rng)) return ODM_STATUS_INVALID_STATE;
    *out_value = odm_rng_step(rng);
    return ODM_STATUS_OK;
}

odm_status odm_rng_next_u64(odm_rng *rng, uint64_t *out_value) {
    uint32_t high;
    uint32_t low;
    odm_status status;
    if (!rng || !out_value) return ODM_STATUS_INVALID_ARGUMENT;
    *out_value = 0u;
    if (!odm_rng_valid(rng)) return ODM_STATUS_INVALID_STATE;
    status = odm_rng_next_u32(rng, &high);
    if (status != ODM_STATUS_OK) return status;
    status = odm_rng_next_u32(rng, &low);
    if (status != ODM_STATUS_OK) return status;
    *out_value = ((uint64_t)high << 32) | (uint64_t)low;
    return ODM_STATUS_OK;
}

odm_status odm_rng_bounded_u32(odm_rng *rng, uint32_t bound,
                               uint32_t *out_value) {
    uint32_t threshold;
    uint32_t value;
    if (!rng || !out_value || bound == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_rng_valid(rng)) return ODM_STATUS_INVALID_STATE;
    threshold = (uint32_t)(0u - bound) % bound;
    do {
        odm_status status = odm_rng_next_u32(rng, &value);
        if (status != ODM_STATUS_OK) return status;
    } while (value < threshold);
    *out_value = value % bound;
    return ODM_STATUS_OK;
}

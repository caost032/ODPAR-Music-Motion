#ifndef ODM_HASH_H
#define ODM_HASH_H

#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_SHA256_DIGEST_BYTES 32u
#define ODM_SHA256_HEX_CHARS 64u
#define ODM_SHA256_HEX_BUFFER_BYTES 65u
#define ODM_SHA256_MAX_MESSAGE_BYTES UINT64_C(2305843009213693951)

typedef struct {
    uint8_t bytes[ODM_SHA256_DIGEST_BYTES];
} odm_sha256_digest;

/* Fixed-width public storage permits stack allocation without exposing an
 * allocator.  Fields are not a serialized representation and callers must not
 * mutate an active context. */
typedef struct {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    uint32_t block_length;
    uint32_t lifecycle;
    uint64_t cookie;
} odm_sha256_context;

#define ODM_SHA256_CONTEXT_INITIALIZER {0}

odm_status odm_sha256_init(odm_sha256_context *context);
odm_status odm_sha256_update(odm_sha256_context *context,
                             const void *data, uint64_t length);
odm_status odm_sha256_final(odm_sha256_context *context,
                            odm_sha256_digest *out_digest);
odm_status odm_sha256(const void *data, uint64_t length,
                      odm_sha256_digest *out_digest);
odm_status odm_sha256_to_hex(const odm_sha256_digest *digest,
                             char *buffer, uint64_t capacity,
                             uint64_t *out_required);
odm_status odm_sha256_from_hex(const char *hex, uint64_t length,
                               odm_sha256_digest *out_digest);
int odm_sha256_equal(const odm_sha256_digest *left,
                     const odm_sha256_digest *right);

#ifdef __cplusplus
}
#endif

#endif /* ODM_HASH_H */

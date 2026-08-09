#ifndef ODM_PACKAGE_INTERNAL_H
#define ODM_PACKAGE_INTERNAL_H

#include "odm_hash.h"
#include "odm_status.h"

#include <stdint.h>

#define ODM_DEFLATE_WINDOW UINT32_C(32768)

odm_status odm_pkg_crc32(const uint8_t *data, uint64_t bytes, uint32_t *out_crc32);
odm_status odm_pkg_deflate_fixed(const uint8_t *data, uint64_t bytes,
                                 uint8_t *out, uint64_t capacity,
                                 uint64_t *out_required);
odm_status odm_pkg_inflate_fixed(const uint8_t *data, uint64_t bytes,
                                 uint8_t *out, uint64_t capacity,
                                 uint64_t expected_bytes,
                                 uint64_t *out_required,
                                 uint32_t *out_crc32,
                                 odm_sha256_digest *out_sha256);

odm_status odm_pkg_ed25519_public_from_seed(const uint8_t seed[32],
                                            uint8_t out_public_key[32]);
odm_status odm_pkg_ed25519_sign(const uint8_t seed[32],
                                const uint8_t *message, uint64_t message_bytes,
                                uint8_t out_signature[64]);
odm_status odm_pkg_ed25519_verify(const uint8_t public_key[32],
                                  const uint8_t *message, uint64_t message_bytes,
                                  const uint8_t signature[64]);

#endif

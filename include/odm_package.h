#ifndef ODM_PACKAGE_H
#define ODM_PACKAGE_H

#include "odm_hash.h"
#include "odm_ir.h"
#include "odm_score.h"
#include "odm_status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_ODPARMS_SCHEMA_MAJOR 1u
#define ODM_ODPARMS_SCHEMA_MINOR 0u
#define ODM_ODPARMS_MANIFEST_HEADER_BYTES UINT32_C(256)
#define ODM_ODPARMS_MANIFEST_ENTRY_BYTES  UINT32_C(160)
#define ODM_ODPARMS_SIGNATURE_BYTES UINT32_C(64)
#define ODM_ODPARMS_PUBLIC_KEY_BYTES UINT32_C(32)
#define ODM_ODPARMS_SIGNING_SEED_BYTES UINT32_C(32)
#define ODM_ODPARMS_MAX_ENTRIES UINT32_C(4096)
#define ODM_ODPARMS_MAX_PATH_BYTES UINT32_C(95)
#define ODM_ODPARMS_MAX_ENTRY_BYTES UINT64_C(536870912)
#define ODM_ODPARMS_MAX_PACKAGE_BYTES UINT64_C(2147483648)
#define ODM_ODPARMS_MAX_TOTAL_UNCOMPRESSED UINT64_C(2147483648)
#define ODM_ODPARMS_MAX_EXPANSION_RATIO UINT32_C(256)

#define ODM_ODPARMS_ENTRY_RENDER_IR UINT32_C(1)
#define ODM_ODPARMS_ENTRY_ANALYSIS  UINT32_C(2)
#define ODM_ODPARMS_ENTRY_MEDIA     UINT32_C(3)
#define ODM_ODPARMS_ENTRY_DATA      UINT32_C(4)

#define ODM_ODPARMS_METHOD_STORE   UINT32_C(0)
#define ODM_ODPARMS_METHOD_DEFLATE UINT32_C(8)

#define ODM_ODPARMS_SIGNATURE_ED25519 UINT32_C(1)

typedef struct {
    uint32_t kind;
    uint32_t resource_id;
    uint32_t flags;
    uint32_t reserved;
    const char *path;
    const uint8_t *data;
    uint64_t data_bytes;
} odm_odparms_asset;

typedef struct {
    const uint8_t *render_ir;
    uint64_t render_ir_bytes;
    const uint8_t *analysis_bin;
    uint64_t analysis_bin_bytes;
} odm_odparms_core_views;

typedef struct {
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint32_t entry_count;
    uint32_t signature_algorithm;
    uint64_t package_bytes;
    uint64_t total_uncompressed_bytes;
    odm_sha256_digest package_sha256;
    odm_sha256_digest manifest_sha256;
    odm_sha256_digest score_sha256;
    odm_sha256_digest capability_sha256;
    odm_sha256_digest music_map_sha256;
    odm_sha256_digest music_policy_sha256;
    odm_sha256_digest render_ir_sha256;
    uint8_t signer_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES];
} odm_odparms_info;

/* Build a deterministic, signed .odparms ZIP32 profile.
 * The authored Score, Render IR and analysis.bin are revalidated before any
 * caller-visible package bytes are published.  Every Score resource must have
 * exactly one MEDIA asset whose bytes match the resource SHA-256.  DATA assets
 * use the canonical fixed-Huffman DEFLATE profile; media and canonical binary
 * artifacts use STORE. */
odm_status odm_odparms_build(
    const odm_score_view *score,
    const uint8_t *render_ir,
    uint64_t render_ir_bytes,
    const uint8_t *analysis_bin,
    uint64_t analysis_bin_bytes,
    const odm_odparms_asset *assets,
    uint32_t asset_count,
    const uint8_t signer_seed[ODM_ODPARMS_SIGNING_SEED_BYTES],
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *out_required,
    odm_odparms_info *out_info);

/* Verify an untrusted .odparms package.  When expected_public_key is non-NULL,
 * the embedded signer key must match it exactly before the Ed25519 signature is
 * accepted.  A NULL expected key proves package self-consistency but is not a
 * trust decision. */
odm_status odm_odparms_verify(
    const uint8_t *package_bytes,
    uint64_t package_size,
    const uint8_t expected_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    odm_odparms_info *out_info);

/* Verify the complete package once and expose immutable views into the two
 * canonical STORE payloads used by master execution. The returned pointers
 * borrow package_bytes and are valid only while that caller-owned buffer stays
 * unchanged and alive. No view is published on verification failure. */
/* Compute the canonical signer-independent identity of the verified package
 * contents.  It binds all manifest semantic hashes and every listed entry
 * (kind/method/resource/path/size/content SHA), but deliberately excludes the
 * signer public key, detached signature bytes, ZIP offsets, CRCs and compression
 * representation.  Re-signing identical content therefore preserves this ID. */
odm_status odm_odparms_content_sha256(
    const uint8_t *package_bytes,
    uint64_t package_size,
    const uint8_t expected_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    odm_sha256_digest *out_content_sha256);

odm_status odm_odparms_verified_core_views(
    const uint8_t *package_bytes,
    uint64_t package_size,
    const uint8_t expected_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    odm_odparms_info *out_info,
    odm_odparms_core_views *out_views);

/* Extract one manifest-listed payload entry.  The complete package is verified
 * first; extraction is size-first and never publishes a partial decoded entry. */
odm_status odm_odparms_extract(
    const uint8_t *package_bytes,
    uint64_t package_size,
    const uint8_t expected_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    const char *path,
    uint8_t *buffer,
    uint64_t capacity,
    uint64_t *out_required);

#ifdef __cplusplus
}
#endif

#endif /* ODM_PACKAGE_H */

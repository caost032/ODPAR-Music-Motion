#include "package_internal.h"

#include <openssl/evp.h>
#include <stddef.h>

static odm_status pkey_from_seed(const uint8_t seed[32], EVP_PKEY **out) {
    EVP_PKEY *p;
    if (seed == NULL || out == NULL) return ODM_STATUS_INVALID_ARGUMENT;
    p = EVP_PKEY_new_raw_private_key_ex(NULL, "ED25519", NULL, seed, 32u);
    if (p == NULL) return ODM_STATUS_INTERNAL_ERROR;
    *out = p;
    return ODM_STATUS_OK;
}

odm_status odm_pkg_ed25519_public_from_seed(const uint8_t seed[32],
                                            uint8_t out_public_key[32]) {
    EVP_PKEY *p = NULL;
    size_t n = 32u;
    odm_status st;
    if (out_public_key == NULL) return ODM_STATUS_INVALID_ARGUMENT;
    st = pkey_from_seed(seed, &p); if (st != ODM_STATUS_OK) return st;
    if (EVP_PKEY_get_raw_public_key(p, out_public_key, &n) != 1 || n != 32u) st = ODM_STATUS_INTERNAL_ERROR;
    EVP_PKEY_free(p);
    return st;
}

odm_status odm_pkg_ed25519_sign(const uint8_t seed[32],
                                const uint8_t *message, uint64_t message_bytes,
                                uint8_t out_signature[64]) {
    EVP_PKEY *p = NULL;
    EVP_MD_CTX *ctx = NULL;
    size_t siglen = 64u;
    odm_status st;
    if (seed == NULL || out_signature == NULL || (message == NULL && message_bytes != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    if (message_bytes > (uint64_t)SIZE_MAX) return ODM_STATUS_OVERFLOW;
    st = pkey_from_seed(seed, &p); if (st != ODM_STATUS_OK) return st;
    ctx = EVP_MD_CTX_new(); if (ctx == NULL) { EVP_PKEY_free(p); return ODM_STATUS_OUT_OF_MEMORY; }
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, p) != 1 ||
        EVP_DigestSign(ctx, out_signature, &siglen, message, (size_t)message_bytes) != 1 || siglen != 64u) st = ODM_STATUS_INTERNAL_ERROR;
    EVP_MD_CTX_free(ctx); EVP_PKEY_free(p);
    return st;
}

odm_status odm_pkg_ed25519_verify(const uint8_t public_key[32],
                                  const uint8_t *message, uint64_t message_bytes,
                                  const uint8_t signature[64]) {
    EVP_PKEY *p;
    EVP_MD_CTX *ctx;
    int ok;
    if (public_key == NULL || signature == NULL || (message == NULL && message_bytes != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    if (message_bytes > (uint64_t)SIZE_MAX) return ODM_STATUS_OVERFLOW;
    p = EVP_PKEY_new_raw_public_key_ex(NULL, "ED25519", NULL, public_key, 32u);
    if (p == NULL) return ODM_STATUS_INTERNAL_ERROR;
    ctx = EVP_MD_CTX_new(); if (ctx == NULL) { EVP_PKEY_free(p); return ODM_STATUS_OUT_OF_MEMORY; }
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, p) != 1) { EVP_MD_CTX_free(ctx); EVP_PKEY_free(p); return ODM_STATUS_INTERNAL_ERROR; }
    ok = EVP_DigestVerify(ctx, signature, 64u, message, (size_t)message_bytes);
    EVP_MD_CTX_free(ctx); EVP_PKEY_free(p);
    if (ok == 1) return ODM_STATUS_OK;
    if (ok == 0) return ODM_STATUS_INTEGRITY_ERROR;
    return ODM_STATUS_INTERNAL_ERROR;
}

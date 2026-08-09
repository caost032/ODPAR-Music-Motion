#include "odm_hash.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if defined(ODM_SHA256_OPENSSL_ACCEL) && !defined(ODM_SHA256_DISABLE_ACCEL)
#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>
#if defined(__GNUC__) || defined(__clang__)
extern int SHA256_Update(SHA256_CTX *context, const void *data, size_t length)
    __attribute__((weak));
#endif
#endif

#define ODM_SHA256_COOKIE UINT64_C(0x4f444d5348413236)
#define ODM_SHA256_LIFECYCLE_ACTIVE UINT32_C(1)
#define ODM_SHA256_LIFECYCLE_FINALIZED UINT32_C(2)

static const uint32_t odm_sha256_round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static uint32_t odm_sha256_rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static uint32_t odm_sha256_load_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void odm_sha256_store_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void odm_sha256_transform_scalar(odm_sha256_context *context,
                                        const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned index;
    for (index = 0u; index < 16u; index++) {
        words[index] = odm_sha256_load_be32(block + (size_t)index * 4u);
    }
    for (index = 16u; index < 64u; index++) {
        uint32_t x = words[index - 15u];
        uint32_t y = words[index - 2u];
        uint32_t sigma0 = odm_sha256_rotate_right(x, 7u) ^
                          odm_sha256_rotate_right(x, 18u) ^ (x >> 3);
        uint32_t sigma1 = odm_sha256_rotate_right(y, 17u) ^
                          odm_sha256_rotate_right(y, 19u) ^ (y >> 10);
        words[index] = words[index - 16u] + sigma0 +
                       words[index - 7u] + sigma1;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0u; index < 64u; index++) {
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t sum0 = odm_sha256_rotate_right(a, 2u) ^
                        odm_sha256_rotate_right(a, 13u) ^
                        odm_sha256_rotate_right(a, 22u);
        uint32_t sum1 = odm_sha256_rotate_right(e, 6u) ^
                        odm_sha256_rotate_right(e, 11u) ^
                        odm_sha256_rotate_right(e, 25u);
        uint32_t first = h + sum1 + choice +
                         odm_sha256_round_constants[index] + words[index];
        uint32_t second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

#if defined(ODM_SHA256_OPENSSL_ACCEL) && !defined(ODM_SHA256_DISABLE_ACCEL)
static int odm_sha256_transform_blocks_accel(odm_sha256_context *context,
                                             const uint8_t *blocks,
                                             size_t block_count) {
    SHA256_CTX accelerated;
    size_t index;
    size_t bytes;
    if (!context || (!blocks && block_count != 0u)) return 0;
#if defined(__GNUC__) || defined(__clang__)
    if (SHA256_Update == NULL) return 0;
#endif
    if (block_count == 0u) return 1;
    if (block_count > SIZE_MAX / 64u) return 0;
    bytes = block_count * 64u;
    memset(&accelerated, 0, sizeof(accelerated));
    for (index = 0u; index < 8u; ++index) accelerated.h[index] = context->state[index];
    accelerated.md_len = SHA256_DIGEST_LENGTH;
    if (SHA256_Update(&accelerated, blocks, bytes) != 1) {
        volatile uint8_t *wipe = (volatile uint8_t *)&accelerated;
        size_t wipe_bytes = sizeof(accelerated);
        while (wipe_bytes != 0u) { *wipe++ = 0u; --wipe_bytes; }
        return 0;
    }
    for (index = 0u; index < 8u; ++index) context->state[index] = accelerated.h[index];
    {
        volatile uint8_t *wipe = (volatile uint8_t *)&accelerated;
        size_t wipe_bytes = sizeof(accelerated);
        while (wipe_bytes != 0u) { *wipe++ = 0u; --wipe_bytes; }
    }
    return 1;
}
#endif

static void odm_sha256_transform_blocks(odm_sha256_context *context,
                                        const uint8_t *blocks,
                                        size_t block_count) {
    size_t index;
#if defined(ODM_SHA256_OPENSSL_ACCEL) && !defined(ODM_SHA256_DISABLE_ACCEL)
    if (odm_sha256_transform_blocks_accel(context, blocks, block_count)) return;
#endif
    for (index = 0u; index < block_count; ++index) {
        odm_sha256_transform_scalar(context, blocks + index * 64u);
    }
}

static void odm_sha256_transform(odm_sha256_context *context,
                                 const uint8_t block[64]) {
    odm_sha256_transform_blocks(context, block, 1u);
}

static void odm_sha256_secure_clear(void *memory, size_t bytes) {
    volatile uint8_t *cursor = (volatile uint8_t *)memory;
    while (bytes != 0u) {
        *cursor++ = 0u;
        bytes--;
    }
}

static int odm_sha256_context_valid(const odm_sha256_context *context) {
    return context != NULL && context->cookie == ODM_SHA256_COOKIE &&
           context->lifecycle == ODM_SHA256_LIFECYCLE_ACTIVE &&
           context->block_length < 64u &&
           (context->total_bytes & UINT64_C(63)) ==
               (uint64_t)context->block_length &&
           context->total_bytes <= ODM_SHA256_MAX_MESSAGE_BYTES;
}

odm_status odm_sha256_init(odm_sha256_context *context) {
    if (!context) return ODM_STATUS_INVALID_ARGUMENT;
    memset(context, 0, sizeof(*context));
    context->state[0] = UINT32_C(0x6a09e667);
    context->state[1] = UINT32_C(0xbb67ae85);
    context->state[2] = UINT32_C(0x3c6ef372);
    context->state[3] = UINT32_C(0xa54ff53a);
    context->state[4] = UINT32_C(0x510e527f);
    context->state[5] = UINT32_C(0x9b05688c);
    context->state[6] = UINT32_C(0x1f83d9ab);
    context->state[7] = UINT32_C(0x5be0cd19);
    context->lifecycle = ODM_SHA256_LIFECYCLE_ACTIVE;
    context->cookie = ODM_SHA256_COOKIE;
    return ODM_STATUS_OK;
}

odm_status odm_sha256_update(odm_sha256_context *context,
                             const void *data, uint64_t length) {
    const uint8_t *cursor = (const uint8_t *)data;
    size_t remaining;
    if (!odm_sha256_context_valid(context)) return ODM_STATUS_INVALID_STATE;
    if (length != 0u && !data) return ODM_STATUS_INVALID_ARGUMENT;
    if (length > ODM_SHA256_MAX_MESSAGE_BYTES - context->total_bytes) {
        return ODM_STATUS_OVERFLOW;
    }
#if SIZE_MAX < UINT64_MAX
    if (length > (uint64_t)SIZE_MAX) return ODM_STATUS_OVERFLOW;
#endif
    remaining = (size_t)length;
    context->total_bytes += length;

    /* Complete an existing partial block first. */
    if (context->block_length != 0u && remaining != 0u) {
        size_t available = 64u - (size_t)context->block_length;
        size_t take = remaining < available ? remaining : available;
        memcpy(context->block + context->block_length, cursor, take);
        context->block_length += (uint32_t)take;
        cursor += take;
        remaining -= take;
        if (context->block_length == 64u) {
            odm_sha256_transform(context, context->block);
            context->block_length = 0u;
        }
    }

    /* Full blocks do not need to round-trip through context->block.  This is
     * semantically identical SHA-256 and leaves only the final tail buffered. */
    if (context->block_length == 0u && remaining >= 64u) {
        size_t blocks = remaining / 64u;
        size_t bytes = blocks * 64u;
        odm_sha256_transform_blocks(context, cursor, blocks);
        cursor += bytes;
        remaining -= bytes;
    }

    if (remaining != 0u) {
        memcpy(context->block, cursor, remaining);
        context->block_length = (uint32_t)remaining;
    }
    return ODM_STATUS_OK;
}

odm_status odm_sha256_final(odm_sha256_context *context,
                            odm_sha256_digest *out_digest) {
    odm_sha256_context working;
    odm_sha256_digest result;
    uint64_t bit_length;
    unsigned index;
    if (!out_digest) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_sha256_context_valid(context)) return ODM_STATUS_INVALID_STATE;
    working = *context;
    bit_length = working.total_bytes << 3;
    working.block[working.block_length++] = UINT8_C(0x80);
    if (working.block_length > 56u) {
        while (working.block_length < 64u) {
            working.block[working.block_length++] = 0u;
        }
        odm_sha256_transform(&working, working.block);
        working.block_length = 0u;
    }
    while (working.block_length < 56u) {
        working.block[working.block_length++] = 0u;
    }
    for (index = 0u; index < 8u; index++) {
        working.block[63u - index] = (uint8_t)(bit_length >> (index * 8u));
    }
    odm_sha256_transform(&working, working.block);
    for (index = 0u; index < 8u; index++) {
        odm_sha256_store_be32(result.bytes + (size_t)index * 4u,
                              working.state[index]);
    }
    odm_sha256_secure_clear(&working, sizeof(working));
    odm_sha256_secure_clear(context->state, sizeof(context->state));
    odm_sha256_secure_clear(context->block, sizeof(context->block));
    context->total_bytes = 0u;
    context->block_length = 0u;
    context->lifecycle = ODM_SHA256_LIFECYCLE_FINALIZED;
    *out_digest = result;
    return ODM_STATUS_OK;
}

odm_status odm_sha256(const void *data, uint64_t length,
                      odm_sha256_digest *out_digest) {
    odm_sha256_context context = ODM_SHA256_CONTEXT_INITIALIZER;
    odm_sha256_digest result;
    odm_status status;
    if (!out_digest || (length != 0u && !data)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_sha256_init(&context);
    if (status == ODM_STATUS_OK) status = odm_sha256_update(&context, data, length);
    if (status == ODM_STATUS_OK) status = odm_sha256_final(&context, &result);
    if (status == ODM_STATUS_OK) *out_digest = result;
    else odm_sha256_secure_clear(&context, sizeof(context));
    return status;
}

odm_status odm_sha256_to_hex(const odm_sha256_digest *digest,
                             char *buffer, uint64_t capacity,
                             uint64_t *out_required) {
    static const char digits[] = "0123456789abcdef";
    char encoded[ODM_SHA256_HEX_BUFFER_BYTES];
    unsigned index;
    if (!out_required || !digest) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_SHA256_HEX_BUFFER_BYTES;
    if (!buffer || capacity < ODM_SHA256_HEX_BUFFER_BYTES) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    for (index = 0u; index < ODM_SHA256_DIGEST_BYTES; index++) {
        encoded[index * 2u] = digits[digest->bytes[index] >> 4];
        encoded[index * 2u + 1u] = digits[digest->bytes[index] & UINT8_C(0x0f)];
    }
    encoded[ODM_SHA256_HEX_CHARS] = '\0';
    memcpy(buffer, encoded, sizeof(encoded));
    return ODM_STATUS_OK;
}

static int odm_sha256_hex_nibble(char value, uint8_t *out_nibble) {
    if (value >= '0' && value <= '9') {
        *out_nibble = (uint8_t)(value - '0');
        return 1;
    }
    if (value >= 'a' && value <= 'f') {
        *out_nibble = (uint8_t)(value - 'a' + 10);
        return 1;
    }
    return 0;
}

odm_status odm_sha256_from_hex(const char *hex, uint64_t length,
                               odm_sha256_digest *out_digest) {
    odm_sha256_digest result;
    unsigned index;
    if (!hex || !out_digest) return ODM_STATUS_INVALID_ARGUMENT;
    if (length != ODM_SHA256_HEX_CHARS) return ODM_STATUS_INVALID_DATA;
    for (index = 0u; index < ODM_SHA256_DIGEST_BYTES; index++) {
        uint8_t high;
        uint8_t low;
        if (!odm_sha256_hex_nibble(hex[index * 2u], &high) ||
            !odm_sha256_hex_nibble(hex[index * 2u + 1u], &low)) {
            return ODM_STATUS_INVALID_DATA;
        }
        result.bytes[index] = (uint8_t)((uint8_t)(high << 4) | low);
    }
    *out_digest = result;
    return ODM_STATUS_OK;
}

int odm_sha256_equal(const odm_sha256_digest *left,
                     const odm_sha256_digest *right) {
    uint8_t difference = 0u;
    unsigned index;
    if (!left || !right) return 0;
    for (index = 0u; index < ODM_SHA256_DIGEST_BYTES; index++) {
        difference |= (uint8_t)(left->bytes[index] ^ right->bytes[index]);
    }
    return difference == 0u;
}

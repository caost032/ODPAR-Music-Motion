#include "test_harness.h"

#include "odm_hash.h"

#include <limits.h>
#include <string.h>

static int odm_test_digest_hex(const odm_sha256_digest *digest,
                               const char *expected) {
    char actual[ODM_SHA256_HEX_BUFFER_BYTES];
    uint64_t required = 0u;
    return odm_sha256_to_hex(digest, actual, sizeof(actual), &required) ==
               ODM_STATUS_OK &&
           required == sizeof(actual) && strcmp(actual, expected) == 0;
}

static void odm_test_hash_vector(odm_test_context *context,
                                 const void *data, uint64_t length,
                                 const char *expected) {
    odm_sha256_digest digest;
    ODM_TEST_CHECK(context, odm_sha256(data, length, &digest) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_test_digest_hex(&digest, expected));
}

void odm_test_hash(odm_test_context *context) {
    static const char vector_448[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const uint64_t boundary_lengths[] = {
        1u, 55u, 56u, 57u, 63u, 64u, 65u, 127u, 128u, 129u, 257u
    };
    static const char *const boundary_hashes[] = {
        "e7cf46a078fed4fafd0b5e3aff144802b853f8ae459a4f0c14add3314b7cc3a6",
        "2900465fcb533e05a158fd2b3be0e5e3b03740d83060aa3580e0d98a96bf2384",
        "31454ff48ef36af2f08fd511bdc37d9d5855ac23e992e5ff5445cb6b7674a674",
        "bcc0a5d3791b985b7550e04ca660a6c63a589ba1edd2283c8e110e5b515df124",
        "5f6401b96532c36de4e65beec0409b69b1d181864c8009b7a04f43e5d56350d1",
        "94eb5de4943613fd048dc93393ab06877405faa39c11f53e9386083339833e7e",
        "fc518669b6eb4b4dd91827ecacef86689c725bd5bab888fd3b26dbb196eec954",
        "0fe729ff19257bd6fec853acc2ea355f6b34b58e6c0f684c3e188fcdfcd9baae",
        "0aedd4856f8eba0963627336ad5144a9a7dbe12498e6066f0165fc97d8ddee4c",
        "4f1757ae4bffbae86d775b831765b75af154d52f7deaa46dd378051a2d3ad57f",
        "08570fb3dfab53bcb3e8f4ca60b7331d9c603460a45d978226e18dd019468d07"
    };
    uint8_t data[257];
    size_t index;
    ODM_TEST_CHECK(context, ODM_SHA256_DIGEST_BYTES == 32u);
    ODM_TEST_CHECK(context,
                   ODM_SHA256_MAX_MESSAGE_BYTES ==
                       (UINT64_MAX >> 3));
    odm_test_hash_vector(
        context, NULL, 0u,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    odm_test_hash_vector(
        context, "abc", 3u,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    odm_test_hash_vector(
        context, vector_448, (uint64_t)(sizeof(vector_448) - 1u),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    for (index = 0u; index < sizeof(data); index++) {
        data[index] = (uint8_t)(((uint32_t)index * 37u + 11u) & 255u);
    }
    for (index = 0u;
         index < sizeof(boundary_lengths) / sizeof(boundary_lengths[0]);
         index++) {
        odm_test_hash_vector(context, data, boundary_lengths[index],
                             boundary_hashes[index]);
    }
    {
        odm_sha256_context stream = ODM_SHA256_CONTEXT_INITIALIZER;
        odm_sha256_digest digest;
        uint8_t block[1000];
        unsigned repeat;
        memset(block, 'a', sizeof(block));
        ODM_TEST_CHECK(context, odm_sha256_init(&stream) == ODM_STATUS_OK);
        for (repeat = 0u; repeat < 1000u; repeat++) {
            ODM_TEST_CHECK(context,
                           odm_sha256_update(&stream, block, sizeof(block)) ==
                               ODM_STATUS_OK);
        }
        ODM_TEST_CHECK(context,
                       odm_sha256_final(&stream, &digest) == ODM_STATUS_OK);
        ODM_TEST_CHECK(
            context,
            odm_test_digest_hex(
                &digest,
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
        ODM_TEST_CHECK(context,
                       odm_sha256_final(&stream, &digest) ==
                           ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context,
                       odm_sha256_update(&stream, "x", 1u) ==
                           ODM_STATUS_INVALID_STATE);
    }
    {
        odm_sha256_digest one_shot;
        uint64_t chunk;
        ODM_TEST_CHECK(context,
                       odm_sha256(data, sizeof(data), &one_shot) == ODM_STATUS_OK);
        for (chunk = 1u; chunk <= 129u; chunk++) {
            odm_sha256_context stream = ODM_SHA256_CONTEXT_INITIALIZER;
            odm_sha256_digest split;
            uint64_t offset = 0u;
            ODM_TEST_CHECK(context, odm_sha256_init(&stream) == ODM_STATUS_OK);
            while (offset < sizeof(data)) {
                uint64_t remaining = (uint64_t)sizeof(data) - offset;
                uint64_t take = remaining < chunk ? remaining : chunk;
                ODM_TEST_CHECK(context,
                               odm_sha256_update(&stream, data + offset, take) ==
                                   ODM_STATUS_OK);
                offset += take;
            }
            ODM_TEST_CHECK(context,
                           odm_sha256_final(&stream, &split) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_sha256_equal(&one_shot, &split));
        }
    }
    {
        odm_sha256_digest digest;
        odm_sha256_digest decoded;
        odm_sha256_digest sentinel;
        char hex[ODM_SHA256_HEX_BUFFER_BYTES];
        char small[ODM_SHA256_HEX_CHARS];
        uint64_t required = 0u;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        decoded = sentinel;
        memset(small, 0x5a, sizeof(small));
        ODM_TEST_CHECK(context,
                       odm_sha256("abc", 3u, &digest) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_sha256_to_hex(&digest, NULL, 0u, &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == ODM_SHA256_HEX_BUFFER_BYTES);
        ODM_TEST_CHECK(context,
                       odm_sha256_to_hex(&digest, small, sizeof(small),
                                         &required) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, small[0] == (char)0x5a);
        ODM_TEST_CHECK(context,
                       odm_sha256_to_hex(&digest, hex, sizeof(hex), &required) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_sha256_from_hex(hex, ODM_SHA256_HEX_CHARS,
                                           &decoded) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_sha256_equal(&digest, &decoded));
        hex[0] = 'B';
        decoded = sentinel;
        ODM_TEST_CHECK(context,
                       odm_sha256_from_hex(hex, ODM_SHA256_HEX_CHARS,
                                           &decoded) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context,
                       memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
        ODM_TEST_CHECK(context, !odm_sha256_equal(NULL, &digest));
    }
    {
        odm_sha256_context invalid = ODM_SHA256_CONTEXT_INITIALIZER;
        odm_sha256_context overflow = ODM_SHA256_CONTEXT_INITIALIZER;
        odm_sha256_digest sentinel;
        odm_sha256_digest output;
        memset(&sentinel, 0x7c, sizeof(sentinel));
        output = sentinel;
        ODM_TEST_CHECK(context,
                       odm_sha256_update(&invalid, NULL, 0u) ==
                           ODM_STATUS_INVALID_STATE);
        ODM_TEST_CHECK(context,
                       odm_sha256(NULL, 1u, &output) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       memcmp(&output, &sentinel, sizeof(output)) == 0);
        ODM_TEST_CHECK(context, odm_sha256_init(&overflow) == ODM_STATUS_OK);
        overflow.total_bytes = ODM_SHA256_MAX_MESSAGE_BYTES;
        overflow.block_length = 63u;
        ODM_TEST_CHECK(context,
                       odm_sha256_update(&overflow, "x", 1u) ==
                           ODM_STATUS_OVERFLOW);
    }
}

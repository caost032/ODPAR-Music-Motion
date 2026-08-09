#include "test_harness.h"

#include "odm_wire.h"

#include <limits.h>
#include <string.h>

static int odm_test_wire_digest_hex(const odm_sha256_digest *digest,
                                    const char *expected) {
    char hex[ODM_SHA256_HEX_BUFFER_BYTES];
    uint64_t required = 0u;
    return odm_sha256_to_hex(digest, hex, sizeof(hex), &required) ==
               ODM_STATUS_OK && strcmp(hex, expected) == 0;
}

void odm_test_wire(odm_test_context *context) {
    {
        static const uint8_t expected[] = {
            0xabu, 0x34u, 0x12u, 0xefu, 0xcdu, 0xabu, 0x89u,
            0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u,
            0xfeu, 0xffu, 0xffu, 0xffu,
            0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x80u,
            0x10u, 0x20u, 0x30u
        };
        uint8_t buffer[sizeof(expected)];
        const uint8_t tail[] = {0x10u, 0x20u, 0x30u};
        odm_wire_writer writer = ODM_WIRE_WRITER_INITIALIZER;
        odm_wire_reader reader = ODM_WIRE_READER_INITIALIZER;
        uint64_t size = 0u;
        uint8_t u8 = 0u;
        uint16_t u16 = 0u;
        uint32_t u32 = 0u;
        uint64_t u64 = 0u;
        int32_t i32 = 0;
        int64_t i64 = 0;
        uint8_t copied[sizeof(tail)] = {0};
        ODM_TEST_CHECK(context,
                       odm_wire_writer_init(&writer, buffer, sizeof(buffer)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_write_u8(&writer, 0xabu) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_write_u16(&writer, UINT16_C(0x1234)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_write_u32(&writer, UINT32_C(0x89abcdef)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_write_u64(&writer,
                                          UINT64_C(0x1122334455667788)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_write_i32(&writer, -2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_write_i64(&writer, INT64_MIN) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_write_bytes(&writer, tail, sizeof(tail)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_writer_finish(&writer, &size) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, size == sizeof(expected));
        ODM_TEST_CHECK(context,
                       memcmp(buffer, expected, sizeof(expected)) == 0);
        ODM_TEST_CHECK(context,
                       odm_wire_reader_init(&reader, buffer, size) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_read_u8(&reader, &u8) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_read_u16(&reader, &u16) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_read_u32(&reader, &u32) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_read_u64(&reader, &u64) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_read_i32(&reader, &i32) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_read_i64(&reader, &i64) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_read_bytes(&reader, copied, sizeof(copied)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_wire_reader_finish(&reader) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, u8 == 0xabu && u16 == UINT16_C(0x1234));
        ODM_TEST_CHECK(context, u32 == UINT32_C(0x89abcdef));
        ODM_TEST_CHECK(context, u64 == UINT64_C(0x1122334455667788));
        ODM_TEST_CHECK(context, i32 == -2 && i64 == INT64_MIN);
        ODM_TEST_CHECK(context, memcmp(copied, tail, sizeof(tail)) == 0);
    }
    {
        uint8_t buffer[2] = {0x5au, 0x6bu};
        odm_wire_writer writer = ODM_WIRE_WRITER_INITIALIZER;
        uint64_t size = UINT64_C(123);
        ODM_TEST_CHECK(context,
                       odm_wire_writer_init(&writer, buffer, 1u) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_write_u16(&writer, UINT16_C(1)) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, writer.offset == 0u);
        ODM_TEST_CHECK(context, buffer[0] == 0x5au && buffer[1] == 0x6bu);
        ODM_TEST_CHECK(context,
                       odm_wire_write_u8(&writer, 1u) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context,
                       odm_wire_writer_finish(&writer, &size) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, size == UINT64_C(123));
    }
    {
        uint8_t buffer[1] = {0x33u};
        odm_wire_reader reader = ODM_WIRE_READER_INITIALIZER;
        uint16_t output = UINT16_C(0xa5a5);
        ODM_TEST_CHECK(context,
                       odm_wire_reader_init(&reader, buffer, sizeof(buffer)) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_read_u16(&reader, &output) ==
                           ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context, output == UINT16_C(0xa5a5));
        ODM_TEST_CHECK(context, reader.offset == 0u);
        ODM_TEST_CHECK(context,
                       odm_wire_read_u8(&reader, buffer) ==
                           ODM_STATUS_INVALID_DATA);
    }
    {
        static const uint8_t expected[] = {
            0x4fu, 0x44u, 0x4du, 0x43u, 0x01u, 0x00u, 0x00u, 0x00u,
            0x44u, 0x33u, 0x22u, 0x11u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x02u, 0x00u, 0x07u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
            0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
            0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
            0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
            0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
            0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu,
            0x61u, 0x62u, 0x63u
        };
        uint8_t record[sizeof(expected)];
        uint64_t required = 0u;
        uint64_t payload_offset = 0u;
        odm_sha256_digest record_digest;
        odm_sha256_digest parsed_digest;
        odm_wire_record_info info;
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(UINT32_C(0x11223344), 2u, 7u,
                                             (const uint8_t *)"abc", 3u,
                                             NULL, 0u, &required, NULL) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == sizeof(expected));
        memset(record, 0xa5, sizeof(record));
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(UINT32_C(0x11223344), 2u, 7u,
                                             (const uint8_t *)"abc", 3u,
                                             record, required - 1u, &required,
                                             NULL) ==
                           ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, record[0] == 0xa5u);
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(UINT32_C(0x11223344), 2u, 7u,
                                             (const uint8_t *)"abc", 3u,
                                             record, sizeof(record), &required,
                                             &record_digest) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       memcmp(record, expected, sizeof(expected)) == 0);
        ODM_TEST_CHECK(
            context,
            odm_test_wire_digest_hex(
                &record_digest,
                "d5367435c77f634689e1569bafac4407403f37516ed43604095a5c42ab9588bb"));
        ODM_TEST_CHECK(context,
                       odm_wire_record_read(record, sizeof(record), &info,
                                            &payload_offset, &parsed_digest) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, payload_offset == ODM_WIRE_RECORD_HEADER_BYTES);
        ODM_TEST_CHECK(context, info.kind == UINT32_C(0x11223344));
        ODM_TEST_CHECK(context, info.schema_version_major == 2u &&
                                info.schema_version_minor == 7u);
        ODM_TEST_CHECK(context, info.payload_bytes == 3u);
        ODM_TEST_CHECK(context,
                       odm_sha256_equal(&record_digest, &parsed_digest));
    }
    {
        uint8_t alias[80];
        uint64_t required = 0u;
        odm_wire_record_info info;
        uint64_t offset = 0u;
        memcpy(alias, "overlap", 7u);
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(9u, 1u, 0u, alias, 7u,
                                             alias, sizeof(alias), &required,
                                             NULL) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, required == 71u);
        ODM_TEST_CHECK(context,
                       memcmp(alias + ODM_WIRE_RECORD_HEADER_BYTES,
                              "overlap", 7u) == 0);
        ODM_TEST_CHECK(context,
                       odm_wire_record_read(alias, required, &info, &offset,
                                            NULL) == ODM_STATUS_OK);
    }
    {
        uint8_t record[68];
        uint8_t pristine[68];
        uint64_t required = 0u;
        uint64_t offset = UINT64_C(0xa5a5);
        uint64_t prefix;
        odm_sha256_digest digest;
        odm_sha256_digest digest_sentinel;
        odm_wire_record_info info;
        odm_wire_record_info sentinel;
        memset(&sentinel, 0x5a, sizeof(sentinel));
        memset(&digest_sentinel, 0x3c, sizeof(digest_sentinel));
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(1u, 1u, 0u,
                                             (const uint8_t *)"data", 4u,
                                             record, sizeof(record), &required,
                                             NULL) == ODM_STATUS_OK);
        memcpy(pristine, record, sizeof(record));
#define ODM_EXPECT_READ_FAILURE(expected_status) \
        do { \
            info = sentinel; \
            digest = digest_sentinel; \
            offset = UINT64_C(0xa5a5); \
            ODM_TEST_CHECK(context, \
                           odm_wire_record_read(record, required, &info, \
                                                &offset, &digest) == \
                               (expected_status)); \
            ODM_TEST_CHECK(context, \
                           memcmp(&info, &sentinel, sizeof(info)) == 0); \
            ODM_TEST_CHECK(context, offset == UINT64_C(0xa5a5)); \
            ODM_TEST_CHECK(context, \
                           memcmp(&digest, &digest_sentinel, \
                                  sizeof(digest)) == 0); \
            memcpy(record, pristine, sizeof(record)); \
        } while (0)
        record[64] ^= 1u;
        ODM_EXPECT_READ_FAILURE(ODM_STATUS_INTEGRITY_ERROR);
        record[0] = (uint8_t)'X';
        ODM_EXPECT_READ_FAILURE(ODM_STATUS_INVALID_DATA);
        record[4] = 2u;
        ODM_EXPECT_READ_FAILURE(ODM_STATUS_VERSION_MISMATCH);
        record[12] = 1u;
        ODM_EXPECT_READ_FAILURE(ODM_STATUS_INVALID_DATA);
        record[20] = 1u;
        ODM_EXPECT_READ_FAILURE(ODM_STATUS_INVALID_DATA);
        for (prefix = 0u; prefix < required; prefix++) {
            info = sentinel;
            digest = digest_sentinel;
            offset = UINT64_C(0xa5a5);
            ODM_TEST_CHECK(context,
                           odm_wire_record_read(record, prefix, &info,
                                                &offset, &digest) ==
                               ODM_STATUS_INVALID_DATA);
            ODM_TEST_CHECK(context,
                           memcmp(&info, &sentinel, sizeof(info)) == 0);
            ODM_TEST_CHECK(context, offset == UINT64_C(0xa5a5));
            ODM_TEST_CHECK(context,
                           memcmp(&digest, &digest_sentinel,
                                  sizeof(digest)) == 0);
        }
        ODM_TEST_CHECK(context,
                       odm_wire_record_read(record, required + 1u, &info,
                                            &offset, NULL) ==
                           ODM_STATUS_INVALID_DATA);
#undef ODM_EXPECT_READ_FAILURE
    }
    {
        uint8_t record[ODM_WIRE_RECORD_HEADER_BYTES];
        uint64_t required = 0u;
        uint64_t offset = 0u;
        odm_wire_record_info info;
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(2u, 1u, 0u, NULL, 0u,
                                             record, sizeof(record), &required,
                                             NULL) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_wire_record_read(record, sizeof(record), &info,
                                            &offset, NULL) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, info.payload_bytes == 0u);
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(0u, 1u, 0u, NULL, 0u,
                                             record, sizeof(record), &required,
                                             NULL) ==
                           ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
                       odm_wire_record_write(1u, 1u, 0u,
                                             (const uint8_t *)"x", UINT64_MAX,
                                             record, sizeof(record), &required,
                                             NULL) == ODM_STATUS_OVERFLOW);
    }
}

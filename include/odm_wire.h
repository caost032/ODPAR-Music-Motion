#ifndef ODM_WIRE_H
#define ODM_WIRE_H

#include "odm_hash.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ODM_WIRE_VERSION_MAJOR 1u
#define ODM_WIRE_VERSION_MINOR 0u
#define ODM_WIRE_RECORD_HEADER_BYTES 64u
#define ODM_WIRE_WRITER_INITIALIZER {0}
#define ODM_WIRE_READER_INITIALIZER {0}

typedef struct {
    uint8_t *buffer;
    uint64_t capacity;
    uint64_t offset;
    odm_status status;
    uint32_t initialized;
} odm_wire_writer;

typedef struct {
    const uint8_t *buffer;
    uint64_t size;
    uint64_t offset;
    odm_status status;
    uint32_t initialized;
} odm_wire_reader;

/* This is an in-memory description, not bytes copied with sizeof. */
typedef struct {
    uint32_t wire_version_major;
    uint32_t wire_version_minor;
    uint32_t kind;
    uint32_t flags;
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint64_t payload_bytes;
    odm_sha256_digest payload_sha256;
} odm_wire_record_info;

odm_status odm_wire_writer_init(odm_wire_writer *writer, uint8_t *buffer,
                                uint64_t capacity);
odm_status odm_wire_write_u8(odm_wire_writer *writer, uint8_t value);
odm_status odm_wire_write_u16(odm_wire_writer *writer, uint16_t value);
odm_status odm_wire_write_u32(odm_wire_writer *writer, uint32_t value);
odm_status odm_wire_write_u64(odm_wire_writer *writer, uint64_t value);
odm_status odm_wire_write_i32(odm_wire_writer *writer, int32_t value);
odm_status odm_wire_write_i64(odm_wire_writer *writer, int64_t value);
odm_status odm_wire_write_bytes(odm_wire_writer *writer, const void *data,
                                uint64_t length);
odm_status odm_wire_writer_finish(const odm_wire_writer *writer,
                                  uint64_t *out_size);

odm_status odm_wire_reader_init(odm_wire_reader *reader,
                                const uint8_t *buffer, uint64_t size);
odm_status odm_wire_read_u8(odm_wire_reader *reader, uint8_t *out_value);
odm_status odm_wire_read_u16(odm_wire_reader *reader, uint16_t *out_value);
odm_status odm_wire_read_u32(odm_wire_reader *reader, uint32_t *out_value);
odm_status odm_wire_read_u64(odm_wire_reader *reader, uint64_t *out_value);
odm_status odm_wire_read_i32(odm_wire_reader *reader, int32_t *out_value);
odm_status odm_wire_read_i64(odm_wire_reader *reader, int64_t *out_value);
odm_status odm_wire_read_bytes(odm_wire_reader *reader, void *out_data,
                               uint64_t length);
odm_status odm_wire_reader_finish(const odm_wire_reader *reader);

/* Canonical ODMC record v1.  Encoding is size-first and all-or-nothing.
 * The record layout is a fixed 64-byte header followed by payload bytes. */
odm_status odm_wire_record_write(uint32_t kind,
                                 uint32_t schema_version_major,
                                 uint32_t schema_version_minor,
                                 const uint8_t *payload,
                                 uint64_t payload_bytes,
                                 uint8_t *buffer,
                                 uint64_t capacity,
                                 uint64_t *out_required,
                                 odm_sha256_digest *out_record_sha256);
odm_status odm_wire_record_read(const uint8_t *record,
                                uint64_t record_bytes,
                                odm_wire_record_info *out_info,
                                uint64_t *out_payload_offset,
                                odm_sha256_digest *out_record_sha256);

#ifdef __cplusplus
}
#endif

#endif /* ODM_WIRE_H */

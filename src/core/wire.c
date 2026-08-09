#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define ODM_WIRE_CURSOR_COOKIE UINT32_C(0x4f444d57)

static int odm_wire_u64_fits_size(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return 1;
#endif
}

static int odm_wire_writer_valid(const odm_wire_writer *writer) {
    return writer != NULL && writer->initialized == ODM_WIRE_CURSOR_COOKIE &&
           writer->offset <= writer->capacity &&
           odm_wire_u64_fits_size(writer->capacity);
}

static int odm_wire_reader_valid(const odm_wire_reader *reader) {
    return reader != NULL && reader->initialized == ODM_WIRE_CURSOR_COOKIE &&
           reader->offset <= reader->size &&
           odm_wire_u64_fits_size(reader->size);
}

odm_status odm_wire_writer_init(odm_wire_writer *writer, uint8_t *buffer,
                                uint64_t capacity) {
    if (!writer || (capacity != 0u && !buffer) ||
        !odm_wire_u64_fits_size(capacity)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->offset = 0u;
    writer->status = ODM_STATUS_OK;
    writer->initialized = ODM_WIRE_CURSOR_COOKIE;
    return ODM_STATUS_OK;
}

static odm_status odm_wire_writer_reserve(odm_wire_writer *writer,
                                          uint64_t length,
                                          uint8_t **out_target) {
    if (!odm_wire_writer_valid(writer) || !out_target) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (writer->status != ODM_STATUS_OK) return writer->status;
    if (length > writer->capacity - writer->offset) {
        writer->status = ODM_STATUS_BUFFER_TOO_SMALL;
        return writer->status;
    }
    *out_target = length == 0u ? writer->buffer :
                  writer->buffer + (size_t)writer->offset;
    writer->offset += length;
    return ODM_STATUS_OK;
}

odm_status odm_wire_write_u8(odm_wire_writer *writer, uint8_t value) {
    uint8_t *target;
    odm_status status = odm_wire_writer_reserve(writer, 1u, &target);
    if (status == ODM_STATUS_OK) target[0] = value;
    return status;
}

odm_status odm_wire_write_u16(odm_wire_writer *writer, uint16_t value) {
    uint8_t *target;
    odm_status status = odm_wire_writer_reserve(writer, 2u, &target);
    if (status == ODM_STATUS_OK) {
        target[0] = (uint8_t)value;
        target[1] = (uint8_t)(value >> 8);
    }
    return status;
}

odm_status odm_wire_write_u32(odm_wire_writer *writer, uint32_t value) {
    uint8_t *target;
    odm_status status = odm_wire_writer_reserve(writer, 4u, &target);
    if (status == ODM_STATUS_OK) {
        target[0] = (uint8_t)value;
        target[1] = (uint8_t)(value >> 8);
        target[2] = (uint8_t)(value >> 16);
        target[3] = (uint8_t)(value >> 24);
    }
    return status;
}

odm_status odm_wire_write_u64(odm_wire_writer *writer, uint64_t value) {
    uint8_t *target;
    unsigned index;
    odm_status status = odm_wire_writer_reserve(writer, 8u, &target);
    if (status == ODM_STATUS_OK) {
        for (index = 0u; index < 8u; index++) {
            target[index] = (uint8_t)(value >> (index * 8u));
        }
    }
    return status;
}

odm_status odm_wire_write_i32(odm_wire_writer *writer, int32_t value) {
    return odm_wire_write_u32(writer, (uint32_t)value);
}

odm_status odm_wire_write_i64(odm_wire_writer *writer, int64_t value) {
    return odm_wire_write_u64(writer, (uint64_t)value);
}

odm_status odm_wire_write_bytes(odm_wire_writer *writer, const void *data,
                                uint64_t length) {
    uint8_t *target;
    odm_status status;
    if (length != 0u && !data) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_wire_u64_fits_size(length)) return ODM_STATUS_OVERFLOW;
    status = odm_wire_writer_reserve(writer, length, &target);
    if (status == ODM_STATUS_OK && length != 0u) {
        memmove(target, data, (size_t)length);
    }
    return status;
}

odm_status odm_wire_writer_finish(const odm_wire_writer *writer,
                                  uint64_t *out_size) {
    if (!odm_wire_writer_valid(writer) || !out_size) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (writer->status != ODM_STATUS_OK) return writer->status;
    *out_size = writer->offset;
    return ODM_STATUS_OK;
}

odm_status odm_wire_reader_init(odm_wire_reader *reader,
                                const uint8_t *buffer, uint64_t size) {
    if (!reader || (size != 0u && !buffer) || !odm_wire_u64_fits_size(size)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    reader->buffer = buffer;
    reader->size = size;
    reader->offset = 0u;
    reader->status = ODM_STATUS_OK;
    reader->initialized = ODM_WIRE_CURSOR_COOKIE;
    return ODM_STATUS_OK;
}

static odm_status odm_wire_reader_take(odm_wire_reader *reader,
                                       uint64_t length,
                                       const uint8_t **out_source) {
    if (!odm_wire_reader_valid(reader) || !out_source) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (reader->status != ODM_STATUS_OK) return reader->status;
    if (length > reader->size - reader->offset) {
        reader->status = ODM_STATUS_INVALID_DATA;
        return reader->status;
    }
    *out_source = length == 0u ? reader->buffer :
                  reader->buffer + (size_t)reader->offset;
    reader->offset += length;
    return ODM_STATUS_OK;
}

odm_status odm_wire_read_u8(odm_wire_reader *reader, uint8_t *out_value) {
    const uint8_t *source;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_reader_take(reader, 1u, &source);
    if (status == ODM_STATUS_OK) *out_value = source[0];
    return status;
}

odm_status odm_wire_read_u16(odm_wire_reader *reader, uint16_t *out_value) {
    const uint8_t *source;
    uint16_t value;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_reader_take(reader, 2u, &source);
    if (status != ODM_STATUS_OK) return status;
    value = (uint16_t)((uint16_t)source[0] |
                       (uint16_t)((uint16_t)source[1] << 8));
    *out_value = value;
    return ODM_STATUS_OK;
}

odm_status odm_wire_read_u32(odm_wire_reader *reader, uint32_t *out_value) {
    const uint8_t *source;
    uint32_t value;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_reader_take(reader, 4u, &source);
    if (status != ODM_STATUS_OK) return status;
    value = (uint32_t)source[0] |
            ((uint32_t)source[1] << 8) |
            ((uint32_t)source[2] << 16) |
            ((uint32_t)source[3] << 24);
    *out_value = value;
    return ODM_STATUS_OK;
}

odm_status odm_wire_read_u64(odm_wire_reader *reader, uint64_t *out_value) {
    const uint8_t *source;
    uint64_t value = 0u;
    unsigned index;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_reader_take(reader, 8u, &source);
    if (status != ODM_STATUS_OK) return status;
    for (index = 0u; index < 8u; index++) {
        value |= (uint64_t)source[index] << (index * 8u);
    }
    *out_value = value;
    return ODM_STATUS_OK;
}

odm_status odm_wire_read_i32(odm_wire_reader *reader, int32_t *out_value) {
    uint32_t encoded;
    int32_t value;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_read_u32(reader, &encoded);
    if (status != ODM_STATUS_OK) return status;
    if (encoded <= (uint32_t)INT32_MAX) value = (int32_t)encoded;
    else if (encoded == (UINT32_C(1) << 31)) value = INT32_MIN;
    else value = -(int32_t)(UINT32_MAX - encoded + UINT32_C(1));
    *out_value = value;
    return ODM_STATUS_OK;
}

odm_status odm_wire_read_i64(odm_wire_reader *reader, int64_t *out_value) {
    uint64_t encoded;
    int64_t value;
    odm_status status;
    if (!out_value) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_read_u64(reader, &encoded);
    if (status != ODM_STATUS_OK) return status;
    if (encoded <= (uint64_t)INT64_MAX) value = (int64_t)encoded;
    else if (encoded == (UINT64_C(1) << 63)) value = INT64_MIN;
    else value = -(int64_t)(UINT64_MAX - encoded + UINT64_C(1));
    *out_value = value;
    return ODM_STATUS_OK;
}

odm_status odm_wire_read_bytes(odm_wire_reader *reader, void *out_data,
                               uint64_t length) {
    const uint8_t *source;
    odm_status status;
    if (length != 0u && !out_data) return ODM_STATUS_INVALID_ARGUMENT;
    if (!odm_wire_u64_fits_size(length)) return ODM_STATUS_OVERFLOW;
    status = odm_wire_reader_take(reader, length, &source);
    if (status == ODM_STATUS_OK && length != 0u) {
        memmove(out_data, source, (size_t)length);
    }
    return status;
}

odm_status odm_wire_reader_finish(const odm_wire_reader *reader) {
    if (!odm_wire_reader_valid(reader)) return ODM_STATUS_INVALID_ARGUMENT;
    if (reader->status != ODM_STATUS_OK) return reader->status;
    return reader->offset == reader->size ? ODM_STATUS_OK :
                                           ODM_STATUS_INVALID_DATA;
}

static void odm_wire_header_write_u16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void odm_wire_header_write_u32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16);
    target[3] = (uint8_t)(value >> 24);
}

static void odm_wire_header_write_u64(uint8_t *target, uint64_t value) {
    unsigned index;
    for (index = 0u; index < 8u; index++) {
        target[index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint16_t odm_wire_header_read_u16(const uint8_t *source) {
    return (uint16_t)((uint16_t)source[0] |
                      (uint16_t)((uint16_t)source[1] << 8));
}

static uint32_t odm_wire_header_read_u32(const uint8_t *source) {
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint64_t odm_wire_header_read_u64(const uint8_t *source) {
    uint64_t value = 0u;
    unsigned index;
    for (index = 0u; index < 8u; index++) {
        value |= (uint64_t)source[index] << (index * 8u);
    }
    return value;
}

odm_status odm_wire_record_write(uint32_t kind,
                                 uint32_t schema_version_major,
                                 uint32_t schema_version_minor,
                                 const uint8_t *payload,
                                 uint64_t payload_bytes,
                                 uint8_t *buffer,
                                 uint64_t capacity,
                                 uint64_t *out_required,
                                 odm_sha256_digest *out_record_sha256) {
    uint64_t required;
    uint8_t header[ODM_WIRE_RECORD_HEADER_BYTES];
    odm_sha256_digest payload_digest;
    odm_sha256_digest record_digest;
    odm_status status;
    if (!out_required || kind == 0u || schema_version_major == 0u ||
        schema_version_major > UINT16_MAX ||
        schema_version_minor > UINT16_MAX ||
        (payload_bytes != 0u && !payload)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (payload_bytes > UINT64_MAX - ODM_WIRE_RECORD_HEADER_BYTES) {
        return ODM_STATUS_OVERFLOW;
    }
    required = payload_bytes + ODM_WIRE_RECORD_HEADER_BYTES;
    *out_required = required;
    if (required > ODM_SHA256_MAX_MESSAGE_BYTES) return ODM_STATUS_OVERFLOW;
    if (!odm_wire_u64_fits_size(required)) return ODM_STATUS_OVERFLOW;
    if (!buffer || capacity < required) return ODM_STATUS_BUFFER_TOO_SMALL;
    status = odm_sha256(payload, payload_bytes, &payload_digest);
    if (status != ODM_STATUS_OK) return status;
    memset(header, 0, sizeof(header));
    header[0] = (uint8_t)'O';
    header[1] = (uint8_t)'D';
    header[2] = (uint8_t)'M';
    header[3] = (uint8_t)'C';
    odm_wire_header_write_u16(header + 4u, (uint16_t)ODM_WIRE_VERSION_MAJOR);
    odm_wire_header_write_u16(header + 6u, (uint16_t)ODM_WIRE_VERSION_MINOR);
    odm_wire_header_write_u32(header + 8u, kind);
    odm_wire_header_write_u32(header + 12u, 0u);
    odm_wire_header_write_u16(header + 16u, (uint16_t)schema_version_major);
    odm_wire_header_write_u16(header + 18u, (uint16_t)schema_version_minor);
    odm_wire_header_write_u32(header + 20u, 0u);
    odm_wire_header_write_u64(header + 24u, payload_bytes);
    memcpy(header + 32u, payload_digest.bytes, ODM_SHA256_DIGEST_BYTES);
    if (payload_bytes != 0u) {
        memmove(buffer + ODM_WIRE_RECORD_HEADER_BYTES, payload,
                (size_t)payload_bytes);
    }
    memcpy(buffer, header, sizeof(header));
    status = odm_sha256(buffer, required, &record_digest);
    if (status != ODM_STATUS_OK) return status;
    if (out_record_sha256) *out_record_sha256 = record_digest;
    return ODM_STATUS_OK;
}

odm_status odm_wire_record_read(const uint8_t *record,
                                uint64_t record_bytes,
                                odm_wire_record_info *out_info,
                                uint64_t *out_payload_offset,
                                odm_sha256_digest *out_record_sha256) {
    odm_wire_record_info info;
    odm_sha256_digest payload_digest;
    odm_sha256_digest record_digest;
    uint64_t expected_bytes;
    unsigned index;
    odm_status status;
    if (!record || !out_info || !out_payload_offset) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (record_bytes < ODM_WIRE_RECORD_HEADER_BYTES ||
        !odm_wire_u64_fits_size(record_bytes)) {
        return ODM_STATUS_INVALID_DATA;
    }
    if (record[0] != (uint8_t)'O' || record[1] != (uint8_t)'D' ||
        record[2] != (uint8_t)'M' || record[3] != (uint8_t)'C') {
        return ODM_STATUS_INVALID_DATA;
    }
    info.wire_version_major = odm_wire_header_read_u16(record + 4u);
    info.wire_version_minor = odm_wire_header_read_u16(record + 6u);
    if (info.wire_version_major != ODM_WIRE_VERSION_MAJOR ||
        info.wire_version_minor != ODM_WIRE_VERSION_MINOR) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    info.kind = odm_wire_header_read_u32(record + 8u);
    info.flags = odm_wire_header_read_u32(record + 12u);
    info.schema_version_major = odm_wire_header_read_u16(record + 16u);
    info.schema_version_minor = odm_wire_header_read_u16(record + 18u);
    if (info.kind == 0u || info.flags != 0u ||
        info.schema_version_major == 0u ||
        odm_wire_header_read_u32(record + 20u) != 0u) {
        return ODM_STATUS_INVALID_DATA;
    }
    info.payload_bytes = odm_wire_header_read_u64(record + 24u);
    if (info.payload_bytes > UINT64_MAX - ODM_WIRE_RECORD_HEADER_BYTES) {
        return ODM_STATUS_OVERFLOW;
    }
    expected_bytes = info.payload_bytes + ODM_WIRE_RECORD_HEADER_BYTES;
    if (record_bytes != expected_bytes) return ODM_STATUS_INVALID_DATA;
    for (index = 0u; index < ODM_SHA256_DIGEST_BYTES; index++) {
        info.payload_sha256.bytes[index] = record[32u + index];
    }
    status = odm_sha256(record + ODM_WIRE_RECORD_HEADER_BYTES,
                        info.payload_bytes, &payload_digest);
    if (status != ODM_STATUS_OK) return status;
    if (!odm_sha256_equal(&info.payload_sha256, &payload_digest)) {
        return ODM_STATUS_INTEGRITY_ERROR;
    }
    status = odm_sha256(record, record_bytes, &record_digest);
    if (status != ODM_STATUS_OK) return status;
    *out_info = info;
    *out_payload_offset = ODM_WIRE_RECORD_HEADER_BYTES;
    if (out_record_sha256) *out_record_sha256 = record_digest;
    return ODM_STATUS_OK;
}

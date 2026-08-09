#include "test_harness.h"

#include "odm_fixed.h"
#include "odm_hash.h"
#include "odm_limits.h"
#include "odm_progress.h"
#include "odm_rng.h"
#include "odm_time.h"
#include "odm_wire.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void odm_test_properties(odm_test_context *context) {
    static const int32_t fps_values[] = {24, 25, 30, 50, 60};
    odm_rng rng;
    uint32_t iteration;
    ODM_TEST_CHECK(context,
                   odm_rng_seed(&rng, UINT64_C(0x6f64706172),
                                UINT64_C(0x6d75736963)) == ODM_STATUS_OK);
    for (iteration = 0u; iteration < 20000u; iteration++) {
        uint32_t random = 0u;
        uint32_t frame_random = 0u;
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&rng, &frame_random) == ODM_STATUS_OK);
        int32_t fps = fps_values[random % 5u];
        int64_t frame = (int64_t)(frame_random % UINT32_C(1000000));
        int64_t samples_per_frame = 0;
        int64_t start = 0;
        odm_timeline timeline;
        ODM_TEST_CHECK(context,
                       odm_time_samples_per_frame(fps, &samples_per_frame) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_time_frame_start(frame, fps, &start) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, start == frame * samples_per_frame);
        ODM_TEST_CHECK(context,
                       odm_time_resolve_timeline(start, fps, &timeline) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, timeline.frame_count == frame);
        ODM_TEST_CHECK(context, timeline.output_end_sample == start);
    }
    for (iteration = 0u; iteration < 20000u; iteration++) {
        uint32_t random = 0u;
        int64_t signed_random;
        ODM_TEST_CHECK(context,
                       odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
        signed_random = (int64_t)(int32_t)random;
        odm_microunit input = signed_random % INT64_C(1000000000);
        odm_q32_32 encoded = 0;
        odm_microunit decoded = 0;
        ODM_TEST_CHECK(context,
                       odm_q32_32_from_microunits(input, &encoded) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_q32_32_to_microunits(encoded, &decoded) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, decoded == input);
        ODM_TEST_CHECK(context,
                       odm_q32_32_mul(encoded, ODM_Q32_ONE, &encoded) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_q32_32_to_microunits(encoded, &decoded) ==
                           ODM_STATUS_OK);
        ODM_TEST_CHECK(context, decoded == input);
    }
    for (iteration = 0u; iteration < 20000u; iteration++) {
        uint32_t bits[4] = {0u, 0u, 0u, 0u};
        odm_rational left;
        odm_rational right;
        int64_t left_cross;
        int64_t right_cross;
        int32_t actual = 0;
        int32_t expected;
        size_t index;
        for (index = 0u; index < 4u; index++) {
            ODM_TEST_CHECK(context,
                           odm_rng_next_u32(&rng, &bits[index]) == ODM_STATUS_OK);
        }
        left.numerator = (int64_t)((int32_t)bits[0] % INT32_C(1000000));
        left.denominator = (int64_t)(bits[1] % UINT32_C(999999)) + 1;
        right.numerator = (int64_t)((int32_t)bits[2] % INT32_C(1000000));
        right.denominator = (int64_t)(bits[3] % UINT32_C(999999)) + 1;
        left_cross = left.numerator * right.denominator;
        right_cross = right.numerator * left.denominator;
        expected = left_cross < right_cross ? -1 : (left_cross > right_cross ? 1 : 0);
        ODM_TEST_CHECK(context,
                       odm_rational_compare(left, right, &actual) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, actual == expected);
    }
    {
        uint64_t total = UINT64_MAX;
        uint64_t done = 0u;
        uint32_t previous = 0u;
        for (iteration = 0u; iteration < 20000u; iteration++) {
            uint32_t current = 0u;
            done += UINT64_C(7919);
            ODM_TEST_CHECK(context,
                           odm_progress_fraction(done, total, &current) ==
                               ODM_STATUS_OK);
            ODM_TEST_CHECK(context, current >= previous);
            ODM_TEST_CHECK(context, current <= ODM_PROGRESS_ONE);
            previous = current;
        }
    }
    {
        uint8_t data[512];
        for (iteration = 0u; iteration < 20000u; iteration++) {
            odm_sha256_context stream = ODM_SHA256_CONTEXT_INITIALIZER;
            odm_sha256_digest one_shot;
            odm_sha256_digest chunked;
            odm_sha256_digest decoded;
            char hex[ODM_SHA256_HEX_BUFFER_BYTES];
            uint64_t required = 0u;
            uint32_t random = 0u;
            uint64_t length;
            uint64_t offset = 0u;
            size_t index;
            ODM_TEST_CHECK(context,
                           odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
            length = (uint64_t)(random % (uint32_t)(sizeof(data) + 1u));
            for (index = 0u; index < (size_t)length; index++) {
                ODM_TEST_CHECK(context,
                               odm_rng_next_u32(&rng, &random) ==
                                   ODM_STATUS_OK);
                data[index] = (uint8_t)random;
            }
            ODM_TEST_CHECK(context,
                           odm_sha256(data, length, &one_shot) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_sha256_init(&stream) == ODM_STATUS_OK);
            while (offset < length) {
                uint64_t remaining = length - offset;
                uint64_t take;
                ODM_TEST_CHECK(context,
                               odm_rng_next_u32(&rng, &random) ==
                                   ODM_STATUS_OK);
                take = (uint64_t)(random % UINT32_C(97)) + 1u;
                if (take > remaining) take = remaining;
                ODM_TEST_CHECK(context,
                               odm_sha256_update(&stream, data + (size_t)offset,
                                                 take) == ODM_STATUS_OK);
                offset += take;
            }
            ODM_TEST_CHECK(context,
                           odm_sha256_final(&stream, &chunked) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_sha256_equal(&one_shot, &chunked));
            ODM_TEST_CHECK(context,
                           odm_sha256_to_hex(&one_shot, hex, sizeof(hex),
                                             &required) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_sha256_from_hex(hex, ODM_SHA256_HEX_CHARS,
                                               &decoded) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_sha256_equal(&one_shot, &decoded));
        }
    }
    {
        uint8_t payload[256];
        uint8_t record[ODM_WIRE_RECORD_HEADER_BYTES + sizeof(payload)];
        for (iteration = 0u; iteration < 10000u; iteration++) {
            odm_wire_record_info info;
            odm_sha256_digest write_digest;
            odm_sha256_digest read_digest;
            uint64_t required = 0u;
            uint64_t payload_offset = 0u;
            uint32_t random = 0u;
            uint32_t kind;
            uint32_t major;
            uint32_t minor;
            uint64_t length;
            size_t index;
            ODM_TEST_CHECK(context,
                           odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
            length = (uint64_t)(random % (uint32_t)(sizeof(payload) + 1u));
            for (index = 0u; index < (size_t)length; index++) {
                ODM_TEST_CHECK(context,
                               odm_rng_next_u32(&rng, &random) ==
                                   ODM_STATUS_OK);
                payload[index] = (uint8_t)random;
            }
            ODM_TEST_CHECK(context,
                           odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
            kind = random == 0u ? 1u : random;
            ODM_TEST_CHECK(context,
                           odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
            major = random % UINT32_C(65535) + 1u;
            ODM_TEST_CHECK(context,
                           odm_rng_next_u32(&rng, &random) == ODM_STATUS_OK);
            minor = random % UINT32_C(65536);
            ODM_TEST_CHECK(context,
                           odm_wire_record_write(kind, major, minor, payload,
                                                 length, record, sizeof(record),
                                                 &required, &write_digest) ==
                               ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           required == ODM_WIRE_RECORD_HEADER_BYTES + length);
            ODM_TEST_CHECK(context,
                           odm_wire_record_read(record, required, &info,
                                                &payload_offset,
                                                &read_digest) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, info.kind == kind);
            ODM_TEST_CHECK(context, info.schema_version_major == major);
            ODM_TEST_CHECK(context, info.schema_version_minor == minor);
            ODM_TEST_CHECK(context, info.payload_bytes == length);
            ODM_TEST_CHECK(context,
                           payload_offset == ODM_WIRE_RECORD_HEADER_BYTES);
            ODM_TEST_CHECK(context,
                           odm_sha256_equal(&write_digest, &read_digest));
            ODM_TEST_CHECK(context,
                           memcmp(record + payload_offset, payload,
                                  (size_t)length) == 0);
            if (length != 0u) {
                record[payload_offset + (uint64_t)(iteration % (uint32_t)length)] ^=
                    UINT8_C(1);
                ODM_TEST_CHECK(context,
                               odm_wire_record_read(record, required, &info,
                                                    &payload_offset, NULL) ==
                                   ODM_STATUS_INTEGRITY_ERROR);
            } else {
                ODM_TEST_CHECK(context, info.payload_bytes == 0u);
            }
        }
    }
    {
        odm_resource_limits limits;
        ODM_TEST_CHECK(context,
                       odm_resource_limits_default(&limits) == ODM_STATUS_OK);
        for (iteration = 0u; iteration < 20000u; iteration++) {
            odm_resource_request request = {
                ODM_RESOURCE_LIMITS_SCHEMA_VERSION, 0u, 0u, 0u, 0u, 0u,
                0u, 0u, 0u, 0u
            };
            odm_admission_result result;
            uint32_t values[8];
            size_t index;
            uint32_t expected_dimension = ODM_LIMIT_DIMENSION_NONE;
            for (index = 0u; index < 8u; index++) {
                ODM_TEST_CHECK(context,
                               odm_rng_next_u32(&rng, &values[index]) ==
                                   ODM_STATUS_OK);
            }
            request.input_bytes = values[0];
            request.output_bytes = values[1];
            request.memory_bytes = values[2];
            request.work_units = values[3];
            request.queue_items = values[4] % UINT32_C(100000);
            request.workers = values[5] % UINT32_C(100);
            request.jobs = values[6] % UINT32_C(10000);
            request.recursion_depth = values[7] % UINT32_C(2000);
            if (request.input_bytes > limits.max_input_bytes) {
                expected_dimension = ODM_LIMIT_DIMENSION_INPUT_BYTES;
            } else if (request.output_bytes > limits.max_output_bytes) {
                expected_dimension = ODM_LIMIT_DIMENSION_OUTPUT_BYTES;
            } else if (request.memory_bytes > limits.max_memory_bytes) {
                expected_dimension = ODM_LIMIT_DIMENSION_MEMORY_BYTES;
            } else if (request.work_units > limits.max_work_units) {
                expected_dimension = ODM_LIMIT_DIMENSION_WORK_UNITS;
            } else if (request.queue_items > limits.max_queue_items) {
                expected_dimension = ODM_LIMIT_DIMENSION_QUEUE_ITEMS;
            } else if (request.workers > limits.max_workers) {
                expected_dimension = ODM_LIMIT_DIMENSION_WORKERS;
            } else if (request.jobs > limits.max_jobs) {
                expected_dimension = ODM_LIMIT_DIMENSION_JOBS;
            } else if (request.recursion_depth >
                       limits.max_recursion_depth) {
                expected_dimension = ODM_LIMIT_DIMENSION_RECURSION_DEPTH;
            }
            ODM_TEST_CHECK(
                context,
                odm_resource_admit(&limits, &request, &result) ==
                    (expected_dimension == ODM_LIMIT_DIMENSION_NONE ?
                         ODM_STATUS_OK : ODM_STATUS_BUDGET_EXCEEDED));
            ODM_TEST_CHECK(context, result.dimension == expected_dimension);
        }
    }
}

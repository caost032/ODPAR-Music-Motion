#include "odm_music_map.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MUSIC_MAP_PAYLOAD_MAGIC_BYTES 8u

static const uint8_t MUSIC_MAP_PAYLOAD_MAGIC[MUSIC_MAP_PAYLOAD_MAGIC_BYTES] = {
    (uint8_t)'O', (uint8_t)'D', (uint8_t)'M', (uint8_t)'M',
    (uint8_t)'A', (uint8_t)'P', (uint8_t)'1', 0u
};

static int music_map_u64_fits_size(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return 1;
#endif
}

static odm_status music_map_payload_bytes(uint64_t tick_count,
                                          uint64_t *out_payload,
                                          uint64_t *out_record) {
    uint64_t ticks_bytes;
    uint64_t payload;
    if (!out_payload || !out_record) return ODM_STATUS_INVALID_ARGUMENT;
    if (tick_count > (UINT64_MAX - ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES) /
                         ODM_MUSIC_TICK_WIRE_BYTES) {
        return ODM_STATUS_OVERFLOW;
    }
    ticks_bytes = tick_count * ODM_MUSIC_TICK_WIRE_BYTES;
    payload = ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES + ticks_bytes;
    if (payload > UINT64_MAX - ODM_WIRE_RECORD_HEADER_BYTES) {
        return ODM_STATUS_OVERFLOW;
    }
    if (payload > ODM_SHA256_MAX_MESSAGE_BYTES ||
        payload + ODM_WIRE_RECORD_HEADER_BYTES > ODM_SHA256_MAX_MESSAGE_BYTES ||
        !music_map_u64_fits_size(payload + ODM_WIRE_RECORD_HEADER_BYTES)) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_payload = payload;
    *out_record = payload + ODM_WIRE_RECORD_HEADER_BYTES;
    return ODM_STATUS_OK;
}

static odm_status music_map_expected_center(uint64_t tick_index,
                                            uint64_t *out_center) {
    if (!out_center) return ODM_STATUS_INVALID_ARGUMENT;
    if (tick_index > UINT64_MAX / ODM_MUSIC_TICK_SAMPLES) {
        return ODM_STATUS_OVERFLOW;
    }
    *out_center = tick_index * ODM_MUSIC_TICK_SAMPLES;
    return ODM_STATUS_OK;
}

static odm_status music_map_validate_plan_identity(
    const odm_music_analysis_plan *plan) {
    uint8_t policy[ODM_MUSIC_POLICY_BYTES];
    uint64_t required = 0u;
    odm_sha256_digest digest;
    odm_status status;
    if (!plan) return ODM_STATUS_INVALID_ARGUMENT;
    if (plan->version != ODM_MUSIC_ANALYSIS_VERSION ||
        plan->sample_rate != ODM_MUSIC_SAMPLE_RATE ||
        plan->tick_rate != ODM_MUSIC_TICK_RATE ||
        plan->tick_samples != ODM_MUSIC_TICK_SAMPLES ||
        plan->window_samples != ODM_MUSIC_WINDOW_SAMPLES ||
        plan->fft_bins != ODM_MUSIC_FFT_BINS || plan->feature_schema != 1u ||
        plan->reserved != 0u || plan->reserved2 != 0u) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    status = odm_music_policy_bytes(plan, policy, sizeof(policy), &required);
    if (status != ODM_STATUS_OK) return status;
    if (required != sizeof(policy)) return ODM_STATUS_INVARIANT_BROKEN;
    status = odm_sha256(policy, sizeof(policy), &digest);
    if (status != ODM_STATUS_OK) return status;
    return odm_sha256_equal(&digest, &plan->policy_sha256)
               ? ODM_STATUS_OK
               : ODM_STATUS_INTEGRITY_ERROR;
}

static odm_status music_map_validate_tick(const odm_music_analysis_tick *tick,
                                          uint64_t expected_index) {
    uint64_t expected_center;
    unsigned index;
    odm_status status;
    if (!tick) return ODM_STATUS_INVALID_ARGUMENT;
    status = music_map_expected_center(expected_index, &expected_center);
    if (status != ODM_STATUS_OK) return status;
    if (tick->tick_index != expected_index || tick->center_sample != expected_center) {
        return ODM_STATUS_INVALID_DATA;
    }
    if (tick->rms_left_q31 < 0 || tick->rms_right_q31 < 0 ||
        tick->rms_mid_q31 < 0 || tick->rms_side_q31 < 0 ||
        tick->silence > 1u || tick->stereo_width_q31 > (uint32_t)INT32_MAX ||
        tick->spectral_centroid_bin_q16_16 >
            (ODM_MUSIC_FFT_BINS - 1u) * UINT32_C(65536)) {
        return ODM_STATUS_INVALID_DATA;
    }
    for (index = 0u; index < 7u; index++) {
        if (tick->reserved[index] != 0u) return ODM_STATUS_INVALID_DATA;
    }
    return ODM_STATUS_OK;
}

static odm_status music_map_write_tick(odm_wire_writer *writer,
                                       const odm_music_analysis_tick *tick) {
    unsigned index;
    odm_status status;
#define MAP_WRITE(call_) do { status = (call_); if (status != ODM_STATUS_OK) return status; } while (0)
    MAP_WRITE(odm_wire_write_u64(writer, tick->tick_index));
    MAP_WRITE(odm_wire_write_u64(writer, tick->center_sample));
    MAP_WRITE(odm_wire_write_i32(writer, tick->rms_left_q31));
    MAP_WRITE(odm_wire_write_i32(writer, tick->rms_right_q31));
    MAP_WRITE(odm_wire_write_i32(writer, tick->rms_mid_q31));
    MAP_WRITE(odm_wire_write_i32(writer, tick->rms_side_q31));
    for (index = 0u; index < ODM_MUSIC_BAND_COUNT; index++) {
        MAP_WRITE(odm_wire_write_u32(writer, tick->band_energy_q31[index]));
    }
    MAP_WRITE(odm_wire_write_u32(writer, tick->total_energy_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->envelope_short_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->envelope_medium_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->envelope_long_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->spectral_flux_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->spectral_centroid_bin_q16_16));
    MAP_WRITE(odm_wire_write_i64(writer, tick->energy_delta_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->silence));
    MAP_WRITE(odm_wire_write_i32(writer, tick->stereo_balance_q31));
    MAP_WRITE(odm_wire_write_u32(writer, tick->stereo_width_q31));
    for (index = 0u; index < 7u; index++) {
        MAP_WRITE(odm_wire_write_u32(writer, tick->reserved[index]));
    }
#undef MAP_WRITE
    return ODM_STATUS_OK;
}

static odm_status music_map_read_tick(odm_wire_reader *reader,
                                      odm_music_analysis_tick *out_tick) {
    odm_music_analysis_tick tick;
    unsigned index;
    odm_status status;
    if (!reader || !out_tick) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&tick, 0, sizeof(tick));
#define MAP_READ(call_) do { status = (call_); if (status != ODM_STATUS_OK) return status; } while (0)
    MAP_READ(odm_wire_read_u64(reader, &tick.tick_index));
    MAP_READ(odm_wire_read_u64(reader, &tick.center_sample));
    MAP_READ(odm_wire_read_i32(reader, &tick.rms_left_q31));
    MAP_READ(odm_wire_read_i32(reader, &tick.rms_right_q31));
    MAP_READ(odm_wire_read_i32(reader, &tick.rms_mid_q31));
    MAP_READ(odm_wire_read_i32(reader, &tick.rms_side_q31));
    for (index = 0u; index < ODM_MUSIC_BAND_COUNT; index++) {
        MAP_READ(odm_wire_read_u32(reader, &tick.band_energy_q31[index]));
    }
    MAP_READ(odm_wire_read_u32(reader, &tick.total_energy_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.envelope_short_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.envelope_medium_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.envelope_long_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.spectral_flux_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.spectral_centroid_bin_q16_16));
    MAP_READ(odm_wire_read_i64(reader, &tick.energy_delta_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.silence));
    MAP_READ(odm_wire_read_i32(reader, &tick.stereo_balance_q31));
    MAP_READ(odm_wire_read_u32(reader, &tick.stereo_width_q31));
    for (index = 0u; index < 7u; index++) {
        MAP_READ(odm_wire_read_u32(reader, &tick.reserved[index]));
    }
#undef MAP_READ
    *out_tick = tick;
    return ODM_STATUS_OK;
}

odm_status odm_music_map_record_write(
    const odm_music_analysis_plan *plan, uint64_t canonical_frames,
    const odm_sha256_digest *canonical_pcm_sha256,
    const odm_music_analysis_tick *ticks, uint64_t tick_count,
    uint8_t *buffer, uint64_t capacity, uint64_t *out_required,
    odm_sha256_digest *out_record_sha256) {
    uint64_t expected_ticks;
    uint64_t payload_bytes;
    uint64_t record_bytes;
    uint64_t encoded;
    uint64_t index;
    uint8_t *payload;
    odm_wire_writer writer = ODM_WIRE_WRITER_INITIALIZER;
    odm_status status;
    if (!plan || !canonical_pcm_sha256 || !out_required ||
        (tick_count != 0u && !ticks)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = music_map_validate_plan_identity(plan);
    if (status != ODM_STATUS_OK) return status;
    status = odm_music_tick_count(canonical_frames, &expected_ticks);
    if (status != ODM_STATUS_OK) return status;
    if (expected_ticks != tick_count) return ODM_STATUS_INVALID_DATA;
    for (index = 0u; index < tick_count; index++) {
        status = music_map_validate_tick(&ticks[index], index);
        if (status != ODM_STATUS_OK) return status;
    }
    status = music_map_payload_bytes(tick_count, &payload_bytes, &record_bytes);
    if (status != ODM_STATUS_OK) return status;
    *out_required = record_bytes;
    if (!buffer || capacity < record_bytes) return ODM_STATUS_BUFFER_TOO_SMALL;
    payload = buffer + ODM_WIRE_RECORD_HEADER_BYTES;
    status = odm_wire_writer_init(&writer, payload, payload_bytes);
    if (status != ODM_STATUS_OK) return status;
#define MAP_WRITE_HEADER(call_) do { status = (call_); if (status != ODM_STATUS_OK) return status; } while (0)
    MAP_WRITE_HEADER(odm_wire_write_bytes(&writer, MUSIC_MAP_PAYLOAD_MAGIC,
                                          MUSIC_MAP_PAYLOAD_MAGIC_BYTES));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, ODM_MUSIC_MAP_SCHEMA_MAJOR));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, ODM_MUSIC_MAP_SCHEMA_MINOR));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, plan->sample_rate));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, plan->tick_rate));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, plan->tick_samples));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, plan->window_samples));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, plan->fft_bins));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, ODM_MUSIC_BAND_COUNT));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, ODM_MUSIC_TICK_WIRE_BYTES));
    MAP_WRITE_HEADER(odm_wire_write_u32(&writer, 0u));
    MAP_WRITE_HEADER(odm_wire_write_u64(&writer, tick_count));
    MAP_WRITE_HEADER(odm_wire_write_u64(&writer, canonical_frames));
    MAP_WRITE_HEADER(odm_wire_write_bytes(&writer, canonical_pcm_sha256->bytes,
                                          ODM_SHA256_DIGEST_BYTES));
    MAP_WRITE_HEADER(odm_wire_write_bytes(&writer, plan->policy_sha256.bytes,
                                          ODM_SHA256_DIGEST_BYTES));
#undef MAP_WRITE_HEADER
    for (index = 0u; index < tick_count; index++) {
        status = music_map_write_tick(&writer, &ticks[index]);
        if (status != ODM_STATUS_OK) return status;
    }
    status = odm_wire_writer_finish(&writer, &encoded);
    if (status != ODM_STATUS_OK) return status;
    if (encoded != payload_bytes) return ODM_STATUS_INVARIANT_BROKEN;
    return odm_wire_record_write(ODM_MUSIC_ANALYSIS_BIN_KIND,
                                 ODM_MUSIC_MAP_SCHEMA_MAJOR,
                                 ODM_MUSIC_MAP_SCHEMA_MINOR,
                                 payload, payload_bytes, buffer, capacity,
                                 out_required, out_record_sha256);
}

static odm_status music_map_read_header(odm_wire_reader *reader,
                                        odm_music_map_info *out_info) {
    uint8_t magic[MUSIC_MAP_PAYLOAD_MAGIC_BYTES];
    uint32_t schema_major;
    uint32_t schema_minor;
    uint32_t sample_rate;
    uint32_t tick_rate;
    uint32_t tick_samples;
    uint32_t window_samples;
    uint32_t fft_bins;
    uint32_t band_count;
    uint32_t tick_bytes;
    uint32_t flags;
    odm_music_map_info info;
    odm_status status;
    memset(&info, 0, sizeof(info));
#define MAP_READ_HEADER(call_) do { status = (call_); if (status != ODM_STATUS_OK) return status; } while (0)
    MAP_READ_HEADER(odm_wire_read_bytes(reader, magic, sizeof(magic)));
    if (memcmp(magic, MUSIC_MAP_PAYLOAD_MAGIC, sizeof(magic)) != 0) {
        return ODM_STATUS_INVALID_DATA;
    }
    MAP_READ_HEADER(odm_wire_read_u32(reader, &schema_major));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &schema_minor));
    if (schema_major != ODM_MUSIC_MAP_SCHEMA_MAJOR ||
        schema_minor != ODM_MUSIC_MAP_SCHEMA_MINOR) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    MAP_READ_HEADER(odm_wire_read_u32(reader, &sample_rate));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &tick_rate));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &tick_samples));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &window_samples));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &fft_bins));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &band_count));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &tick_bytes));
    MAP_READ_HEADER(odm_wire_read_u32(reader, &flags));
    if (sample_rate != ODM_MUSIC_SAMPLE_RATE || tick_rate != ODM_MUSIC_TICK_RATE ||
        tick_samples != ODM_MUSIC_TICK_SAMPLES ||
        window_samples != ODM_MUSIC_WINDOW_SAMPLES ||
        fft_bins != ODM_MUSIC_FFT_BINS || band_count != ODM_MUSIC_BAND_COUNT ||
        tick_bytes != ODM_MUSIC_TICK_WIRE_BYTES || flags != 0u) {
        return ODM_STATUS_INVALID_DATA;
    }
    MAP_READ_HEADER(odm_wire_read_u64(reader, &info.tick_count));
    MAP_READ_HEADER(odm_wire_read_u64(reader, &info.canonical_frames));
    MAP_READ_HEADER(odm_wire_read_bytes(reader, info.canonical_pcm_sha256.bytes,
                                        ODM_SHA256_DIGEST_BYTES));
    MAP_READ_HEADER(odm_wire_read_bytes(reader, info.policy_sha256.bytes,
                                        ODM_SHA256_DIGEST_BYTES));
#undef MAP_READ_HEADER
    info.schema_version_major = schema_major;
    info.schema_version_minor = schema_minor;
    info.feature_schema = 1u;
    info.flags = 0u;
    *out_info = info;
    return ODM_STATUS_OK;
}


odm_status odm_music_map_record_validate(
    const uint8_t *record, uint64_t record_bytes,
    const odm_sha256_digest *expected_policy_sha256,
    odm_music_map_info *out_info, uint64_t *out_required_ticks) {
    odm_wire_record_info record_info;
    odm_music_map_info info;
    odm_music_analysis_tick tick;
    odm_sha256_digest record_sha;
    odm_wire_reader reader = ODM_WIRE_READER_INITIALIZER;
    uint64_t payload_offset;
    uint64_t expected_ticks;
    uint64_t expected_payload;
    uint64_t expected_record;
    uint64_t index;
    odm_status status;
    if (!record || !out_info || !out_required_ticks) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_wire_record_read(record, record_bytes, &record_info,
                                  &payload_offset, &record_sha);
    if (status != ODM_STATUS_OK) return status;
    if (record_info.kind != ODM_MUSIC_ANALYSIS_BIN_KIND) return ODM_STATUS_INVALID_DATA;
    if (record_info.schema_version_major != ODM_MUSIC_MAP_SCHEMA_MAJOR ||
        record_info.schema_version_minor != ODM_MUSIC_MAP_SCHEMA_MINOR) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    status = odm_wire_reader_init(&reader, record + payload_offset,
                                  record_info.payload_bytes);
    if (status != ODM_STATUS_OK) return status;
    status = music_map_read_header(&reader, &info);
    if (status != ODM_STATUS_OK) return status;
    if (expected_policy_sha256 &&
        !odm_sha256_equal(expected_policy_sha256, &info.policy_sha256)) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    status = odm_music_tick_count(info.canonical_frames, &expected_ticks);
    if (status != ODM_STATUS_OK) return status;
    if (expected_ticks != info.tick_count) return ODM_STATUS_INVALID_DATA;
    status = music_map_payload_bytes(info.tick_count, &expected_payload,
                                     &expected_record);
    if (status != ODM_STATUS_OK) return status;
    if (expected_payload != record_info.payload_bytes || expected_record != record_bytes) {
        return ODM_STATUS_INVALID_DATA;
    }
    for (index = 0u; index < info.tick_count; ++index) {
        status = music_map_read_tick(&reader, &tick);
        if (status != ODM_STATUS_OK) return status;
        status = music_map_validate_tick(&tick, index);
        if (status != ODM_STATUS_OK) return status;
    }
    status = odm_wire_reader_finish(&reader);
    if (status != ODM_STATUS_OK) return status;
    info.record_sha256 = record_sha;
    *out_required_ticks = info.tick_count;
    *out_info = info;
    return ODM_STATUS_OK;
}


odm_status odm_music_map_view_open(
    const uint8_t *record, uint64_t record_bytes,
    const odm_sha256_digest *expected_policy_sha256,
    odm_music_map_view *out_view) {
    odm_music_map_info info;
    odm_music_map_view view;
    uint64_t ticks = 0u;
    odm_status status;
    if (!record || !out_view) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_music_map_record_validate(record, record_bytes,
                                           expected_policy_sha256,
                                           &info, &ticks);
    if (status != ODM_STATUS_OK) return status;
    memset(&view, 0, sizeof(view));
    view.record = record;
    view.record_bytes = record_bytes;
    view.tick_bytes = record + ODM_WIRE_RECORD_HEADER_BYTES +
                      ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES;
    view.tick_count = ticks;
    view.info = info;
    *out_view = view;
    return ODM_STATUS_OK;
}

odm_status odm_music_map_view_read_tick(
    const odm_music_map_view *view, uint64_t tick_index,
    odm_music_analysis_tick *out_tick) {
    odm_wire_reader reader = ODM_WIRE_READER_INITIALIZER;
    odm_music_analysis_tick tick;
    uint64_t byte_offset;
    odm_status status;
    if (!view || !out_tick || !view->record || !view->tick_bytes ||
        view->record_bytes < ODM_WIRE_RECORD_HEADER_BYTES +
                             ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES ||
        tick_index >= view->tick_count ||
        view->tick_count != view->info.tick_count) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    if (tick_index > UINT64_MAX / ODM_MUSIC_TICK_WIRE_BYTES)
        return ODM_STATUS_OVERFLOW;
    byte_offset = tick_index * ODM_MUSIC_TICK_WIRE_BYTES;
    status = odm_wire_reader_init(&reader, view->tick_bytes + byte_offset,
                                  ODM_MUSIC_TICK_WIRE_BYTES);
    if (status != ODM_STATUS_OK) return status;
    status = music_map_read_tick(&reader, &tick);
    if (status != ODM_STATUS_OK) return status;
    status = odm_wire_reader_finish(&reader);
    if (status != ODM_STATUS_OK) return status;
    status = music_map_validate_tick(&tick, tick_index);
    if (status != ODM_STATUS_OK) return status;
    *out_tick = tick;
    return ODM_STATUS_OK;
}

odm_status odm_music_map_record_read(
    const uint8_t *record, uint64_t record_bytes,
    const odm_sha256_digest *expected_policy_sha256,
    odm_music_map_info *out_info, odm_music_analysis_tick *ticks,
    uint64_t tick_capacity, uint64_t *out_required_ticks) {
    odm_wire_record_info record_info;
    odm_music_map_info info;
    odm_music_analysis_tick tick;
    odm_sha256_digest record_sha;
    odm_wire_reader reader = ODM_WIRE_READER_INITIALIZER;
    uint64_t payload_offset;
    uint64_t expected_ticks;
    uint64_t expected_payload;
    uint64_t expected_record;
    uint64_t index;
    odm_status status;
    if (!record || !out_info || !out_required_ticks ||
        (tick_capacity != 0u && !ticks)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_wire_record_read(record, record_bytes, &record_info,
                                  &payload_offset, &record_sha);
    if (status != ODM_STATUS_OK) return status;
    if (record_info.kind != ODM_MUSIC_ANALYSIS_BIN_KIND) return ODM_STATUS_INVALID_DATA;
    if (record_info.schema_version_major != ODM_MUSIC_MAP_SCHEMA_MAJOR ||
        record_info.schema_version_minor != ODM_MUSIC_MAP_SCHEMA_MINOR) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    status = odm_wire_reader_init(&reader, record + payload_offset,
                                  record_info.payload_bytes);
    if (status != ODM_STATUS_OK) return status;
    status = music_map_read_header(&reader, &info);
    if (status != ODM_STATUS_OK) return status;
    if (expected_policy_sha256 &&
        !odm_sha256_equal(expected_policy_sha256, &info.policy_sha256)) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    status = odm_music_tick_count(info.canonical_frames, &expected_ticks);
    if (status != ODM_STATUS_OK) return status;
    if (expected_ticks != info.tick_count) return ODM_STATUS_INVALID_DATA;
    status = music_map_payload_bytes(info.tick_count, &expected_payload,
                                     &expected_record);
    if (status != ODM_STATUS_OK) return status;
    if (expected_payload != record_info.payload_bytes || expected_record != record_bytes) {
        return ODM_STATUS_INVALID_DATA;
    }
    *out_required_ticks = info.tick_count;
    for (index = 0u; index < info.tick_count; index++) {
        status = music_map_read_tick(&reader, &tick);
        if (status != ODM_STATUS_OK) return status;
        status = music_map_validate_tick(&tick, index);
        if (status != ODM_STATUS_OK) return status;
    }
    status = odm_wire_reader_finish(&reader);
    if (status != ODM_STATUS_OK) return status;
    if (tick_capacity < info.tick_count) return ODM_STATUS_BUFFER_TOO_SMALL;

    status = odm_wire_reader_init(&reader,
                                  record + payload_offset +
                                      ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES,
                                  info.tick_count * ODM_MUSIC_TICK_WIRE_BYTES);
    if (status != ODM_STATUS_OK) return status;
    for (index = 0u; index < info.tick_count; index++) {
        status = music_map_read_tick(&reader, &ticks[index]);
        if (status != ODM_STATUS_OK) return status;
    }
    status = odm_wire_reader_finish(&reader);
    if (status != ODM_STATUS_OK) return status;
    info.record_sha256 = record_sha;
    *out_info = info;
    return ODM_STATUS_OK;
}

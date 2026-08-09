#include "media_internal.h"
#include "odm_wire.h"

#include <string.h>

odm_status odm_media_facts_validate(const odm_media_facts *f) {
    if (!f) return ODM_STATUS_INVALID_ARGUMENT;
    if (f->reserved0 != 0u || f->reserved1 != 0u) return ODM_STATUS_INVALID_DATA;
    if (f->media_kind == ODM_MEDIA_KIND_AUDIO) {
        if (f->channels == 0u || f->channels > ODM_MEDIA_MAX_AUDIO_CHANNELS ||
            f->sample_rate == 0u || f->sample_rate > ODM_MEDIA_MAX_SAMPLE_RATE ||
            f->source_frames == 0u || f->canonical_48k_frames == 0u ||
            f->width != 0u || f->height != 0u) return ODM_STATUS_INVALID_DATA;
    } else if (f->media_kind == ODM_MEDIA_KIND_IMAGE) {
        uint64_t pixels;
        if (f->width == 0u || f->height == 0u ||
            f->width > ODM_MEDIA_MAX_IMAGE_DIMENSION ||
            f->height > ODM_MEDIA_MAX_IMAGE_DIMENSION ||
            f->pixel_channels != 4u || f->channels != 0u || f->sample_rate != 0u)
            return ODM_STATUS_INVALID_DATA;
        if (odm_media_u64_mul(f->width, f->height, &pixels) != ODM_STATUS_OK ||
            pixels > ODM_MEDIA_MAX_IMAGE_PIXELS) return ODM_STATUS_INVALID_DATA;
    } else {
        return ODM_STATUS_INVALID_DATA;
    }
    return ODM_STATUS_OK;
}

static odm_status encode_payload(const odm_media_facts *f, uint8_t *p) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status s = odm_wire_writer_init(&w, p, ODM_MEDIA_FACTS_PAYLOAD_BYTES);
    if (s != ODM_STATUS_OK) return s;
#define W32(x) do { s = odm_wire_write_u32(&w, (x)); if (s != ODM_STATUS_OK) return s; } while (0)
#define W64(x) do { s = odm_wire_write_u64(&w, (x)); if (s != ODM_STATUS_OK) return s; } while (0)
    W32(f->media_kind); W32(f->container); W32(f->codec); W32(f->support);
    W32(f->channels); W32(f->sample_rate); W32(f->bits_per_sample); W32(0u);
    W64(f->source_frames); W64(f->canonical_48k_frames);
    W32(f->width); W32(f->height); W32(f->pixel_channels); W32(0u);
    W64(f->decoded_bytes);
    s = odm_wire_write_bytes(&w, f->source_sha256.bytes, ODM_SHA256_DIGEST_BYTES);
    if (s != ODM_STATUS_OK) return s;
    s = odm_wire_write_bytes(&w, f->decoded_sha256.bytes, ODM_SHA256_DIGEST_BYTES);
    if (s != ODM_STATUS_OK) return s;
    while (w.offset < ODM_MEDIA_FACTS_PAYLOAD_BYTES) {
        s = odm_wire_write_u8(&w, 0u); if (s != ODM_STATUS_OK) return s;
    }
    { uint64_t final_size = 0u;
      s = odm_wire_writer_finish(&w, &final_size);
      if (s != ODM_STATUS_OK) return s;
      return final_size == ODM_MEDIA_FACTS_PAYLOAD_BYTES ? ODM_STATUS_OK : ODM_STATUS_INVARIANT_BROKEN; }
#undef W32
#undef W64
}

odm_status odm_media_facts_write_record(const odm_media_facts *facts,
                                        uint8_t *buffer, uint64_t capacity,
                                        uint64_t *out_required,
                                        odm_sha256_digest *out_record_sha256) {
    uint8_t payload[ODM_MEDIA_FACTS_PAYLOAD_BYTES];
    odm_status s;
    if (!out_required || !facts) return ODM_STATUS_INVALID_ARGUMENT;
    s = odm_media_facts_validate(facts); if (s != ODM_STATUS_OK) return s;
    s = encode_payload(facts, payload); if (s != ODM_STATUS_OK) return s;
    return odm_wire_record_write(ODM_MEDIA_FACTS_WIRE_KIND,
                                 ODM_MEDIA_FACTS_SCHEMA_MAJOR,
                                 ODM_MEDIA_FACTS_SCHEMA_MINOR,
                                 payload, sizeof(payload), buffer, capacity,
                                 out_required, out_record_sha256);
}

odm_status odm_media_facts_read_record(const uint8_t *record,
                                       uint64_t record_bytes,
                                       odm_media_facts *out_facts) {
    odm_wire_record_info info;
    uint64_t offset = 0u;
    odm_wire_reader r = ODM_WIRE_READER_INITIALIZER;
    odm_media_facts t;
    uint8_t reserved[24];
    odm_status s;
    if (!out_facts) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&t, 0, sizeof(t));
    s = odm_wire_record_read(record, record_bytes, &info, &offset, NULL);
    if (s != ODM_STATUS_OK) return s;
    if (info.kind != ODM_MEDIA_FACTS_WIRE_KIND ||
        info.schema_version_major != ODM_MEDIA_FACTS_SCHEMA_MAJOR ||
        info.schema_version_minor != ODM_MEDIA_FACTS_SCHEMA_MINOR ||
        info.payload_bytes != ODM_MEDIA_FACTS_PAYLOAD_BYTES) return ODM_STATUS_VERSION_MISMATCH;
    s = odm_wire_reader_init(&r, record + offset, info.payload_bytes); if (s != ODM_STATUS_OK) return s;
#define R32(x) do { s = odm_wire_read_u32(&r, &(x)); if (s != ODM_STATUS_OK) return s; } while (0)
#define R64(x) do { s = odm_wire_read_u64(&r, &(x)); if (s != ODM_STATUS_OK) return s; } while (0)
    R32(t.media_kind); R32(t.container); R32(t.codec); R32(t.support);
    R32(t.channels); R32(t.sample_rate); R32(t.bits_per_sample); R32(t.reserved0);
    R64(t.source_frames); R64(t.canonical_48k_frames);
    R32(t.width); R32(t.height); R32(t.pixel_channels); R32(t.reserved1);
    R64(t.decoded_bytes);
    s = odm_wire_read_bytes(&r, t.source_sha256.bytes, ODM_SHA256_DIGEST_BYTES); if (s != ODM_STATUS_OK) return s;
    s = odm_wire_read_bytes(&r, t.decoded_sha256.bytes, ODM_SHA256_DIGEST_BYTES); if (s != ODM_STATUS_OK) return s;
    s = odm_wire_read_bytes(&r, reserved, sizeof(reserved)); if (s != ODM_STATUS_OK) return s;
    for (size_t i = 0u; i < sizeof(reserved); ++i) if (reserved[i] != 0u) return ODM_STATUS_INVALID_DATA;
    s = odm_wire_reader_finish(&r); if (s != ODM_STATUS_OK) return s;
    s = odm_media_facts_validate(&t); if (s != ODM_STATUS_OK) return s;
    *out_facts = t;
    return ODM_STATUS_OK;
#undef R32
#undef R64
}

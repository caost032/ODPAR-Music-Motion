#include "media_internal.h"

#include <string.h>

typedef struct {
    uint16_t tag;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
    uint64_t data_offset;
    uint64_t data_bytes;
} wav_desc;

static int guid_matches(const uint8_t *p, uint32_t tag) {
    static const uint8_t tail[12] = {0u,0u,0x10u,0u,0x80u,0u,0u,0xaau,0u,0x38u,0x9bu,0x71u};
    return p[0] == (uint8_t)tag && p[1] == 0u && p[2] == 0u && p[3] == 0u &&
           memcmp(p + 4u, tail, sizeof(tail)) == 0;
}

static odm_status wav_parse(const uint8_t *b, uint64_t n, wav_desc *out) {
    uint64_t declared;
    uint64_t pos = 12u;
    int have_fmt = 0;
    int have_data = 0;
    wav_desc d;
    memset(&d, 0, sizeof(d));
    if (!b || !out) return ODM_STATUS_INVALID_ARGUMENT;
    if (n < 12u || memcmp(b, "RIFF", 4u) != 0 || memcmp(b + 8u, "WAVE", 4u) != 0)
        return ODM_STATUS_INVALID_DATA;
    declared = (uint64_t)odm_media_read_le32(b + 4u) + 8u;
    if (declared != n) return ODM_STATUS_INVALID_DATA;
    while (pos < n) {
        uint32_t csize;
        uint64_t data_pos;
        uint64_t next;
        if (n - pos < 8u) return ODM_STATUS_INVALID_DATA;
        csize = odm_media_read_le32(b + pos + 4u);
        data_pos = pos + 8u;
        if ((uint64_t)csize > n - data_pos) return ODM_STATUS_INVALID_DATA;
        if (memcmp(b + pos, "fmt ", 4u) == 0) {
            uint16_t raw_tag;
            if (have_fmt || csize < 16u) return ODM_STATUS_INVALID_DATA;
            raw_tag = odm_media_read_le16(b + data_pos);
            d.channels = odm_media_read_le16(b + data_pos + 2u);
            d.sample_rate = odm_media_read_le32(b + data_pos + 4u);
            d.byte_rate = odm_media_read_le32(b + data_pos + 8u);
            d.block_align = odm_media_read_le16(b + data_pos + 12u);
            d.bits = odm_media_read_le16(b + data_pos + 14u);
            d.tag = raw_tag;
            if (raw_tag == UINT16_C(0xfffe)) {
                uint16_t cb;
                if (csize < 40u) return ODM_STATUS_INVALID_DATA;
                cb = odm_media_read_le16(b + data_pos + 16u);
                if (cb < 22u) return ODM_STATUS_INVALID_DATA;
                if (guid_matches(b + data_pos + 24u, 1u)) d.tag = 1u;
                else if (guid_matches(b + data_pos + 24u, 3u)) d.tag = 3u;
                else return ODM_STATUS_UNSUPPORTED;
            }
            have_fmt = 1;
        } else if (memcmp(b + pos, "data", 4u) == 0) {
            if (have_data) return ODM_STATUS_INVALID_DATA;
            d.data_offset = data_pos;
            d.data_bytes = csize;
            have_data = 1;
        }
        next = data_pos + (uint64_t)csize;
        if ((csize & 1u) != 0u) {
            if (next >= n) return ODM_STATUS_INVALID_DATA;
            next++;
        }
        if (next <= pos) return ODM_STATUS_OVERFLOW;
        pos = next;
    }
    if (!have_fmt || !have_data || pos != n) return ODM_STATUS_INVALID_DATA;
    if (d.channels == 0u || d.channels > ODM_MEDIA_MAX_AUDIO_CHANNELS ||
        d.sample_rate == 0u || d.sample_rate > ODM_MEDIA_MAX_SAMPLE_RATE ||
        (d.tag != 1u && d.tag != 3u)) return ODM_STATUS_UNSUPPORTED;
    if (d.bits == 0u || (d.bits & 7u) != 0u) return ODM_STATUS_INVALID_DATA;
    if (d.tag == 1u && !(d.bits == 8u || d.bits == 16u || d.bits == 24u || d.bits == 32u))
        return ODM_STATUS_UNSUPPORTED;
    if (d.tag == 3u && !(d.bits == 32u || d.bits == 64u)) return ODM_STATUS_UNSUPPORTED;
    {
        uint64_t expected_align = (uint64_t)d.channels * (uint64_t)(d.bits / 8u);
        uint64_t expected_rate = expected_align * (uint64_t)d.sample_rate;
        if (expected_align > UINT16_MAX || expected_rate > UINT32_MAX ||
            d.block_align != (uint16_t)expected_align || d.byte_rate != (uint32_t)expected_rate)
            return ODM_STATUS_INVALID_DATA;
    }
    if (d.block_align == 0u || d.data_bytes == 0u || d.data_bytes % d.block_align != 0u)
        return ODM_STATUS_INVALID_DATA;
    *out = d;
    return ODM_STATUS_OK;
}

odm_status odm_wav_decode_q31(const uint8_t *bytes, uint64_t size,
                              odm_pcm_stereo_q31 *frames,
                              uint64_t frame_capacity,
                              uint64_t *out_required_frames,
                              odm_media_facts *out_facts) {
    wav_desc d;
    uint64_t count;
    uint64_t canonical;
    uint64_t decoded_bytes;
    uint64_t i;
    uint32_t bytes_per_sample;
    odm_media_facts facts;
    odm_sha256_context h = ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status s;
    if (!out_required_frames || !out_facts || (!bytes && size != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required_frames = 0u;
    s = wav_parse(bytes, size, &d); if (s != ODM_STATUS_OK) return s;
    count = d.data_bytes / d.block_align;
    *out_required_frames = count;
    if (!frames || frame_capacity < count) return ODM_STATUS_BUFFER_TOO_SMALL;
    s = odm_media_ceil_mul_div_u64(count, 48000u, d.sample_rate, &canonical);
    if (s != ODM_STATUS_OK) return s;
    s = odm_media_u64_mul(count, sizeof(odm_pcm_stereo_q31), &decoded_bytes);
    if (s != ODM_STATUS_OK) return s;
    bytes_per_sample = d.bits / 8u;
    for (i = 0u; i < count; ++i) {
        const uint8_t *p = bytes + d.data_offset + i * d.block_align;
        int32_t l;
        int32_t r;
        s = odm_pcm_decode_sample(p, d.bits, d.tag == 3u, &l); if (s != ODM_STATUS_OK) return s;
        if (d.channels == 2u) {
            s = odm_pcm_decode_sample(p + bytes_per_sample, d.bits, d.tag == 3u, &r);
            if (s != ODM_STATUS_OK) return s;
        } else r = l;
        frames[i].left = l; frames[i].right = r;
    }
    memset(&facts, 0, sizeof(facts));
    facts.media_kind = ODM_MEDIA_KIND_AUDIO;
    facts.container = ODM_MEDIA_CONTAINER_WAV;
    facts.codec = d.tag == 1u ? ODM_MEDIA_CODEC_PCM_INT : ODM_MEDIA_CODEC_PCM_FLOAT;
    facts.support = ODM_MEDIA_SUPPORT_SUPPORTED;
    facts.channels = d.channels;
    facts.sample_rate = d.sample_rate;
    facts.bits_per_sample = d.bits;
    facts.source_frames = count;
    facts.canonical_48k_frames = canonical;
    facts.decoded_bytes = decoded_bytes;
    s = odm_sha256_init(&h); if (s != ODM_STATUS_OK) return s;
    s = odm_sha256_update(&h, bytes, size); if (s != ODM_STATUS_OK) return s;
    s = odm_sha256_final(&h, &facts.source_sha256); if (s != ODM_STATUS_OK) return s;
    s = odm_media_hash_q31_frames(frames, count, &facts.decoded_sha256); if (s != ODM_STATUS_OK) return s;
    s = odm_media_facts_validate(&facts); if (s != ODM_STATUS_OK) return s;
    *out_facts = facts;
    return ODM_STATUS_OK;
}

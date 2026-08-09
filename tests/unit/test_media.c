#include "test_harness.h"
#include "odm_media.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint64_t make_wav16_stereo(uint8_t *b, uint32_t rate) {
    const int16_t samples[6] = {INT16_MIN, INT16_MAX, 0, 16384, -1, 1};
    uint64_t i;
    memcpy(b, "RIFF", 4u); put_le32(b + 4u, 48u); memcpy(b + 8u, "WAVE", 4u);
    memcpy(b + 12u, "fmt ", 4u); put_le32(b + 16u, 16u);
    put_le16(b + 20u, 1u); put_le16(b + 22u, 2u); put_le32(b + 24u, rate);
    put_le32(b + 28u, rate * 4u); put_le16(b + 32u, 4u); put_le16(b + 34u, 16u);
    memcpy(b + 36u, "data", 4u); put_le32(b + 40u, 12u);
    for (i = 0u; i < 6u; ++i) put_le16(b + 44u + i * 2u, (uint16_t)samples[i]);
    return 56u;
}

static uint64_t make_wav_float_mono(uint8_t *b) {
    const uint32_t bits[2] = {UINT32_C(0xbf800000), UINT32_C(0x3f000000)};
    uint64_t i;
    memcpy(b, "RIFF", 4u); put_le32(b + 4u, 44u); memcpy(b + 8u, "WAVE", 4u);
    memcpy(b + 12u, "fmt ", 4u); put_le32(b + 16u, 16u);
    put_le16(b + 20u, 3u); put_le16(b + 22u, 1u); put_le32(b + 24u, 48000u);
    put_le32(b + 28u, 192000u); put_le16(b + 32u, 4u); put_le16(b + 34u, 32u);
    memcpy(b + 36u, "data", 4u); put_le32(b + 40u, 8u);
    for (i = 0u; i < 2u; ++i) put_le32(b + 44u + i * 4u, bits[i]);
    return 52u;
}

static uint64_t make_wav_extensible_mono(uint8_t *b) {
    static const uint8_t pcm_guid[16] = {1u,0u,0u,0u,0u,0u,0x10u,0u,0x80u,0u,0u,0xaau,0u,0x38u,0x9bu,0x71u};
    memcpy(b, "RIFF", 4u); put_le32(b + 4u, 62u); memcpy(b + 8u, "WAVE", 4u);
    memcpy(b + 12u, "fmt ", 4u); put_le32(b + 16u, 40u);
    put_le16(b + 20u, UINT16_C(0xfffe)); put_le16(b + 22u, 1u); put_le32(b + 24u, 48000u);
    put_le32(b + 28u, 96000u); put_le16(b + 32u, 2u); put_le16(b + 34u, 16u);
    put_le16(b + 36u, 22u); put_le16(b + 38u, 16u); put_le32(b + 40u, 4u);
    memcpy(b + 44u, pcm_guid, sizeof(pcm_guid));
    memcpy(b + 60u, "data", 4u); put_le32(b + 64u, 2u); put_le16(b + 68u, UINT16_C(0x4000));
    return 70u;
}

static void test_wav(odm_test_context *c) {
    uint8_t wav[80] = {0};
    odm_media_probe_result probe = {0};
    odm_media_facts facts;
    odm_media_facts decoded;
    odm_pcm_stereo_q31 frames[4];
    uint64_t required = 0u;
    uint64_t rec_required = 0u;
    uint8_t record[256];
    uint64_t n = make_wav16_stereo(wav, 44100u);
    memset(&facts, 0x5a, sizeof(facts));
    ODM_TEST_CHECK(c, odm_media_probe(wav, n, &probe) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, probe.container == ODM_MEDIA_CONTAINER_WAV);
    ODM_TEST_CHECK(c, probe.support == ODM_MEDIA_SUPPORT_SUPPORTED);
    ODM_TEST_CHECK(c, odm_media_decode_audio_q31(wav, n, NULL, 0u, &required, &facts) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(c, required == 3u);
    ODM_TEST_CHECK(c, facts.media_kind == 0u); /* failure publishes no facts */
    ODM_TEST_CHECK(c, odm_media_decode_audio_q31(wav, n, frames, 4u, &required, &facts) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, facts.sample_rate == 44100u && facts.channels == 2u);
    ODM_TEST_CHECK(c, facts.source_frames == 3u);
    ODM_TEST_CHECK(c, facts.canonical_48k_frames == 4u); /* rational ceiling */
    ODM_TEST_CHECK(c, frames[0].left == INT32_MIN && frames[0].right == INT32_C(2147418112));
    ODM_TEST_CHECK(c, frames[1].left == 0 && frames[1].right == INT32_C(1073741824));
    ODM_TEST_CHECK(c, frames[2].left == -INT32_C(65536) && frames[2].right == INT32_C(65536));
    ODM_TEST_CHECK(c, odm_media_facts_write_record(&facts, record, sizeof(record), &rec_required, NULL) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, rec_required == 224u);
    memset(&decoded, 0, sizeof(decoded));
    ODM_TEST_CHECK(c, odm_media_facts_read_record(record, rec_required, &decoded) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, memcmp(&decoded, &facts, sizeof(facts)) == 0);
    record[100] ^= 1u;
    ODM_TEST_CHECK(c, odm_media_facts_read_record(record, rec_required, &decoded) == ODM_STATUS_INTEGRITY_ERROR);
}

static void test_float_and_extensible(odm_test_context *c) {
    uint8_t wav[96] = {0};
    odm_media_facts f;
    odm_pcm_stereo_q31 frames[4];
    uint64_t required = 0u;
    uint64_t n = make_wav_float_mono(wav);
    ODM_TEST_CHECK(c, odm_media_decode_audio_q31(wav, n, frames, 4u, &required, &f) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, frames[0].left == INT32_MIN && frames[0].right == INT32_MIN);
    ODM_TEST_CHECK(c, frames[1].left == INT32_C(1073741824) && frames[1].right == INT32_C(1073741824));
    memset(wav, 0, sizeof(wav));
    n = make_wav_extensible_mono(wav);
    ODM_TEST_CHECK(c, odm_media_decode_audio_q31(wav, n, frames, 4u, &required, &f) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, required == 1u && frames[0].left == INT32_C(1073741824));
}

static void test_dispatch_and_corruption(odm_test_context *c) {
    static const uint8_t flac[8] = {'f','L','a','C',0u,0u,0u,0u};
    uint8_t wav[80] = {0};
    odm_media_probe_result p;
    odm_media_facts f;
    odm_pcm_stereo_q31 frame[4];
    uint64_t required = 0u;
    uint64_t n = make_wav16_stereo(wav, 48000u);
    ODM_TEST_CHECK(c, odm_media_probe(flac, sizeof(flac), &p) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, p.container == ODM_MEDIA_CONTAINER_FLAC && p.support == ODM_MEDIA_SUPPORT_RECOGNIZED);
    ODM_TEST_CHECK(c, odm_media_decode_audio_q31(flac, sizeof(flac), frame, 4u, &required, &f) == ODM_STATUS_UNSUPPORTED);
    wav[4] ^= 1u; /* still a recognized WAV signature, but corrupt RIFF extent */
    ODM_TEST_CHECK(c, odm_media_probe(wav, n, &p) == ODM_STATUS_OK && p.container == ODM_MEDIA_CONTAINER_WAV);
    ODM_TEST_CHECK(c, odm_media_decode_audio_q31(wav, n, frame, 4u, &required, &f) == ODM_STATUS_INVALID_DATA);
}

static void test_png(odm_test_context *c) {
    static const uint8_t png[] = {
        0x89u,0x50u,0x4eu,0x47u,0x0du,0x0au,0x1au,0x0au,0x00u,0x00u,0x00u,0x0du,0x49u,0x48u,0x44u,0x52u,0x00u,0x00u,0x00u,0x01u,0x00u,0x00u,0x00u,0x01u,0x08u,0x06u,0x00u,0x00u,0x00u,0x1fu,0x15u,0xc4u,0x89u,0x00u,0x00u,0x00u,0x0du,0x49u,0x44u,0x41u,0x54u,0x78u,0x9cu,0x63u,0x10u,0x54u,0x32u,0x76u,0x01u,0x00u,0x01u,0x59u,0x00u,0xabu,0x13u,0x2au,0x25u,0xabu,0x00u,0x00u,0x00u,0x00u,0x49u,0x45u,0x4eu,0x44u,0xaeu,0x42u,0x60u,0x82u
    };
    uint8_t bad[sizeof(png)];
    uint8_t rgba[4] = {0};
    uint64_t required = 0u;
    odm_media_facts f;
    odm_media_probe_result p;
    ODM_TEST_CHECK(c, odm_media_probe(png, sizeof(png), &p) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, p.container == ODM_MEDIA_CONTAINER_PNG && p.support == ODM_MEDIA_SUPPORT_SUPPORTED);
    ODM_TEST_CHECK(c, odm_media_decode_image_rgba8(png, sizeof(png), NULL, 0u, &required, &f) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(c, required == 4u);
    ODM_TEST_CHECK(c, odm_media_decode_image_rgba8(png, sizeof(png), rgba, sizeof(rgba), &required, &f) == ODM_STATUS_OK);
    ODM_TEST_CHECK(c, rgba[0] == 0x11u && rgba[1] == 0x22u && rgba[2] == 0x33u && rgba[3] == 0x44u);
    ODM_TEST_CHECK(c, f.width == 1u && f.height == 1u && f.decoded_bytes == 4u);
    memcpy(bad, png, sizeof(png)); bad[31] ^= 1u; /* IHDR CRC */
    ODM_TEST_CHECK(c, odm_media_decode_image_rgba8(bad, sizeof(bad), rgba, sizeof(rgba), &required, &f) == ODM_STATUS_INTEGRITY_ERROR);
}

static void test_duration_math(odm_test_context *c) {
    uint32_t rates[] = {8000u,11025u,16000u,22050u,32000u,44100u,48000u,88200u,96000u,192000u,384000u};
    size_t i;
    ODM_TEST_CHECK(c, odm_media_canonical_48k_frames(1u, 0u, NULL) == ODM_STATUS_INVALID_ARGUMENT);
    for (i = 0u; i < sizeof(rates)/sizeof(rates[0]); ++i) {
        uint64_t n;
        for (n = 0u; n <= 4096u; ++n) {
            uint64_t got = UINT64_MAX;
            uint64_t expected = (n * UINT64_C(48000) + rates[i] - 1u) / rates[i];
            ODM_TEST_CHECK(c, odm_media_canonical_48k_frames(n, rates[i], &got) == ODM_STATUS_OK);
            ODM_TEST_CHECK(c, got == expected);
        }
    }
    {
        uint64_t got = 0u;
        ODM_TEST_CHECK(c, odm_media_canonical_48k_frames(UINT64_MAX, 384000u, &got) == ODM_STATUS_OK);
        ODM_TEST_CHECK(c, got == UINT64_C(2305843009213693952));
        ODM_TEST_CHECK(c, odm_media_canonical_48k_frames(UINT64_MAX, 1u, &got) == ODM_STATUS_OVERFLOW);
        ODM_TEST_CHECK(c, odm_media_canonical_48k_frames(1u, ODM_MEDIA_MAX_SAMPLE_RATE + 1u, &got) == ODM_STATUS_INVALID_ARGUMENT);
    }
}

void odm_test_media(odm_test_context *c) {
    test_wav(c);
    test_float_and_extensible(c);
    test_dispatch_and_corruption(c);
    test_png(c);
    test_duration_math(c);
}

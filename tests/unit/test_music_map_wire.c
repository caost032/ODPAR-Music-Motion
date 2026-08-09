#include "test_harness.h"

#include "odm_media.h"
#include "odm_music_map.h"
#include "odm_wire.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int music_tick_equal(const odm_music_analysis_tick *a,
                            const odm_music_analysis_tick *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

void odm_test_music_map_wire(odm_test_context *context) {
    int32_t *window = (int32_t *)calloc(ODM_MUSIC_WINDOW_SAMPLES, sizeof(*window));
    odm_music_complex_q31 *twiddles = (odm_music_complex_q31 *)calloc(
        ODM_MUSIC_FFT_TWIDDLES, sizeof(*twiddles));
    odm_music_analysis_scratch *scratch =
        (odm_music_analysis_scratch *)calloc(1u, sizeof(*scratch));
    odm_pcm_stereo_q31 pcm[960];
    odm_music_analysis_tick ticks[2];
    odm_music_analysis_tick decoded[2];
    odm_music_analysis_tick sentinels[2];
    odm_music_analysis_state state;
    odm_music_analysis_plan plan;
    odm_music_map_info info;
    odm_sha256_digest pcm_sha;
    odm_sha256_digest record_sha;
    uint8_t *record = NULL;
    uint8_t *hostile = NULL;
    uint64_t required = 0u;
    uint64_t required_ticks = 0u;
    uint64_t payload_offset = 0u;
    odm_wire_record_info wire_info;
    uint64_t i;

    ODM_TEST_CHECK(context, window != NULL);
    ODM_TEST_CHECK(context, twiddles != NULL);
    ODM_TEST_CHECK(context, scratch != NULL);
    if (!window || !twiddles || !scratch) goto cleanup;

    for (i = 0u; i < 960u; i++) {
        int32_t sample = (i & 1u) == 0u ? INT32_C(268435456) : -INT32_C(268435456);
        pcm[i].left = sample;
        pcm[i].right = sample / 2;
    }
    ODM_TEST_CHECK(context,
                   odm_music_analysis_plan_build(window, ODM_MUSIC_WINDOW_SAMPLES,
                                                 twiddles, ODM_MUSIC_FFT_TWIDDLES,
                                                 &plan) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_analysis_state_init(&state) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_analyze_map(&plan, window, twiddles, pcm, 960u,
                                         &state, scratch, ticks, 2u,
                                         &required_ticks, NULL) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required_ticks == 2u);
    ODM_TEST_CHECK(context,
                   odm_sha256("canonical-pcm-test", UINT64_C(18), &pcm_sha) ==
                       ODM_STATUS_OK);

    ODM_TEST_CHECK(context,
                   odm_music_map_record_write(&plan, 960u, &pcm_sha, ticks, 2u,
                                              NULL, 0u, &required,
                                              NULL) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context,
                   required == ODM_WIRE_RECORD_HEADER_BYTES +
                                   ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES +
                                   2u * ODM_MUSIC_TICK_WIRE_BYTES);
    record = (uint8_t *)malloc((size_t)required + 1u);
    hostile = (uint8_t *)malloc((size_t)required + 1u);
    ODM_TEST_CHECK(context, record != NULL);
    ODM_TEST_CHECK(context, hostile != NULL);
    if (!record || !hostile) goto cleanup;
    memset(record, 0xa5, (size_t)required + 1u);
    ODM_TEST_CHECK(context,
                   odm_music_map_record_write(&plan, 960u, &pcm_sha, ticks, 2u,
                                              record, required, &required,
                                              &record_sha) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, record[required] == UINT8_C(0xa5));
    ODM_TEST_CHECK(context,
                   odm_wire_record_read(record, required, &wire_info,
                                        &payload_offset, NULL) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, wire_info.kind == ODM_MUSIC_ANALYSIS_BIN_KIND);
    ODM_TEST_CHECK(context, wire_info.payload_bytes ==
                               ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES +
                                   2u * ODM_MUSIC_TICK_WIRE_BYTES);
    ODM_TEST_CHECK(context, payload_offset == ODM_WIRE_RECORD_HEADER_BYTES);

    memset(&info, 0xcc, sizeof(info));
    memset(decoded, 0xcc, sizeof(decoded));
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(record, required, &plan.policy_sha256,
                                             &info, decoded, 2u,
                                             &required_ticks) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, required_ticks == 2u);
    ODM_TEST_CHECK(context, info.tick_count == 2u);
    ODM_TEST_CHECK(context, info.canonical_frames == 960u);
    ODM_TEST_CHECK(context,
                   odm_sha256_equal(&info.canonical_pcm_sha256, &pcm_sha) != 0);
    ODM_TEST_CHECK(context,
                   odm_sha256_equal(&info.policy_sha256, &plan.policy_sha256) != 0);
    ODM_TEST_CHECK(context, odm_sha256_equal(&info.record_sha256, &record_sha) != 0);
    ODM_TEST_CHECK(context, music_tick_equal(&decoded[0], &ticks[0]) != 0);
    ODM_TEST_CHECK(context, music_tick_equal(&decoded[1], &ticks[1]) != 0);

    memset(sentinels, 0x5a, sizeof(sentinels));
    memcpy(decoded, sentinels, sizeof(decoded));
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(record, required, &plan.policy_sha256,
                                             &info, decoded, 1u,
                                             &required_ticks) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, memcmp(decoded, sentinels, sizeof(decoded)) == 0);
    ODM_TEST_CHECK(context, required_ticks == 2u);

    {
        odm_sha256_digest wrong = plan.policy_sha256;
        wrong.bytes[0] ^= UINT8_C(1);
        memcpy(decoded, sentinels, sizeof(decoded));
        ODM_TEST_CHECK(context,
                       odm_music_map_record_read(record, required, &wrong,
                                                 &info, decoded, 2u,
                                                 &required_ticks) ==
                           ODM_STATUS_VERSION_MISMATCH);
        ODM_TEST_CHECK(context, memcmp(decoded, sentinels, sizeof(decoded)) == 0);
    }

    memcpy(hostile, record, (size_t)required);
    hostile[ODM_WIRE_RECORD_HEADER_BYTES + ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES + 8u] ^=
        UINT8_C(1);
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(hostile, required, NULL, &info,
                                             decoded, 2u, &required_ticks) ==
                       ODM_STATUS_INTEGRITY_ERROR);

    /* Re-sign a semantically invalid payload: integrity is valid, tick semantics are not. */
    memcpy(hostile, record + ODM_WIRE_RECORD_HEADER_BYTES,
           (size_t)wire_info.payload_bytes);
    hostile[ODM_MUSIC_MAP_PAYLOAD_HEADER_BYTES] = UINT8_C(1); /* tick 0 index -> 1 */
    ODM_TEST_CHECK(context,
                   odm_wire_record_write(ODM_MUSIC_ANALYSIS_BIN_KIND,
                                         ODM_MUSIC_MAP_SCHEMA_MAJOR,
                                         ODM_MUSIC_MAP_SCHEMA_MINOR,
                                         hostile, wire_info.payload_bytes,
                                         hostile, required, &required_ticks,
                                         NULL) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(hostile, required, NULL, &info,
                                             decoded, 2u, &required_ticks) ==
                       ODM_STATUS_INVALID_DATA);

    memcpy(hostile, record, (size_t)required);
    hostile[8] = UINT8_C(0x11); /* wrong kind, payload still intact */
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(hostile, required, NULL, &info,
                                             decoded, 2u, &required_ticks) ==
                       ODM_STATUS_INVALID_DATA);
    memcpy(hostile, record, (size_t)required);
    hostile[16] = UINT8_C(2); /* unknown schema major */
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(hostile, required, NULL, &info,
                                             decoded, 2u, &required_ticks) ==
                       ODM_STATUS_VERSION_MISMATCH);
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(record, required - 1u, NULL, &info,
                                             decoded, 2u, &required_ticks) ==
                       ODM_STATUS_INVALID_DATA);
    memcpy(hostile, record, (size_t)required);
    hostile[required] = 0u;
    ODM_TEST_CHECK(context,
                   odm_music_map_record_read(hostile, required + 1u, NULL, &info,
                                             decoded, 2u, &required_ticks) ==
                       ODM_STATUS_INVALID_DATA);

    {
        odm_music_analysis_tick bad[2];
        memcpy(bad, ticks, sizeof(bad));
        bad[1].reserved[6] = 1u;
        ODM_TEST_CHECK(context,
                       odm_music_map_record_write(&plan, 960u, &pcm_sha, bad, 2u,
                                                  record, required, &required,
                                                  NULL) == ODM_STATUS_INVALID_DATA);
    }
    {
        odm_music_analysis_plan bad_plan = plan;
        bad_plan.policy_sha256.bytes[3] ^= UINT8_C(1);
        ODM_TEST_CHECK(context,
                       odm_music_map_record_write(&bad_plan, 960u, &pcm_sha,
                                                  ticks, 2u, record, required,
                                                  &required, NULL) ==
                           ODM_STATUS_INTEGRITY_ERROR);
    }

cleanup:
    free(hostile);
    free(record);
    free(scratch);
    free(twiddles);
    free(window);
}

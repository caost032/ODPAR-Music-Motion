#include "test_harness.h"

#include "odm_package.h"
#include "odm_music_map.h"
#include "package_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


static uint16_t package_test_rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t package_test_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void package_test_wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8u);
}

static void package_test_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8u);
    p[2] = (uint8_t)(v >> 16u); p[3] = (uint8_t)(v >> 24u);
}

static uint64_t package_test_central_n(const uint8_t *pkg, uint64_t pkg_bytes, uint32_t n) {
    uint64_t eocd = pkg_bytes - 22u;
    uint64_t off = package_test_rd32(pkg + eocd + 16u);
    uint32_t i;
    for (i = 0u; i < n; ++i) {
        off += 46u + package_test_rd16(pkg + off + 28u) +
               package_test_rd16(pkg + off + 30u) + package_test_rd16(pkg + off + 32u);
    }
    return off;
}

static uint64_t package_test_payload_from_central(const uint8_t *pkg, uint64_t central) {
    uint64_t local = package_test_rd32(pkg + central + 42u);
    return local + 30u + package_test_rd16(pkg + local + 26u);
}

static void package_init_node(odm_score_node *n, uint32_t id, uint32_t scene,
                              uint32_t role, uint32_t capability,
                              int32_t z_index) {
    memset(n, 0, sizeof(*n));
    n->node_id = id;
    n->scene_id = scene;
    n->role = role;
    n->capability_id = capability;
    n->capability_major = 1u;
    n->capability_minor = 0u;
    n->state_model = ODM_STATELESS;
    n->z_index = z_index;
    n->start_sample = 0;
    n->end_sample = 48000;
    n->scale_x_micro = ODM_MICRO_ONE;
    n->scale_y_micro = ODM_MICRO_ONE;
    n->opacity_micro = ODM_MICRO_ONE;
    n->seed_domain = id;
}

void odm_test_package(odm_test_context *context) {
    static const uint8_t rfc_seed[32] = {
        0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60
    };
    static const uint8_t rfc_pk[32] = {
        0xd7,0x5a,0x98,0x01,0x82,0xb1,0x0a,0xb7,0xd5,0x4b,0xfe,0xd3,0xc9,0x64,0x07,0x3a,
        0x0e,0xe1,0x72,0xf3,0xda,0xa6,0x23,0x25,0xaf,0x02,0x1a,0x68,0xf7,0x07,0x51,0x1a
    };
    static const uint8_t rfc_sig[64] = {
        0xe5,0x56,0x43,0x00,0xc3,0x60,0xac,0x72,0x90,0x86,0xe2,0xcc,0x80,0x6e,0x82,0x8a,
        0x84,0x87,0x7f,0x1e,0xb8,0xe5,0xd9,0x74,0xd8,0x73,0xe0,0x65,0x22,0x49,0x01,0x55,
        0x5f,0xb8,0x82,0x15,0x90,0xa3,0x3b,0xac,0xc6,0x1e,0x39,0x70,0x1c,0xf9,0xb4,0x6b,
        0xd2,0x5b,0xf5,0xf0,0x59,0x5b,0xbe,0x24,0x65,0x51,0x41,0x43,0x8e,0x7a,0x10,0x0b
    };
    uint8_t pk[32], sig[64];
    int32_t *window = NULL;
    odm_music_complex_q31 *twiddles = NULL;
    odm_music_analysis_tick *ticks = NULL;
    odm_music_analysis_plan plan;
    odm_sha256_digest pcm_sha, map_sha;
    uint8_t *analysis = NULL, *ir = NULL, *package = NULL, *package2 = NULL, *extract = NULL;
    uint64_t analysis_bytes = 0u, ir_bytes = 0u, package_bytes = 0u, package_bytes2 = 0u, extract_bytes = 0u;
    odm_score_header h;
    odm_score_act act;
    odm_score_scene scene;
    odm_score_resource resource;
    odm_score_node nodes[2];
    odm_score_view score;
    odm_music_map_binding binding;
    odm_odparms_asset assets[2];
    odm_odparms_info info, info2;
    uint64_t tick_count = 0u, i;
    static const uint8_t data_blob[] = "ODPAR Music canonical Gate 5 data data data data data data data";
    static const uint8_t media_blob[] = "canonical-image-resource-bytes-gate5";
    static const uint8_t bad_media_blob[] = "canonical-image-resource-bytes-gateX";

    memset(&plan, 0, sizeof(plan));
    memset(&h, 0, sizeof(h)); memset(&act, 0, sizeof(act)); memset(&scene, 0, sizeof(scene));
    memset(&resource, 0, sizeof(resource)); memset(nodes, 0, sizeof(nodes)); memset(&score, 0, sizeof(score)); memset(&binding, 0, sizeof(binding));
    memset(assets, 0, sizeof(assets)); memset(&info, 0, sizeof(info)); memset(&info2, 0, sizeof(info2));

    ODM_TEST_CHECK(context, odm_pkg_ed25519_public_from_seed(rfc_seed, pk) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(pk, rfc_pk, sizeof(pk)) == 0);
    ODM_TEST_CHECK(context, odm_pkg_ed25519_sign(rfc_seed, NULL, 0u, sig) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(sig, rfc_sig, sizeof(sig)) == 0);
    ODM_TEST_CHECK(context, odm_pkg_ed25519_verify(rfc_pk, NULL, 0u, rfc_sig) == ODM_STATUS_OK);
    sig[0] ^= 1u;
    ODM_TEST_CHECK(context, odm_pkg_ed25519_verify(rfc_pk, NULL, 0u, sig) == ODM_STATUS_INTEGRITY_ERROR);

    {
        uint8_t repetitive[4096], decoded[4096];
        uint8_t *compressed = NULL;
        uint64_t comp_bytes = 0u, got = 0u;
        uint32_t crc = 0u;
        odm_sha256_digest d;
        for (i = 0u; i < sizeof(repetitive); ++i) repetitive[i] = (uint8_t)((i % 17u) + (i / 1024u));
        ODM_TEST_CHECK(context, odm_pkg_deflate_fixed(repetitive, sizeof(repetitive), NULL, 0u, &comp_bytes) == ODM_STATUS_OK);
        compressed = (uint8_t *)malloc((size_t)comp_bytes);
        ODM_TEST_CHECK(context, compressed != NULL);
        if (compressed) {
            ODM_TEST_CHECK(context, odm_pkg_deflate_fixed(repetitive, sizeof(repetitive), compressed, comp_bytes, &got) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, got == comp_bytes && comp_bytes < sizeof(repetitive));
            ODM_TEST_CHECK(context, odm_pkg_inflate_fixed(compressed, comp_bytes, decoded, sizeof(decoded), sizeof(repetitive), &got, &crc, &d) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, got == sizeof(repetitive) && memcmp(repetitive, decoded, sizeof(repetitive)) == 0);
        }
        free(compressed);
    }

    window = (int32_t *)malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES * sizeof(*window));
    twiddles = (odm_music_complex_q31 *)malloc((size_t)ODM_MUSIC_FFT_TWIDDLES * sizeof(*twiddles));
    ODM_TEST_CHECK(context, window != NULL && twiddles != NULL);
    if (!window || !twiddles) goto cleanup;
    ODM_TEST_CHECK(context, odm_music_analysis_plan_build(window, ODM_MUSIC_WINDOW_SAMPLES, twiddles, ODM_MUSIC_FFT_TWIDDLES, &plan) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_tick_count(48000u, &tick_count) == ODM_STATUS_OK && tick_count == 100u);
    ticks = (odm_music_analysis_tick *)calloc((size_t)tick_count, sizeof(*ticks));
    ODM_TEST_CHECK(context, ticks != NULL);
    if (!ticks) goto cleanup;
    for (i = 0u; i < tick_count; ++i) {
        ticks[i].tick_index = i;
        ticks[i].center_sample = i * ODM_MUSIC_TICK_SAMPLES;
        ticks[i].silence = 1u;
    }
    ODM_TEST_CHECK(context, odm_sha256("gate5-pcm", 9u, &pcm_sha) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_music_map_record_write(&plan, 48000u, &pcm_sha, ticks, tick_count, NULL, 0u, &analysis_bytes, NULL) == ODM_STATUS_BUFFER_TOO_SMALL);
    analysis = (uint8_t *)malloc((size_t)analysis_bytes);
    ODM_TEST_CHECK(context, analysis != NULL);
    if (!analysis) goto cleanup;
    ODM_TEST_CHECK(context, odm_music_map_record_write(&plan, 48000u, &pcm_sha, ticks, tick_count, analysis, analysis_bytes, &analysis_bytes, &map_sha) == ODM_STATUS_OK);

    h.schema_version_major = ODM_SCORE_SCHEMA_MAJOR; h.schema_version_minor = ODM_SCORE_SCHEMA_MINOR;
    h.fps = 30; h.width = 1920u; h.height = 1080u; h.project_end_sample = 48000; h.project_seed = 123u;
    h.resource_count = 1u; h.act_count = 1u; h.scene_count = 1u; h.node_count = 2u;
    resource.resource_id = 1u; resource.kind = ODM_SCORE_RESOURCE_IMAGE;
    ODM_TEST_CHECK(context, odm_sha256(media_blob, sizeof(media_blob) - 1u, &resource.content_sha256) == ODM_STATUS_OK);
    act.act_id = 1u; act.start_sample = 0; act.end_sample = 48000;
    scene.scene_id = 1u; scene.act_id = 1u; scene.environment_node_id = 1u; scene.primary_node_id = 2u; scene.start_sample = 0; scene.end_sample = 48000;
    package_init_node(&nodes[0], 1u, 1u, ODM_SCORE_NODE_ENVIRONMENT, ODM_CAP_BLACK_VOID, -100);
    package_init_node(&nodes[1], 2u, 1u, ODM_SCORE_NODE_PRIMARY, ODM_CAP_PRIMARY_IMAGE, 0);
    nodes[1].resource_id = 1u;
    score.header = &h; score.resources = &resource; score.acts = &act; score.scenes = &scene; score.nodes = nodes;
    ODM_TEST_CHECK(context, odm_score_validate(&score) == ODM_STATUS_OK);
    binding.canonical_frames = 48000u; binding.music_map_sha256 = map_sha; binding.music_policy_sha256 = plan.policy_sha256;
    ODM_TEST_CHECK(context, odm_score_compile_render_ir(&score, &binding, NULL, 0u, &ir_bytes, NULL) == ODM_STATUS_BUFFER_TOO_SMALL);
    ir = (uint8_t *)malloc((size_t)ir_bytes); ODM_TEST_CHECK(context, ir != NULL); if (!ir) goto cleanup;
    ODM_TEST_CHECK(context, odm_score_compile_render_ir(&score, &binding, ir, ir_bytes, &ir_bytes, NULL) == ODM_STATUS_OK);

    assets[0].kind = ODM_ODPARMS_ENTRY_MEDIA; assets[0].resource_id = 1u; assets[0].path = "media/image.bin"; assets[0].data = media_blob; assets[0].data_bytes = sizeof(media_blob) - 1u;
    assets[1].kind = ODM_ODPARMS_ENTRY_DATA; assets[1].path = "data/readme.txt"; assets[1].data = data_blob; assets[1].data_bytes = sizeof(data_blob) - 1u;
    ODM_TEST_CHECK(context, odm_odparms_build(&score, ir, ir_bytes, analysis, analysis_bytes, assets, 2u, rfc_seed, NULL, 0u, &package_bytes, &info) == ODM_STATUS_BUFFER_TOO_SMALL);
    package = (uint8_t *)malloc((size_t)package_bytes); package2 = (uint8_t *)malloc((size_t)package_bytes);
    ODM_TEST_CHECK(context, package != NULL && package2 != NULL); if (!package || !package2) goto cleanup;
    memset(package, 0xa5, (size_t)package_bytes); memset(package2, 0x5a, (size_t)package_bytes);
    ODM_TEST_CHECK(context, odm_odparms_build(&score, ir, ir_bytes, analysis, analysis_bytes, assets, 2u, rfc_seed, package, package_bytes, &package_bytes, &info) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_odparms_build(&score, ir, ir_bytes, analysis, analysis_bytes, assets, 2u, rfc_seed, package2, package_bytes, &package_bytes2, &info2) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, package_bytes2 == package_bytes && memcmp(package, package2, (size_t)package_bytes) == 0);
    ODM_TEST_CHECK(context, odm_odparms_verify(package, package_bytes, rfc_pk, &info2) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_sha256_equal(&info.package_sha256, &info2.package_sha256) != 0);
    pk[0] ^= 1u;
    ODM_TEST_CHECK(context, odm_odparms_verify(package, package_bytes, pk, &info2) == ODM_STATUS_INTEGRITY_ERROR);
    ODM_TEST_CHECK(context, odm_odparms_extract(package, package_bytes, rfc_pk, "data/readme.txt", NULL, 0u, &extract_bytes) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, extract_bytes == sizeof(data_blob) - 1u);
    extract = (uint8_t *)malloc((size_t)extract_bytes); ODM_TEST_CHECK(context, extract != NULL); if (!extract) goto cleanup;
    ODM_TEST_CHECK(context, odm_odparms_extract(package, package_bytes, rfc_pk, "data/readme.txt", extract, extract_bytes, &extract_bytes) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(extract, data_blob, sizeof(data_blob) - 1u) == 0);
    free(extract); extract = NULL; extract_bytes = 0u;
    ODM_TEST_CHECK(context, odm_odparms_extract(package, package_bytes, rfc_pk, "media/image.bin", NULL, 0u, &extract_bytes) == ODM_STATUS_BUFFER_TOO_SMALL);
    ODM_TEST_CHECK(context, extract_bytes == sizeof(media_blob) - 1u);
    extract = (uint8_t *)malloc((size_t)extract_bytes); ODM_TEST_CHECK(context, extract != NULL); if (!extract) goto cleanup;
    ODM_TEST_CHECK(context, odm_odparms_extract(package, package_bytes, rfc_pk, "media/image.bin", extract, extract_bytes, &extract_bytes) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, memcmp(extract, media_blob, sizeof(media_blob) - 1u) == 0);

    {
        odm_odparms_asset bad_assets[2]; uint64_t req = 0u; odm_odparms_info tmp;
        memcpy(bad_assets, assets, sizeof(bad_assets));
        bad_assets[0].data = bad_media_blob; bad_assets[0].data_bytes = sizeof(bad_media_blob) - 1u;
        ODM_TEST_CHECK(context, odm_odparms_build(&score, ir, ir_bytes, analysis, analysis_bytes, bad_assets, 2u, rfc_seed, NULL, 0u, &req, &tmp) == ODM_STATUS_INTEGRITY_ERROR);
    }

    {
        uint8_t *bad = (uint8_t *)malloc((size_t)package_bytes);
        ODM_TEST_CHECK(context, bad != NULL);
        if (bad) {
            uint64_t eocd = package_bytes - 22u;
            uint64_t c0 = package_test_central_n(package, package_bytes, 0u);
            uint64_t c1 = package_test_central_n(package, package_bytes, 1u);
            uint64_t c4 = package_test_central_n(package, package_bytes, 4u);
            uint64_t l0 = package_test_rd32(package + c0 + 42u);
            uint64_t l1 = package_test_rd32(package + c1 + 42u);
            uint64_t l4 = package_test_rd32(package + c4 + 42u);
            uint64_t p0 = package_test_payload_from_central(package, c0);
            uint64_t p1 = package_test_payload_from_central(package, c1);
            uint64_t p4 = package_test_payload_from_central(package, c4);
            uint32_t crc = 0u;

            memcpy(bad, package, (size_t)package_bytes); package_test_wr16(bad + eocd + 4u, 1u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); package_test_wr16(bad + c0 + 30u, 1u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); package_test_wr32(bad + c0 + 42u, 1u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); package_test_wr16(bad + l0 + 8u, ODM_ODPARMS_METHOD_DEFLATE);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); package_test_wr16(bad + l0 + 10u, 1u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); package_test_wr16(bad + c0 + 12u, 1u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); package_test_wr32(bad + c0 + 20u, package_test_rd32(bad + c0 + 20u) - 1u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); bad[p1] ^= 1u;
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes); bad[p4] ^= 1u;
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);
            memcpy(bad, package, (size_t)package_bytes);
            memcpy(bad + c4 + 46u, "../a/readme.txt", 15u); memcpy(bad + l4 + 30u, "../a/readme.txt", 15u);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) != ODM_STATUS_OK);

            /* Re-sign a structurally valid package whose manifest lies about the
             * MEDIA resource id. Signature and CRCs remain valid: only the
             * transitive Render-IR <-> manifest resource binding can reject it. */
            memcpy(bad, package, (size_t)package_bytes);
            package_test_wr32(bad + p0 + ODM_ODPARMS_MANIFEST_HEADER_BYTES + 3u * ODM_ODPARMS_MANIFEST_ENTRY_BYTES + 12u, 2u);
            ODM_TEST_CHECK(context, odm_pkg_crc32(bad + p0, package_test_rd32(bad + c0 + 24u), &crc) == ODM_STATUS_OK);
            package_test_wr32(bad + l0 + 14u, crc); package_test_wr32(bad + c0 + 16u, crc);
            ODM_TEST_CHECK(context, odm_pkg_ed25519_sign(rfc_seed, bad + p0, package_test_rd32(bad + c0 + 24u), bad + p1) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_pkg_crc32(bad + p1, 64u, &crc) == ODM_STATUS_OK);
            package_test_wr32(bad + l1 + 14u, crc); package_test_wr32(bad + c1 + 16u, crc);
            ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &info2) == ODM_STATUS_INTEGRITY_ERROR);

            {
                odm_odparms_info untouched; odm_odparms_info before;
                memset(&untouched, 0x6d, sizeof(untouched)); before = untouched;
                bad[eocd] ^= 1u;
                ODM_TEST_CHECK(context, odm_odparms_verify(bad, package_bytes, rfc_pk, &untouched) != ODM_STATUS_OK);
                ODM_TEST_CHECK(context, memcmp(&untouched, &before, sizeof(before)) == 0);
            }
            free(bad);
        }
    }

    {
        uint8_t guard_extract[8]; uint64_t req = 0u;
        memset(guard_extract, 0x39, sizeof(guard_extract));
        ODM_TEST_CHECK(context, odm_odparms_extract(package, package_bytes, rfc_pk, "data/readme.txt", guard_extract, sizeof(guard_extract), &req) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, req == sizeof(data_blob) - 1u);
        for (i = 0u; i < sizeof(guard_extract); ++i) ODM_TEST_CHECK(context, guard_extract[i] == UINT8_C(0x39));
    }

    {
        uint8_t guard[32]; uint64_t req = 0u; odm_odparms_info tmp;
        memset(guard, 0x7c, sizeof(guard));
        ODM_TEST_CHECK(context, odm_odparms_build(&score, ir, ir_bytes, analysis, analysis_bytes, assets, 2u, rfc_seed, guard, sizeof(guard), &req, &tmp) == ODM_STATUS_BUFFER_TOO_SMALL);
        for (i = 0u; i < sizeof(guard); ++i) ODM_TEST_CHECK(context, guard[i] == UINT8_C(0x7c));
    }

cleanup:
    free(extract); free(package2); free(package); free(ir); free(analysis); free(ticks); free(twiddles); free(window);
}

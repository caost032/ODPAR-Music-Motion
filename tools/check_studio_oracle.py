#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import struct
import subprocess
import tempfile
import zipfile

PROBE = r'''
#include "odm_studio.h"
#include "odm_music_map.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t SEED_A[32] = {
    0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
    0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60
};
static const uint8_t SEED_B[32] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
    17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
};
static const uint8_t DATA_A[] = "studio-author-data-v1";
static const uint8_t DATA_B[] = "studio-author-data-v2";

typedef struct {
    odm_score_header header;
    odm_score_act act;
    odm_score_scene scene;
    odm_score_node nodes[2];
    odm_score_view score;
    odm_music_analysis_plan analysis_plan;
    int32_t *window;
    odm_music_complex_q31 *twiddles;
    odm_music_analysis_tick ticks[4];
    odm_pcm_stereo_q31 *pcm;
    odm_sha256_digest pcm_sha;
    odm_sha256_digest map_sha;
    uint8_t *analysis;
    uint64_t analysis_bytes;
    uint8_t *ir;
    uint64_t ir_bytes;
    odm_render_asset_set assets;
} fixture;

static void node_init(odm_score_node *node, uint32_t id, uint32_t role,
                      uint32_t capability, int32_t z_index) {
    memset(node, 0, sizeof(*node));
    node->node_id = id;
    node->scene_id = 1u;
    node->role = role;
    node->capability_id = capability;
    node->capability_major = 1u;
    node->capability_minor = 0u;
    node->state_model = ODM_STATELESS;
    node->z_index = z_index;
    node->start_sample = 0;
    node->end_sample = 3200;
    node->scale_x_micro = ODM_MICRO_ONE;
    node->scale_y_micro = ODM_MICRO_ONE;
    node->opacity_micro = ODM_MICRO_ONE;
    node->seed_domain = (uint64_t)id;
}

static int fixture_init(fixture *f) {
    odm_music_map_binding binding;
    uint64_t i;
    if (!f) return 0;
    memset(f, 0, sizeof(*f));
    f->window = (int32_t *)malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES * sizeof(*f->window));
    f->twiddles = (odm_music_complex_q31 *)malloc((size_t)ODM_MUSIC_FFT_TWIDDLES * sizeof(*f->twiddles));
    f->pcm = (odm_pcm_stereo_q31 *)calloc(1600u, sizeof(*f->pcm));
    if (!f->window || !f->twiddles || !f->pcm) return 0;
    if (odm_music_analysis_plan_build(f->window, ODM_MUSIC_WINDOW_SAMPLES,
                                      f->twiddles, ODM_MUSIC_FFT_TWIDDLES,
                                      &f->analysis_plan) != ODM_STATUS_OK) return 0;
    for (i = 0u; i < 4u; ++i) {
        f->ticks[i].tick_index = i;
        f->ticks[i].center_sample = i * ODM_MUSIC_TICK_SAMPLES;
        f->ticks[i].silence = 1u;
    }
    if (odm_master_pcm_sha256(f->pcm, 1600u, &f->pcm_sha) != ODM_STATUS_OK) return 0;
    if (odm_music_map_record_write(&f->analysis_plan, 1600u, &f->pcm_sha,
                                   f->ticks, 4u, NULL, 0u,
                                   &f->analysis_bytes, NULL) != ODM_STATUS_BUFFER_TOO_SMALL) return 0;
    f->analysis = (uint8_t *)malloc((size_t)f->analysis_bytes);
    if (!f->analysis) return 0;
    if (odm_music_map_record_write(&f->analysis_plan, 1600u, &f->pcm_sha,
                                   f->ticks, 4u, f->analysis, f->analysis_bytes,
                                   &f->analysis_bytes, &f->map_sha) != ODM_STATUS_OK) return 0;

    f->header.schema_version_major = ODM_SCORE_SCHEMA_MAJOR;
    f->header.schema_version_minor = ODM_SCORE_SCHEMA_MINOR;
    f->header.fps = 30;
    f->header.width = 4u;
    f->header.height = 4u;
    f->header.project_end_sample = 3200;
    f->header.project_seed = UINT64_C(0x53545544494f3031);
    f->header.act_count = 1u;
    f->header.scene_count = 1u;
    f->header.node_count = 2u;
    f->act.act_id = 1u;
    f->act.start_sample = 0;
    f->act.end_sample = 3200;
    f->scene.scene_id = 1u;
    f->scene.act_id = 1u;
    f->scene.environment_node_id = 1u;
    f->scene.primary_node_id = 2u;
    f->scene.start_sample = 0;
    f->scene.end_sample = 3200;
    node_init(&f->nodes[0], 1u, ODM_SCORE_NODE_ENVIRONMENT, ODM_CAP_BLACK_VOID, -100);
    node_init(&f->nodes[1], 2u, ODM_SCORE_NODE_PRIMARY, ODM_CAP_PRIMARY_EMPTY, 0);
    f->score.header = &f->header;
    f->score.acts = &f->act;
    f->score.scenes = &f->scene;
    f->score.nodes = f->nodes;
    if (odm_score_validate(&f->score) != ODM_STATUS_OK) return 0;
    memset(&binding, 0, sizeof(binding));
    binding.canonical_frames = 1600u;
    binding.music_map_sha256 = f->map_sha;
    binding.music_policy_sha256 = f->analysis_plan.policy_sha256;
    if (odm_score_compile_render_ir(&f->score, &binding, NULL, 0u,
                                    &f->ir_bytes, NULL) != ODM_STATUS_BUFFER_TOO_SMALL) return 0;
    f->ir = (uint8_t *)malloc((size_t)f->ir_bytes);
    if (!f->ir) return 0;
    if (odm_score_compile_render_ir(&f->score, &binding, f->ir, f->ir_bytes,
                                    &f->ir_bytes, NULL) != ODM_STATUS_OK) return 0;
    memset(&f->assets, 0, sizeof(f->assets));
    return 1;
}

static void fixture_destroy(fixture *f) {
    if (!f) return;
    free(f->ir);
    free(f->analysis);
    free(f->pcm);
    free(f->twiddles);
    free(f->window);
    memset(f, 0, sizeof(*f));
}

static uint8_t *package_build(fixture *f, const uint8_t seed[32],
                              const uint8_t *data, uint64_t data_bytes,
                              uint64_t *out_bytes, odm_odparms_info *out_info) {
    odm_odparms_asset asset;
    uint8_t *package;
    uint64_t required = 0u;
    odm_status st;
    if (!f || !seed || !data || !out_bytes || !out_info) return NULL;
    memset(&asset, 0, sizeof(asset));
    asset.kind = ODM_ODPARMS_ENTRY_DATA;
    asset.path = "notes/meta.txt";
    asset.data = data;
    asset.data_bytes = data_bytes;
    st = odm_odparms_build(&f->score, f->ir, f->ir_bytes,
                           f->analysis, f->analysis_bytes,
                           &asset, 1u, seed, NULL, 0u, &required, out_info);
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || required == 0u || required > (uint64_t)SIZE_MAX) return NULL;
    package = (uint8_t *)malloc((size_t)required);
    if (!package) return NULL;
    st = odm_odparms_build(&f->score, f->ir, f->ir_bytes,
                           f->analysis, f->analysis_bytes,
                           &asset, 1u, seed, package, required, &required, out_info);
    if (st != ODM_STATUS_OK) { free(package); return NULL; }
    *out_bytes = required;
    return package;
}

static int path_join(char *out, size_t cap, const char *dir, const char *name) {
    int n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

static int save_file(const char *path, const void *data, size_t bytes) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (bytes != 0u && fwrite(data, 1u, bytes, f) != bytes) { fclose(f); return 0; }
    return fclose(f) == 0;
}

int main(int argc, char **argv) {
    fixture f;
    uint8_t *pkg_a = NULL, *pkg_b = NULL, *pkg_resigned = NULL;
    uint64_t pkg_a_bytes = 0u, pkg_b_bytes = 0u, pkg_resigned_bytes = 0u;
    odm_odparms_info info_a, info_b, info_resigned;
    uint8_t rev0[ODM_STUDIO_REVISION_RECORD_BYTES];
    uint8_t rev1[ODM_STUDIO_REVISION_RECORD_BYTES];
    uint64_t rev_required = 0u;
    odm_studio_revision_info r0, r1;
    odm_preview_config cfg;
    odm_preview_plan plan;
    odm_preview_frame_info pinfo;
    uint8_t *preview_scratch = NULL, *preview_pixels = NULL;
    uint64_t required_pixels = 0u, required_scratch = 0u;
    uint8_t approval[ODM_STUDIO_APPROVAL_RECORD_BYTES];
    uint64_t approval_required = 0u;
    odm_studio_preview_approval_info ainfo;
    char p[4096];
    int rc = 1;

    if (argc != 2) return 2;
    memset(&f, 0, sizeof(f));
    memset(&info_a, 0, sizeof(info_a));
    memset(&info_b, 0, sizeof(info_b));
    memset(&info_resigned, 0, sizeof(info_resigned));
    memset(&r0, 0, sizeof(r0));
    memset(&r1, 0, sizeof(r1));
    memset(&cfg, 0, sizeof(cfg));
    memset(&plan, 0, sizeof(plan));
    memset(&pinfo, 0, sizeof(pinfo));
    memset(&ainfo, 0, sizeof(ainfo));

    if (!fixture_init(&f)) goto done;
    pkg_a = package_build(&f, SEED_A, DATA_A, sizeof(DATA_A) - 1u, &pkg_a_bytes, &info_a);
    pkg_b = package_build(&f, SEED_A, DATA_B, sizeof(DATA_B) - 1u, &pkg_b_bytes, &info_b);
    pkg_resigned = package_build(&f, SEED_B, DATA_A, sizeof(DATA_A) - 1u,
                                 &pkg_resigned_bytes, &info_resigned);
    if (!pkg_a || !pkg_b || !pkg_resigned) goto done;

    if (odm_studio_revision_freeze(pkg_a, pkg_a_bytes, info_a.signer_public_key,
                                   &f.score, NULL, 0u, rev0, sizeof(rev0),
                                   &rev_required, &r0) != ODM_STATUS_OK) goto done;
    if (odm_studio_revision_freeze(pkg_b, pkg_b_bytes, info_b.signer_public_key,
                                   &f.score, rev0, sizeof(rev0), rev1, sizeof(rev1),
                                   &rev_required, &r1) != ODM_STATUS_OK) goto done;

    cfg.schema_version = ODM_PREVIEW_CONFIG_SCHEMA_VERSION;
    cfg.raster_class = ODM_PREVIEW_RASTER_HALF;
    cfg.pixel_format = ODM_PREVIEW_PIXEL_RGBA8_SRGB_STRAIGHT;
    if (odm_studio_preview_plan(rev1, sizeof(rev1), f.ir, f.ir_bytes,
                                f.analysis, f.analysis_bytes, 0, &cfg,
                                0u, 0u, &plan) != ODM_STATUS_OK) goto done;
    preview_scratch = (uint8_t *)malloc((size_t)plan.scratch_bytes);
    preview_pixels = (uint8_t *)malloc((size_t)plan.output_frame_bytes);
    if (!preview_scratch || !preview_pixels) goto done;
    if (odm_studio_preview_render(rev1, sizeof(rev1), f.ir, f.ir_bytes,
                                  f.analysis, f.analysis_bytes, 0, &cfg,
                                  NULL, 0u, NULL, 0u, NULL, 0u, NULL,
                                  preview_scratch, plan.scratch_bytes,
                                  preview_pixels, plan.output_frame_bytes,
                                  &required_pixels, &required_scratch,
                                  &pinfo) != ODM_STATUS_OK) goto done;
    if (odm_studio_preview_approval_write(rev1, sizeof(rev1), &plan, &pinfo,
                                          approval, sizeof(approval),
                                          &approval_required, &ainfo) != ODM_STATUS_OK) goto done;

#define SAVE(name, data, bytes) do { \
    if (!path_join(p, sizeof(p), argv[1], (name)) || !save_file(p, (data), (size_t)(bytes))) goto done; \
} while (0)
    SAVE("package_a.odparms", pkg_a, pkg_a_bytes);
    SAVE("package_b.odparms", pkg_b, pkg_b_bytes);
    SAVE("package_resigned.odparms", pkg_resigned, pkg_resigned_bytes);
    SAVE("revision0.odm", rev0, sizeof(rev0));
    SAVE("revision1.odm", rev1, sizeof(rev1));
    SAVE("approval.odm", approval, sizeof(approval));
    SAVE("preview.rgba8", preview_pixels, required_pixels);
    SAVE("render.ir", f.ir, f.ir_bytes);
    SAVE("analysis.bin", f.analysis, f.analysis_bytes);
#undef SAVE

    printf("package_a=%llu package_b=%llu resigned=%llu revision=%u approval=%u preview=%llu\n",
           (unsigned long long)pkg_a_bytes, (unsigned long long)pkg_b_bytes,
           (unsigned long long)pkg_resigned_bytes,
           (unsigned)ODM_STUDIO_REVISION_RECORD_BYTES,
           (unsigned)ODM_STUDIO_APPROVAL_RECORD_BYTES,
           (unsigned long long)required_pixels);
    rc = 0;

done:
    free(preview_pixels);
    free(preview_scratch);
    free(pkg_resigned);
    free(pkg_b);
    free(pkg_a);
    fixture_destroy(&f);
    return rc;
}
'''


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def i32(b, o): return struct.unpack_from('<i', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]
def i64(b, o): return struct.unpack_from('<q', b, o)[0]
def le32(v): return struct.pack('<I', v & 0xffffffff)
def le64(v): return struct.pack('<Q', v & ((1 << 64) - 1))


def parse_odmc(record: bytes, expected_kind: int, expected_payload: int):
    if len(record) != 64 + expected_payload or record[:4] != b'ODMC':
        raise AssertionError('ODMC envelope/size mismatch')
    if (u16(record, 4), u16(record, 6), u32(record, 8), u32(record, 12),
        u16(record, 16), u16(record, 18), u32(record, 20), u64(record, 24)) != (
            1, 0, expected_kind, 0, 1, 0, 0, expected_payload):
        raise AssertionError('ODMC header mismatch')
    payload = record[64:]
    if hashlib.sha256(payload).digest() != record[32:64]:
        raise AssertionError('ODMC payload SHA mismatch')
    return payload, hashlib.sha256(record).digest()


def parse_manifest(pkg: bytes):
    with zipfile.ZipFile(pathlib.Path('/dev/null') if False else None) as _:
        pass


def open_package(path: pathlib.Path):
    package_bytes = path.read_bytes()
    with zipfile.ZipFile(path, 'r') as z:
        names = z.namelist()
        if names != ['manifest.odm', 'signature.ed25519', 'render.ir', 'analysis.bin', 'notes/meta.txt']:
            raise AssertionError(f'package entry order drift: {names}')
        manifest = z.read('manifest.odm')
        sig = z.read('signature.ed25519')
        contents = {name: z.read(name) for name in names if name not in ('manifest.odm', 'signature.ed25519')}
        infos = {zi.filename: zi for zi in z.infolist()}
    if len(manifest) < 256 or manifest[:8] != b'ODPARMS\0':
        raise AssertionError('manifest magic/size')
    if u32(manifest, 8) != 1 or u32(manifest, 12) != 0 or u32(manifest, 16) != 0:
        raise AssertionError('manifest schema/flags')
    count = u32(manifest, 20)
    total = u64(manifest, 24)
    if len(manifest) != 256 + count * 160:
        raise AssertionError('manifest length')
    if u32(manifest, 224) != 1 or u32(manifest, 228) != 160 or any(manifest[232:256]):
        raise AssertionError('manifest signature/entry contract')
    key = manifest[192:224]
    if len(sig) != 64:
        raise AssertionError('signature length')
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        Ed25519PublicKey.from_public_bytes(key).verify(sig, manifest)
    except Exception as exc:
        raise AssertionError(f'independent Ed25519 verify failed: {exc}') from exc
    entries = []
    by_path = {}
    for n in range(count):
        e = manifest[256 + n * 160: 256 + (n + 1) * 160]
        kind, method, flags, rid = u32(e, 0), u32(e, 4), u32(e, 8), u32(e, 12)
        data_bytes = u64(e, 16)
        sha = e[24:56]
        plen = u32(e, 56)
        if u32(e, 60) != 0 or plen == 0 or plen > 96 or any(e[64 + plen:160]):
            raise AssertionError('manifest entry canonicality')
        path_bytes = e[64:64 + plen]
        name = path_bytes.decode('utf-8')
        if name not in contents:
            raise AssertionError(f'manifest path missing from ZIP: {name}')
        data = contents[name]
        if len(data) != data_bytes or hashlib.sha256(data).digest() != sha:
            raise AssertionError(f'manifest content mismatch: {name}')
        zi = infos[name]
        zip_method = zi.compress_type
        expected_zip = 0 if method == 0 else 8
        if zip_method != expected_zip:
            raise AssertionError(f'ZIP method mismatch: {name}')
        item = dict(kind=kind, method=method, flags=flags, resource_id=rid,
                    data_bytes=data_bytes, sha=sha, path_bytes=path_bytes, name=name)
        entries.append(item)
        by_path[name] = item
    if sum(e['data_bytes'] for e in entries) != total:
        raise AssertionError('total_uncompressed mismatch')
    h = hashlib.sha256()
    h.update(b'ODPAR_ODPARMS_CONTENT_V1\0')
    h.update(le32(1)); h.update(le32(0)); h.update(le32(count)); h.update(le64(total))
    h.update(manifest[32:64]); h.update(manifest[64:96]); h.update(manifest[96:128]); h.update(manifest[128:160]); h.update(manifest[160:192])
    for e in entries:
        h.update(le32(e['kind'])); h.update(le32(e['method'])); h.update(le32(e['flags'])); h.update(le32(e['resource_id']))
        h.update(le64(e['data_bytes'])); h.update(e['sha']); h.update(le32(len(e['path_bytes']))); h.update(e['path_bytes'])
    return dict(bytes=package_bytes, sha=hashlib.sha256(package_bytes).digest(), manifest=manifest,
                key=key, entries=entries, by_path=by_path, content_sha=h.digest(),
                score=manifest[32:64], cap=manifest[64:96], music_map=manifest[96:128],
                policy=manifest[128:160], ir=manifest[160:192])


def parse_revision(record: bytes):
    p, record_sha = parse_odmc(record, 0x52555453, 512)
    if p[:8] != b'ODMSTUR1' or any(p[448:512]):
        raise AssertionError('revision magic/reserved')
    r = dict(schema_major=u32(p,8), schema_minor=u32(p,12), revision_number=u64(p,16),
             asset_count=u32(p,24), flags=u32(p,28), width=u32(p,32), height=u32(p,36),
             fps=i32(p,40), reserved0=u32(p,44), project_end=i64(p,48), seed=u64(p,56),
             parent=p[64:96], package=p[96:128], score=p[128:160], cap=p[160:192],
             music_map=p[192:224], policy=p[224:256], ir=p[256:288], asset_index=p[288:320],
             content=p[320:352], project_id=p[352:384], signer=p[384:416], revision_id=p[416:448],
             record_sha=record_sha)
    if (r['schema_major'], r['schema_minor'], r['flags'], r['reserved0'], r['width'], r['height'], r['fps'], r['project_end'], r['seed']) != (1,0,0,0,4,4,30,3200,0x53545544494f3031):
        raise AssertionError('revision scalar drift')
    h = hashlib.sha256()
    h.update(b'ODPAR_STUDIO_REVISION_ID_V1\0')
    h.update(le32(r['schema_major'])); h.update(le32(r['schema_minor'])); h.update(le64(r['revision_number']))
    h.update(le32(r['asset_count'])); h.update(le32(r['flags'])); h.update(le32(r['width'])); h.update(le32(r['height']))
    h.update(le32(r['fps'])); h.update(le32(r['reserved0'])); h.update(le64(r['project_end'])); h.update(le64(r['seed']))
    for k in ('parent','package','score','cap','music_map','policy','ir','asset_index','content','project_id'):
        h.update(r[k])
    h.update(r['signer'])
    if h.digest() != r['revision_id']:
        raise AssertionError('revision_id mismatch')
    return r


def parse_approval(record: bytes):
    p, record_sha = parse_odmc(record, 0x41555453, 256)
    if p[:8] != b'ODMSTUA1' or any(p[248:256]):
        raise AssertionError('approval magic/reserved')
    a = dict(schema_major=u32(p,8), schema_minor=u32(p,12), raster=u32(p,16), pixel=u32(p,20),
             semantics=u32(p,24), flags=u32(p,28), frame=i64(p,32), sample=i64(p,40),
             srcw=u32(p,48), srch=u32(p,52), outw=u32(p,56), outh=u32(p,60), fps=i32(p,64),
             reserved0=u32(p,68), project_end=i64(p,72), revision_number=u64(p,80),
             revision_id=p[88:120], ir=p[120:152], canonical=p[152:184], preview=p[184:216],
             approval_id=p[216:248], record_sha=record_sha)
    if (a['schema_major'],a['schema_minor'],a['raster'],a['pixel'],a['semantics'],a['flags'],a['frame'],a['sample'],
        a['srcw'],a['srch'],a['outw'],a['outh'],a['fps'],a['reserved0'],a['project_end']) != (1,0,1,1,1,0,0,0,4,4,2,2,30,0,3200):
        raise AssertionError('approval scalar drift')
    h=hashlib.sha256(); h.update(b'ODPAR_STUDIO_PREVIEW_APPROVAL_V1\0')
    for v in (a['schema_major'],a['schema_minor'],a['raster'],a['pixel'],a['semantics'],a['flags']): h.update(le32(v))
    h.update(le64(a['frame'])); h.update(le64(a['sample']))
    for v in (a['srcw'],a['srch'],a['outw'],a['outh'],a['fps'],a['reserved0']): h.update(le32(v))
    h.update(le64(a['project_end'])); h.update(le64(a['revision_number']))
    h.update(a['revision_id']); h.update(a['ir']); h.update(a['canonical']); h.update(a['preview'])
    if h.digest()!=a['approval_id']:
        raise AssertionError('approval_id mismatch')
    return a


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    args=ap.parse_args()
    root=pathlib.Path(args.root).resolve(); lib=pathlib.Path(args.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm_g10_oracle_') as td0:
        td=pathlib.Path(td0); c=td/'probe.c'; exe=td/'probe'
        c.write_text(PROBE, encoding='utf-8')
        cmd=[args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror',
             '-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        subprocess.run(cmd,check=True,cwd=root)
        proc=subprocess.run([str(exe),str(td)],check=True,cwd=root,text=True,capture_output=True)
        pa=open_package(td/'package_a.odparms'); pb=open_package(td/'package_b.odparms'); pr=open_package(td/'package_resigned.odparms')
        r0=parse_revision((td/'revision0.odm').read_bytes()); r1=parse_revision((td/'revision1.odm').read_bytes())
        approval=parse_approval((td/'approval.odm').read_bytes())
        preview=(td/'preview.rgba8').read_bytes(); ir=(td/'render.ir').read_bytes(); analysis=(td/'analysis.bin').read_bytes()

        if pa['content_sha'] != pr['content_sha'] or pa['content_sha'] == pb['content_sha']:
            raise SystemExit('package content identity invariance failed')
        if pa['sha'] == pr['sha']:
            raise SystemExit('re-signed package byte identity unexpectedly equal')
        if pa['key'] == pr['key']:
            raise SystemExit('re-signed package public key unexpectedly equal')
        if pa['by_path']['notes/meta.txt']['sha'] == pb['by_path']['notes/meta.txt']['sha']:
            raise SystemExit('DATA mutation not represented in manifest')
        if hashlib.sha256(ir).digest()!=pa['ir'] or hashlib.sha256(analysis).digest()!=pa['music_map']:
            raise SystemExit('core record SHA mismatch with package manifest')

        asset_index = hashlib.sha256(b'ODPAR_STUDIO_ASSET_INDEX_V1\0' + le32(0)).digest()
        if r0['asset_index'] != asset_index or r1['asset_index'] != asset_index:
            raise SystemExit('asset index mismatch')
        if r0['content'] != pa['content_sha'] or r1['content'] != pb['content_sha']:
            raise SystemExit('revision package content mismatch')
        if r0['package'] != pa['sha'] or r1['package'] != pb['sha']:
            raise SystemExit('revision package byte SHA mismatch')
        for r,p in ((r0,pa),(r1,pb)):
            if (r['score'],r['cap'],r['music_map'],r['policy'],r['ir'],r['signer']) != (p['score'],p['cap'],p['music_map'],p['policy'],p['ir'],p['key']):
                raise SystemExit('revision/package authority mismatch')

        ph=hashlib.sha256(); ph.update(b'ODPAR_STUDIO_PROJECT_ID_V1\0'); ph.update(le64(r0['seed'])); ph.update(r0['score']); ph.update(r0['content']); ph.update(r0['signer'])
        if ph.digest()!=r0['project_id']:
            raise SystemExit('project_id mismatch')
        if r1['project_id'] != r0['project_id'] or r0['revision_number']!=0 or any(r0['parent']) or r1['revision_number']!=1 or r1['parent']!=r0['revision_id']:
            raise SystemExit('revision lineage mismatch')
        if r1['signer'] != r0['signer']:
            raise SystemExit('revision signer continuity mismatch')

        if approval['revision_number'] != 1 or approval['revision_id'] != r1['revision_id'] or approval['ir'] != r1['ir']:
            raise SystemExit('approval revision binding mismatch')
        if hashlib.sha256(preview).digest() != approval['preview']:
            raise SystemExit('preview pixel SHA mismatch')
        if len(preview) != 16:
            raise SystemExit('preview byte length mismatch')

        print(f'Studio oracle: pass (3 signed packages, 2 revisions, 1 approval, {len(preview)} preview bytes)')
        print(f'Package content A SHA-256: {pa["content_sha"].hex()}')
        print(f'Package content B SHA-256: {pb["content_sha"].hex()}')
        print(f'Project ID SHA-256: {r0["project_id"].hex()}')
        print(f'Revision 0 ID SHA-256: {r0["revision_id"].hex()}')
        print(f'Revision 1 ID SHA-256: {r1["revision_id"].hex()}')
        print(f'Approval ID SHA-256: {approval["approval_id"].hex()}')
        print(proc.stdout.strip())

if __name__ == '__main__':
    main()

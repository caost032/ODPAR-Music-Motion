#!/usr/bin/env python3
"""Assemble ODPAR Music 0.13 extension evidence from source-bound lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, re, subprocess
from pathlib import Path

LANES = [
    'strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang',
    'tsan_gcc','gcc_analyzer','reproducibility'
]
EXPECTED_SPINE = {
    'modules':61,'dependencies':229,'invariants':113,'capabilities':101,
    'security_investigations':86,'output_profiles':2,'parity_contracts':1,
}
CANONICAL = {
    'layered_config_sha256':'1e49b1a0f78dfeedcb679f24ed50421c1701abd2a51337e1cf955e4fe5486d9a',
    'layered_policy_sha256':'4a584966752ba54df5a2c509ec56713e6e8378325f78ca7b13e1c4b33e8f7bfb',
    'export_policy_sha256':'1aecce212f79de534423d4f193b3a464e9f90dc3817b13299040593884156740',
    'export_profile_sha256':'56776e2f1bf16165b9d9d4116d61f52b0b7108d9c57b8f4c0ad143ebb57a8c1e',
    'export_recipe_bytes_sha256':'1ce9e2d125a4744770b1c768da8a5a952e4476c25f64fbab7d4895db05c284ba',
    'export_recipe_identity':'96628ef6e3a593b5f15b9b7e6b883becdf57a35cd4f6e5463ed4b3a0af90bc6a',
    'export_run_raw_video_file_sha256':'b4b5f22c090e2e24065d799643ed13af086f371edb98b382c724eaaf553b13fd',
    'export_run_raw_audio_file_sha256':'42c0e3ef57b9c472cb38731ffd2e9a3fd00eaebd3b17bafff2fbd59e4d8e0042',
    'export_run_receipt_file_sha256':'b2a8ad441d4ad8735f229ebc7b7b5a52dba3405bf83be78f54c93e22f99a22a8',
}

def sha(b: bytes) -> str: return hashlib.sha256(b).hexdigest()
def first(cmd: list[str], root: Path):
    try: p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    except (OSError, subprocess.TimeoutExpired): return None
    lines=p.stdout.decode(errors='replace').splitlines()
    return lines[0] if p.returncode==0 and lines else None

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/extension013_lanes'))
    ap.add_argument('--binary',type=Path,default=Path('build/final013-gcc/odpar-music'))
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args(); root=a.root.resolve(); ld=(root/a.lanes_dir).resolve(); binary=(root/a.binary).resolve(); out=(root/a.output).resolve()
    records=[]
    for name in LANES:
        path=ld/f'{name}.json'
        if not path.is_file(): raise SystemExit(f'missing 0.13 lane record: {path}')
        records.append(json.loads(path.read_text()))
    if not binary.is_file(): raise SystemExit(f'missing 0.13 binary: {binary}')
    sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    if sp.returncode: raise SystemExit('cannot read 0.13 spine summary')
    spine=json.loads(sp.stdout); source=spine.get('source_id')
    ver=subprocess.run([str(binary),'--version'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    version=ver.stdout.strip() if ver.returncode==0 else None
    source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in records)
    spine_ok=all(spine.get(k)==v for k,v in EXPECTED_SPINE.items())
    version_ok=spine.get('version')=='0.13.0-layered-compositor-export-v1-local' and version is not None and '0.13.0-layered-compositor-export-v1-local' in version
    totals={r['name']:r.get('last_test_totals') for r in records if r.get('last_test_totals')}
    test_ok=all(v and v.get('checks')==v.get('passed') and v.get('failed')==0 for k,v in totals.items() if k in ('strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc'))
    strict_gcc=next((r for r in records if r['name']=='strict_gcc'),{})
    strict_clang=next((r for r in records if r['name']=='strict_clang'),{})
    same_tests=(strict_gcc.get('last_test_totals')==strict_clang.get('last_test_totals') and strict_gcc.get('last_test_totals') is not None)
    log=(ld/'strict_gcc.stdout.log').read_text(errors='replace') if (ld/'strict_gcc.stdout.log').is_file() else ''
    required_markers=[
        'SHA BACKEND ORACLE: PASS','LAYERED ORACLE: PASS','LAYERED RASTER ORACLE: PASS',
        'PERSPECTIVE GRID ORACLE: PASS','HUD GEOMETRY ORACLE: PASS',
        'RADIAL GEOMETRY ORACLE: PASS','PARTICLE GEOMETRY ORACLE: PASS',
        'EXPORT ENGINE ORACLE: PASS','EXPORT RUN SMOKE PASS',
        'architectural guardians: pass (8 rules)','Spine reconciliation: pass (61 modules, 229 edges, 113 invariants',
    ]
    oracle_ok=all(x in log for x in required_markers) and all(v in log for v in CANONICAL.values() if v not in (CANONICAL['export_run_receipt_file_sha256'],))
    # Receipt SHA is printed by export-run smoke too; keep it explicit.
    oracle_ok=oracle_ok and CANONICAL['export_run_receipt_file_sha256'] in log
    full=source_ok and spine_ok and version_ok and test_ok and same_tests and oracle_ok
    lanes=[]
    for r in records:
        q=dict(r); q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes()); lanes.append(q)
    evidence={
        'schema':'odm_extension013_evidence_v1',
        'extension':'0.13-layered-composition-native-export-v1',
        'maturity':'implemented_uncertified',
        'status':'pass' if full else 'fail',
        'source_id':source,
        'version':version,
        'all_lanes_same_source':source_ok,
        'spine_identity_match':spine_ok,
        'expected_spine':EXPECTED_SPINE,
        'spine':spine,
        'strict_gcc_clang_test_totals_equal':same_tests,
        'test_suite':strict_gcc.get('last_test_totals'),
        'canonical_identities':CANONICAL,
        'contracts':{
            'layer_order':['background','core_media','reactive_field','hud'],
            'aspect_families':['custom','16:9','1:1','9:16','4:3','4:5'],
            'core_shapes':['circle','square','rounded_rect'],
            'particle_time_authority':'canonical_48khz_sample',
            'transactional_frame_publication':True,
            'export_profile_bytes':256,
            'export_recipe_bytes':512,
            'export_run_receipt_bytes':512,
            'shell_free_ffmpeg_argv':True,
            'shortest_is_not_timeline_authority':True,
            'padded_s32le_audio':True,
            'transactional_export_sink':True,
            'raw_video_audio_domain_hashes':True,
            'pcm_admission_identity_rechecked_before_commit':True,
            'single_terminal_session_event':'commit_xor_abort',
            'runtime_plan_nonsemantic_and_overridable':True,
            'canonical_config_builder':True,
            'canonical_h264_aac_profile_builder':True,
            'metadata_builtin_path':'bounded_ascii',
        },
        'verification_lanes':lanes,
        'platform':{'system':platform.system(),'machine':platform.machine()},
        'toolchains':{'gcc':first(['gcc','--version'],root),'clang':first(['clang','--version'],root),'ffmpeg':first(['ffmpeg','-version'],root),'python':platform.python_version()},
        'explicit_nonclaims':[
            'No external or formal certification.',
            'No guarantee of byte-identical encoded MP4 across FFmpeg/x264/AAC versions or hosts.',
            'No claim that encoder color conversion is canonical engine truth.',
            'No GPU backend parity claim in 0.13.',
            'No production Flutter/Android, Cloud Run or Private Studio UI validation added by 0.13.',
            'Timing observations are not performance SLAs.',
        ],
    }
    out.parent.mkdir(parents=True,exist_ok=True); tmp=out.with_suffix(out.suffix+'.tmp'); tmp.write_text(json.dumps(evidence,indent=2,sort_keys=True)+'\n'); tmp.replace(out)
    print(f"0.13 evidence: status={evidence['status']} source_id={source} output={out}")
    return 0 if full else 1
if __name__=='__main__': raise SystemExit(main())

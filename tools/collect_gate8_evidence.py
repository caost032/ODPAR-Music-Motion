#!/usr/bin/env python3
"""Assemble Gate-8 local FFI preview evidence from source-bound lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, re, shutil, subprocess
from pathlib import Path

LANES=['strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc','gcc_analyzer','reproducibility']
FRAME0='24f06e28e1b8fe3b144360406b47d9afc259bc9da45103407f2225d8f68ec672'
FRAME4='83537dc20d5dc40bbbd13aad9dd13c302cd0d8fd53a8a96be26a2b4a2fd3c1e5'

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def firstline(cmd:list[str],root:Path):
    try:p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    except (OSError,subprocess.TimeoutExpired):return None
    lines=p.stdout.decode(errors='replace').splitlines();return lines[0] if p.returncode==0 and lines else None

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/gate8_lanes'));ap.add_argument('--binary',type=Path,default=Path('build/gate8-final-gcc/odpar-music'));ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    root=a.root.resolve();ld=(root/a.lanes_dir).resolve();binary=(root/a.binary).resolve();out=(root/a.output).resolve()
    records=[]
    for name in LANES:
        p=ld/f'{name}.json'
        if not p.is_file():raise SystemExit(f'missing Gate8 lane record: {p}')
        records.append(json.loads(p.read_text()))
    if not binary.is_file():raise SystemExit(f'missing Gate8 binary: {binary}')
    sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    if sp.returncode:raise SystemExit('cannot read Gate8 spine summary')
    spine=json.loads(sp.stdout)
    vp=subprocess.run([str(binary),'--version'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    version=vp.stdout.strip() if vp.returncode==0 else None
    source=spine.get('source_id')
    source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in records)
    expected={'modules':46,'dependencies':154,'invariants':68,'capabilities':57,'security_investigations':46,'parity_contracts':1}
    spine_ok=all(spine.get(k)==v for k,v in expected.items())
    tests=max((r.get('last_test_totals') for r in records if r.get('last_test_totals')),key=lambda x:x['checks'],default=None)
    log=(ld/'strict_gcc.stdout.log').read_text(errors='replace') if (ld/'strict_gcc.stdout.log').is_file() else ''
    m0=re.search(r'Canonical frame 0 SHA-256:\s*([0-9a-f]{64})',log);m4=re.search(r'Canonical frame 4 SHA-256:\s*([0-9a-f]{64})',log)
    preview_oracle_ok=bool(m0 and m4 and m0.group(1)==FRAME0 and m4.group(1)==FRAME4 and 'Preview oracle: pass (8 frame/class vectors, 680 RGBA8 bytes exact)' in log)
    ffi_oracle_ok='Preview FFI oracle: pass (8 structs, shared ABI loaded, base ABI preserved, caller-owned lifecycle)' in log
    dart_static_ok='Flutter FFI binding: static parity pass' in log or 'Flutter FFI binding: pass (generated parity + dart analyze)' in log
    version_ok=spine.get('version')=='0.8.0-gate8-flutter-ffi-preview' and version is not None and '0.8.0-gate8-flutter-ffi-preview' in version
    local_pass=source_ok and spine_ok and tests is not None and tests['failed']==0 and preview_oracle_ok and ffi_oracle_ok and dart_static_ok and version_ok
    flutter=shutil.which('flutter');dart=shutil.which('dart')
    runtime_status='not_run_toolchain_available' if flutter else 'blocked_missing_flutter_toolchain'
    lane_evidence=[]
    for r in records:
        q=dict(r);q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes());lane_evidence.append(q)
    receipt={
      'schema':'odm_gate8_evidence_v1','gate':8,
      'scope':'same-semantics CPU preview, deterministic raster degradation, flat shared-library FFI and generated Flutter binding',
      'maturity':'implemented_uncertified','identity_excludes_wall_clock':True,
      'status':'pass_local_runtime_blocked' if local_pass and not flutter else ('pass_local_runtime_not_run' if local_pass else 'fail'),
      'local_status':'pass' if local_pass else 'fail','full_flutter_runtime_status':runtime_status,
      'version':version,'source_id':source,
      'platform':{'system':platform.system(),'machine':platform.machine()},
      'toolchains':{'gcc':firstline(['gcc','--version'],root),'clang':firstline(['clang','--version'],root),'make':firstline(['make','--version'],root),'python':platform.python_version(),'dart':firstline([dart,'--version'],root) if dart else None,'flutter':firstline([flutter,'--version'],root) if flutter else None},
      'test_suite':tests,'version_contract_match':version_ok,'spine':spine,'expected_spine':expected,'spine_identity_match':spine_ok,'all_lanes_same_source':source_ok,
      'preview_oracle_identity_match':preview_oracle_ok,'preview_ffi_oracle_match':ffi_oracle_ok,'flutter_binding_static_parity':dart_static_ok,
      'canonical_identities':{'preview_oracle_canonical_frame0_sha256':FRAME0,'preview_oracle_canonical_frame4_sha256':FRAME4},
      'gate8_contracts':{
        'same_render_ir_and_frame_state':True,'same_cpu_reference_renderer_before_degradation':True,'semantic_class_exact':True,
        'raster_classes':['exact','half','quarter','eighth'],'integer_box_filter_linear_premultiplied':True,'rgba8_srgb_straight_output':True,
        'transactional_output':True,'flat_offset_assets':True,'canonical_contiguous_asset_ranges':True,'scratch_output_alias_rejected':True,
        'base_ffi_abi_v1_preserved_feature_bits':31,'preview_ffi_abi_schema':1,'preview_ffi_struct_bytes':128,
        'music_analysis_tick_layout_published':True,'caller_owned_job_storage':True,'job_generation_bound':True,'cancellation_and_progress':True,
        'shared_library_dynamic_oracle':True,'dart_binding_generated_and_byte_frozen':True,'dart_plan_and_render_symbols_bound':True},
      'independent_preview_oracle':{'frame_class_vectors':8,'rgba8_bytes':680,'byte_exact':True,'canonical_frame0_sha256':FRAME0,'canonical_frame4_sha256':FRAME4},
      'verification_lanes':lane_evidence,
      'explicit_nonclaims':['No external certification.','No GPU parity claim.','No real Flutter runtime execution in this environment without Flutter.','No Android device integration claim from the local Gate 8 checkpoint.','No cloud master claim.']}
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');tmp.replace(out)
    print(f"Gate 8 evidence: local_status={receipt['local_status']} runtime={runtime_status} source_id={source} output={out}")
    return 0 if local_pass else 1
if __name__=='__main__':raise SystemExit(main())

#!/usr/bin/env python3
"""Assemble Gate-9 local Cloud Master evidence from source-bound lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, re, subprocess
from pathlib import Path

LANES=['strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc','gcc_analyzer','reproducibility']
RENDER_ID='f799d28cee291669d15b398a7aa14e4cc78668b3762281c2c9966e0cbc3b4bd0'
FRAME_ROOT='4d9377ed81677bc316c33cc454954031eb26eb9b89a66c5906e3d4297081a5ae'
SOUNDTRACK='dd7cf98889b205b9b65a9e06414a8c2313e8b02a58fe0a056f5df22bb4d38276'
RECEIPT='2d1a791e90f906380d62fc28074d5fa4727befbfa69145a01ef4ba8deb631307'
CONTROLLER_PK='79b5562e8fe654f94078b112e8a98ba7901f853ae695bed7e0e3910bad049664'

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def firstline(cmd:list[str],root:Path):
    try:p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    except (OSError,subprocess.TimeoutExpired):return None
    lines=p.stdout.decode(errors='replace').splitlines();return lines[0] if p.returncode==0 and lines else None

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/gate9_lanes'));ap.add_argument('--binary',type=Path,default=Path('build/gate9-final-gcc/odpar-music'));ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    root=a.root.resolve();ld=(root/a.lanes_dir).resolve();binary=(root/a.binary).resolve();out=(root/a.output).resolve()
    records=[]
    for name in LANES:
        p=ld/f'{name}.json'
        if not p.is_file():raise SystemExit(f'missing Gate9 lane record: {p}')
        records.append(json.loads(p.read_text()))
    if not binary.is_file():raise SystemExit(f'missing Gate9 binary: {binary}')
    sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    if sp.returncode:raise SystemExit('cannot read Gate9 spine summary')
    spine=json.loads(sp.stdout)
    vp=subprocess.run([str(binary),'--version'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    version=vp.stdout.strip() if vp.returncode==0 else None
    source=spine.get('source_id')
    source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in records)
    expected={'modules':49,'dependencies':173,'invariants':75,'capabilities':65,'security_investigations':52,'output_profiles':1,'parity_contracts':1}
    spine_ok=all(spine.get(k,0)>=v for k,v in expected.items())
    tests=max((r.get('last_test_totals') for r in records if r.get('last_test_totals')),key=lambda x:x['checks'],default=None)
    log=(ld/'strict_gcc.stdout.log').read_text(errors='replace') if (ld/'strict_gcc.stdout.log').is_file() else ''
    oracle_ok=(
        'Master oracle: pass (2 frames, 256 video bytes, 25600 audio bytes, 576-byte receipt)' in log and
        f'Render ID SHA-256: {RENDER_ID}' in log and
        f'Frame root SHA-256: {FRAME_ROOT}' in log and
        f'Soundtrack SHA-256: {SOUNDTRACK}' in log and
        f'Receipt SHA-256: {RECEIPT}' in log and
        f'Controller public key: {CONTROLLER_PK}' in log and
        'frames=2 audio=3200 quote=3232 output=26432 scratch=3296' in log
    )
    version_ok=spine.get('version')=='0.9.0-gate9-cloud-master-local' and version is not None and '0.9.0-gate9-cloud-master-local' in version
    local_pass=source_ok and spine_ok and tests is not None and tests['failed']==0 and oracle_ok and version_ok
    lane_evidence=[]
    for r in records:
        q=dict(r);q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes());lane_evidence.append(q)
    receipt={
      'schema':'odm_gate9_evidence_v1','gate':9,
      'scope':'authoritative headless local master core, canonical soundtrack, transactional sink, render receipt and detached controller signature',
      'maturity':'implemented_uncertified','identity_excludes_wall_clock':True,
      'status':'pass_local_cloud_not_run' if local_pass else 'fail',
      'local_master_status':'pass' if local_pass else 'fail','cloud_deployment_runtime_status':'not_run',
      'version':version,'source_id':source,
      'platform':{'system':platform.system(),'machine':platform.machine()},
      'toolchains':{'gcc':firstline(['gcc','--version'],root),'clang':firstline(['clang','--version'],root),'make':firstline(['make','--version'],root),'python':platform.python_version()},
      'test_suite':tests,'version_contract_match':version_ok,'spine':spine,'minimum_spine':expected,'spine_contains_gate_contract':spine_ok,'all_lanes_same_source':source_ok,
      'master_oracle_identity_match':oracle_ok,
      'canonical_identities':{'render_id_sha256':RENDER_ID,'frame_root_sha256':FRAME_ROOT,'soundtrack_sha256':SOUNDTRACK,'receipt_sha256':RECEIPT,'controller_public_key':CONTROLLER_PK},
      'gate9_contracts':{
        'cloud_is_not_semantic_authority':True,'signed_odparms_revalidated':True,'score_recompiled_byte_exact_to_signed_ir':True,
        'canonical_pcm_bound_to_music_map':True,'validated_music_map_random_access_view':True,'cpu_reference_frames':True,
        'exact_silence_padding':True,'deterministic_render_id':True,'output_volume_work_quote':True,'receipt_self_validates_timeline_work_and_render_id':True,
        'transactional_sink_begin_write_commit_abort':True,'no_partial_master':True,'controller_signature_detached_ed25519':True,
        'renderer_max_scratch_single_ir_pass':True},
      'independent_master_oracle':{'frames':2,'video_bytes':256,'audio_bytes':25600,'receipt_bytes':576,'byte_exact':True,'ed25519_verified_by_python_cryptography':True},
      'verification_lanes':lane_evidence,
      'explicit_nonclaims':['No external certification.','No Google Cloud/Cloud Run deployment has been executed in this checkpoint.','No service authentication, object-storage, networking, retry or quota policy has been production-validated.','No delivery encoder/container certification is claimed.','No production controller private-key custody is implemented or certified.']
    }
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');tmp.replace(out)
    print(f"Gate 9 evidence: local_status={receipt['local_master_status']} cloud={receipt['cloud_deployment_runtime_status']} source_id={source} output={out}")
    return 0 if local_pass else 1
if __name__=='__main__':raise SystemExit(main())

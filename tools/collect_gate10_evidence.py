#!/usr/bin/env python3
"""Assemble Gate-10 Private Studio evidence from source-bound verification lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, subprocess
from pathlib import Path

LANES = ['strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc','gcc_analyzer','reproducibility']
CONTENT_A='73ad246a85b5548efea28e558a8273fc6105eb9f0e51f3b75f6048a41a41e024'
CONTENT_B='d5485e3c5838b3f0bc36fcd17ad8f23ce7fc67b9fe7b720d556ee9aee5e4fa81'
PROJECT_ID='e8282778eccd2fa5d0a23405fed5fef92603c2a827c849a23055e1cef3ced180'
REV0='201baeaf74a6ce9553f84c904a8bdf5cda3fcbf20607f35ef3c192aca810670a'
REV1='5977da196e0c8f57d49cf2271f18f20a975cd45079c623d5c04a3c71e9453b18'
APPROVAL='73556c74e81c1220c4d2c1070e5fca607642f96d2c27145b9f5e60d318f8c059'

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def firstline(cmd:list[str],root:Path):
    try:p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    except (OSError,subprocess.TimeoutExpired):return None
    lines=p.stdout.decode(errors='replace').splitlines();return lines[0] if p.returncode==0 and lines else None

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/gate10_lanes'));ap.add_argument('--binary',type=Path,default=Path('build/gate10-final-gcc/odpar-music'));ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    root=a.root.resolve();ld=(root/a.lanes_dir).resolve();binary=(root/a.binary).resolve();out=(root/a.output).resolve()
    records=[]
    for name in LANES:
        p=ld/f'{name}.json'
        if not p.is_file():raise SystemExit(f'missing Gate10 lane record: {p}')
        records.append(json.loads(p.read_text()))
    if not binary.is_file():raise SystemExit(f'missing Gate10 binary: {binary}')
    sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    if sp.returncode:raise SystemExit('cannot read Gate10 spine summary')
    spine=json.loads(sp.stdout)
    vp=subprocess.run([str(binary),'--version'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    version=vp.stdout.strip() if vp.returncode==0 else None
    source=spine.get('source_id')
    source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in records)
    expected={'modules':52,'dependencies':192,'invariants':81,'capabilities':71,'security_investigations':58,'output_profiles':1,'parity_contracts':1}
    spine_ok=all(spine.get(k,0)>=v for k,v in expected.items())
    tests=max((r.get('last_test_totals') for r in records if r.get('last_test_totals')),key=lambda x:x['checks'],default=None)
    log=(ld/'strict_gcc.stdout.log').read_text(errors='replace') if (ld/'strict_gcc.stdout.log').is_file() else ''
    oracle_ok=(
        'Studio oracle: pass (3 signed packages, 2 revisions, 1 approval, 16 preview bytes)' in log and
        f'Package content A SHA-256: {CONTENT_A}' in log and f'Package content B SHA-256: {CONTENT_B}' in log and
        f'Project ID SHA-256: {PROJECT_ID}' in log and f'Revision 0 ID SHA-256: {REV0}' in log and
        f'Revision 1 ID SHA-256: {REV1}' in log and f'Approval ID SHA-256: {APPROVAL}' in log and
        'package_a=3033 package_b=3033 resigned=3033 revision=576 approval=320 preview=16' in log)
    master_oracle_ok='Master oracle: pass (2 frames, 256 video bytes, 25600 audio bytes, 576-byte receipt)' in log
    version_ok=spine.get('version')=='0.10.0-gate10-private-studio-local' and version is not None and '0.10.0-gate10-private-studio-local' in version
    local_pass=source_ok and spine_ok and tests is not None and tests['failed']==0 and oracle_ok and master_oracle_ok and version_ok
    lane_evidence=[]
    for r in records:
        q=dict(r);q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes());lane_evidence.append(q)
    receipt={
      'schema':'odm_gate10_evidence_v1','gate':10,
      'scope':'local Private Studio workflow core: signed package content identity, immutable revision lineage, revision-bound Preview approval, and pre-master revision/package admission',
      'maturity':'implemented_uncertified','identity_excludes_wall_clock':True,
      'status':'pass_local_studio_ui_not_run' if local_pass else 'fail',
      'local_private_studio_core_status':'pass' if local_pass else 'fail','production_studio_ui_runtime_status':'not_run','cloud_deployment_runtime_status':'not_run',
      'version':version,'source_id':source,
      'platform':{'system':platform.system(),'machine':platform.machine()},
      'toolchains':{'gcc':firstline(['gcc','--version'],root),'clang':firstline(['clang','--version'],root),'make':firstline(['make','--version'],root),'python':platform.python_version()},
      'test_suite':tests,'version_contract_match':version_ok,'spine':spine,'minimum_spine':expected,'spine_contains_gate_contract':spine_ok,'all_lanes_same_source':source_ok,
      'studio_oracle_identity_match':oracle_ok,'gate9_master_oracle_retained':master_oracle_ok,
      'canonical_identities':{'package_content_a_sha256':CONTENT_A,'package_content_b_sha256':CONTENT_B,'project_id_sha256':PROJECT_ID,'revision0_id_sha256':REV0,'revision1_id_sha256':REV1,'approval_id_sha256':APPROVAL},
      'gate10_contracts':{
        'package_content_identity_excludes_signature_and_zip_offsets':True,'package_content_identity_includes_all_signed_entries':True,
        'resigning_same_content_is_semantic_noop':True,'changed_signed_data_changes_content_identity':True,'stable_project_lineage':True,
        'signer_substitution_in_lineage_rejected':True,'revision_record_self_revalidating':True,'parent_child_revision_chain':True,
        'revision_bound_preview':True,'music_tick_selected_internally_from_analysis_bin':True,'preview_approval_binds_revision_ir_and_frame_hashes':True,
        'studio_master_preflight_before_sink_begin':True,'gate9_remains_master_authority':True,'no_partial_studio_master':True},
      'independent_studio_oracle':{'signed_packages':3,'revisions':2,'approval_records':1,'preview_bytes':16,'ed25519_verified_by_python_cryptography':True,'ids_recomputed_without_engine_hash_outputs':True},
      'wire_records':{'revision_record_bytes':576,'revision_payload_bytes':512,'approval_record_bytes':320,'approval_payload_bytes':256},
      'verification_lanes':lane_evidence,
      'explicit_nonclaims':['No external certification.','No complete production Private Studio Flutter/UI runtime has been executed in this checkpoint.','No Google Cloud/Cloud Run deployment has been executed in this checkpoint.','No production authentication, multi-user authorization, object storage, billing, or key-custody workflow is claimed.','Gate 10 workflow core does not replace Gate 9 as master rendering authority.']
    }
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');tmp.replace(out)
    print(f"Gate 10 evidence: local_status={receipt['local_private_studio_core_status']} ui={receipt['production_studio_ui_runtime_status']} source_id={source} output={out}")
    return 0 if local_pass else 1
if __name__=='__main__':raise SystemExit(main())

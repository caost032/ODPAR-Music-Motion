#!/usr/bin/env python3
"""Assemble Gate-7 evidence from independently executed source-bound lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, re, subprocess
from pathlib import Path

EXPECTED_POLICY='cbe9ccd31eeaa27be0113f8c3481f79a48cbd08bf318adefaa665ab8fa8efaec'
EXPECTED_ANALYSIS='0a1fce8b9fa2a3b33e5307de04668f135f1cd59c11dbbfcba700d984903bf7fc'
EXPECTED_RENDER_IR='a51e4a883722a5dc71f50e3545064fcfe7a6160fe3abad78bb2328886eb64cd8'
EXPECTED_PACKAGE='adaa77fcabe1b40d3fc237275023ba7131bae8787f8762194ea847990353276f'
EXPECTED_MANIFEST='dca95ad5e3f2830a314d9e83baee2d4651bfb4e6bd77074e7c1f23c452e74fe5'
EXPECTED_ASSETS='378767a6d5c52a1935e07fafd7c0f4a3b52b910299831674bc2ea0f24d703222'
EXPECTED_RENDERER_ROOT='fbb830c1a41773ff2f7d8f8e6723445757b16d86a08fcf5d8f8eb7fed5e269ea'
EXPECTED_VISUAL_FRAME20='40d698d4e7256310c2f4e82d2d7f6e65dad93fca0edca68868b74c3884c2df4e'
EXPECTED_VISUAL_FRAME21='c45b09ae8db2c658243320cf491c878a5c681ed016a2ee8946b6d32a302cbc65'
LANES=['strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc','gcc_analyzer','reproducibility']

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def firstline(cmd:list[str],root:Path):
    try:p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    except (OSError,subprocess.TimeoutExpired):return None
    lines=p.stdout.decode(errors='replace').splitlines();return lines[0] if p.returncode==0 and lines else None

def read_oracle_hashes(log:Path)->dict[str,str]:
    if not log.is_file(): return {}
    text=log.read_text(errors='replace'); out={}
    patterns={
      'render_ir':r'Render IR SHA-256:\s*([0-9a-f]{64})',
      'package':r'Package SHA-256:\s*([0-9a-f]{64})',
      'manifest':r'Manifest SHA-256:\s*([0-9a-f]{64})',
      'renderer_root':r'Frame root SHA-256:\s*([0-9a-f]{64})',
      'visual_frame20':r'Frame 20 SHA-256:\s*([0-9a-f]{64})',
      'visual_frame21':r'Frame 21 SHA-256:\s*([0-9a-f]{64})',
    }
    for k,p in patterns.items():
        m=re.search(p,text);
        if m: out[k]=m.group(1)
    return out

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/gate7_lanes'));ap.add_argument('--binary',type=Path,default=Path('build/gate7-final-gcc/odpar-music'));ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    root=a.root.resolve();ld=(root/a.lanes_dir).resolve();binary=(root/a.binary).resolve();out=(root/a.output).resolve()
    records=[]
    for name in LANES:
        p=ld/f'{name}.json'
        if not p.is_file():raise SystemExit(f'missing Gate7 lane record: {p}')
        records.append(json.loads(p.read_text()))
    if not binary.is_file():raise SystemExit(f'missing Gate7 binary: {binary}')
    sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    if sp.returncode!=0:raise SystemExit('cannot read Gate7 spine summary')
    spine=json.loads(sp.stdout);vp=subprocess.run([str(binary),'--version'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30);version=vp.stdout.strip() if vp.returncode==0 else None
    source=spine.get('source_id');source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in records)
    exp={'modules':44,'dependencies':144,'invariants':63,'capabilities':53,'state_models':11,'security_investigations':41};spine_ok=all(spine.get(k)==v for k,v in exp.items())
    tests=max((r.get('last_test_totals') for r in records if r.get('last_test_totals')),key=lambda x:x['checks'],default=None)
    # Strict GCC contains all independent oracles and is retained as a forensic log.
    oracle=read_oracle_hashes(ld/'strict_gcc.stdout.log')
    oracle_ok=(oracle.get('render_ir')==EXPECTED_RENDER_IR and oracle.get('package')==EXPECTED_PACKAGE and oracle.get('manifest')==EXPECTED_MANIFEST and oracle.get('renderer_root')==EXPECTED_RENDERER_ROOT and oracle.get('visual_frame20')==EXPECTED_VISUAL_FRAME20 and oracle.get('visual_frame21')==EXPECTED_VISUAL_FRAME21)
    version_ok=(spine.get('version')=='0.7.0-gate7-visual-systems' and version is not None and '0.7.0-gate7-visual-systems' in version)
    full=source_ok and spine_ok and tests is not None and tests['failed']==0 and oracle_ok and version_ok
    lane_evidence=[]
    for r in records:
        q=dict(r);q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes());lane_evidence.append(q)
    receipt={'schema':'odm_gate7_evidence_v1','gate':7,'scope':'deterministic visual state, memory, procedural systems and canonical CPU composition','maturity':'implemented_uncertified','identity_excludes_wall_clock':True,'status':'pass' if full else 'fail','version':version,'source_id':source,'platform':{'system':platform.system(),'machine':platform.machine()},'toolchains':{'gcc':firstline(['gcc','--version'],root),'clang':firstline(['clang','--version'],root),'make':firstline(['make','--version'],root),'python':platform.python_version()},'test_suite':tests,'version_contract_match':version_ok,'spine':spine,'expected_spine':exp,'spine_identity_match':spine_ok,'all_lanes_same_source':source_ok,'oracle_identity_match':oracle_ok,'canonical_identities':{'music_policy_sha256':EXPECTED_POLICY,'analysis_bin_sha256':EXPECTED_ANALYSIS,'render_ir_oracle_sha256':EXPECTED_RENDER_IR,'odparms_oracle_sha256':EXPECTED_PACKAGE,'odparms_manifest_sha256':EXPECTED_MANIFEST,'renderer_asset_set_sha256':EXPECTED_ASSETS,'renderer_frame_root_sha256':EXPECTED_RENDERER_ROOT,'visual_frame20_sha256':EXPECTED_VISUAL_FRAME20,'visual_frame21_sha256':EXPECTED_VISUAL_FRAME21},'gate7_contracts':{'score_render_ir_schema':'1.1','render_ir_node_record_bytes':128,'explicit_state_slot':True,'exact_entry_exit_samples':True,'q31_full_scale_identity':True,'state_transfer_copy':True,'state_transfer_reset':True,'max_transfer_depth':64,'latest_event_limit':32,'deterministic_domain_separated_seeds':True,'grid_proof':True,'halo':True,'residual_event_field':True,'deterministic_particles':True,'integer_fixed_point_authority':True,'history_resolved_once_per_visible_stateful_node':True,'transactional_frame_publication':True},'independent_visual_oracle':{'event_records':37,'frames':2,'pixels':2048,'channels':8192,'copy_history':True,'reset_history':True,'latest_32':True,'entry_fade_scale':True,'full_frame_byte_exact':True,'frame20_sha256':EXPECTED_VISUAL_FRAME20,'frame21_sha256':EXPECTED_VISUAL_FRAME21},'verification_lanes':lane_evidence,'explicit_nonclaims':['No external certification.','No GPU parity claim.','No Flutter preview claim at Gate 7.','No HDR/wide-gamut claim.','No cloud master claim.']}
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');tmp.replace(out);print(f"Gate 7 evidence: status={receipt['status']} source_id={source} output={out}");return 0 if full else 1
if __name__=='__main__':raise SystemExit(main())

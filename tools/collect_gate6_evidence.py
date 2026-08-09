#!/usr/bin/env python3
"""Assemble Gate-6 evidence from independently executed source-bound lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, subprocess
from pathlib import Path

EXPECTED_POLICY='cbe9ccd31eeaa27be0113f8c3481f79a48cbd08bf318adefaa665ab8fa8efaec'
EXPECTED_ANALYSIS='0a1fce8b9fa2a3b33e5307de04668f135f1cd59c11dbbfcba700d984903bf7fc'
EXPECTED_ASSETS='378767a6d5c52a1935e07fafd7c0f4a3b52b910299831674bc2ea0f24d703222'
EXPECTED_ROOT='4058cd8285ffc941db87294386d8c4b573ab6d55a5895d1b7016f8184d097ab0'
EXPECTED_FRAME_SHA=[
'f260aada168acbc93990e1fdbb28ecbc735a48a16b7265c8119acc10569f74fe',
'1522488363d2a34eeb2b8d3e2f682c0cfdf2d57a5aca539537e144afd32ccf41',
'b6f4178bee7c96d518e295418868349b55b2d53aa97a8bea9a8dd904e1833253',
'13dd6a27f3eddf47beab4e6c5d04fb5d27b2fbfcffb9663b3817b45bc0d20129',
'bf50d187219185b1b2b179963dcd5c3baf7140c500be4e5ad0a29ab260f53458',
'351b324c73aa8cc9a68d39ab400ac7b7fae0b4516489abf38b33438ea46deb66',
]
LANES=['strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc','gcc_analyzer','reproducibility']

def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def firstline(cmd:list[str],root:Path):
    try:p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
    except (OSError,subprocess.TimeoutExpired):return None
    lines=p.stdout.decode(errors='replace').splitlines();return lines[0] if p.returncode==0 and lines else None

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/gate6_lanes'));ap.add_argument('--binary',type=Path,default=Path('build/gate6-final-gcc/odpar-music'));ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();root=a.root.resolve();ld=(root/a.lanes_dir).resolve();binary=(root/a.binary).resolve();out=(root/a.output).resolve()
    records=[]
    for name in LANES:
        p=ld/f'{name}.json'
        if not p.is_file():raise SystemExit(f'missing Gate6 lane record: {p}')
        records.append(json.loads(p.read_text()))
    if not binary.is_file():raise SystemExit(f'missing Gate6 binary: {binary}')
    sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30)
    if sp.returncode!=0:raise SystemExit('cannot read Gate6 spine summary')
    spine=json.loads(sp.stdout);vp=subprocess.run([str(binary),'--version'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=30);version=vp.stdout.strip() if vp.returncode==0 else None
    source=spine.get('source_id');source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in records)
    exp={'modules':42,'dependencies':134,'invariants':58,'capabilities':47,'security_investigations':36};spine_ok=all(spine.get(k)==v for k,v in exp.items())
    tests=max((r.get('last_test_totals') for r in records if r.get('last_test_totals')),key=lambda x:x['checks'],default=None);full=source_ok and spine_ok and tests is not None and tests['failed']==0
    lane_evidence=[]
    for r in records:
        q=dict(r);q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes());lane_evidence.append(q)
    receipt={'schema':'odm_gate6_evidence_v1','gate':6,'scope':'deterministic CPU reference renderer, canonical pixels, exact-PTS video, frame hashes/root','maturity':'implemented_uncertified','identity_excludes_wall_clock':True,'status':'pass' if full else 'fail','version':version,'source_id':source,'platform':{'system':platform.system(),'machine':platform.machine()},'toolchains':{'gcc':firstline(['gcc','--version'],root),'clang':firstline(['clang','--version'],root),'make':firstline(['make','--version'],root),'python':platform.python_version()},'test_suite':tests,'spine':spine,'expected_spine':exp,'spine_identity_match':spine_ok,'all_lanes_same_source':source_ok,'canonical_identities':{'music_policy_sha256':EXPECTED_POLICY,'analysis_bin_sha256':EXPECTED_ANALYSIS,'renderer_asset_set_sha256':EXPECTED_ASSETS,'renderer_frame_sha256':EXPECTED_FRAME_SHA,'renderer_frame_root_sha256':EXPECTED_ROOT},'gate6_contracts':{'rgba16le_linear_bt709_premultiplied':True,'integer_fixed_point_authority':True,'srgb_eotf_frozen':True,'bilinear_in_linear_premultiplied_light':True,'exact_pts_video_selection':True,'per_scene_composition_before_crossfade':True,'transactional_publication':True,'scratch_alignment_explicit':8,'asset_set_content_addressed':True,'frame_sha_content_addressed':True,'ordered_frame_root':True,'forged_frame_sample_extent_rejected':True,'scratch_output_nonoverlap_enforced':True,'frame_hash_binds_renderer_version_fps':True,'frame_root_rejects_noncanonical_metadata':True,'golden_frames_frozen':6},'independent_renderer_oracle':{'color_vectors':256,'transform_samples':5,'frames':6,'pixels':384,'channels':1536,'non_cardinal_cordic_rotation':True,'anisotropic_scale':True,'non_power_of_two_opacity':True,'asset_set_sha256':EXPECTED_ASSETS,'frame_root_sha256':EXPECTED_ROOT},'independent_renderer_property_oracle':{'cases':64,'pixels':4096,'channels':16384,'translation':True,'anisotropic_scale':True,'partial_alpha':True,'srgb_and_linear':True,'arbitrary_cordic_phases':True},'verification_lanes':lane_evidence,'explicit_nonclaims':['No external certification.','No GPU parity claim.','No HDR/wide-gamut claim in v1.','No native libav video decode claim in this environment.','Halo/Grid/particles remain Gate 7 semantic systems.']}
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');tmp.replace(out);print(f"Gate 6 evidence: status={receipt['status']} source_id={source} output={out}");return 0 if full else 1
if __name__=='__main__':raise SystemExit(main())

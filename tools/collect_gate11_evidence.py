#!/usr/bin/env python3
"""Assemble Gate 11 Commercial Evidence from frozen source-bound lanes."""
from __future__ import annotations
import argparse, hashlib, json, platform, subprocess
from pathlib import Path
LANES=['strict_gcc','strict_clang','asan_ubsan_gcc','asan_ubsan_clang','tsan_gcc','gcc_analyzer','reproducibility','ffmpeg_delivery']
VID='82e179147c0b218f43b0e6cd6771199cccc0e697632923f29aad3fe09875e780'
AUD='6110db50e3c583f3de7f719f4733436950e0941302443be4feb4fe5944e577c4'
SET='0d91a362aced9612d34dc42c40fb5f1c71035dc53e969ca101622c15f01e4f3b'
CID='e4c7265f31772ec19eaade5caa90c8b931639edd2df41e88c08212b2cdb4339b'
AID='911d4b09c01287c97659c8a38c03685e126c5e44d9a875267f30e85f2b26fabb'
def sha(b:bytes)->str:return hashlib.sha256(b).hexdigest()
def first(cmd:list[str],root:Path):
 try:p=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
 except (OSError,subprocess.TimeoutExpired):return None
 x=p.stdout.decode(errors='replace').splitlines();return x[0] if p.returncode==0 and x else None
def main()->int:
 ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--lanes-dir',type=Path,default=Path('evidence/gate11_lanes'));ap.add_argument('--binary',type=Path,default=Path('build/gate11-final-gcc/odpar-music'));ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
 root=a.root.resolve();ld=(root/a.lanes_dir).resolve();binary=(root/a.binary).resolve();out=(root/a.output).resolve()
 rec=[]
 for n in LANES:
  p=ld/f'{n}.json'
  if not p.is_file():raise SystemExit(f'missing Gate11 lane record: {p}')
  rec.append(json.loads(p.read_text()))
 if not binary.is_file():raise SystemExit(f'missing Gate11 binary: {binary}')
 sp=subprocess.run([str(binary),'--spine-summary'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
 if sp.returncode:raise SystemExit('cannot read Gate11 spine summary')
 spine=json.loads(sp.stdout);source=spine.get('source_id')
 vp=subprocess.run([str(binary),'--version'],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30);version=vp.stdout.strip() if vp.returncode==0 else None
 source_ok=bool(source) and all(r.get('status')=='pass' and r.get('source_stable') and r.get('source_id_before')==source and r.get('source_id_after')==source for r in rec)
 expected={'modules':53,'dependencies':197,'invariants':86,'capabilities':74,'security_investigations':62,'output_profiles':2,'parity_contracts':1}
 spine_ok=all(spine.get(k,0)>=v for k,v in expected.items())
 tests=max((r.get('last_test_totals') for r in rec if r.get('last_test_totals')),key=lambda x:x['checks'],default=None)
 strict=(ld/'strict_gcc.stdout.log').read_text(errors='replace') if (ld/'strict_gcc.stdout.log').is_file() else ''
 oracle_ok=('Delivery oracle: pass (receipt + 384-byte contract + 256-byte artifact)' in strict and f'video_descriptor_sha256={VID}' in strict and f'audio_descriptor_sha256={AUD}' in strict and f'settings_sha256={SET}' in strict and f'delivery_contract_id={CID}' in strict and f'artifact_id={AID}' in strict)
 claims_ok='Commercial claims checker: pass (6 supported scoped claims, 11 explicit nonclaims)' in strict
 obs_path=root/'evidence/gate11_ffmpeg_observation.json';obs=json.loads(obs_path.read_text()) if obs_path.is_file() else {};ffmpeg_ok=obs.get('status')=='pass_local_observation'
 benchmark_path=root/'evidence/gate11_core_benchmark.json';benchmark=json.loads(benchmark_path.read_text()) if benchmark_path.is_file() else {};benchmark_ok=benchmark.get('status')=='pass_local_observation' and benchmark.get('semantic_authority') is False and benchmark.get('pricing_authority') is False
 # A gate receipt must not pin the engine to the version that happened to
 # exist when the gate closed: the tree keeps moving and the evidence would
 # become permanently unregenerable. What is actually load-bearing is that
 # the Spine and the CLI report the SAME version, i.e. one coherent tree.
 spine_version=spine.get('version')
 version_ok=bool(spine_version) and version is not None and spine_version in version
 local=source_ok and spine_ok and tests is not None and tests.get('checks')>=8047810 and tests.get('failed')==0 and oracle_ok and claims_ok and ffmpeg_ok and benchmark_ok and version_ok
 lanes=[]
 for r in rec:
  q=dict(r);q['record_sha256']=sha((ld/f"{r['name']}.json").read_bytes());lanes.append(q)
 e={'schema':'odm_gate11_evidence_v1','gate':11,'scope':'local Commercial Evidence and Delivery contract/artifact identity over sealed Gate 9 Render Receipt, plus one host-local FFmpeg delivery observation','maturity':'implemented_uncertified','status':'pass_local_commercial_evidence_uncertified' if local else 'fail','source_id':source,'version':version,'identity_excludes_wall_clock':True,'pricing_is_nonsemantic':True,'wall_clock_is_nonsemantic':True,'all_lanes_same_source':source_ok,'version_contract_match':version_ok,'test_suite':tests,'spine':spine,'minimum_spine':expected,'spine_contains_gate_contract':spine_ok,'delivery_oracle_identity_match':oracle_ok,'commercial_claim_catalog_match':claims_ok,'ffmpeg_local_observation_status':'pass' if ffmpeg_ok else 'fail','core_benchmark_observation_status':'pass' if benchmark_ok else 'fail','core_benchmark_observation':benchmark,'canonical_delivery_oracle':{'video_descriptor_sha256':VID,'audio_descriptor_sha256':AUD,'settings_sha256':SET,'delivery_contract_id':CID,'artifact_id':AID},'delivery_contracts':{'render_id_unchanged_by_delivery_choices':True,'delivery_contract_id_separate':True,'delivered_file_sha256_separate':True,'artifact_id_separate':True,'profile_v1':'MP4/H264/AAC/SDR_BT709/YUV420P/CRF/48kHz','exact_fps_only':True,'even_bounded_geometry':True,'encoder_descriptors_content_addressed':True},'host_ffmpeg_observation':obs,'verification_lanes':lanes,'platform':{'system':platform.system(),'machine':platform.machine()},'toolchains':{'gcc':first(['gcc','--version'],root),'clang':first(['clang','--version'],root),'ffmpeg':first(['ffmpeg','-version'],root),'python':platform.python_version()},'explicit_nonclaims':['No formal or external certification.','No universal H.264/AAC/container support claim.','No bit-exact or lossless equivalence after H.264/AAC.','No byte-identical FFmpeg output across versions/hosts.','No production Flutter/Android runtime validation.','No deployed Cloud Run SLA/security/cost claim.','No production Studio UI validation.','No third-party security/legal/regulatory certification.','No licensing conclusion for FFmpeg/codecs.','No pricing, revenue, profitability, or wall-clock performance SLA.','No Delivery Contract or Artifact Record by itself certifies encoded color-transfer or sample conformance.']}
 out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(e,indent=2,sort_keys=True)+'\n');tmp.replace(out)
 print(f"Gate 11 evidence: status={e['status']} source_id={source} output={out}")
 return 0 if local else 1
if __name__=='__main__':raise SystemExit(main())

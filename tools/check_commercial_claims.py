#!/usr/bin/env python3
"""Fail-closed Gate 11 public-claim catalog derived only from sealed evidence."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path

REQ={2:('full_gate2_status','pass'),3:('full_gate3_status','pass'),4:('status','pass'),5:('status','pass'),6:('status','pass'),7:('status','pass'),8:('status','pass_local_runtime_blocked'),9:('status','pass_local_cloud_not_run'),10:('status','pass_local_studio_ui_not_run')}
SUPPORTED=[
'Local deterministic C11 pipeline is evidenced through the Private Studio core in the tested scope.',
'Canonical render_id is separate from delivery-contract identity; delivered bytes receive a separate artifact identity.',
'Native WAV/PNG Media Truth is implemented and tested in the documented Gate 2 scope.',
'Headless canonical master core is implemented and locally evidenced; cloud deployment itself is not claimed.',
'Private Studio revision/approval/workflow core is locally evidenced; production Studio UI is not claimed.',
'On this evidence host, one observed FFmpeg build successfully encoded the frozen MP4/H.264/AAC delivery smoke vector.'
]
NONCLAIMS=[
'No formal or external certification is claimed.',
'No universal codec/container compatibility is claimed.',
'No bit-exact or lossless equivalence is claimed after H.264/AAC encoding.',
'No byte-identical FFmpeg output across versions, builds, CPUs or hosts is claimed.',
'No production Flutter/Android runtime validation is claimed.',
'No deployed Cloud Run SLA, security posture or cost claim is made.',
'No production Private Studio UI validation is claimed.',
'No third-party security, legal or regulatory certification is claimed.',
'No conclusion about third-party codec/FFmpeg licensing obligations is encoded by the motor.',
'No pricing, revenue, profitability or wall-clock performance SLA is semantic engine truth.',
'No Delivery Contract or Artifact Record by itself certifies encoded color-transfer or sample conformance; the FFmpeg result is only a host-local adapter observation.'
]
def sha256_file(p:Path)->str:return hashlib.sha256(p.read_bytes()).hexdigest()
def canonical_bytes(v:dict)->bytes:return json.dumps(v,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()
def main()->int:
 ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--json-out',type=Path);a=ap.parse_args();root=a.root.resolve()
 gates={}
 for n,(key,want) in REQ.items():
  p=root/f'evidence/gate{n}_evidence.json'
  if not p.is_file(): raise SystemExit(f'missing sealed evidence: {p}')
  d=json.loads(p.read_text()); got=d.get(key)
  if got!=want: raise SystemExit(f'gate{n} evidence status mismatch: {key}={got!r}, expected {want!r}')
  gates[str(n)]={'evidence':str(p.relative_to(root)),'evidence_sha256':sha256_file(p),'status':got,'source_id':d.get('source_id')}
 obs=root/'evidence/gate11_ffmpeg_observation.json'; ffmpeg=False; obs_ref=None
 if obs.is_file():
  od=json.loads(obs.read_text());ffmpeg=od.get('status')=='pass_local_observation'
  if not ffmpeg: raise SystemExit('FFmpeg observation exists but is not PASS')
  obs_ref={'evidence':str(obs.relative_to(root)),'evidence_sha256':sha256_file(obs),'status':od.get('status')}
 claims=SUPPORTED if ffmpeg else SUPPORTED[:-1]
 result={'schema':'odm_gate11_claim_catalog_v1','status':'pass','evidence_gates':gates,'supported_scoped_claims':claims,'explicit_nonclaims':NONCLAIMS,'pricing_is_nonsemantic':True,'wall_clock_is_nonsemantic':True,'delivery_artifact_is_not_conformance_certificate':True,'ffmpeg_local_observation':obs_ref}
 result['catalog_id_sha256']=hashlib.sha256(canonical_bytes(result)).hexdigest()
 if a.json_out:
  out=a.json_out if a.json_out.is_absolute() else root/a.json_out;out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
 print(f'Commercial claims checker: pass ({len(claims)} supported scoped claims, {len(NONCLAIMS)} explicit nonclaims)')
 print('catalog_id_sha256='+result['catalog_id_sha256'])
 return 0
if __name__=='__main__':raise SystemExit(main())

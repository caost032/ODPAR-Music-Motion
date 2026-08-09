#!/usr/bin/env python3
"""Record Gate 11 host-local benchmark observations without making them semantic."""
from __future__ import annotations
import argparse, hashlib, json, platform, subprocess
from pathlib import Path

def main()->int:
 ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();root=a.root.resolve();binary=(root/a.binary).resolve() if not a.binary.is_absolute() else a.binary;out=(root/a.output).resolve() if not a.output.is_absolute() else a.output
 p=subprocess.run([str(binary)],cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
 if p.returncode: raise SystemExit(f'benchmark failed: {p.stderr}')
 lines=[x for x in p.stdout.splitlines() if x.lstrip().startswith('{')]
 if not lines: raise SystemExit('benchmark JSON missing')
 raw=json.loads(lines[-1])
 if raw.get('schema')!='odm_core_benchmark_v1' or raw.get('status')!='pass': raise SystemExit('benchmark status/schema mismatch')
 rec={'schema':'odm_gate11_benchmark_observation_v1','status':'pass_local_observation','semantic_authority':False,'pricing_authority':False,'sla_authority':False,'platform':{'system':platform.system(),'machine':platform.machine()},'measurement':raw,'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'explicit_nonclaims':['Nanosecond timings are host-local observations.','No price, cloud cost, SLA, revenue, or profitability is derived from this record.','Deterministic work_units and wall-clock time are different quantities.']}
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(rec,indent=2,sort_keys=True)+'\n')
 print('Gate 11 benchmark observation: pass_local_observation')
 return 0
if __name__=='__main__':raise SystemExit(main())

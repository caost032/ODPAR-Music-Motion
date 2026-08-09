#!/usr/bin/env python3
"""Run one expensive verification lane and emit a source-bound JSON record.

This exists so gate evidence can be assembled from separately executed lanes
without relying on one fragile multi-minute wrapper process.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, signal, subprocess, sys, tempfile, time
from pathlib import Path

from gen_build_meta import identity_inputs, source_id

CHECKS_RE = re.compile(rb"ODM TESTS checks=(\d+) passed=(\d+) failed=(\d+)")

def sha(b: bytes) -> str: return hashlib.sha256(b).hexdigest()

def stable_env() -> dict[str,str]:
    e=os.environ.copy();e.update({'LC_ALL':'C','LANG':'C','TZ':'UTC','SOURCE_DATE_EPOCH':'0'});return e

def sid() -> str: return 'sha256:'+source_id(identity_inputs())

def main() -> int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--name',required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--timeout',type=int,default=600);ap.add_argument('command',nargs=argparse.REMAINDER);a=ap.parse_args()
    root=a.root.resolve();out=(root/a.output).resolve() if not a.output.is_absolute() else a.output
    cmd=a.command[1:] if a.command and a.command[0]=='--' else a.command
    if not cmd: raise SystemExit('lane command missing')
    before=sid();started=time.monotonic();failure=None
    # File-backed capture avoids pipe back-pressure and, more importantly,
    # prevents a timed-out grandchild from keeping communicate() pipes open.
    # Every lane gets its own process group so timeout means the entire lane,
    # not merely the top-level make process, is terminated.
    with tempfile.TemporaryDirectory(prefix=f"odm-lane-{a.name}-") as temporary:
        stdout_path=Path(temporary)/"stdout.log";stderr_path=Path(temporary)/"stderr.log"
        with stdout_path.open("wb") as so, stderr_path.open("wb") as se:
            proc=subprocess.Popen(
                cmd,cwd=root,env=stable_env(),stdout=so,stderr=se,
                start_new_session=(os.name=="posix"),
            )
            try:
                code=proc.wait(timeout=a.timeout)
            except subprocess.TimeoutExpired:
                failure=f'timeout_{a.timeout}s';code=124
                if os.name=="posix":
                    try: os.killpg(proc.pid,signal.SIGTERM)
                    except ProcessLookupError: pass
                else:
                    proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    if os.name=="posix":
                        try: os.killpg(proc.pid,signal.SIGKILL)
                        except ProcessLookupError: pass
                    else:
                        proc.kill()
                    proc.wait()
        stdout=stdout_path.read_bytes();stderr=stderr_path.read_bytes()
    elapsed_ns=int((time.monotonic()-started)*1_000_000_000);after=sid()
    norm=lambda b:b.replace(str(root).encode(),b'.')
    no,ne=norm(stdout),norm(stderr);matches=CHECKS_RE.findall(no+ne)
    rec={'schema':'odm_evidence_lane_v1','name':a.name,'command':cmd,'exit_code':code,'status':'pass' if code==0 and before==after else 'fail','source_id_before':before,'source_id_after':after,'source_stable':before==after,'elapsed_ns_observation_only':elapsed_ns,'stdout_sha256':sha(no),'stderr_sha256':sha(ne),'stdout_tail':no.decode(errors='replace').splitlines()[-30:],'stderr_tail':ne.decode(errors='replace').splitlines()[-30:]}
    if failure:rec['failure']=failure
    if matches:
        c,pa,f=matches[-1];rec['last_test_totals']={'checks':int(c),'passed':int(pa),'failed':int(f)}
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(rec,indent=2,sort_keys=True)+'\n');tmp.replace(out)
    # Keep normalized full lane output next to the record for forensic inspection.
    out.with_suffix('.stdout.log').write_bytes(no);out.with_suffix('.stderr.log').write_bytes(ne)
    print(f"{a.name}: {rec['status']} exit={code} source={before} elapsed_ns={elapsed_ns}")
    if rec.get('last_test_totals'):print('tests:',rec['last_test_totals'])
    return 0 if rec['status']=='pass' else 1

if __name__=='__main__':raise SystemExit(main())

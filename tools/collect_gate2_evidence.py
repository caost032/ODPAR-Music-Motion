#!/usr/bin/env python3
"""Execute Gate 2 verification and emit an honest evidence receipt."""
from __future__ import annotations
import argparse, hashlib, json, os, platform, re, shutil, subprocess
from pathlib import Path
CHECKS_RE=re.compile(rb"ODM TESTS checks=(\d+) passed=(\d+) failed=(\d+)")
def sha(b): return hashlib.sha256(b).hexdigest()
def env():
    e=os.environ.copy();e.update({'LC_ALL':'C','LANG':'C','TZ':'UTC','SOURCE_DATE_EPOCH':'0'});return e
def run(root,name,cmd):
    p=subprocess.run(cmd,cwd=root,env=env(),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    norm=lambda b:b.replace(str(root).encode(),b'.')
    out,err=norm(p.stdout),norm(p.stderr); m=CHECKS_RE.findall(out+err)
    o={'name':name,'command':cmd,'exit_code':p.returncode,'status':'pass' if p.returncode==0 else 'fail',
       'stdout_sha256':sha(out),'stderr_sha256':sha(err),
       'stdout_tail':out.decode(errors='replace').splitlines()[-12:],
       'stderr_tail':err.decode(errors='replace').splitlines()[-12:]}
    if m:
        c,pa,f=m[-1];o['last_test_totals']={'checks':int(c),'passed':int(pa),'failed':int(f)}
    return o
def firstline(cmd,root):
    try:p=subprocess.run(cmd,cwd=root,env=env(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    except OSError:return None
    return p.stdout.decode(errors='replace').splitlines()[0] if p.returncode==0 and p.stdout else None
def pkg(name,root):
    p=subprocess.run(['pkg-config','--modversion',name],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
    return p.stdout.decode().strip() if p.returncode==0 else None
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();root=a.root.resolve();out=(root/a.output).resolve() if not a.output.is_absolute() else a.output
    steps=[
      ('strict_gcc_matrix',['make','test-gcc']),('strict_clang_matrix',['make','test-clang']),
      ('asan_ubsan_gcc',['make','asan-gcc']),('asan_ubsan_clang',['make','asan-clang']),
      ('tsan_gcc',['make','tsan-gcc']),('gcc_static_analyzer',['make','analyze-gcc']),
      ('clean_root_reproducibility',['make','repro'])]
    obs=[run(root,n,c) for n,c in steps]
    spine=None
    p=subprocess.run([str(root/'build/gcc/odpar-music'),'--spine-summary'],cwd=root,stdout=subprocess.PIPE)
    if p.returncode==0:
      try: spine=json.loads(p.stdout)
      except json.JSONDecodeError: pass
    full=all(x['status']=='pass' for x in obs)
    receipt={'schema':'odm_gate2_evidence_v1','identity_excludes_wall_clock':True,
      'platform':{'system':platform.system(),'machine':platform.machine()},
      'toolchains':{'gcc':firstline(['gcc','--version'],root),'clang':firstline(['clang','--version'],root),'make':firstline(['make','--version'],root),'python':platform.python_version()},
      'media_environment':{'ffmpeg':firstline(['ffmpeg','-version'],root),'ffprobe':firstline(['ffprobe','-version'],root),
        'libpng':pkg('libpng',root),'libjpeg':pkg('libjpeg',root),'libwebp':pkg('libwebp',root),
        'libavformat':pkg('libavformat',root),'libavcodec':pkg('libavcodec',root),'libavutil':pkg('libavutil',root),'libswresample':pkg('libswresample',root),'libswscale':pkg('libswscale',root)},
      'spine_summary':spine,'source_id':spine.get('source_id') if spine else None,
      'full_gate2_status':'pass' if full else 'fail','steps':obs,
      'limitations':['Capabilities are implemented_uncertified, not externally certified.',
        'Direct FFmpeg library integration is not claimed when libav development packages are absent.',
        'LeakSanitizer detect_leaks remains disabled in sanitizer lanes because the container tracing model can block thread enumeration.']}
    out.parent.mkdir(parents=True,exist_ok=True);tmp=out.with_suffix(out.suffix+'.tmp');tmp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n');tmp.replace(out)
    print(f"Gate 2 evidence: full={receipt['full_gate2_status']} output={out}");return 0 if full else 1
if __name__=='__main__':raise SystemExit(main())

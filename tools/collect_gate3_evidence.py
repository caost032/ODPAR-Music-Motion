#!/usr/bin/env python3
"""Execute Gate 3 verification and emit an honest, content-addressed receipt."""
from __future__ import annotations
import argparse, hashlib, json, os, platform, re, subprocess
from pathlib import Path

CHECKS_RE = re.compile(rb"ODM TESTS checks=(\d+) passed=(\d+) failed=(\d+)")
POLICY_RE = re.compile(r"policy(?:_sha256| SHA-256)[:= ]+([0-9a-f]{64})", re.I)
ANALYSIS_RE = re.compile(r"analysis(?:\.bin)?(?:_sha256| SHA-256)[:= ]+([0-9a-f]{64})", re.I)
EXPECTED_POLICY = "cbe9ccd31eeaa27be0113f8c3481f79a48cbd08bf318adefaa665ab8fa8efaec"
EXPECTED_ANALYSIS = "0a1fce8b9fa2a3b33e5307de04668f135f1cd59c11dbbfcba700d984903bf7fc"

def sha(b: bytes) -> str: return hashlib.sha256(b).hexdigest()
def env():
    e = os.environ.copy(); e.update({'LC_ALL':'C','LANG':'C','TZ':'UTC','SOURCE_DATE_EPOCH':'0'}); return e

def run(root: Path, name: str, cmd: list[str]):
    try:
        p = subprocess.run(cmd, cwd=root, env=env(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    except subprocess.TimeoutExpired as ex:
        out = ex.stdout or b''; err = ex.stderr or b''
        return {'name': name, 'command': cmd, 'exit_code': 124, 'status': 'fail',
                'failure': 'timeout_90s', 'stdout_sha256': sha(out), 'stderr_sha256': sha(err),
                'stdout_tail': out.decode(errors='replace').splitlines()[-16:],
                'stderr_tail': err.decode(errors='replace').splitlines()[-16:]}

    norm = lambda b: b.replace(str(root).encode(), b'.')
    out, err = norm(p.stdout), norm(p.stderr)
    matches = CHECKS_RE.findall(out + err)
    rec = {
        'name': name, 'command': cmd, 'exit_code': p.returncode,
        'status': 'pass' if p.returncode == 0 else 'fail',
        'stdout_sha256': sha(out), 'stderr_sha256': sha(err),
        'stdout_tail': out.decode(errors='replace').splitlines()[-16:],
        'stderr_tail': err.decode(errors='replace').splitlines()[-16:],
    }
    if matches:
        c, pa, f = matches[-1]
        rec['last_test_totals'] = {'checks': int(c), 'passed': int(pa), 'failed': int(f)}
    txt = (out + b'\n' + err).decode(errors='replace')
    pm = POLICY_RE.search(txt); am = ANALYSIS_RE.search(txt)
    if pm: rec['music_policy_sha256'] = pm.group(1).lower()
    if am: rec['analysis_bin_sha256'] = am.group(1).lower()
    return rec

def firstline(cmd: list[str], root: Path):
    try: p = subprocess.run(cmd, cwd=root, env=env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError: return None
    return p.stdout.decode(errors='replace').splitlines()[0] if p.returncode == 0 and p.stdout else None

def pkg(name: str, root: Path):
    p = subprocess.run(['pkg-config','--modversion',name], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return p.stdout.decode().strip() if p.returncode == 0 else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args(); root = a.root.resolve()
    out = (root / a.output).resolve() if not a.output.is_absolute() else a.output

    steps = [
        ('strict_gcc_matrix',['make','test-gcc']),
        ('strict_clang_matrix',['make','test-clang']),
        ('resampler_independent_oracle',['make','resampler-oracle']),
        ('music_analysis_independent_oracle',['make','music-analysis-oracle']),
        ('asan_ubsan_gcc',['make','asan-gcc']),
        ('asan_ubsan_clang',['make','asan-clang']),
        ('tsan_gcc',['make','tsan-gcc']),
        ('gcc_static_analyzer',['make','analyze-gcc']),
        ('clean_root_reproducibility',['make','repro']),
    ]
    obs = [run(root, n, c) for n, c in steps]

    spine = None
    p = subprocess.run([str(root/'build/gcc/odpar-music'),'--spine-summary'], cwd=root, stdout=subprocess.PIPE)
    if p.returncode == 0:
        try: spine = json.loads(p.stdout)
        except json.JSONDecodeError: pass

    policy = None; analysis = None
    for x in obs:
        policy = x.get('music_policy_sha256', policy)
        analysis = x.get('analysis_bin_sha256', analysis)
    # The oracle prints are authoritative evidence; fail the gate if the known
    # canonical identities disappear or differ.
    identity_ok = (policy == EXPECTED_POLICY and analysis == EXPECTED_ANALYSIS)
    full = all(x['status'] == 'pass' for x in obs) and identity_ok

    receipt = {
        'schema':'odm_gate3_evidence_v1', 'identity_excludes_wall_clock':True,
        'platform':{'system':platform.system(),'machine':platform.machine()},
        'toolchains':{
            'gcc':firstline(['gcc','--version'],root),
            'clang':firstline(['clang','--version'],root),
            'make':firstline(['make','--version'],root),
            'python':platform.python_version(),
        },
        'media_environment':{
            'ffmpeg':firstline(['ffmpeg','-version'],root), 'ffprobe':firstline(['ffprobe','-version'],root),
            'libpng':pkg('libpng',root), 'libjpeg':pkg('libjpeg',root), 'libwebp':pkg('libwebp',root),
            'libavformat':pkg('libavformat',root), 'libavcodec':pkg('libavcodec',root),
            'libavutil':pkg('libavutil',root), 'libswresample':pkg('libswresample',root), 'libswscale':pkg('libswscale',root),
        },
        'canonical_music':{
            'sample_rate':48000, 'tick_rate_hz':100, 'samples_per_tick':480,
            'window_samples':2048, 'fft_bins':1025, 'bands':6,
            'policy_bytes':192, 'analysis_payload_header_bytes':128, 'tick_record_bytes':128,
            'music_policy_sha256':policy,
            'oracle_analysis_bin_sha256':analysis,
            'expected_music_policy_sha256':EXPECTED_POLICY,
            'expected_oracle_analysis_bin_sha256':EXPECTED_ANALYSIS,
            'identity_match':identity_ok,
        },
        'spine_summary':spine, 'source_id':spine.get('source_id') if spine else None,
        'full_gate3_status':'pass' if full else 'fail', 'steps':obs,
        'limitations':[
            'Capabilities are implemented_uncertified, not externally certified.',
            'Beat/onset/BPM/phrase/section/semantic labels remain inference and are not canonical Music Map facts.',
            'Direct FFmpeg library integration is not claimed when libav development packages are absent.',
            'LeakSanitizer detect_leaks remains disabled in sanitizer lanes because the container tracing model can block thread enumeration.',
            'The reconstructed Music Policy hash is evidence for this source and is not claimed byte-identical to the unfinished Work-session policy.',
        ],
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + '.tmp')
    tmp.write_text(json.dumps(receipt, indent=2, sort_keys=True)+'\n')
    tmp.replace(out)
    print(f"Gate 3 evidence: full={receipt['full_gate3_status']} policy={policy} analysis={analysis} output={out}")
    return 0 if full else 1

if __name__ == '__main__': raise SystemExit(main())

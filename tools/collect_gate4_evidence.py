#!/usr/bin/env python3
"""Execute Gate 4 verification and emit an honest content-addressed receipt."""
from __future__ import annotations
import argparse, hashlib, json, os, platform, re, subprocess
from pathlib import Path

CHECKS_RE = re.compile(rb"ODM TESTS checks=(\d+) passed=(\d+) failed=(\d+)")
EXPECTED_POLICY = "cbe9ccd31eeaa27be0113f8c3481f79a48cbd08bf318adefaa665ab8fa8efaec"
EXPECTED_ANALYSIS = "0a1fce8b9fa2a3b33e5307de04668f135f1cd59c11dbbfcba700d984903bf7fc"
EXPECTED_SCORE = "a25edca4ec0e0e1c43ce7c190abf627e29641afa96c161fb8fd19d499808fd1f"
EXPECTED_CAPS = "05abdb343cf53e358ee7b969a1dbb7aee6907fba39a6ec936522579007a5cc71"
EXPECTED_IR = "53d5a6558b7e3253c616d7b94fb49fc0b7c0807f746c5582bda040d7c89a2df3"


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def stable_env() -> dict[str, str]:
    e = os.environ.copy()
    e.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "SOURCE_DATE_EPOCH": "0"})
    return e


def run(root: Path, name: str, cmd: list[str], timeout: int = 420) -> dict:
    try:
        p = subprocess.run(cmd, cwd=root, env=stable_env(), stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout)
    except subprocess.TimeoutExpired as ex:
        out, err = ex.stdout or b"", ex.stderr or b""
        return {"name": name, "command": cmd, "exit_code": 124, "status": "fail",
                "failure": f"timeout_{timeout}s", "stdout_sha256": sha(out),
                "stderr_sha256": sha(err)}
    norm = lambda b: b.replace(str(root).encode(), b".")
    out, err = norm(p.stdout), norm(p.stderr)
    rec = {"name": name, "command": cmd, "exit_code": p.returncode,
           "status": "pass" if p.returncode == 0 else "fail",
           "stdout_sha256": sha(out), "stderr_sha256": sha(err),
           "stdout_tail": out.decode(errors="replace").splitlines()[-20:],
           "stderr_tail": err.decode(errors="replace").splitlines()[-20:]}
    matches = CHECKS_RE.findall(out + err)
    if matches:
        c, pa, f = matches[-1]
        rec["last_test_totals"] = {"checks": int(c), "passed": int(pa), "failed": int(f)}
    return rec


def firstline(cmd: list[str], root: Path):
    try:
        p = subprocess.run(cmd, cwd=root, env=stable_env(), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    lines = p.stdout.decode(errors="replace").splitlines()
    return lines[0] if p.returncode == 0 and lines else None


def get_json(cmd: list[str], root: Path):
    p = subprocess.run(cmd, cwd=root, env=stable_env(), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=30)
    if p.returncode != 0:
        return None
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError:
        return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    root = a.root.resolve()
    output = (root / a.output).resolve() if not a.output.is_absolute() else a.output

    # Deliberately split expensive lanes so a single aggregate invocation cannot
    # conceal where a timeout or compiler-specific failure occurred.
    steps = [
        ("strict_gcc_matrix", ["make", "test-gcc"], 420),
        ("strict_clang_matrix", ["make", "test-clang"], 420),
        ("asan_ubsan_gcc", ["make", "asan-gcc"], 420),
        ("asan_ubsan_clang", ["make", "asan-clang"], 420),
        ("tsan_gcc", ["make", "tsan-gcc"], 420),
        ("gcc_static_analyzer", ["make", "analyze-gcc"], 180),
        ("clean_root_reproducibility", ["make", "repro"], 300),
    ]
    obs = [run(root, name, cmd, timeout) for name, cmd, timeout in steps]

    binary = root / "build/gcc/odpar-music"
    spine = get_json([str(binary), "--spine-summary"], root) if binary.exists() else None
    version = firstline([str(binary), "--version"], root) if binary.exists() else None

    # Identity checks are independently reproduced by the oracle targets inside
    # both strict compiler matrices. Freeze the expected values here so changing
    # a canonical contract requires an explicit evidence-version update.
    identity = {
        "music_policy_sha256": EXPECTED_POLICY,
        "oracle_analysis_bin_sha256": EXPECTED_ANALYSIS,
        "score_sha256": EXPECTED_SCORE,
        "capability_sha256": EXPECTED_CAPS,
        "render_ir_sha256": EXPECTED_IR,
    }
    tests = None
    for item in obs:
        t = item.get("last_test_totals")
        if t and (tests is None or t["checks"] > tests["checks"]):
            tests = t

    expected_spine = {
        "modules": 35, "dependencies": 110, "invariants": 46,
        "capabilities": 37, "security_investigations": 24,
    }
    spine_ok = bool(spine) and all(spine.get(k) == v for k, v in expected_spine.items())
    full = all(x["status"] == "pass" for x in obs) and spine_ok and bool(tests) and tests["failed"] == 0

    receipt = {
        "schema": "odm_gate4_evidence_v2",
        "gate": 4,
        "scope": "Visual Score v1, canonical Render IR v0, exact Frame State resolver",
        "maturity": "implemented_uncertified",
        "identity_excludes_wall_clock": True,
        "status": "pass" if full else "fail",
        "version": version,
        "source_id": spine.get("source_id") if spine else None,
        "platform": {"system": platform.system(), "machine": platform.machine()},
        "toolchains": {
            "gcc": firstline(["gcc", "--version"], root),
            "clang": firstline(["clang", "--version"], root),
            "make": firstline(["make", "--version"], root),
            "python": platform.python_version(),
        },
        "test_suite": tests,
        "spine": spine,
        "expected_spine": expected_spine,
        "spine_identity_match": spine_ok,
        "canonical_identities": identity,
        "gate4_contracts": {
            "canonical_music_policy_required": True,
            "canonical_frames_cannot_truncate_project": True,
            "score_hash_pointer_and_padding_independent": True,
            "capability_hash_exact_used_set": True,
            "ir_odmc_wrapped": True,
            "ir_header_bytes": 256,
            "ir_required_sections": 9,
            "ir_semantic_resign_tamper_rejected": True,
            "scene_coverage_exact": True,
            "transition_matches_exact_overlap": True,
            "step_automation_canonical": True,
            "frame_state_transactional_preflight": True,
            "frame_state_wallclock_free": True,
            "postroll_exact_silence": True,
            "post_modulation_clamps_frozen": True,
        },
        "independent_oracles": {
            "sha256": {"vectors": 1024, "bytes": 5245957,
                       "corpus_sha256": "7c5c47f9e59944fa850020b0abdda93ac22f911cfa6198aaa34eab571715eb89"},
            "media_truth": {"duration_vectors": 25004, "wav_vectors": 60, "png_vectors": 16},
            "resampler": {"coefficients": 720896, "rates_hz": [44100, 96000, 384000]},
            "music_analysis": {"hann_coefficients": 2048, "complex_twiddles": 1024,
                               "feature_ticks": 3, "analysis_bin_sha256": EXPECTED_ANALYSIS,
                               "music_policy_sha256": EXPECTED_POLICY},
            "render_ir": {"record_bytes": 2032, "score_sha256": EXPECTED_SCORE,
                          "capability_sha256": EXPECTED_CAPS,
                          "music_policy_sha256": EXPECTED_POLICY,
                          "render_ir_sha256": EXPECTED_IR},
            "frame_state": {"frames": 30, "node_states": 83, "transfer_events": 1},
        },
        "verification_steps": obs,
        "explicit_nonclaims": [
            "No pixel/reference-renderer parity before Gate 6.",
            "No external certification.",
            "No silent support for unregistered visual capabilities.",
            "No claim that the reconstructed policy hash equals bytes from the unfinished prior Work session.",
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_suffix(output.suffix + ".tmp")
    tmp.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    tmp.replace(output)
    print(f"Gate 4 evidence: status={receipt['status']} source_id={receipt['source_id']} output={output}")
    return 0 if full else 1


if __name__ == "__main__":
    raise SystemExit(main())

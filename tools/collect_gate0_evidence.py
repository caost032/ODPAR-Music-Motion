#!/usr/bin/env python3
"""Execute the Gate 0 matrix and write an honest machine-readable receipt."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path


CHECKS_RE = re.compile(rb"ODM TESTS checks=(\d+) passed=(\d+) failed=(\d+)")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def tail(data: bytes, limit: int = 12) -> list[str]:
    lines = [line for line in data.decode("utf-8", errors="replace").splitlines() if line]
    return lines[-limit:]


def normalize_output(data: bytes, root: Path) -> bytes:
    return data.replace(str(root).encode("utf-8"), b".")


def environment() -> dict[str, str]:
    result = os.environ.copy()
    result.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "SOURCE_DATE_EPOCH": "0"})
    return result


def run_step(root: Path, name: str, command: list[str]) -> dict[str, object]:
    completed = subprocess.run(
        command,
        cwd=root,
        env=environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    normalized_stdout = normalize_output(completed.stdout, root)
    normalized_stderr = normalize_output(completed.stderr, root)
    combined = normalized_stdout + normalized_stderr
    check_matches = CHECKS_RE.findall(combined)
    observation: dict[str, object] = {
        "name": name,
        "command": command,
        "exit_code": completed.returncode,
        "status": "pass" if completed.returncode == 0 else "fail",
        "output_paths_normalized": True,
        "stdout_sha256": sha256(normalized_stdout),
        "stderr_sha256": sha256(normalized_stderr),
        "stdout_tail": tail(normalized_stdout),
        "stderr_tail": tail(normalized_stderr),
    }
    if check_matches:
        checks, passed, failed = check_matches[-1]
        observation["last_test_totals"] = {
            "checks": int(checks),
            "passed": int(passed),
            "failed": int(failed),
        }
    return observation


def version(command: list[str], root: Path) -> str | None:
    try:
        completed = subprocess.run(
            command,
            cwd=root,
            env=environment(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
    return lines[0] if lines else None


def load_spine(binary: Path, root: Path) -> dict[str, object] | None:
    if not binary.is_file():
        return None
    completed = subprocess.run(
        [str(binary), "--spine-summary"],
        cwd=root,
        env=environment(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        return None
    try:
        value = json.loads(completed.stdout)
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    output = (root / args.output).resolve() if not args.output.is_absolute() else args.output
    steps = [
        ("strict_gcc_matrix", ["make", "test-gcc"]),
        ("asan_ubsan_gcc", ["make", "asan-gcc"]),
        ("tsan_gcc", ["make", "tsan-gcc"]),
        ("gcc_static_analyzer", ["make", "analyze-gcc"]),
        ("clean_root_reproducibility", ["make", "repro"]),
    ]
    observations = [run_step(root, name, command) for name, command in steps]
    clang_path = shutil.which("clang")
    if clang_path:
        observations.append(run_step(root, "strict_clang_matrix", ["make", "test-clang"]))
        observations.append(run_step(root, "asan_ubsan_clang", ["make", "asan-clang"]))
    else:
        for name, target in (
            ("strict_clang_matrix", "test-clang"),
            ("asan_ubsan_clang", "asan-clang"),
        ):
            blocked = run_step(root, name, ["make", target])
            blocked["status"] = "blocked_missing_toolchain"
            blocked["reason"] = "clang executable not found; Make target failed closed"
            observations.append(blocked)
    local_observations = observations[: len(steps)]
    local_pass = all(item.get("status") == "pass" for item in local_observations)
    clang_observations = observations[len(steps) :]
    clang_pass = all(item.get("status") == "pass" for item in clang_observations)
    if local_pass and clang_pass:
        full_status = "pass"
    elif local_pass and not clang_path:
        full_status = "blocked_missing_clang"
    else:
        full_status = "fail"
    spine = load_spine(root / "build" / "gcc" / "odpar-music", root)
    receipt: dict[str, object] = {
        "schema": "odm_gate0_evidence_v1",
        "identity_excludes_wall_clock": True,
        "platform": {"system": platform.system(), "machine": platform.machine()},
        "toolchains": {
            "gcc": version(["gcc", "--version"], root),
            "clang": version([clang_path, "--version"], root) if clang_path else None,
            "make": version(["make", "--version"], root),
            "python": platform.python_version(),
        },
        "spine_summary": spine,
        "source_id": spine.get("source_id") if spine else None,
        "local_status": "pass" if local_pass else "fail",
        "full_gate0_status": full_status,
        "steps": observations,
        "limitations": [
            "Clang lanes are mandatory and are not inferred from GCC results."
        ]
        + (["Clang is unavailable in this execution environment."] if not clang_path else [])
        + [
            "LeakSanitizer detection is disabled because thread enumeration is blocked by the container tracing model; ASan and UBSan remain active."
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    temporary.replace(output)
    print(
        f"Gate 0 evidence: local={receipt['local_status']} "
        f"full={receipt['full_gate0_status']} output={output}"
    )
    return 0 if local_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())

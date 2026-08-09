#!/usr/bin/env python3
"""Prove byte-identical observable outputs across repeated executions."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path


TIMESTAMP_RE = re.compile(
    rb"(?:\b20\d{2}-\d{2}-\d{2}[T ]\d{2}:\d{2}|"
    rb"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun)\s+"
    rb"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\b)"
)


def environment() -> dict[str, str]:
    result = os.environ.copy()
    result.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "SOURCE_DATE_EPOCH": "0"})
    return result


def execute(command: list[str], expected: int = 0) -> tuple[bytes, bytes]:
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment(),
        check=False,
    )
    if completed.returncode != expected:
        raise RuntimeError(
            f"{' '.join(command)} returned {completed.returncode}; expected {expected}"
        )
    return completed.stdout, completed.stderr


def stable(command: list[str], expected: int = 0) -> tuple[bytes, bytes]:
    observations = [execute(command, expected) for _ in range(3)]
    if observations[1:] != observations[:-1]:
        raise RuntimeError(f"non-deterministic output: {' '.join(command)}")
    stdout, stderr = observations[0]
    if TIMESTAMP_RE.search(stdout + stderr):
        raise RuntimeError(f"wall-clock shaped data leaked: {' '.join(command)}")
    return stdout, stderr


def parse_json(raw: bytes, label: str) -> dict[str, object]:
    try:
        value = json.loads(raw)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid JSON for {label}: {error}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"JSON root for {label} is not an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--tests", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    tests = args.tests.resolve()
    try:
        if not binary.is_file() or not tests.is_file():
            raise RuntimeError("binary or test runner missing")
        outputs: list[bytes] = []
        for option in ("--version", "--spine-summary", "--spine", "selftest"):
            stdout, stderr = stable([str(binary), option])
            if stderr:
                raise RuntimeError(f"unexpected stderr for {option}")
            outputs.append(stdout)
            if option != "--version":
                parse_json(stdout, option)
        spine = parse_json(outputs[2], "--spine")
        modules = spine.get("modules")
        if not isinstance(modules, list):
            raise RuntimeError("Spine modules are missing")
        for module in modules:
            if not isinstance(module, dict) or not isinstance(module.get("name"), str):
                raise RuntimeError("malformed module in Spine")
            stdout, stderr = stable(
                [str(binary), f"--spine-impact={module['name']}"]
            )
            if stderr:
                raise RuntimeError(f"unexpected impact stderr for {module['name']}")
            parse_json(stdout, f"impact {module['name']}")
            outputs.append(stdout)
        test_stdout, test_stderr = stable([str(tests)])
        if test_stderr:
            raise RuntimeError("test runner emitted stderr")
        if b"failed=0" not in test_stdout:
            raise RuntimeError("test runner did not report zero failures")
        outputs.append(test_stdout)
        invalid_stdout, invalid_stderr = stable([str(binary), "--invalid"], expected=2)
        if invalid_stdout or b"usage:" not in invalid_stderr:
            raise RuntimeError("invalid-command contract changed")
        outputs.append(invalid_stderr)
        digest = hashlib.sha256()
        for output in outputs:
            digest.update(len(output).to_bytes(8, "little"))
            digest.update(output)
        print(
            "deterministic outputs: pass "
            f"({len(outputs)} streams, sha256:{digest.hexdigest()})"
        )
        return 0
    except (OSError, RuntimeError) as error:
        print(f"deterministic outputs: fail: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

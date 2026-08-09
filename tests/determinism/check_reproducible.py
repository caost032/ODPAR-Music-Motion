#!/usr/bin/env python3
"""Build in two distinct roots and compare artifacts byte for byte."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ARTIFACTS = (
    "build/generated/odm_build_meta.h",
    "build/libodpar_music.a",
    "build/odpar-music",
)


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def copy_source(source: Path, destination: Path) -> None:
    ignored = shutil.ignore_patterns("build", "__pycache__", "*.pyc", ".git")
    shutil.copytree(source, destination, ignore=ignored)


def build(root: Path, cc: str) -> None:
    environment = os.environ.copy()
    environment.update(
        {
            "LC_ALL": "C",
            "LANG": "C",
            "TZ": "UTC",
            "SOURCE_DATE_EPOCH": "0",
            "ZERO_AR_DATE": "1",
        }
    )
    completed = subprocess.run(
        ["make", "-j2", f"CC={cc}", "all"],
        cwd=root,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"reproducibility build failed in {root}:\n"
            + completed.stdout.decode("utf-8", errors="replace")
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    source = args.root.resolve()
    try:
        with tempfile.TemporaryDirectory(prefix="odm-repro-") as temporary:
            base = Path(temporary)
            first = base / "root-one" / "project"
            second = base / "a-different-and-longer-root" / "project"
            first.parent.mkdir(parents=True)
            second.parent.mkdir(parents=True)
            copy_source(source, first)
            copy_source(source, second)
            build(first, args.cc)
            build(second, args.cc)
            artifact_digests: dict[str, str] = {}
            for relative in ARTIFACTS:
                left = first / relative
                right = second / relative
                if not left.is_file() or not right.is_file():
                    raise RuntimeError(f"missing reproducibility artifact: {relative}")
                left_digest = digest(left)
                right_digest = digest(right)
                if left_digest != right_digest or left.read_bytes() != right.read_bytes():
                    raise RuntimeError(
                        f"artifact differs across build roots: {relative} "
                        f"({left_digest} != {right_digest})"
                    )
                artifact_digests[relative] = left_digest
            for option in ("--version", "--spine-summary", "--spine", "selftest"):
                left = subprocess.run(
                    [str(first / "build" / "odpar-music"), option],
                    cwd=first,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )
                right = subprocess.run(
                    [str(second / "build" / "odpar-music"), option],
                    cwd=second,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )
                if (
                    left.returncode != 0
                    or right.returncode != 0
                    or left.stdout != right.stdout
                    or left.stderr != right.stderr
                ):
                    raise RuntimeError(f"observable output differs across roots: {option}")
            print(
                "reproducible build: pass (3/3 byte-identical; "
                f"binary sha256:{artifact_digests['build/odpar-music']})"
            )
        return 0
    except (OSError, RuntimeError) as error:
        print(f"reproducible build: fail: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Run GCC's interprocedural static analyzer over every production unit."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


MODULE_RE = re.compile(
    r'^ODM_MODULE\(\s*[A-Z0-9_]+\s*,\s*[^,]+,\s*"[^"]+"\s*,\s*"([^"]+)"'
)


def production_sources(root: Path) -> list[Path]:
    result: list[Path] = []
    for line in (root / "src" / "spine" / "modules.def").read_text(encoding="utf-8").splitlines():
        match = MODULE_RE.match(line.strip())
        if match:
            result.append(root / match.group(1))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--generated", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    generated = (root / args.generated).resolve() if not args.generated.is_absolute() else args.generated
    sources = production_sources(root)
    if not sources or any(not source.is_file() for source in sources):
        print("GCC analyzer: fail: production inventory is empty or missing", file=sys.stderr)
        return 1
    include_dirs = [root / "include", generated]
    include_dirs.extend(sorted({source.parent for source in sources}, key=lambda item: item.as_posix()))
    flags = [
        "-std=c11",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wshadow",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Werror",
        "-fanalyzer",
        "-fno-diagnostics-color",
    ]
    for include_dir in include_dirs:
        flags.extend(["-I", str(include_dir)])
    try:
        with tempfile.TemporaryDirectory(prefix="odm-analyzer-") as temporary:
            directory = Path(temporary)
            for index, source in enumerate(sources):
                command = [args.cc, *flags, "-c", str(source), "-o", str(directory / f"{index}.o")]
                completed = subprocess.run(
                    command, cwd=root, capture_output=True, text=True, check=False
                )
                if completed.returncode != 0:
                    relative = source.relative_to(root).as_posix()
                    raise RuntimeError(
                        f"{relative}\n{completed.stdout}{completed.stderr}"
                    )
        print(f"GCC analyzer: pass ({len(sources)} production units)")
        return 0
    except (OSError, RuntimeError) as error:
        print(f"GCC analyzer: fail: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

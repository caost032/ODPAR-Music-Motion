#!/usr/bin/env python3
"""Compile every installed public header as a strict, standalone C11 unit."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


STRICT_FLAGS = (
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wshadow",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Werror",
    "-fsyntax-only",
)


def compile_unit(cc: str, root: Path, generated: Path, source: Path) -> None:
    command = [
        cc,
        *STRICT_FLAGS,
        "-I",
        str(root / "include"),
        "-I",
        str(generated),
        str(source),
    ]
    completed = subprocess.run(command, cwd=root, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"header compilation failed for {source.name}\n{completed.stdout}{completed.stderr}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--generated", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    generated = (root / args.generated).resolve() if not args.generated.is_absolute() else args.generated
    headers = sorted((root / "include").glob("*.h"))
    if not headers:
        print("public header check: fail: no headers", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="odm-headers-") as temporary:
            directory = Path(temporary)
            for index, header in enumerate(headers):
                unit = directory / f"header_{index}.c"
                unit.write_text(
                    f'#include "{header.name}"\n#include "{header.name}"\n'
                    "int main(void) { return 0; }\n",
                    encoding="utf-8",
                    newline="\n",
                )
                compile_unit(args.cc, root, generated, unit)
            aggregate = directory / "all_headers.c"
            aggregate.write_text(
                "".join(f'#include "{header.name}"\n' for header in headers)
                + "int main(void) { return 0; }\n",
                encoding="utf-8",
                newline="\n",
            )
            compile_unit(args.cc, root, generated, aggregate)
        print(f"public header check: pass ({len(headers)} standalone + aggregate)")
        return 0
    except (OSError, RuntimeError) as error:
        print(f"public header check: fail: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

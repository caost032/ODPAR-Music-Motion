#!/usr/bin/env python3
"""Verify release-binary hardening from ELF metadata, not build flags."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path


def run(command: list[str], root: Path) -> str:
    completed = subprocess.run(
        command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, check=False
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command returned {completed.returncode}: {' '.join(command)}\n"
            f"{completed.stdout}{completed.stderr}"
        )
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    binary = (root / args.binary).resolve() if not args.binary.is_absolute() else args.binary
    try:
        readelf = shutil.which("readelf")
        nm = shutil.which("nm")
        if not readelf or not nm:
            raise RuntimeError("readelf and nm are required")
        if not binary.is_file():
            raise RuntimeError(f"binary is missing: {binary}")
        header = run([readelf, "-hW", str(binary)], root)
        program = run([readelf, "-lW", str(binary)], root)
        dynamic = run([readelf, "-dW", str(binary)], root)
        notes = run([readelf, "-nW", str(binary)], root)
        symbols = run([nm, "-D", str(binary)], root)
        failures: list[str] = []
        if not re.search(r"Type:\s+DYN\b", header) or "PIE" not in dynamic:
            failures.append("binary is not verified PIE")
        if "GNU_RELRO" not in program:
            failures.append("GNU_RELRO segment is missing")
        stack_lines = [line for line in program.splitlines() if "GNU_STACK" in line]
        if len(stack_lines) != 1 or " RW " not in stack_lines[0] or " RWE " in stack_lines[0]:
            failures.append("GNU_STACK is missing or executable")
        if "BIND_NOW" not in dynamic:
            failures.append("immediate binding is missing")
        if "TEXTREL" in dynamic:
            failures.append("text relocations are present")
        if "Build ID:" in notes:
            failures.append("nondeterministic linker build-id is present")
        if "__stack_chk_fail" not in symbols:
            failures.append("stack protector symbol is absent")
        if "_chk@" not in symbols:
            failures.append("FORTIFY checked libc calls are absent")
        if failures:
            raise RuntimeError("; ".join(failures))
        print(
            "ELF hardening: pass (PIE, full RELRO/NOW, NX stack, canary, "
            "FORTIFY, no TEXTREL, no linker build-id)"
        )
        return 0
    except (OSError, RuntimeError) as error:
        print(f"ELF hardening: fail: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Compile a native probe and compare ABI v1 with its independent golden map."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


HEADERS = (
    "odm_executor.h",
    "odm_ffi.h",
    "odm_hash.h",
    "odm_limits.h",
    "odm_progress.h",
    "odm_wire.h",
)

CONSTANTS = (
    "ODM_FFI_ABI_VERSION",
    "ODM_FFI_ENDIAN_MARKER",
    "ODM_STATUS_OK",
    "ODM_STATUS_VERSION_MISMATCH",
    "ODM_WIRE_RECORD_HEADER_BYTES",
    "ODM_WIRE_VERSION_MAJOR",
    "ODM_WIRE_VERSION_MINOR",
)

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
    "-O2",
)


def probe_source(golden: dict[str, object]) -> str:
    layouts = golden.get("layouts")
    if not isinstance(layouts, dict):
        raise RuntimeError("golden layouts are missing")
    lines = [*(f'#include "{header}"' for header in HEADERS)]
    lines.extend(("#include <inttypes.h>", "#include <stddef.h>", "#include <stdio.h>"))
    lines.append("int main(void) {")
    for name in sorted(layouts):
        layout = layouts[name]
        if not isinstance(layout, dict) or not isinstance(layout.get("fields"), dict):
            raise RuntimeError(f"malformed golden layout: {name}")
        lines.append(f'    printf("SIZE {name} %zu\\n", sizeof({name}));')
        for field in sorted(layout["fields"]):
            lines.append(
                f'    printf("OFFSET {name} {field} %zu\\n", '
                f'offsetof({name}, {field}));'
            )
    for constant in CONSTANTS:
        lines.append(
            f'    printf("CONST {constant} %" PRIu64 "\\n", '
            f'(uint64_t)({constant}));'
        )
    lines.extend(
        (
            "    odm_ffi_abi_info info;",
            "    uint64_t required = 0u;",
            "    if (odm_ffi_abi_query(ODM_FFI_ABI_VERSION, &info, sizeof(info), &required) != ODM_STATUS_OK) return 2;",
            '    printf("RUNTIME abi_version %" PRIu32 "\\n", info.abi_version);',
            '    printf("RUNTIME feature_bits %" PRIu64 "\\n", info.feature_bits);',
            '    printf("RUNTIME required %" PRIu64 "\\n", required);',
            "    return 0;",
            "}",
        )
    )
    return "\n".join(lines) + "\n"


def parse_probe(text: str) -> dict[str, object]:
    result: dict[str, object] = {"layouts": {}, "constants": {}, "runtime": {}}
    layouts = result["layouts"]
    constants = result["constants"]
    runtime = result["runtime"]
    assert isinstance(layouts, dict) and isinstance(constants, dict) and isinstance(runtime, dict)
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "SIZE":
            layouts.setdefault(parts[1], {"fields": {}})["size"] = int(parts[2])
        elif len(parts) == 4 and parts[0] == "OFFSET":
            layouts.setdefault(parts[1], {"fields": {}})["fields"][parts[2]] = int(parts[3])
        elif len(parts) == 3 and parts[0] == "CONST":
            constants[parts[1]] = int(parts[2])
        elif len(parts) == 3 and parts[0] == "RUNTIME":
            runtime[parts[1]] = int(parts[2])
        else:
            raise RuntimeError(f"unparseable ABI probe output: {line!r}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--golden", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    library = (root / args.library).resolve() if not args.library.is_absolute() else args.library
    golden_path = (root / args.golden).resolve() if not args.golden.is_absolute() else args.golden
    environment = os.environ.copy()
    environment.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    try:
        golden = json.loads(golden_path.read_text(encoding="utf-8"))
        source_text = probe_source(golden)
        with tempfile.TemporaryDirectory(prefix="odm-abi-") as temporary:
            directory = Path(temporary)
            source = directory / "probe.c"
            binary = directory / "probe"
            source.write_text(source_text, encoding="utf-8", newline="\n")
            compiled = subprocess.run(
                [args.cc, *STRICT_FLAGS, "-I", str(root / "include"),
                 str(source), str(library), "-pthread", "-lssl", "-lcrypto", "-o", str(binary)],
                cwd=root,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if compiled.returncode != 0:
                raise RuntimeError(
                    "ABI probe compilation failed\n"
                    + compiled.stdout.decode(errors="replace")
                    + compiled.stderr.decode(errors="replace")
                )
            executed = subprocess.run(
                [str(binary)], cwd=root, env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
            )
            if executed.returncode != 0:
                raise RuntimeError(
                    f"ABI probe returned {executed.returncode}\n"
                    + executed.stderr.decode(errors="replace")
                )
        actual = parse_probe(executed.stdout.decode("ascii"))
        if actual["layouts"] != golden.get("layouts"):
            raise RuntimeError("native layouts differ from tests/golden/abi_v1.json")
        if actual["constants"] != golden.get("constants"):
            raise RuntimeError("ABI constants differ from tests/golden/abi_v1.json")
        runtime = actual["runtime"]
        if not isinstance(runtime, dict) or runtime != {
            "abi_version": golden.get("abi_version"),
            "feature_bits": golden.get("runtime_feature_bits"),
            "required": golden["layouts"]["odm_ffi_abi_info"]["size"],
        }:
            raise RuntimeError(f"runtime ABI discovery mismatch: {runtime}")
        print(
            "ABI v1 golden reconciliation: pass "
            f"({len(golden['layouts'])} structs, {len(golden['constants'])} constants)"
        )
        return 0
    except (OSError, UnicodeError, ValueError, KeyError, RuntimeError) as error:
        print(f"ABI v1 golden reconciliation: fail: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

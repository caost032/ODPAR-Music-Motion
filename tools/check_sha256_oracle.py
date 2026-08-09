#!/usr/bin/env python3
"""Differentially compare the C SHA-256 implementation with Python hashlib."""

from __future__ import annotations

import argparse
import hashlib
import os
import random
import struct
import subprocess
import tempfile
from pathlib import Path


PROBE_SOURCE = r'''
#include "odm_hash.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int read_exact(void *buffer, size_t bytes) {
    return bytes == 0u || fread(buffer, 1u, bytes, stdin) == bytes;
}

int main(void) {
    uint8_t length_bytes[4];
    while (fread(length_bytes, 1u, sizeof(length_bytes), stdin) ==
           sizeof(length_bytes)) {
        uint32_t length = (uint32_t)length_bytes[0] |
                          ((uint32_t)length_bytes[1] << 8) |
                          ((uint32_t)length_bytes[2] << 16) |
                          ((uint32_t)length_bytes[3] << 24);
        uint8_t *data = length == 0u ? NULL : (uint8_t *)malloc(length);
        odm_sha256_digest digest;
        char hex[ODM_SHA256_HEX_BUFFER_BYTES];
        uint64_t required = 0u;
        if ((length != 0u && !data) || !read_exact(data, length) ||
            odm_sha256(data, length, &digest) != ODM_STATUS_OK ||
            odm_sha256_to_hex(&digest, hex, sizeof(hex), &required) !=
                ODM_STATUS_OK ||
            required != sizeof(hex)) {
            free(data);
            return 2;
        }
        free(data);
        if (puts(hex) == EOF) return 3;
    }
    return ferror(stdin) ? 4 : 0;
}
'''


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


def vectors() -> list[bytes]:
    lengths = [
        0, 1, 2, 3, 7, 31, 32, 33, 55, 56, 57, 63, 64, 65, 127,
        128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
        4095, 4096, 4097, 65535, 65536, 1_000_000,
    ]
    rng = random.Random(0x4F445041524D5553)
    lengths.extend(rng.randrange(0, 8193) for _ in range(992))
    result: list[bytes] = []
    for index, length in enumerate(lengths):
        local = random.Random((index << 32) ^ length ^ 0x534841323536)
        result.append(local.randbytes(length))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    library = (root / args.library).resolve() if not args.library.is_absolute() else args.library
    cases = vectors()
    protocol = b"".join(struct.pack("<I", len(case)) + case for case in cases)
    expected = [hashlib.sha256(case).hexdigest() for case in cases]
    environment = os.environ.copy()
    environment.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    try:
        with tempfile.TemporaryDirectory(prefix="odm-sha-oracle-") as temporary:
            directory = Path(temporary)
            source = directory / "probe.c"
            binary = directory / "probe"
            source.write_text(PROBE_SOURCE, encoding="utf-8", newline="\n")
            compiled = subprocess.run(
                [
                    args.cc,
                    *STRICT_FLAGS,
                    "-I",
                    str(root / "include"),
                    str(source),
                    str(library),
                    "-pthread",
                    "-lssl",
                    "-lcrypto",
                    "-o",
                    str(binary),
                ],
                cwd=root,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if compiled.returncode != 0:
                raise RuntimeError(
                    "probe compilation failed\n"
                    + compiled.stdout.decode(errors="replace")
                    + compiled.stderr.decode(errors="replace")
                )
            executed = subprocess.run(
                [str(binary)],
                cwd=root,
                env=environment,
                input=protocol,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if executed.returncode != 0:
                raise RuntimeError(
                    f"probe returned {executed.returncode}\n"
                    + executed.stderr.decode(errors="replace")
                )
            actual = executed.stdout.decode("ascii").splitlines()
            if actual != expected:
                mismatch = next(
                    (index for index, pair in enumerate(zip(actual, expected))
                     if pair[0] != pair[1]),
                    min(len(actual), len(expected)),
                )
                raise RuntimeError(
                    f"digest mismatch at vector {mismatch}; "
                    f"actual_count={len(actual)} expected_count={len(expected)}"
                )
        corpus_hash = hashlib.sha256(protocol).hexdigest()
        print(
            "SHA-256 independent oracle: pass "
            f"({len(cases)} vectors, {sum(map(len, cases))} bytes, "
            f"corpus sha256:{corpus_hash})"
        )
        return 0
    except (OSError, RuntimeError, UnicodeError) as error:
        print(f"SHA-256 independent oracle: fail: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Independent integer oracle for Gate-3 canonical resampler tables.

This deliberately reimplements the fixed-point policy in Python integers rather
than calling the C coefficient generator. It compares complete coefficient
streams and SHA-256 identities for the frozen reference rates.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

CANONICAL_RATE = 48_000
PHASES = 1024
BASE_TAPS = 64
MAX_TAPS = 512
Q30_ONE = 1 << 30
Q31_ONE = 1 << 31
Q32_ONE = 1 << 32
PI_Q30 = 3_373_259_426
CORDIC_K_Q31 = 1_304_065_748
QUARTER_PHASE = 0x40000000
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1
ATAN_PHASE = (
    536870912, 316933406, 167458907, 85004756, 42667331, 21354465,
    10679838, 5340245, 2670163, 1335087, 667544, 333772, 166886,
    83443, 41722, 20861, 10430, 5215, 2608, 1304, 652, 326, 163,
    81, 41, 20, 10, 5, 3, 1, 1,
)


def round_ratio(n: int, d: int) -> int:
    if d <= 0:
        raise ValueError("non-positive denominator")
    q, r = divmod(abs(n), d)
    if r and r >= d - r:
        q += 1
    return -q if n < 0 else q


def q1_from_ratio(n: int, d: int) -> int:
    if d <= 0 or abs(n) > d:
        raise ValueError("invalid Q1.31 ratio")
    if abs(n) == d:
        return -(1 << 31) if n < 0 else (1 << 31) - 1
    encoded = round_ratio(abs(n) * Q31_ONE, d)
    if n < 0:
        return -(1 << 31) if encoded == (1 << 31) else -encoded
    return min(encoded, (1 << 31) - 1)


def q1_mul(a: int, b: int) -> int:
    p = a * b
    m = abs(p)
    q = (m >> 31) + (1 if (m & 0x7FFFFFFF) >= 0x40000000 else 0)
    if p >= 0:
        return min(q, (1 << 31) - 1)
    if q >= 1 << 31:
        return -(1 << 31)
    return -q


def q32_mul(a: int, b: int) -> int:
    p = a * b
    m = abs(p)
    q = (m >> 32) + (1 if (m & 0xFFFFFFFF) >= 0x80000000 else 0)
    limit = (1 << 63) if p < 0 else (1 << 63) - 1
    if q > limit:
        raise OverflowError("Q32.32 product")
    return -q if p < 0 else q


def trunc_div_pow2(value: int, shift: int) -> int:
    divisor = 1 << shift
    return value // divisor if value >= 0 else -((-value) // divisor)


def cordic_sin_q31(phase: int) -> int:
    quadrant = (phase & MASK32) >> 30
    offset = phase & 0x3FFFFFFF
    sign = -1 if quadrant >= 2 else 1
    z = offset if quadrant in (0, 2) else QUARTER_PHASE - offset
    x = CORDIC_K_Q31
    y = 0
    for i, angle in enumerate(ATAN_PHASE):
        xs = trunc_div_pow2(x, i)
        ys = trunc_div_pow2(y, i)
        if z >= 0:
            x, y, z = x - ys, y + xs, z - angle
        else:
            x, y, z = x + ys, y - xs, z + angle
    if sign < 0:
        y = -y
    return max(-(1 << 31), min((1 << 31) - 1, y))


def sinc_q31(x_q32: int) -> int:
    if x_q32 == 0:
        return (1 << 31) - 1
    phase = (((x_q32 & MASK64) >> 1) & MASK32)
    sine = cordic_sin_q31(phase)
    if sine == 0:
        return 0
    negative = (sine < 0) != (x_q32 < 0)
    numerator = abs(sine) << 62
    denominator = abs(x_q32) * PI_Q30
    quotient = round_ratio(numerator, denominator)
    quotient = min(quotient, 0x80000000)
    if negative:
        return -(1 << 31) if quotient >= 0x80000000 else -quotient
    return min(quotient, (1 << 31) - 1)


def taps_for_rate(rate: int) -> int:
    if rate == CANONICAL_RATE:
        return 0
    factor = (rate + CANONICAL_RATE - 1) // CANONICAL_RATE
    taps = BASE_TAPS * factor
    if taps > MAX_TAPS:
        raise ValueError("unsupported rate")
    return taps


def raw_coefficient(rate: int, taps: int, phase: int, tap: int) -> int:
    center = taps // 2 - 1
    fractional = phase << 22
    x = (tap - center) * Q32_ONE - fractional
    window_x = round_ratio(x, taps // 2)
    if rate <= CANONICAL_RATE:
        cutoff = q1_from_ratio(23, 25)
    else:
        cutoff = q1_from_ratio(23 * CANONICAL_RATE, 25 * rate)
    scaled_x = q32_mul(x, cutoff * 2)
    filter_sinc = sinc_q31(scaled_x)
    window_sinc = sinc_q31(window_x)
    return q1_mul(q1_mul(cutoff, filter_sinc), window_sinc)


def independent_table(rate: int) -> tuple[int, bytes, str]:
    taps = taps_for_rate(rate)
    if taps == 0:
        return 0, b"", hashlib.sha256(b"").hexdigest()
    encoded = bytearray(PHASES * taps * 4)
    pos = 0
    for phase in range(PHASES):
        row = [raw_coefficient(rate, taps, phase, tap) for tap in range(taps)]
        total = sum(row)
        if total <= 0:
            raise AssertionError(f"non-positive raw row {rate=} {phase=}")
        row = [round_ratio(value * Q30_ONE, total) for value in row]
        delta = Q30_ONE - sum(row)
        row[taps // 2 - 1] += delta
        if sum(row) != Q30_ONE:
            raise AssertionError("row normalization")
        for value in row:
            struct.pack_into("<i", encoded, pos, value)
            pos += 4
    data = bytes(encoded)
    return taps, data, hashlib.sha256(data).hexdigest()


PROBE_SOURCE = r'''
#include "odm_resample.h"
#include "odm_hash.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int write_u32(FILE *f, uint32_t v) {
    unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8),
                          (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    return fwrite(b, 1u, 4u, f) == 4u;
}
static int write_u64(FILE *f, uint64_t v) {
    unsigned char b[8]; unsigned i;
    for (i = 0u; i < 8u; ++i) b[i] = (unsigned char)(v >> (i * 8u));
    return fwrite(b, 1u, 8u, f) == 8u;
}
int main(int argc, char **argv) {
    uint32_t rate, taps; uint64_t count, required, i; int32_t *coeffs = NULL;
    odm_resample_plan plan; FILE *f; char hex[65]; uint64_t hex_required = 0u;
    if (argc != 3) return 2;
    rate = (uint32_t)strtoul(argv[1], NULL, 10);
    if (odm_resample_plan_requirements(rate, &taps, &count) != ODM_STATUS_OK) return 3;
    if (count != 0u) {
        if (count > (uint64_t)(SIZE_MAX / sizeof(*coeffs))) return 4;
        coeffs = (int32_t *)malloc((size_t)count * sizeof(*coeffs));
        if (!coeffs) return 5;
    }
    if (odm_resample_plan_build(rate, coeffs, count, &required, &plan) != ODM_STATUS_OK || required != count) return 6;
    if (odm_resample_plan_validate(&plan, coeffs, count) != ODM_STATUS_OK) return 7;
    if (odm_sha256_to_hex(&plan.table_sha256, hex, sizeof(hex), &hex_required) != ODM_STATUS_OK) return 8;
    f = fopen(argv[2], "wb"); if (!f) return 9;
    if (!write_u32(f, rate) || !write_u32(f, taps) || !write_u64(f, count) || fwrite(plan.table_sha256.bytes,1u,32u,f)!=32u) return 10;
    for (i = 0u; i < count; ++i) if (!write_u32(f, (uint32_t)coeffs[i])) return 11;
    if (fclose(f) != 0) return 12;
    printf("%u %u %llu %s\n", rate, taps, (unsigned long long)count, hex);
    free(coeffs); return 0;
}
'''


def compile_probe(root: Path, cc: str, library: Path, output: Path) -> None:
    source = output.with_suffix(".c")
    source.write_text(PROBE_SOURCE, encoding="utf-8")
    png_flags = subprocess.run(
        ["pkg-config", "--libs", "libpng"], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True
    ).stdout.split()
    command = [
        cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
        "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes",
        "-Wmissing-prototypes", "-Werror", "-I", str(root / "include"),
        str(source), str(library), "-pthread", *png_flags, "-lssl", "-lcrypto", "-o", str(output)
    ]
    subprocess.run(command, cwd=root, check=True)


def read_probe_file(path: Path) -> tuple[int, int, int, bytes, bytes]:
    data = path.read_bytes()
    if len(data) < 48:
        raise RuntimeError("truncated C probe output")
    rate, taps = struct.unpack_from("<II", data, 0)
    (count,) = struct.unpack_from("<Q", data, 8)
    digest = data[16:48]
    coefficients = data[48:]
    if len(coefficients) != count * 4:
        raise RuntimeError("C coefficient byte count mismatch")
    return rate, taps, count, digest, coefficients


def check_rate(root: Path, probe: Path, rate: int, temp: Path) -> dict[str, object]:
    c_file = temp / f"table-{rate}.bin"
    completed = subprocess.run(
        [str(probe), str(rate), str(c_file)], cwd=root, check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    c_rate, c_taps, c_count, c_digest, c_bytes = read_probe_file(c_file)
    py_taps, py_bytes, py_hex = independent_table(rate)
    if c_rate != rate or c_taps != py_taps or c_count != len(py_bytes) // 4:
        raise RuntimeError(f"plan metadata mismatch for {rate}: {completed.stdout.strip()}")
    actual_hex = c_digest.hex()
    if hashlib.sha256(c_bytes).digest() != c_digest:
        raise RuntimeError(f"C plan digest does not match C table bytes for {rate}")
    if c_bytes != py_bytes:
        for offset, (left, right) in enumerate(zip(c_bytes, py_bytes)):
            if left != right:
                coeff = offset // 4
                phase = coeff // c_taps
                tap = coeff % c_taps
                raise RuntimeError(
                    f"independent table mismatch rate={rate} phase={phase} tap={tap} "
                    f"byte={offset % 4}: C={left} Python={right}"
                )
        raise RuntimeError(f"independent table length mismatch for {rate}")
    if actual_hex != py_hex:
        raise RuntimeError(f"independent SHA mismatch for {rate}")
    return {
        "rate": rate,
        "taps": c_taps,
        "coefficients": c_count,
        "sha256": actual_hex,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    library = args.library if args.library.is_absolute() else root / args.library

    with tempfile.TemporaryDirectory(prefix="odm-resample-oracle-") as td:
        temp = Path(td)
        probe = temp / "resample-probe"
        compile_probe(root, args.cc, library, probe)
        results = [check_rate(root, probe, rate, temp) for rate in (44_100, 96_000, 384_000)]

    total = sum(int(item["coefficients"]) for item in results)
    print(f"Resampler oracle: pass ({total} full coefficients across {len(results)} rates)")
    for item in results:
        print(
            f"  {item['rate']} Hz: taps={item['taps']} coeffs={item['coefficients']} "
            f"sha256={item['sha256']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

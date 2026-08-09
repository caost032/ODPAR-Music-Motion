#!/usr/bin/env python3
"""Independent oracle for ODPAR Music Visual Policy v8.

This script deliberately reconstructs the canonical policy bytes from a second
set of literal constants.  It never parses procedural.c and never asks C for
expected values.  The C API is used only as the observed implementation under
test.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import struct
import sys

POLICY_BYTES = 1024
Q = 2147483647

# Literal Q1.31 policy values. Keep independent from the C source.
C005=107374182; C008=171798692; C010=214748365; C012=257698038
C015=322122547; C018=386547057; C020=429496730; C025=536870912
C030=644245094; C035=751619277; C045=966367642; C048=1030792151
C050=1073741824; C055=1181116006; C058=1245540516; C060=1288490189
C065=1395864371; C070=1503238553; C075=1610612735; C080=1717986918; C085=1825361100; C090=1932735282
D002=42949673; D004=85899346; D008=171798692; D022=472446402
D028=601295421; D038=816043786; D040=858993459; D044=944892805
D046=987842478; D052=1116691497; D056=1202590842; D062=1331439861
D068=1460288880; D072=1546188226; D078=1675037245; D085=1825361100
D088=1889785610; D092=1975684955

# Each row: core, orbit, wings, memory, tunnel, architecture, backdrop,
# depth, edge, layer_count, pane_count.
TARGETS = [
    (D056,C015,C012,D008,C020,C035,C015,C020,C025,2,0),
    (C048,D088,C020,C012,C035,C060,C020,C045,C060,4,0),
    (D046,C035,D092,C018,C030,D052,D022,C050,D078,3,2),
    (D044,D028,C025,D092,C055,C065,C030,D072,C055,3,4),
    (D040,D072,C070,C030,D092,D085,D038,C080,D085,5,2),
    (D052,C005,D002,C020,D008,C015,D002,C012,C005,1,0),
    (C045,D052,D062,D068,C045,D078,D028,C060,Q,4,3),
]

def u32(out: bytearray, v: int) -> None:
    out += struct.pack('<I', v & 0xffffffff)

def u64(out: bytearray, v: int) -> None:
    out += struct.pack('<Q', v & 0xffffffffffffffff)

def expected_policy() -> bytes:
    out = bytearray(b'ODMVPOL1')
    # Identity/header.
    for v in (8, 1, 3, 100, Q, 6, 48, 96, 4, 8, 1, 1, 1, 1, 1,
              1, 1, 1, 1, 1,
              C020, C055, C080, C035, C085, 1, 1, 96,
              4, 7, 1, 2, 3, 1, 1, 1):
        u32(out, v)

    # Advanced Composition v1 / profiled resolver.
    vals = [
        3, 3, 4, 3, 18, C018, 12, 6291456, 8, 10, C025, C010,
        C015, C048, C058, C020, C025, C060, C080, C025, C018, C090,
        C045, C090, C010, C080, C005, C030, C090, C025, C070, C090,
        C035, C090, C025, C005, C020, C055, C070, C018, C080, 5,
        8421504, 2, 2654435761,
    ]
    for v in vals: u32(out, v)

    # Visual Director v3 normalization/candidates/smoothing/dwell.
    vals = [
        C048,C058,C010,C080,C070,C090,
        C065,C045,C035,C055,C065,C055,C050,C012,D040,
        5,5,4,3,
        1200,180,10,1200,80,180,20,10,2800,180,70,
    ]
    for v in vals: u32(out, v)
    u64(out, 180 * Q)
    u64(out, 550 * Q)
    for v in (1048576,7,9,D004,C025,C020,C015,C020,C035,C020,C035,C020,C012,0xa5a5a5a5):
        u32(out, v)
    for v in (0x9e3779b97f4a7c15,0xbf58476d1ce4e5b9,0x94d049bb133111eb):
        u64(out, v)
    for v in (0,1,2,3,4): u32(out, v)

    for layout, row in enumerate(TARGETS):
        u32(out, layout)
        for v in row: u32(out, v)

    # Multiscale causal radial morphology/provenance contract.
    # Independent literals: morphology=3, provenance=16, timescale=32,
    # fast-body ceiling=0.45 Q31, release weight=0.35 Q31, then four invariants.
    for v in (3, 16, 32, C045, C035, 1, 1, 1, 1): u32(out, v)

    if len(out) > POLICY_BYTES:
        raise AssertionError(f'oracle policy overflow: {len(out)} > {POLICY_BYTES}')
    out.extend(b'\0' * (POLICY_BYTES - len(out)))
    return bytes(out)

class Digest(ctypes.Structure):
    _fields_ = [('bytes', ctypes.c_ubyte * 32)]

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--library', default='build/libodpar_music.so')
    args=ap.parse_args()
    expected=expected_policy()
    expected_sha=hashlib.sha256(expected).hexdigest()

    lib=ctypes.CDLL(args.library)
    lib.odm_visual_policy_bytes.argtypes=[ctypes.POINTER(ctypes.c_ubyte),ctypes.c_uint64,ctypes.POINTER(ctypes.c_uint64)]
    lib.odm_visual_policy_bytes.restype=ctypes.c_int
    lib.odm_visual_policy_current_sha256.argtypes=[ctypes.POINTER(Digest)]
    lib.odm_visual_policy_current_sha256.restype=ctypes.c_int

    required=ctypes.c_uint64(0)
    # Query semantics are fail-closed: NULL output tells caller exact required bytes.
    rc=lib.odm_visual_policy_bytes(None,0,ctypes.byref(required))
    if rc != 9 or required.value != POLICY_BYTES:
        print(f'FAIL query rc={rc} required={required.value}', file=sys.stderr); return 1

    arr=(ctypes.c_ubyte*POLICY_BYTES)()
    rc=lib.odm_visual_policy_bytes(arr,POLICY_BYTES,ctypes.byref(required))
    if rc != 0 or required.value != POLICY_BYTES:
        print(f'FAIL encode rc={rc} required={required.value}', file=sys.stderr); return 1
    actual=bytes(arr)
    if actual != expected:
        first=next((i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),None)
        print(f'FAIL policy bytes differ first_offset={first} actual={actual[first] if first is not None else None} expected={expected[first] if first is not None else None}', file=sys.stderr)
        return 1

    d=Digest()
    rc=lib.odm_visual_policy_current_sha256(ctypes.byref(d))
    actual_sha=bytes(d.bytes).hex()
    if rc != 0 or actual_sha != expected_sha:
        print(f'FAIL hash rc={rc} actual={actual_sha} expected={expected_sha}', file=sys.stderr); return 1

    print('VISUAL POLICY ORACLE: PASS')
    print(f'  canonical bytes: {POLICY_BYTES}')
    print(f'  encoded semantic bytes: {len(expected.rstrip(bytes([0])))} (remaining canonical zero)')
    print('  independent reconstruction: byte-exact')
    print(f'  sha256: {expected_sha}')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

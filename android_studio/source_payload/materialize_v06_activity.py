#!/usr/bin/env python3
from pathlib import Path
import base64, gzip, hashlib

BASE = Path(__file__).resolve().parent
PARTS = [BASE / f"MainActivity_v06.{i:02d}.part" for i in range(4)]
TARGET = BASE.parent / "app/src/main/java/com/odpar/musicmotion/MainActivity.java"
EXPECTED_SHA256 = "413a6f3bd68f5e97a221c8ff3f7230aa0420306593e133b77163c880dab39e59"
EXPECTED_SIZE = 46707

encoded = "".join(p.read_text(encoding="ascii").strip() for p in PARTS)
data = gzip.decompress(base64.b64decode(encoded, validate=True))
actual = hashlib.sha256(data).hexdigest()
if len(data) != EXPECTED_SIZE:
    raise SystemExit(f"v0.6 MainActivity size mismatch: {len(data)} != {EXPECTED_SIZE}")
if actual != EXPECTED_SHA256:
    raise SystemExit(f"v0.6 MainActivity sha256 mismatch: {actual} != {EXPECTED_SHA256}")
TARGET.parent.mkdir(parents=True, exist_ok=True)
TARGET.write_bytes(data)
print(f"materialized {TARGET} bytes={len(data)} sha256={actual}")

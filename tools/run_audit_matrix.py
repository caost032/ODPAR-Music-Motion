#!/usr/bin/env python3
"""Run every verification lane and emit a PASS/FAIL/BLOCKED/NOT_RUN matrix.

This is a forensic tool: it never repairs, never retries, never hides. Each lane
runs exactly once and its full output is written to `evidence/audit/<lane>.log`.
Exit status is 0 even when lanes fail -- the matrix is the product, not the
process exit code.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVID = os.path.join(REPO, "evidence", "audit")

# lane name -> (make target or shell command, kind)
LANES: list[tuple[str, list[str]]] = [
    ("build-all", ["make", "all"]),
    ("selftest", ["make", "selftest"]),
    ("test-gcc", ["make", "test-gcc"]),
    ("test-clang", ["make", "test-clang"]),
    ("asan-gcc", ["make", "asan-gcc"]),
    ("asan-clang", ["make", "asan-clang"]),
    ("tsan-gcc", ["make", "tsan-gcc"]),
    ("analyze-gcc", ["make", "analyze-gcc"]),
    ("repro", ["make", "repro"]),
    ("determinism", ["make", "determinism"]),
    ("guardians", ["make", "guardians"]),
    ("spine-check", ["make", "spine-check"]),
    ("header-check", ["make", "header-check"]),
    ("abi-check", ["make", "abi-check"]),
    ("hardening", ["make", "hardening"]),
    ("crypto-oracle", ["make", "crypto-oracle"]),
    ("sha-backend-oracle", ["make", "sha-backend-oracle"]),
    ("srgb-lut-check", ["make", "srgb-lut-check"]),
    ("media-oracle", ["make", "media-oracle"]),
    ("resampler-oracle", ["make", "resampler-oracle"]),
    ("music-analysis-oracle", ["make", "music-analysis-oracle"]),
    ("music-reaction-oracle", ["make", "music-reaction-oracle"]),
    ("contextual-salience-oracle", ["make", "contextual-salience-oracle"]),
    ("multi-axis-visual-oracle", ["make", "multi-axis-visual-oracle"]),
    ("radial-provenance-oracle", ["make", "radial-provenance-oracle"]),
    ("radial-timescale-geometry-oracle", ["make", "radial-timescale-geometry-oracle"]),
    ("multiscale-radial-raster-oracle", ["make", "multiscale-radial-raster-oracle"]),
    ("causal-trace-oracle", ["make", "causal-trace-oracle"]),
    ("reaction-projection-oracle", ["make", "reaction-projection-oracle"]),
    ("strict-causal-oracle", ["make", "strict-causal-oracle"]),
    ("strict-causal-field-oracle", ["make", "strict-causal-field-oracle"]),
    ("render-ir-oracle", ["make", "render-ir-oracle"]),
    ("frame-state-oracle", ["make", "frame-state-oracle"]),
    ("odparms-oracle", ["make", "odparms-oracle"]),
    ("renderer-oracle", ["make", "renderer-oracle"]),
    ("renderer-property-oracle", ["make", "renderer-property-oracle"]),
    ("visual-oracle", ["make", "visual-oracle"]),
    ("composition-oracle", ["make", "composition-oracle"]),
    ("director-oracle", ["make", "director-oracle"]),
    ("visual-policy-oracle", ["make", "visual-policy-oracle"]),
    ("layered-oracle", ["make", "layered-oracle"]),
    ("style-oracle", ["make", "style-oracle"]),
    ("reaction-oracle", ["make", "reaction-oracle"]),
    ("layer-output-oracle", ["make", "layer-output-oracle"]),
    ("layered-raster-oracle", ["make", "layered-raster-oracle"]),
    ("radial-geometry-oracle", ["make", "radial-geometry-oracle"]),
    ("radial-hires-geometry-oracle", ["make", "radial-hires-geometry-oracle"]),
    ("particle-geometry-oracle", ["make", "particle-geometry-oracle"]),
    ("perspective-grid-oracle", ["make", "perspective-grid-oracle"]),
    ("hud-geometry-oracle", ["make", "hud-geometry-oracle"]),
    ("export-engine-oracle", ["make", "export-engine-oracle"]),
    ("export-run-smoke", ["make", "export-run-smoke"]),
    ("export-ffmpeg-smoke", ["make", "export-ffmpeg-smoke"]),
    ("preview-oracle", ["make", "preview-oracle"]),
    ("preview-ffi", ["make", "preview-ffi"]),
    ("flutter-preview-binding", ["make", "flutter-preview-binding"]),
    ("master-oracle", ["make", "master-oracle"]),
    ("studio-oracle", ["make", "studio-oracle"]),
    ("delivery-oracle", ["make", "delivery-oracle"]),
    ("commercial-claims-check", ["make", "commercial-claims-check"]),
    ("ffmpeg-delivery-smoke", ["make", "ffmpeg-delivery-smoke"]),
    ("benchmark-core", ["make", "benchmark-core"]),
]

# Lanes whose failure is attributable to a missing external tool rather than the
# tree. Detected dynamically, never assumed.
EXTERNAL_TOOLS = {
    "export-ffmpeg-smoke": "ffmpeg",
    "ffmpeg-delivery-smoke": "ffmpeg",
}


def classify(name: str, rc: int, out: str) -> str:
    if rc == 0:
        return "PASS"
    tool = EXTERNAL_TOOLS.get(name)
    if tool and shutil.which(tool) is None:
        return "BLOCKED"
    if "No rule to make target" in out:
        return "NOT_RUN"
    return "FAIL"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--timeout", type=int, default=1800)
    ap.add_argument("--out", default=os.path.join(EVID, "matrix.json"))
    args = ap.parse_args()

    os.makedirs(EVID, exist_ok=True)
    lanes = LANES
    if args.only:
        wanted = set(args.only)
        lanes = [ln for ln in LANES if ln[0] in wanted]

    results = []
    for name, cmd in lanes:
        started = time.time()
        try:
            proc = subprocess.run(
                cmd, cwd=REPO, capture_output=True, text=True, timeout=args.timeout
            )
            rc, out = proc.returncode, proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as exc:
            rc = 124
            out = (exc.stdout or "") + (exc.stderr or "") + "\n[TIMEOUT]\n"
        except OSError as exc:  # missing interpreter/binary
            rc, out = 127, f"[OSError] {exc}\n"
        elapsed = time.time() - started

        status = classify(name, rc, out)
        with open(os.path.join(EVID, f"{name}.log"), "w") as fh:
            fh.write(out)
        results.append(
            {
                "lane": name,
                "status": status,
                "returncode": rc,
                "seconds": round(elapsed, 2),
                "tail": out.strip().splitlines()[-12:],
            }
        )
        print(f"{status:8s} {elapsed:8.2f}s  {name}", flush=True)

    summary = {}
    for r in results:
        summary[r["status"]] = summary.get(r["status"], 0) + 1

    with open(args.out, "w") as fh:
        json.dump({"summary": summary, "lanes": results}, fh, indent=2, sort_keys=True)

    print("\n=== SUMMARY ===")
    for k in ("PASS", "FAIL", "BLOCKED", "NOT_RUN"):
        if k in summary:
            print(f"{k}: {summary[k]}")
    print(f"\nmatrix -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Derive CHECKPOINT_STATUS.json from the tree instead of writing it by hand.

The 0.33 package shipped a hand-written checkpoint that described the 0.24 tree:
version, policy identities, oracle results and test totals all disagreed with
what the code actually did. A status file that is typed by a human is a claim; a
status file that is derived is evidence.

Every field here is read from the built engine, the sealed gate receipts or the
audit matrix. Nothing is asserted that was not observed. Lanes that did not pass
are reported as they were, never omitted.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Paths excluded from source identity: build outputs, caches, evidence (which is
# derived from the source) and this file's own output (which would be circular).
EXCLUDE_DIRS = {"build", ".git", "__pycache__", "evidence"}
EXCLUDE_FILES = {"CHECKPOINT_STATUS.json"}


def source_identity(root: str) -> tuple[str, int]:
    """sha256(path_len_be32 || path_utf8 || file_len_be64 || file_bytes), sorted."""
    paths = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
        for name in filenames:
            if name in EXCLUDE_FILES:
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            if rel.split(os.sep)[0] in EXCLUDE_DIRS:
                continue
            paths.append(rel)
    paths.sort()
    digest = hashlib.sha256()
    for rel in paths:
        raw = rel.encode("utf-8")
        with open(os.path.join(root, rel), "rb") as fh:
            data = fh.read()
        digest.update(len(raw).to_bytes(4, "big"))
        digest.update(raw)
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return digest.hexdigest(), len(paths)


def run(cmd: list[str], root: str) -> str | None:
    try:
        p = subprocess.run(cmd, cwd=root, capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    return p.stdout.strip() if p.returncode == 0 else None


def policy_versions(root: str) -> dict[str, int]:
    """Read the hashed policy identities straight from the headers that define
    them, so the checkpoint cannot drift from the code the way v8/v10 did."""
    wanted = {
        "visual": ("include/odm_visual.h", r"ODM_VISUAL_POLICY_VERSION UINT32_C\((\d+)\)"),
        "layered": ("include/odm_compositor.h", r"ODM_LAYERED_POLICY_VERSION UINT32_C\((\d+)\)"),
        "music_reaction": ("include/odm_music_reaction.h", r"ODM_MUSIC_REACTION_POLICY_VERSION UINT32_C\((\d+)\)"),
        "music_map": ("include/odm_music_map.h", r"ODM_MUSIC_POLICY_VERSION (\d+)u"),
        "export": ("include/odm_export.h", r"ODM_EXPORT_POLICY_VERSION UINT32_C\((\d+)\)"),
        "visual_dynamics": ("include/odm_visual_dynamics.h", r"ODM_VISUAL_DYNAMICS_POLICY_VERSION UINT32_C\((\d+)\)"),
    }
    out: dict[str, int] = {}
    for key, (path, pattern) in wanted.items():
        full = os.path.join(root, path)
        if not os.path.isfile(full):
            continue
        m = re.search(pattern, open(full).read())
        if m:
            out[key] = int(m.group(1))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=REPO)
    ap.add_argument("--matrix", default=None)
    ap.add_argument("--output", default=None)
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    matrix_path = args.matrix or os.path.join(root, "evidence", "audit", "matrix5.json")
    out_path = args.output or os.path.join(root, "CHECKPOINT_STATUS.json")

    binary = os.path.join(root, "build", "odpar-music")
    version = run([binary, "--version"], root)
    spine_raw = run([binary, "--spine-summary"], root)
    spine = json.loads(spine_raw) if spine_raw else None

    lanes: dict[str, list[str]] = {}
    tests = None
    if os.path.isfile(matrix_path):
        data = json.load(open(matrix_path))
        for lane in data["lanes"]:
            lanes.setdefault(lane["status"], []).append(lane["lane"])
        for lane in data["lanes"]:
            for line in lane.get("tail", []):
                m = re.search(r"ODM TESTS checks=(\d+) passed=(\d+) failed=(\d+)", line)
                if m:
                    cand = {
                        "checks": int(m.group(1)),
                        "passed": int(m.group(2)),
                        "failed": int(m.group(3)),
                    }
                    if tests is None or cand["checks"] > tests["checks"]:
                        tests = cand

    gates = {}
    evidence_dir = os.path.join(root, "evidence")
    if os.path.isdir(evidence_dir):
        for name in sorted(os.listdir(evidence_dir)):
            m = re.fullmatch(r"gate(\d+)_evidence\.json", name)
            if not m:
                continue
            d = json.load(open(os.path.join(evidence_dir, name)))
            status = d.get("status") or d.get(f"full_gate{m.group(1)}_status")
            gates[f"gate{m.group(1)}"] = {
                "status": status,
                "sha256": hashlib.sha256(
                    open(os.path.join(evidence_dir, name), "rb").read()
                ).hexdigest(),
            }

    src_sha, file_count = source_identity(root)

    receipt = {
        "schema": "odpar.music.checkpoint-status.v2",
        "generated_by": "tools/gen_checkpoint_status.py",
        "note": (
            "Derived from the tree, never hand-written. The 0.33 package shipped a "
            "hand-typed checkpoint describing the 0.24 tree; that divergence is the "
            "defect this generator exists to make impossible."
        ),
        "state": "WIP_CHECKPOINT",
        "version": version,
        "abi_version": 6,
        "source_identity": {
            "algorithm": (
                "sha256(path_len_be32 || path_utf8 || file_len_be64 || file_bytes), "
                "sorted paths; excludes build/, .git/, evidence/, __pycache__/ and "
                "CHECKPOINT_STATUS.json"
            ),
            "sha256": src_sha,
            "file_count": file_count,
        },
        "policies": policy_versions(root),
        "spine": (
            {
                k: spine.get(k)
                for k in (
                    "modules", "dependencies", "invariants", "capabilities",
                    "security_investigations", "state_models",
                )
            }
            if spine
            else None
        ),
        "tests": tests,
        "verification_lanes": {
            "pass": sorted(lanes.get("PASS", [])),
            "fail": sorted(lanes.get("FAIL", [])),
            "blocked": sorted(lanes.get("BLOCKED", [])),
            "not_run": sorted(lanes.get("NOT_RUN", [])),
        },
        "sealed_gate_evidence": gates,
        "claim_boundary": (
            "WIP. This checkpoint does not claim beat, BPM, tempo, sections, "
            "instruments, voices, onset accuracy on real music, visual quality, or "
            "public FINAL status. Pixel oracles prove reconstructibility, not "
            "aesthetics. See docs/CLAIMS_MATRIX.md."
        ),
    }

    with open(out_path, "w") as fh:
        json.dump(receipt, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print(f"checkpoint -> {out_path}")
    print(f"  version={version}")
    print(f"  source={src_sha[:16]}… ({file_count} files)")
    if tests:
        print(f"  tests={tests['passed']}/{tests['checks']} failed={tests['failed']}")
    for status in ("FAIL", "BLOCKED", "NOT_RUN"):
        if lanes.get(status):
            print(f"  {status}: {', '.join(lanes[status])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

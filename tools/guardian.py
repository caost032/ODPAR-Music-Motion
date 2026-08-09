#!/usr/bin/env python3
"""Fail-closed architectural guardians with mandatory negative controls."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Rule:
    identifier: str
    roots: tuple[str, ...]
    patterns: tuple[re.Pattern[str], ...]


def compiled(*patterns: str) -> tuple[re.Pattern[str], ...]:
    return tuple(re.compile(pattern, re.IGNORECASE | re.MULTILINE) for pattern in patterns)


RULES = (
    Rule(
        "NO_WALLCLOCK_IN_RENDER_CORE",
        ("src/render", "src/renderer", "src/score", "src/ir", "src/visual", "src/preview", "src/master", "src/studio", "src/compositor", "src/export"),
        compiled(
            r"\bclock_gettime\s*\(",
            r"\bgettimeofday\s*\(",
            r"\btime\s*\(",
            r"\bstd::chrono\b",
            r"\bperformance\.now\s*\(",
        ),
    ),
    Rule(
        "NO_UNSEEDED_RANDOMNESS",
        ("include", "src"),
        compiled(
            r"(?<!odm_)\brand\s*\(",
            r"(?<!odm_)\bsrand\s*\(",
            r"(?<!odm_)\brandom\s*\(",
            r"\bdrand48\s*\(",
            r"\barc4random(?:_uniform)?\s*\(",
            r"/dev/(?:u|s)?random",
        ),
    ),
    Rule(
        "NO_DIRECT_AUDIO_IN_VISUAL_EFFECTS",
        ("src/render", "src/renderer", "src/visual"),
        compiled(
            r"\b(?:decode|open|read)_audio\s*\(",
            r"\bavcodec_(?:send_packet|receive_frame)\s*\(",
            r"\bsndfile\b",
            r"\bAudioFileReadPackets\b",
        ),
    ),
    Rule(
        "NO_UI_SEMANTICS_IN_ENGINE",
        ("include", "src"),
        compiled(
            r"\b(?:QWidget|NSView|UIView|HWND|ImGuiContext)\b",
            r"\bdocument\.querySelector\s*\(",
            r"\bui_(?:button|slider|window|dialog)\b",
        ),
    ),
    Rule(
        "NO_PRESET_AS_TRUTH",
        ("src/ir", "src/render", "src/renderer", "src/visual"),
        compiled(
            r"\bpreset_(?:is_)?(?:truth|ground_truth)\b",
            r"\btrust_preset_without_validation\b",
            r"\bpreset\s*==\s*(?:truth|ground_truth)\b",
        ),
    ),
    Rule(
        "NO_PARTIAL_MASTER",
        ("src/render", "src/renderer", "src/master", "src/studio", "src/compositor", "src/export"),
        compiled(
            r"\bpublish_partial_master\s*\(",
            r"\ballow_partial_master\b",
            r"\bmaster_status\s*=\s*PARTIAL\b",
        ),
    ),
    Rule(
        "NO_NATIVE_FLOAT_IN_CANONICAL_IR",
        ("include/odm_ir", "src/ir"),
        compiled(r"\b(?:float|double|long\s+double)\b"),
    ),
    Rule(
        "NO_TIMESTAMP_BUILD_ID",
        ("include/odm_version.h", "src/core/version.c", "tools/gen_build_meta.py"),
        compiled(
            r"\b__DATE__\b",
            r"\b__TIME__\b",
            r"\b__TIMESTAMP__\b",
            r"\bBUILD_(?:DATE|TIME|TIMESTAMP)\b",
            r"\bclock_gettime\s*\(",
            r"\bgettimeofday\s*\(",
            r"\btime\s*\(",
            r"\bdatetime\.(?:now|utcnow)\s*\(",
            r"\bdate\.today\s*\(",
        ),
    ),
)

SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".m", ".mm", ".py"}
EXPECT_RE = re.compile(r"EXPECT_GUARDIAN:\s*([A-Z0-9_]+)")


def candidates(root: Path, rule: Rule) -> list[Path]:
    paths: set[Path] = set()
    for relative in rule.roots:
        base = root / relative
        if base.is_file():
            paths.add(base)
        elif base.is_dir():
            paths.update(
                path
                for path in base.rglob("*")
                if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
            )
        else:
            # A prefix such as include/odm_ir intentionally matches future headers.
            parent = base.parent
            if parent.is_dir():
                paths.update(
                    path
                    for path in parent.glob(base.name + "*")
                    if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
                )
    return sorted(paths, key=lambda path: path.relative_to(root).as_posix())


def findings_for_text(rule: Rule, text: str) -> list[tuple[int, str]]:
    findings: list[tuple[int, str]] = []
    for pattern in rule.patterns:
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            findings.append((line, match.group(0)))
    return sorted(set(findings))


def scan_repository(root: Path) -> list[tuple[str, Path, int, str]]:
    findings: list[tuple[str, Path, int, str]] = []
    for rule in RULES:
        for path in candidates(root, rule):
            text = path.read_text(encoding="utf-8", errors="replace")
            for line, match in findings_for_text(rule, text):
                findings.append((rule.identifier, path, line, match))
    return findings


def check_negative_fixtures(root: Path) -> list[str]:
    directory = root / "tests" / "guardians" / "fixtures"
    errors: list[str] = []
    seen: set[str] = set()
    fixtures = sorted(directory.glob("*.bad")) if directory.is_dir() else []
    for fixture in fixtures:
        text = fixture.read_text(encoding="utf-8", errors="strict")
        expected_match = EXPECT_RE.search(text)
        if not expected_match:
            errors.append(f"{fixture}: missing EXPECT_GUARDIAN marker")
            continue
        expected = expected_match.group(1)
        matching_rules = {
            rule.identifier for rule in RULES if findings_for_text(rule, text)
        }
        if matching_rules != {expected}:
            errors.append(
                f"{fixture}: expected only {expected}, detected {sorted(matching_rules)}"
            )
        seen.add(expected)
    expected_ids = {rule.identifier for rule in RULES}
    missing = sorted(expected_ids - seen)
    unexpected = sorted(seen - expected_ids)
    if missing:
        errors.append("negative controls missing for: " + ", ".join(missing))
    if unexpected:
        errors.append("negative controls name unknown guardians: " + ", ".join(unexpected))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--negative-fixtures", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    if args.negative_fixtures:
        errors = check_negative_fixtures(root)
        if errors:
            for error in errors:
                print(f"GUARDIAN NEGATIVE CONTROL FAILURE: {error}", file=sys.stderr)
            return 1
        print(f"guardian negative controls: pass ({len(RULES)}/{len(RULES)})")
        return 0
    findings = scan_repository(root)
    if findings:
        for identifier, path, line, match in findings:
            relative = path.relative_to(root).as_posix()
            print(f"{identifier}: {relative}:{line}: {match}", file=sys.stderr)
        print(f"architectural guardians: fail ({len(findings)} finding(s))", file=sys.stderr)
        return 1
    print(f"architectural guardians: pass ({len(RULES)} rules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

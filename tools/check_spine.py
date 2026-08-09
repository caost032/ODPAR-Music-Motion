#!/usr/bin/env python3
"""Independently reconcile the live Music Spine with files, DAG and symbols."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import deque
from pathlib import Path


MODULE_RE = re.compile(
    r'^ODM_MODULE\(\s*([A-Z0-9_]+)\s*,\s*(\d+)u?\s*,\s*"([^"]+)"\s*,'
    r'\s*"([^"]+)"\s*,\s*(\d+)u?\s*,\s*"([^"]+)"\s*\)'
)
DEP_RE = re.compile(r"^ODM_DEP\(\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*\)")
SOURCE_ID_RE = re.compile(r"sha256:[0-9a-f]{64}")
EXPECTED_GUARDIANS = {
    "NO_WALLCLOCK_IN_RENDER_CORE",
    "NO_UNSEEDED_RANDOMNESS",
    "NO_DIRECT_AUDIO_IN_VISUAL_EFFECTS",
    "NO_UI_SEMANTICS_IN_ENGINE",
    "NO_PRESET_AS_TRUTH",
    "NO_PARTIAL_MASTER",
    "NO_NATIVE_FLOAT_IN_CANONICAL_IR",
    "NO_TIMESTAMP_BUILD_ID",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def run(command: list[str], *, cwd: Path, expected: int = 0) -> bytes:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != expected:
        fail(
            f"command returned {completed.returncode}, expected {expected}: "
            f"{' '.join(command)}\nstdout:\n{completed.stdout.decode(errors='replace')}"
            f"\nstderr:\n{completed.stderr.decode(errors='replace')}"
        )
    return completed.stdout


def load_json(command: list[str], root: Path) -> dict[str, object]:
    raw = run(command, cwd=root)
    try:
        value = json.loads(raw)
    except json.JSONDecodeError as error:
        fail(f"invalid JSON from {' '.join(command)}: {error}")
    if not isinstance(value, dict):
        fail(f"JSON root from {' '.join(command)} is not an object")
    return value


def parse_inventory(path: Path) -> tuple[list[dict[str, object]], list[tuple[str, str]]]:
    modules: list[dict[str, object]] = []
    dependencies: list[tuple[str, str]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        module_match = MODULE_RE.match(stripped)
        dependency_match = DEP_RE.match(stripped)
        if module_match:
            symbol, identifier, name, source, layer, role = module_match.groups()
            modules.append(
                {
                    "symbol": symbol,
                    "id": int(identifier),
                    "name": name,
                    "path": source,
                    "layer": int(layer),
                    "role": role,
                }
            )
        elif dependency_match:
            dependencies.append(dependency_match.groups())
        elif stripped.startswith("ODM_MODULE(") or stripped.startswith("ODM_DEP("):
            fail(f"unparseable inventory declaration at {path}:{line_number}")
    if not modules:
        fail("module inventory is empty")
    return modules, dependencies


def validate_inventory(
    root: Path, modules: list[dict[str, object]], dependencies: list[tuple[str, str]]
) -> None:
    symbols = [str(module["symbol"]) for module in modules]
    names = [str(module["name"]) for module in modules]
    identifiers = [int(module["id"]) for module in modules]
    paths = [str(module["path"]) for module in modules]
    for label, values in (
        ("module symbols", symbols),
        ("module names", names),
        ("module identifiers", identifiers),
        ("module paths", paths),
    ):
        if len(values) != len(set(values)):
            fail(f"duplicate {label}")
    by_symbol = {str(module["symbol"]): module for module in modules}
    edge_set = set(dependencies)
    if len(edge_set) != len(dependencies):
        fail("duplicate dependency edge")
    dependents: dict[str, list[str]] = {symbol: [] for symbol in symbols}
    indegree: dict[str, int] = {symbol: 0 for symbol in symbols}
    direct_dependencies: dict[str, list[str]] = {symbol: [] for symbol in symbols}
    for module, dependency in dependencies:
        if module not in by_symbol or dependency not in by_symbol or module == dependency:
            fail(f"invalid dependency {module} -> {dependency}")
        direct_dependencies[module].append(dependency)
        dependents[dependency].append(module)
        indegree[module] += 1
    queue = deque(sorted(symbol for symbol, degree in indegree.items() if degree == 0))
    visited: list[str] = []
    while queue:
        current = queue.popleft()
        visited.append(current)
        for dependent in sorted(dependents[current]):
            indegree[dependent] -= 1
            if indegree[dependent] == 0:
                queue.append(dependent)
    if len(visited) != len(modules):
        fail("module dependency graph contains a cycle")
    for symbol, module in by_symbol.items():
        expected_layer = (
            max(int(by_symbol[dependency]["layer"]) for dependency in direct_dependencies[symbol])
            + 1
            if direct_dependencies[symbol]
            else 0
        )
        if int(module["layer"]) != expected_layer:
            fail(
                f"layer mismatch for {module['name']}: {module['layer']} != {expected_layer}"
            )
        source_path = root / str(module["path"])
        if not source_path.is_file():
            fail(f"registered source does not exist: {module['path']}")
    registered = set(paths)
    discovered = {
        path.relative_to(root).as_posix()
        for path in (root / "src").rglob("*.c")
        if path.is_file()
    }
    if registered != discovered:
        fail(
            "production source inventory mismatch; unregistered="
            f"{sorted(discovered - registered)}, missing={sorted(registered - discovered)}"
        )


def defined_symbols(library: Path, root: Path) -> set[str]:
    nm = shutil.which("nm")
    if not nm:
        fail("nm is required for ownership symbol reconciliation")
    output = run([nm, "-g", "--defined-only", str(library)], cwd=root).decode(
        "utf-8", errors="replace"
    )
    result: set[str] = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and not line.endswith(":"):
            result.add(parts[-1])
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    binary = (root / args.binary).resolve() if not args.binary.is_absolute() else args.binary
    library = (root / args.library).resolve() if not args.library.is_absolute() else args.library
    try:
        if not binary.is_file() or not library.is_file():
            fail("Spine binary or static library is missing")
        modules, dependencies = parse_inventory(root / "src" / "spine" / "modules.def")
        validate_inventory(root, modules, dependencies)
        report = load_json([str(binary), "--spine"], root)
        summary = load_json([str(binary), "--spine-summary"], root)
        if report.get("schema") != "odm_music_spine_v1":
            fail("unexpected full Spine schema")
        if summary.get("schema") != "odm_spine_summary_v1":
            fail("unexpected Spine summary schema")
        report_modules = report.get("modules")
        report_dependencies = report.get("dependencies")
        if not isinstance(report_modules, list) or not isinstance(report_dependencies, list):
            fail("Spine modules/dependencies are not arrays")
        by_symbol = {str(module["symbol"]): module for module in modules}
        expected_modules = [
            {
                "id": int(module["id"]),
                "name": str(module["name"]),
                "path": str(module["path"]),
                "layer": int(module["layer"]),
                "source_lines": (root / str(module["path"])).read_bytes().count(b"\n"),
                "role": str(module["role"]),
            }
            for module in modules
        ]
        if report_modules != expected_modules:
            fail("compiled module registry differs from modules.def/filesystem")
        expected_dependencies = [
            {
                "module": int(by_symbol[module]["id"]),
                "dependency": int(by_symbol[dependency]["id"]),
            }
            for module, dependency in dependencies
        ]
        if report_dependencies != expected_dependencies:
            fail("compiled dependencies differ from modules.def")
        totals = report.get("totals")
        if not isinstance(totals, dict):
            fail("Spine totals are missing")
        expected_counts = {
            "modules": len(modules),
            "dependencies": len(dependencies),
            "state_models": 11,
            "renderer_backends": 1,
            "media_adapters": 8,
            "output_profiles": 2,
            "parity_contracts": 1,
            "known_bugs": 0,
            "security_investigations": 96,
            "source_files": len(modules),
            "source_lines": sum(module["source_lines"] for module in expected_modules),
        }
        for key, expected in expected_counts.items():
            if totals.get(key) != expected or summary.get(key) != expected:
                fail(f"Spine count mismatch for {key}")
        if report.get("graph_acyclic") is not True or summary.get("graph_acyclic") is not True:
            fail("Spine does not report an acyclic graph")
        source_id = report.get("build", {}).get("source_id") if isinstance(report.get("build"), dict) else None
        if not isinstance(source_id, str) or SOURCE_ID_RE.fullmatch(source_id) is None:
            fail("source identity is not a SHA-256 identifier")
        if summary.get("source_id") != source_id:
            fail("summary and full Spine source identities differ")
        meta = binary.parent / "generated" / "odm_build_meta.h"
        if not meta.is_file():
            fail("generated build metadata is missing")
        if source_id not in meta.read_text(encoding="utf-8"):
            fail("binary source identity differs from generated metadata")
        invariants = report.get("invariants")
        ownership = report.get("ownership")
        guardians = report.get("guardians")
        capabilities = report.get("capabilities")
        state_models = report.get("state_models")
        renderer_backends = report.get("renderer_backends")
        media_adapters = report.get("media_adapters")
        output_profiles = report.get("output_profiles")
        parity_contracts = report.get("parity_contracts")
        known_bugs = report.get("known_bugs")
        investigations = report.get("security_investigations")
        registries = (
            invariants,
            ownership,
            guardians,
            capabilities,
            state_models,
            renderer_backends,
            media_adapters,
            output_profiles,
            parity_contracts,
            known_bugs,
            investigations,
        )
        if not all(isinstance(value, list) for value in registries):
            fail("one or more Spine registries are not arrays")
        assert isinstance(invariants, list)
        assert isinstance(ownership, list)
        assert isinstance(guardians, list)
        assert isinstance(capabilities, list)
        assert isinstance(state_models, list)
        assert isinstance(renderer_backends, list)
        assert isinstance(media_adapters, list)
        assert isinstance(output_profiles, list)
        assert isinstance(parity_contracts, list)
        assert isinstance(known_bugs, list)
        assert isinstance(investigations, list)
        if known_bugs:
            fail("known-bug registry must remain explicitly empty")
        expected_outputs = {"master.reference_rgba16_pcm_q31_v1": "implemented_uncertified", "delivery.mp4_h264_aac_sdr_bt709_v1": "observed_local_adapter_only"}
        actual_outputs = {
            str(item.get("id")): str(item.get("state"))
            for item in output_profiles if isinstance(item, dict)
        }
        if actual_outputs != expected_outputs:
            fail(f"output-profile registry differs from Gate 9 contract: {actual_outputs}")
        for profile in output_profiles:
            if not isinstance(profile, dict) or not (root / str(profile.get("evidence", ""))).is_file():
                fail(f"output-profile evidence missing: {profile}")
        expected_parity = {"preview.cpu_reference_semantics": "implemented_uncertified"}
        actual_parity = {str(item.get("id")): str(item.get("state")) for item in parity_contracts if isinstance(item, dict)}
        if actual_parity != expected_parity:
            fail(f"parity registry differs from Gate 8 contract: {actual_parity}")
        expected_state_models = {
            "visual.black_void": "STATELESS",
            "visual.primary_empty": "STATELESS",
            "visual.primary_image": "STATELESS",
            "visual.subject_image": "STATELESS",
            "visual.look_identity": "STATELESS",
            "visual.primary_video": "STATELESS",
            "visual.subject_video": "STATELESS",
            "visual.grid_proof": "STATELESS",
            "visual.halo": "STATELESS",
            "visual.residual_event_field": "EVENT_HISTORY",
            "visual.deterministic_particles": "EVENT_HISTORY",
        }
        actual_state_models = {
            str(item.get("capability")): str(item.get("model"))
            for item in state_models if isinstance(item, dict)
        }
        if actual_state_models != expected_state_models:
            fail(f"state-model registry differs from Gate 7 contract: {actual_state_models}")
        expected_renderers = {"cpu_reference_v1": "implemented_uncertified"}
        actual_renderers = {
            str(item.get("id")): str(item.get("state"))
            for item in renderer_backends if isinstance(item, dict)
        }
        if actual_renderers != expected_renderers:
            fail(f"renderer backend registry differs from Gate 7 contract: {actual_renderers}")
        for backend in renderer_backends:
            if not isinstance(backend, dict) or not (root / str(backend.get("evidence", ""))).is_file():
                fail(f"renderer backend evidence missing: {backend}")
        for item in state_models:
            if not isinstance(item, dict) or not (root / str(item.get("evidence", ""))).is_file():
                fail(f"state-model evidence missing: {item}")
        expected_media = {
            "wav.pcm": "implemented_uncertified",
            "png.rgba8": "implemented_uncertified",
            "flac": "recognized_only",
            "jpeg": "recognized_only",
            "webp": "recognized_only",
            "mp3": "recognized_only",
            "isobmff": "recognized_only",
            "webm": "recognized_only",
        }
        actual_media = {
            str(item.get("id")): str(item.get("state"))
            for item in media_adapters if isinstance(item, dict)
        }
        if actual_media != expected_media:
            fail(f"media adapter registry differs from Gate 2 contract: {actual_media}")
        for adapter in media_adapters:
            if not isinstance(adapter, dict):
                fail(f"malformed media adapter: {adapter}")
            if adapter.get("state") == "implemented_uncertified" and not (
                root / str(adapter.get("evidence", ""))
            ).is_file():
                fail(f"implemented media adapter evidence missing: {adapter}")
            if adapter.get("state") == "recognized_only" and not str(adapter.get("evidence", "")):
                fail(f"recognized-only adapter must explain its boundary: {adapter}")
        for invariant in invariants:
            if not isinstance(invariant, dict) or not (root / str(invariant.get("evidence", ""))).is_file():
                fail(f"invariant evidence is missing: {invariant}")
        symbols = defined_symbols(library, root)
        for pair in ownership:
            if not isinstance(pair, dict):
                fail("malformed ownership entry")
            for field in ("acquire", "release", "transfer"):
                symbol = pair.get(field)
                if isinstance(symbol, str) and symbol.startswith("odm_") and symbol not in symbols:
                    fail(f"ownership {field} symbol is not defined: {symbol}")
        guardian_ids = {
            str(item.get("id")) for item in guardians if isinstance(item, dict)
        }
        if guardian_ids != EXPECTED_GUARDIANS:
            fail("compiled guardian registry differs from independent expected set")
        for guardian in guardians:
            if not isinstance(guardian, dict) or not (root / str(guardian.get("path", ""))).is_file():
                fail(f"guardian implementation missing: {guardian}")
        allowed_states = {"implemented_uncertified", "observed_local_adapter_only", "contract_only", "not_implemented"}
        for capability in capabilities:
            if not isinstance(capability, dict) or capability.get("state") not in allowed_states:
                fail(f"invalid capability maturity: {capability}")
            if capability.get("state") == "implemented_uncertified" and not (
                root / str(capability.get("evidence", ""))
            ).is_file():
                fail(f"implemented capability evidence missing: {capability}")
        for investigation in investigations:
            if not isinstance(investigation, dict) or not (
                root / str(investigation.get("evidence", ""))
            ).exists():
                fail(f"security investigation evidence missing: {investigation}")
        if summary.get("gate0_claim") != "requires_external_gate_evidence":
            fail("Gate 0 maturity is being overclaimed")
        if summary.get("gate1_claim") != "requires_external_gate_evidence":
            fail("Gate 1 maturity is being overclaimed")
        for module in modules:
            impact = load_json([str(binary), f"--spine-impact={module['name']}"], root)
            if impact.get("schema") != "odm_spine_impact_v1" or impact.get("module_id") != module["id"]:
                fail(f"invalid impact report for {module['name']}")
        selftest = load_json([str(binary), "selftest"], root)
        if selftest.get("status") != "pass" or selftest.get("failed") != 0:
            fail("embedded selftest does not pass")
        version = run([str(binary), "--version"], cwd=root).decode("utf-8", errors="strict")
        if source_id not in version:
            fail("version output does not carry the live source identity")
        run([str(binary), "--definitely-invalid"], cwd=root, expected=2)
        print(
            "Spine reconciliation: pass "
            f"({len(modules)} modules, {len(dependencies)} edges, "
            f"{len(invariants)} invariants, {len(ownership)} ownership records, "
            f"{len(guardians)} guardians, {len(capabilities)} capabilities, "
            f"{len(investigations)} scoped investigations)"
        )
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Spine reconciliation: fail: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

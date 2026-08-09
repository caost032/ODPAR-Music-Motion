# Verification contract and current scope

No verification lane changes a capability to externally certified. A gate is
closed only when its functional tests, independent oracles, architecture
registry and required hardening/reproducibility lanes agree on the same source
identity.

## Canonical lanes

| Command | Evidence produced |
|---|---|
| `make test-gcc` | strict GCC C11 build; full test suite; selftest; guardians; live-Spine reconciliation; standalone public headers; deterministic outputs; SHA/Media Truth/resampler/Music Analysis/Render IR/Frame State/`.odparms`/CPU-renderer independent oracles; ABI golden; ELF hardening |
| `make test-clang` | the same strict functional/oracle matrix under Clang |
| `make asan-gcc` | full core test suite under GCC AddressSanitizer + UndefinedBehaviorSanitizer |
| `make asan-clang` | full core test suite under Clang AddressSanitizer + UndefinedBehaviorSanitizer |
| `make tsan-gcc` | full test suite under GCC ThreadSanitizer |
| `make analyze-gcc` | GCC `-fanalyzer` over every registered production translation unit |
| `make repro` | clean-root reproducibility: separate absolute roots must produce byte-identical canonical build artifacts/observable outputs |
| `make renderer-oracle` | independent Python reconstruction of Gate 6 fixed-point pixels, golden frames, asset SHA, frame SHA and frame-root |

`tools/guardian.py --negative-fixtures` requires one deliberately broken fixture
for every compiled guardian and requires each fixture to trigger exactly its
declared rule. `tools/check_spine.py` independently reconciles modules, paths,
line counts, DAG/layers, evidence paths, ownership symbols, capability/state
registries, security investigations and CLI impact behavior.

## Source-bound expensive lanes

Gate 6 and later may take long enough that rerunning every compiler/sanitizer lane
inside one wrapper is operationally fragile. `tools/run_evidence_lane.py` executes
one lane at a time, records its exit status plus normalized stdout/stderr hashes,
and records the content-derived source ID both before and after execution. A lane
is not accepted if the source changes while it runs.

`tools/collect_gate6_evidence.py` does not infer or recreate missing passes. It
requires all seven lane records (strict GCC/Clang, both ASan+UBSan lanes, TSan,
GCC analyzer and reproducibility), requires all records to bind to the same live
Spine source ID, and only then emits `evidence/gate6_evidence.json`.

## Sanitizer limitation represented explicitly

LeakSanitizer thread enumeration is not usable under this container tracing
model, so ASan lanes use `detect_leaks=0`; AddressSanitizer and
UndefinedBehaviorSanitizer remain active and fail fast. Lifetime/resource
regressions are additionally covered by explicit ownership/budget accounting,
fault paths and GCC static analysis. This is not represented as an equivalent
LeakSanitizer pass.

## Evidence boundary

Wall-clock runtime is observational only and is excluded from semantic identity.
Compiler version, source ID, canonical content hashes and deterministic output
bytes are retained. A PASS means the stated contracts survived the exercised
lanes; it does not imply proof over every possible machine, codec, driver or
future capability.

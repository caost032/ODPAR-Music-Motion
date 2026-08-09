# Baseline forensic audit — ODPAR Music 0.33 experimental working tree

Method: every verification lane the Makefile exposes was run exactly once against
the tree as imported, with full output captured to `evidence/audit/<lane>.log` and
a machine-readable matrix at `evidence/audit/matrix*.json`
(`tools/run_audit_matrix.py`). No lane was retried to obtain a better result and
no expected value was edited before its correct value had been reconstructed
independently.

Host: Linux x86_64, GCC 13.3.0, Clang 18.1.3, Python 3.11.15, 4 cores.

---

## 1. Inherited claims vs. observed reality

`CHECKPOINT_STATUS.json` (inherited, describing 0.24) claimed:

```
tests: 8764484 checks, 0 failed
oracles: Radial Provenance PASS, Radial Timescale Geometry PASS,
         Multiscale Radial Raster pixel-exact PASS, Causal Trace PASS,
         Visual Policy v8 byte-exact PASS, ...
```

`EXPERIMENTAL_0_33_STATUS.json` correctly warned that those identities had **not**
been reconciled for the 0.33 tree. That warning was accurate and load-bearing.

Observed on first run (`matrix.json`): **45 PASS / 16 FAIL / 1 BLOCKED**.

| Lane | Observed |
|---|---|
| test-gcc, test-clang, asan-gcc, asan-clang, tsan-gcc, determinism | FAIL — 1 check of 8,764,484 |
| multi-axis-visual, radial-provenance, radial-timescale-geometry, multiscale-radial-raster, causal-trace | FAIL |
| odparms, master, studio oracles | FAIL — host Python `cryptography` broken |
| export-run-smoke, ffmpeg-delivery-smoke | BLOCKED — no ffmpeg on host |
| commercial-claims-check | FAIL — sealed gate evidence absent from the package |
| everything else (45 lanes) | PASS |

## 2. Root causes

### A. Radial morphology semantics changed without versioning — *implementation correct, identity false*

Classification: **D (unversioned policy) + F (stale identity)**, not B (stale test).

0.33 introduced perceptual soft knees on body, release and attack in
`src/visual/procedural.c` before publishing radial provenance, and changed field
raster optics in `src/compositor/field.c` (dual-domain body authority; quadratic
tip opacity). `ODM_VISUAL_POLICY_VERSION` stayed at 8 and
`ODM_LAYERED_POLICY_VERSION` stayed at 10.

The decisive evidence that this is an identity bug and not merely stale oracles:
Layered Policy v10 encoded, as a hashed policy fact,

```c
LP(odm_wire_write_u32(&w, 1u)); /* transient tip opacity is exact same-lane attack */
```

while the tree computed `mul(attack, attack)`. **A policy hash that does not cover
the semantics it governs is not an identity.** Visual Policy v8 likewise encoded
the ungated morphology, so `visual-policy-oracle` passed while five behavioural
oracles failed — the policy was self-consistent and wrong.

The behaviour itself was evaluated on its merits and **kept** (see
`docs/RADIAL_MORPHOLOGY_V2.md` §5): knee-gating is the minimum precondition for
"no lines without cause", and publishing post-gate provenance is strictly better
than publishing pre-gate values because it keeps `final` reconstructible from the
published tuple, so a forged plan still fails closed at the render boundary.

Resolution: froze the semantics as **Visual Policy v9 / Layered Policy v11**, wrote
the normative specification first, then derived `tools/odm_radial_spec.py` from
that text, and pointed all five oracles at it. Added engine-side negative controls
proving the knees are real dead zones (a sub-knee lane publishes exactly `(0,0,0,0)`)
and that an above-knee lane on identical envelopes publishes an unattenuated attack.

The single failing unit check (`test_visual.c:405`) expected `radial_attack_q31[39]
== 50000000`, the pre-gate value. Its *intent* — lane isolation — was correct and
is preserved; the literal was stale. It now asserts the reconstructed value (0,
because 50000000 ≈ 0.023 is below the 0.08 attack knee) plus four stronger
properties, including that a sub-knee lane still publishes its own body.

### B. Two further multi-axis timescale changes, hidden behind (A)

Only visible once (A) was fixed, because the oracle aborts on first mismatch:

| Control | v8 oracle | tree (kept) |
|---|---|---|
| `core_breath` | `lowa·0.20 + broadband_attack·0.08` | `max(lowa·0.35, memory_gate·0.18)` |
| `grid` | `mid·0.20 + mid_attack·0.25` | `mid·0.25 + memory_gate·0.08` |

Both are deliberate and documented in-code, and both are correct: the grid no
longer takes a same-tick mid attack, so the background cannot whip at Halo speed
on every onset. Oracle updated to the v9 semantics.

### C. Debug `fprintf` left in production export path — *real defect*

`src/export/export.c` emitted six `DBG export …` lines to stderr on failure paths.
This is why `determinism` failed with *"test runner emitted stderr"* even when all
checks passed: the determinism lane treats any stderr as nondeterminism, correctly.
Removed; typed status already propagates the same information.

(Removing them exposed that `stdio.h` was still needed for `snprintf` in delivery
argv construction — caught by `analyze-gcc`, not by `-Werror`, and restored with
an explicit comment.)

### D. Evidence bootstrap was structurally impossible — *real defect*

`commercial-claims-check` is fail-closed against sealed `gate2…gate10` evidence.
It was listed inside `test-gcc` and `test-clang`. That evidence is produced by
running `test-gcc`/`test-clang`. The catalog could therefore only pass once it had
already passed — an unsatisfiable cycle, and the reason `commercial-claims-check`
had never run green in this package.

This is a layering inversion: a release-claim catalog is not a unit lane. Removed
from both suites; it already exists in `gate11-local`, after the lanes that seal
the evidence it consumes.

### E. Host environment — *not tree defects*

- `odparms/master/studio` oracles: the host's Debian `cryptography` package had a
  broken `_cffi_backend`. Fixed by installing working wheels. Classified E, not a
  tree fault.
- `ffmpeg`/`ffprobe` absent, and Clang's ASan runtime (`libclang-rt-18-dev`)
  absent. Installed; three lanes moved BLOCKED → PASS.
- Dart/Flutter toolchains remain absent. `flutter-preview-binding` passes its
  static parity check and reports `BLOCKED_MISSING_DART_TOOLCHAIN` for the runtime
  lane. Recorded as BLOCKED, never as PASS.

## 3. Outcome

| | first run | after reconciliation |
|---|---:|---:|
| PASS | 45 | 61 |
| FAIL | 16 | 0 |
| BLOCKED | 1 | 1 (Flutter/Dart runtime) |
| unit checks | 8,764,483 / 8,764,484 | 8,764,488 / 8,764,488 |

Sanitizers (ASan+UBSan under both GCC and Clang), TSan, the GCC static analyzer,
reproducible-build and determinism lanes are all green. LeakSanitizer stays
disabled because the container tracing model blocks thread enumeration — recorded
as a host limitation in the gate evidence, not silently dropped.

## 4. What this audit does *not* claim

- No statement about beat, BPM, sections, instruments or voices. The engine still
  deliberately does not claim them.
- Pixel oracles prove *reconstructibility*, not visual quality. Nothing here says
  the output looks good; that is a separate axis with separate gates.
- FFmpeg delivery remains a single host-local adapter observation, not codec
  certification.
- The knees frozen as v9 are memoryless. They move flicker to the knee instead of
  removing it, and they suppress genuinely soft onsets. That limitation is
  recorded in `docs/RADIAL_MORPHOLOGY_V2.md` §5 and is the motivation for Visual
  Dynamics, not something this audit fixed.

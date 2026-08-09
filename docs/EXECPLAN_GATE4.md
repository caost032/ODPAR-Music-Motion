# Gate 4 execution plan — Visual Score compiler

Status: Visual Score v1, capability registry, canonical Render IR v0 and exact
Frame State resolver implemented; final compiler/sanitizer/reproducibility lanes
are the Gate 4 exit evidence.

## Frozen contracts

- Visual Score is authored data, not runtime mutable renderer state.
- Project/scene/cue/automation/transition coordinates are 48 kHz sample integers.
- Every Scene owns exactly one Environment and one Primary Composition.
- Only an explicit Transition Compositor may mix two adjacent scenes.
- Resources are immutable and SHA-256-addressed.
- Every visual capability declares an explicit state model.
- Score identity excludes host pointers/padding.
- Render IR is fixed-width little-endian, pointer-free and float-free.
- Required IR sections have one canonical order and version.
- IR binds Score, capability set, Music Map and Music Policy identities.
- Frame State is resolved only from exact frame index/sample coordinates.
- Music modulation consumes exact Music Map facts; visual code does not analyze
  audio.
- Failed validation or arithmetic never publishes partial Frame State.

## Implemented evidence

- Host/ABI-independent Score and capability hashes.
- Size-first deterministic Score-to-IR compiler.
- Semantic IR validator able to reject correctly re-hashed hostile records.
- Exact capability use/declaration parity.
- Exact transition-to-adjacent-overlap parity.
- Parameter-range enforcement at both Score and compiled-IR boundaries.
- Exact frame/sample conversion for all supported v0 FPS.
- Exact transition weighting, automation and Music Map modulation.
- Explicit state-transfer publication at frame intervals.
- Exact post-roll silence semantics.
- Repeated compilation byte-identical on the canonical test fixture.
- Music Spine state-model mappings for every implemented visual capability.
- Strict compilers, sanitizers, static analysis, determinism and clean-root
  reproducibility as exit lanes.

## Explicit non-claims

- Gate 4 does not render pixels or claim reference-renderer parity.
- Gate 4 does not implement the later Halo/Grid/particle/Residual Event Field
  visual systems.
- The current visual capability set is deliberately small; unsupported required
  semantics fail instead of degrading silently.
- `implemented_uncertified` is not external certification.

## Exit rule

Gate 5 may package only a Score that validates and a Render IR that independently
validates against the expected Score/Music Policy identity. Gate 6 may render
only validated IR through the exact Frame State resolver; it must not implement
a second timeline or reinterpret Score semantics.

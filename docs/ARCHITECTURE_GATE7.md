# Gate 7 — Deterministic Visual Systems

Gate 7 extends the canonical Score/Render-IR contract without creating a second visual truth.
The authoritative path remains `Score -> Render IR -> Frame State -> CPU reference renderer`.

## Schema evolution

Score and Render IR advance from schema 1.0 to **1.1**. The fixed Render-IR node record remains
128 bytes; fields previously reserved in v1.0 carry the new semantics, so no variable or host ABI
layout is introduced.

Every node now binds, explicitly and canonically:

- `state_slot` (zero for stateless nodes, nonzero for stateful nodes),
- `entry_kind` / `exit_kind`,
- exact `entry_duration_samples` / `exit_duration_samples`.

Gate 7 v1 implements `NONE` and `FADE_SCALE`. Frame State publishes exact Q1.31 entry/exit
weights. Q1.31 `INT32_MAX` is treated as exact semantic identity rather than routed through a
generic multiply that would lose one LSB.

## Visual memory

`ODM_EVENT_HISTORY` is a real state model, not metadata. State Transfer is resolved at exact scene
boundaries:

- `COPY`: the destination state slot inherits the source ancestry.
- `RESET`: ancestry stops at the boundary.

The resolver is bounded to 64 transfer hops and publishes at most the **latest 32 canonical cues**.
Events are ordered deterministically and include exact sample age and a domain-separated seed. A
COPY is only valid when the same stateful slot exists on both sides; RESET requires the destination
slot to exist. Invalid or ambiguous state topology fails closed during Score/IR validation.

## Deterministic systems

Gate 7 defines four semantic systems:

1. **Grid Proof** — stateless integer/fixed-point grid field.
2. **Halo** — stateless integer/fixed-point radial field.
3. **Residual Event Field** — event-history-driven residual energy field.
4. **Deterministic Particles** — event-history-driven particles with domain-separated seeds.

No native `float`/`double`, wall clock, random device, audio polling or preset-name truth defines
canonical pixels. Procedural systems output premultiplied Q1.31 RGBA and are composited by the same
reference renderer as resource-backed nodes.

## Performance boundary

History is resolved **once per visible stateful node per frame**, before the pixel loop. The pixel
loop consumes the bounded precomputed event history; it does not walk transfers or scan cues per
pixel. This keeps semantic authority deterministic while avoiding an accidental O(pixels × cues)
architecture.

## Evidence boundary

The independent visual oracle reimplements seed derivation, COPY/RESET ancestry, latest-32
retention, Entry/Exit, procedural formulas, Q1.31 identity behavior, source-over blending and
RGBA16LE quantization in Python. It compares complete frames, not selected sample pixels.

Maturity at Gate 7 closure is `implemented_uncertified`. Gate 7 does **not** claim GPU parity,
Flutter preview, HDR/wide-gamut output, or external certification.

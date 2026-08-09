# Roadmap vNext

Ordered by dependency, not by appeal. Each milestone states its exit condition,
because "done" without an exit condition is a mood.

Status legend: **DONE** · **NEXT** · planned

---

## M0 — Reconciled baseline · **DONE**

Import, forensic audit, root-cause every failure, freeze the changed semantics
with real identities.

Exit: every applicable lane green or explicitly BLOCKED with a demonstrable
external cause; version, policies, Spine, tests, oracles and docs describe one
tree. → 62 PASS / 0 FAIL, 8.9M checks. See `docs/BASELINE_AUDIT.md`.

## M1 — Visual Dynamics v1 · **DONE**

Sample-domain response kernels between evidence and geometry.

Exit: FPS invariance, seek exactness and monotone release proven as properties
rather than goldens; independent oracle reconstructs the kernel from the spec.
→ `docs/VISUAL_DYNAMICS_V1.md`, 4837 oracle responses exact.

## M2 — Wire Visual Dynamics into the visual path · **NEXT**

Nothing consumes the kernels yet. Route Composition targets through them: core
scale/breath, radial tip, halo body, grid, background, particles, memory field.
Each target declares its class; the class table stays the only place temporal
character is decided.

Exit: a rendered impulse shows a visible, monotone relaxation at 60 fps; the
existing radial/composition oracles still pass or are re-derived with a versioned
policy bump; a slow-motion preview at 0.25× shows no one-frame flicker on the
diagnostic scenes.

Risk to watch: this changes every visual golden. Bump Visual Policy to v10 and
re-derive from spec — never edit goldens to match output.

## M3 — Transient lane + trajectory novelty

Multirate evidence (§3–4 of `ARCHITECTURE_VNEXT.md`). 100 Hz macro map stays;
add a ≥200 Hz transient lane, sample-domain event coordinates, and a
trajectory-aware multi-resolution novelty front end.

Exit: the synthetic corpus discriminates spectral *movement* from *new energy* —
specifically, vibrato produces no onset while a real attack during vibrato does.
Strictly causal path preserved and separately tested.

## M4 — Relax the admission knees

Only safe after M2: once persistence comes from response kernels, the knee no
longer has to prevent flicker, so it can be lowered to admit soft bowed and
brushed onsets. Repairs the limitation recorded in
`RADIAL_MORPHOLOGY_V2.md` §5.

Exit: soft-onset corpus admitted; no regression in the flicker diagnostics from
M2; Visual Policy versioned again.

## M5 — Rational frame rates

Numerator/denominator timeline; 24000/1001, 30000/1001, 60000/1001
representable. Drift proven absent over millions of frames.

Exit: no accumulated drift over a full-length project at each rate; event
identity unchanged across rates (metamorphic FPS CHANGE at the timeline level).

## M6 — Media adapter boundary

Real audio in: MP3, M4A/AAC, FLAC, OGG, Opus. Real images: JPEG, WebP. Hardened
adapter at the canonicalisation boundary; decoder version and provenance
recorded; hostile-input corpus (truncation, bombs, absurd durations, corrupt
timestamps, VFR, NaN/Inf from any float adapter) fails closed.

Exit: the user's own tracks import and analyse end to end; fuzzing lane green;
after canonicalisation the external decoder is no longer authority.

## M7 — Rhythm and structure evidence

Tempo candidates with confidence and ambiguity; boundaries with strength and
sources. No labels. Director consumes boundaries.

Exit: ambiguous material reports ambiguity instead of inventing a number;
Director builds a whole-song arc rather than reacting per onset.

## M8 — Raster quality and typography

Quality tiers, analytic/SDF antialiasing, subpixel geometry, high-quality
resampling, controlled bloom, dithered gradients, real text shaping with hashed
font assets.

Exit: pixel-peep QA at 100/200/400% on text, progress bar, circle edge, thin
radial lines and core mask shows no aliasing or bitmap text; preview and master
differ only inside the documented raster boundary.

## M9 — Reaction Inspector

On-demand causal traces: pixel/target → response → source → evidence → audio
interval.

Exit: for any frame and target, the chain can be printed and independently
verified against the oracles.

## M10 — One extremely polished scene

Not ten mediocre ones. Background, core, spectral object, minimal structural
field, minimal HUD — taken to a quality bar that survives a frozen frame.

Exit: the ZERO CONFIG BEAUTY gate — import audio and an image, touch nothing,
and the default output already looks professional. If it needs forty sliders to
stop looking like a prototype, the defaults are the bug.

## M11 — Studio

Flutter + FFI shell around the real engine: media, canvas, design, reaction,
analysis inspector, scenes, timeline, export, project. Editable project format
kept separate from the immutable render package.

Exit: import → preview → edit → save/restore → seek → render → export with
audio/video sample-synchronised, receipt emitted, and a byte-identical repeat of
the canonical render.

---

## Standing rules for every milestone

1. Tests before the milestone; evidence and docs after.
2. No green commit that is green because an expected value was edited.
3. A lane blocked by tooling is BLOCKED, never PASS, and never blocks unrelated
   work that can proceed.
4. Any change to hashed semantics moves the policy version in the same commit.

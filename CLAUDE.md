# ODPAR Music — working doctrine

Only doctrine that must survive **every** session lives here. Procedures live in
`docs/`. If this file starts describing how to do things, move that part out.

ODPAR Music is a verifiable audiovisual compiler, not a music visualizer. The
distinguishing property is that it tries to *prove* what it does. Never trade that
away for a nicer frame or a faster loop.

## 1. The authority chain

```
MEDIA TRUTH → CANONICAL AUDIO → MUSIC EVIDENCE → MUSIC INFERENCE →
VISUAL DYNAMICS → VISUAL COMPOSITION / DIRECTOR → FRAME PLAN →
RENDERER → PREVIEW / MASTER → DELIVERY
```

A layer may derive from the layer below it. **No layer may retroactively modify
the truth of a lower one.** UI, Flutter, JavaScript, FFmpeg and any preview or GPU
backend are downstream consumers; none of them may reinterpret music.

## 2. Non-negotiables

1. Wall-clock time never defines render state. FPS is not a musical clock.
2. No unseeded randomness in render semantics. `Math.random()`-shaped code never
   enters a musical or visual path.
3. Visual systems never decode or analyze audio. They consume typed resolved state.
4. Presets are never authoritative; they compile to explicit effective parameters.
5. Preview and master share Render IR and Frame State semantics. Preview may
   degrade raster quality only — never timing, values, seeds, population, map or
   scene selection.
6. Failed or cancelled work never becomes a successful master.
7. Unsupported capability fails explicitly. No silent semantic fallback.
8. The CPU reference renderer is the oracle. Other backends prove parity against
   it, or are labelled non-authoritative.
9. Never claim beat, BPM, section, instrument or voice without evidence and a
   published confidence. Ambiguity is a legitimate, reportable result.
10. An uncertain semantic detector never overwrites exact DSP evidence.

## 3. Changing frozen semantics

This is the rule the 0.33 experiment broke, and the most expensive one to get
wrong. See `docs/BASELINE_AUDIT.md` for what it cost.

**A policy identity that does not cover the semantics it governs is not an
identity.** If you change what a hashed policy describes, the version must move in
the same commit.

Order of operations — never reorder, never skip:

1. Reconstruct mathematically what the value *should* be. Decide on the merits
   whether implementation or oracle is wrong. **Never pick "stale test" because it
   is convenient.**
2. Write the normative specification in `docs/` *first*.
3. Derive the independent checker from that text, not from `src/`.
4. Bump the policy version and encode the new constants into the policy bytes.
5. Add negative controls that observe the **engine**, not the checker against
   itself.
6. Update golden data and hashes — taking hashes from the independent oracle, not
   from the implementation under test.
7. Reconcile Spine, docs and checkpoint identity.
8. Only now touch the test.

Corollary: never edit an expected value to make a lane green. If you cannot derive
the value independently, you do not yet understand the change.

## 4. Evidence discipline

- A lane is PASS, FAIL, BLOCKED or NOT_RUN. Missing a tool is BLOCKED, never PASS.
- Any stderr from the test runner is a determinism failure. Debug printing does not
  belong in engine code; typed status already carries the information.
- Verification lanes must be bootstrappable. A gate that can only pass once it has
  already passed is a layering inversion, not a strict gate.
- `make all` builds `-Werror` with `-Wconversion -Wsign-conversion -Wshadow`.
  `analyze-gcc` catches things `-Werror` does not. Run it before believing a build.
- Docs that disagree with executables are bugs, not documentation debt.

## 5. Reference material is not authority

`references/signal_studio_concept/` is a product-intent prototype. Its
`AnalyserNode`, four-band split, bass-threshold "beat", `Math.random()` and
`MediaRecorder` export must never migrate into the engine. Harvest vocabulary
(timeline, halo, grid, scenes, aspect authority), redesign around the real engine.

## 6. Visual quality is a product requirement, not decoration

A frozen frame from anywhere in a render should read as deliberate motion design.
Four independent axes — raster quality, composition, motion, art direction — and
4K does not fix the last three. Effects are the last layer; if a scene only works
with heavy glow, the composition is weak.

Specifically forbidden by default: lines that appear without cause, lines that
persist without causal memory, one-frame flicker, procedural motion in
STRICT_CAUSAL, camera shake, permanent glitch, rainbow spectrum, and anything
whose reaction changes because FPS changed.

## 7. Autonomy

Investigate, measure, choose, document. Ask only when a decision changes the
product, public identity, licensing, important public compatibility, or the
primary subjective aesthetic direction — or when two strong technical options
genuinely need a human preference.

Deliver working tree changes, not plans.

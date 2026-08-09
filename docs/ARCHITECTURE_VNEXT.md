# Architecture vNext

What the engine is, what it is becoming, and — more usefully — the specific
structural reasons the current shape cannot reach the product goal without
changing. Grounded in the tree as it exists after the 0.33 reconciliation
(`docs/BASELINE_AUDIT.md`), not in aspiration.

---

## 1. Current shape, honestly

```
Media Facts → Music Map (100 Hz) → Music Reaction (96 lanes, 6 families)
           → Composition (multi-axis) → Director (scene) → Layered Frame Plan
           → CPU reference raster → Export → Delivery
```

Genuinely strong, and worth protecting:

- Fixed-point everywhere; no float in canonical semantics.
- Every stage has a hashed policy identity and an independent Python oracle that
  reconstructs expectations rather than reading them back from C.
- Render boundary is fail-closed: a frame plan whose provenance does not
  reconstruct its own `final` is rejected.
- 8.9M checks, sanitizers under two compilers, TSan, static analyzer, determinism
  and reproducible-build lanes.

Real structural limits, each with a consequence rather than a complaint:

| Limit | Consequence |
|---|---|
| Music Map is 100 Hz only | Attack timing quantised to 10 ms; a transient front-end cannot be sharper than the map |
| Integral FPS via `samples_per_frame` | 24000/1001, 30000/1001, 60000/1001 are unrepresentable |
| Novelty is same-lane + fixed ±1 neighbour at 3/4 | Vibrato still produces false onsets; a moving partial is only partly explained |
| No rhythm layer at all | Director has no periodicity to build an arc from |
| No structure layer | Scene changes cannot align to musical boundaries |
| Native decode is WAV/PNG only | Real songs cannot enter without an adapter |
| 5×7 bitmap typography | HUD is pixelated at any master resolution |
| One raster quality | No supersampling path; master and preview differ only in size |

## 2. The load-bearing insight

The defect the user sees in slow motion is **not** a resolution problem, and four
independent axes are involved:

```
raster quality   — aliasing, pixelation, bad resampling
composition      — hierarchy, negative space, proportion
motion           — response character, timing, easing
art direction    — palette, material, restraint
```

4K fixes only the first. A visual can be preschool-grade *in 4K*. The engine
therefore needs the layer that was missing between evidence and geometry:

```
MUSIC EVIDENCE → VISUAL DYNAMICS → DESIGN SYSTEM → COMPOSITION → RASTER
```

Visual Dynamics v1 (`docs/VISUAL_DYNAMICS_V1.md`) exists as of this checkpoint.
It is the piece that makes "receives the blow, then relaxes" expressible at all,
and it makes FPS invariance and seek exactness *structural* rather than tested.

## 3. Multirate evidence — the next structural change

100 Hz should stay as the **stable macro map** (compatible, cheap, well tested)
but must stop being the temporal ceiling of the whole system.

| Lane | Rate | Owns |
|---|---|---|
| MACRO/STABLE | ~100 Hz | sustained state, level, timbre |
| TRANSIENT | ≥200 Hz, hop ~240 @48 kHz | attack detection, novelty |
| SAMPLE-DOMAIN EVENT | exact sample | the published coordinate of an event |
| MACRO STRUCTURE | ~2–5 Hz | boundaries, density regime, contrast |

The critical rule: **do not replace one representation with another. Fuse them
with explicit provenance.** A radial should be able to say which lane, at which
rate, at which window size, produced it.

Window sizes must differ by purpose — attacks, tonal body and structure do not
need the same time/frequency trade-off. 1024 / 2048 / 4096 is the obvious
starting grid, to be measured rather than assumed.

Visual Dynamics already consumes sample coordinates, so this lands without
reworking the visual layer — which is why it was built first.

## 4. Spectral trajectory, properly

The 0.33 idea — compare a lane against its own past *and* weighted neighbours, so
a partial moving in frequency does not read as new energy — is well founded. The
implementation (fixed ±1, fixed 3/4) is a first sketch, not the answer.

The direction is a trajectory-aware, multi-resolution novelty front end:
log-frequency representation, maximum filtering across frequency, positive
spectral differences, multiple temporal offsets, optional whitening. A neighbour
may **explain** displaced energy; it may never **fabricate** positive energy.

Non-negotiable: a strictly causal path must survive. Offline improvements are
allowed but must be labelled `OFFLINE_GLOBAL` and must never leak into an API
that advertises `STRICT_CAUSAL`.

Synthetic corpus this must be proven against, before any claim: steady sine,
vibrato, tremolo without pitch change, sweep, chirp, glissando, two tones
crossing, real attack *during* vibrato, soft bowed onset, bass transient, noise
burst, silence↔signal.

## 5. Rhythm and structure, without lying

Add **evidence**, not answers.

Rhythm Evidence publishes onset envelopes, a tempogram, periodicity candidates,
multiple tempo hypotheses with octave ambiguity flagged, confidence, phase
candidates. `ambiguous = true` with two candidates is a *correct* result, not a
failure. Beat/downbeat inference is a separate, optional module on top; any
learned model must be versioned, hashed, confidence-publishing, and must have a
deterministic DSP fallback. The visuals must work perfectly without it.

Structure Evidence publishes `boundary_sample`, `boundary_strength`, `confidence`,
`evidence_sources`, `recurrence_group`. Detecting *"a strong boundary exists
here"* is a different claim from *"this is the chorus"*, and only the first is
made. The Director consumes boundaries without needing any label.

## 6. Time: rational frame rates

`samples_per_frame` as an integer cannot express 30000/1001. Replace with a
rational numerator/denominator timeline where each frame derives its exact
presentation sample by cumulative rational arithmetic that provably does not
drift over millions of frames. Rounding and duration policy become explicit and
versioned. Music-Reaction projection keeps sample authority.

## 7. Raster quality

Quality tiers `PREVIEW_FAST / PREVIEW_HIGH / MASTER / MASTER_ULTRA` decide raster
fidelity **only**. They must never change event timing, routing, response state
or scene decisions — the same project at the same presentation sample must
produce the same semantic state at every tier, with differences confined to a
documented raster boundary.

Required: analytic/SDF coverage antialiasing, subpixel geometry held until
rasterisation, high-quality resampling for core media, thresholded multi-scale
bloom with energy conservation, deterministic dithering against banding in dark
gradients, and real typography (shaping, kerning, tabular numerals, versioned and
hashed font assets) replacing the 5×7 bitmap.

## 8. Explainability — the Reaction Inspector

The highest-leverage debugging tool this project can have, because it converts
"that line looks wrong" from an opinion into a query:

```
presentation sample → visual target → response state → reaction source
                    → event / lane / family → music evidence → audio interval
```

Traces on demand with compact IDs; no permanent per-pixel graph. Visual Dynamics
state is already a small POD of fixed-width integers, which is what makes a
trace cheap to capture and compare.

## 9. What must not change

Everything in `CLAUDE.md` §2. In particular: no layer may retroactively modify a
lower one; the CPU reference renderer stays the oracle; preview may degrade
raster quality only; and every hashed policy identity must cover the semantics it
governs — the failure mode that cost this checkpoint its first full audit.

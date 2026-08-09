# Radial Morphology v2 — normative specification

Status: NORMATIVE. Frozen by `ODM_VISUAL_POLICY_VERSION 9` (composition side) and
`ODM_LAYERED_POLICY_VERSION 11` (raster side).

Supersedes: the ungated morphology frozen by Visual Policy v8 / Layered Policy v10.

This document is the *source* of the independent oracles. `tools/odm_radial_spec.py`
is written from this text, not from `src/`. If the two disagree, one of them is a
bug and the disagreement is the finding.

---

## 0. Why this document exists

The 0.33 experimental working tree changed two things about how a spectral lane
becomes a visible radial, and versioned neither:

1. **Composition** (`src/visual/procedural.c`) began gating body, release and
   attack through perceptual soft knees before publishing provenance.
2. **Field raster** (`src/compositor/field.c`) replaced flat body opacity with a
   dual-domain activity authority, and replaced linear tip opacity with a
   quadratic one.

Visual Policy stayed at v8 and Layered Policy stayed at v10. Layered Policy v10
still asserted, as a hashed policy fact:

```
/* transient tip opacity is exact same-lane attack */
```

while the implementation computed `attack²`. A policy identity that does not
cover the semantics it governs is not an identity. Five independent oracles and
one unit check failed against the tree precisely because they were still
describing v8/v10 truth.

Both behavioural changes are **kept** — they are perceptually correct (§5) — and
are now frozen, hashed and independently reconstructed.

---

## 1. Numeric domain

All morphology values are unsigned Q1.31 with unity

```
Q = 2147483647            (2^31 - 1, ODM's COMP_Q31_ONE)
```

Saturating multiply, round-half-up, used by both composition and raster:

```
mul(a, b) = min(Q, (a*b + 1073741823) / Q)          integer division
```

`1073741823 = floor(Q/2)`. Composition (`comp_mul_u31`) and field raster
(`field_mul_q31`) are bit-identical under this definition; the raster spells its
unity `INT32_MAX`, which is the same value.

Saturating add:

```
add(a, b) = min(Q, a + b)
```

## 2. Soft knee (perceptual dead zone + smoothstep)

```
knee(v, lo, hi):
    if v <= lo            -> 0
    if v >= hi or hi <= lo -> Q
    t  = (v - lo)*Q + (hi-lo)/2, integer-divided by (hi - lo)     # round-half-up
    t2 = mul(t, t)
    t3 = mul(t2, t)
    r  = 3*t2 - 2*t3           # exact uint64, NOT re-divided by Q
    return min(Q, max(0, r))
```

`3t² − 2t³` is the standard smoothstep. Because `t2`/`t3` are already Q1.31, the
polynomial is evaluated without a further scaling division; this is exact.

**Dead zone below `lo` is hard and intentional**: sub-threshold spectral wobble
must not be able to publish a visible component. **Above `hi` the gate is exactly
unity**: a real event is never attenuated.

## 3. Composition morphology (Visual Policy v9)

Per spectral lane `i ∈ [0, 96)`, inputs are the three same-lane reaction values
`lane_q31` (slow), `lane_fast_q31` (fast), `lane_attack_q31` (same-tick attack).

### 3.1 Frozen knee constants

| Gate | `lo` (Q31) | `lo` ≈ | `hi` (Q31) | `hi` ≈ |
|---|---:|---:|---:|---:|
| body | `107374182` | 0.05 | `429496729` | 0.20 |
| attack | `171798692` | 0.08 | `536870912` | 0.25 |
| release | `64424509` | 0.03 | `322122547` | 0.15 |

Note: the body `hi` literal is `429496729`, one ULP below the named constant
`COMP_Q31_020 = 429496730`. This specification freezes the **literal**, because
that is the value the 0.33 tree evaluated and the value all evidence was produced
under. Changing it is a separate, benchmarked policy change, not a reconciliation.

### 3.2 Frozen mix weights

| Weight | Q31 | ≈ |
|---|---:|---:|
| body ceiling | `966367642` | 0.45 |
| release influence | `751619277` | 0.35 |

### 3.3 Exact computation

```
body_gate    = knee(fast,        107374182, 429496729)
attack_gate  = knee(attack_in,   171798692, 536870912)
release_raw  = slow - fast                       # exact same-lane excess
release_gate = knee(release_raw,  64424509, 322122547)

body         = mul(mul(fast, body_gate), 966367642)
release      = mul(release_raw, release_gate)     # PUBLISHED
attack       = mul(attack_in, attack_gate)        # PUBLISHED

release_mix   = mul(release, 751619277)
after_release = add(body, mul(Q - body, release_mix))
final         = add(after_release, mul(Q - after_release, attack))
```

Published tuple per lane: `(final, body, release, attack)`.

### 3.4 Invariants

- **I-RM-1 — no lane borrowing.** Every term of lane `i` is a function of lane `i`
  inputs only. No neighbour, no project time, no macro salience.
- **I-RM-2 — published = contributing.** `release` and `attack` are published
  *post*-gate. The value in the provenance tuple is exactly the value that built
  `final`. Render-side validation (`odm_layered_render_*`) recomputes `final` from
  the published tuple and rejects any mismatch, so a forged component fails closed.
- **I-RM-3 — dead zone is total.** `attack_in ≤ 171798692 ⇒ attack = 0` and the
  lane contributes no tip. Same for body and release at their knees.
- **I-RM-4 — headroom ordering.** `body ≤ after_release ≤ final`. Each stage can
  only consume remaining headroom `Q − previous`, so no stage can shrink the one
  before it and `final` never exceeds `Q`.
- **I-RM-5 — monotone in each input.** With the other two inputs held, `final` is
  non-decreasing in `fast`, in `release_raw` and in `attack_in`.

## 4. Field raster optics (Layered Policy v11)

Applies when `RADIAL_PROVENANCE` is set. Lengths are Q16.16 pixel radii; `bar_max`
is `field.bar_max_q16`; `field_op` is `field_opacity_q31`.

```
body_len     = bar_max * body  / Q                       # round-half-up
release_base = body + mul(Q - body, mul(release, 751619277))   # if RADIAL_TIMESCALE
             = body                                             # otherwise
release_len  = bar_max * release_base / Q
final_len    = bar_max * final / Q
```

### 4.1 Body segment — dual-domain optical authority (v11, new)

```
if body_len > 0:
    activity  = max(attack, mul(release, 1395864371))     # 0.65
    authority = add(322122547, mul(Q - 322122547, activity))   # 0.15 floor
    draw(from r0, to body_len, SECONDARY, opacity = mul(field_op, authority))
```

Rationale: under v10 a sustained lane drew its body at full field opacity. On a
dense track every lane is sustained, so the Halo degenerated into a **static
bright crown** around the Core — high ink, zero information. v11 makes brightness
require same-lane transient *or* release evidence, with a 0.15 floor so that
sustained spectrum stays legible instead of disappearing. Held spectrum is
present but subdued; only moving spectrum is bright.

### 4.2 Release segment (unchanged from v10)

```
if release_len > body_len and release != 0:
    draw(from body_len, to release_len, SECONDARY, opacity = mul(field_op, release))
```

### 4.3 Attack tip — quadratic authority (v11, changed)

```
if final_len > release_len and attack != 0:
    tip_authority = mul(attack, attack)                  # quadratic
    draw(from release_len, to final_len, PRIMARY, opacity = mul(field_op, tip_authority))
```

Rationale: tip opacity was linear in attack under v10. Linear opacity makes weak
attacks visually comparable to strong ones once the knee admits them, which reads
as noise. Squaring expands the *perceived* dynamic range of the tip: a half-scale
attack draws at a quarter opacity. Length stays linear, so the tip's geometry is
still exact evidence — only its luminance is shaped.

### 4.4 Invariants

- **I-RO-1 — geometry is never shaped.** Only opacity is curved. `body_len`,
  `release_len` and `final_len` remain linear in the published Q31 values.
- **I-RO-2 — ordered radii.** `body_len ≤ release_len ≤ final_len`; each segment
  starts where the previous ended, so no gap and no overlap.
- **I-RO-3 — zero component draws nothing.** `release == 0` draws no release
  segment; `attack == 0` draws no tip. Combined with I-RM-3 this is what makes a
  sub-threshold lane invisible rather than faint.

## 5. Why the gated semantics is the correct one

The reconciliation question was: is the 0.33 behaviour a bug, or is the v8/v10
oracle stale? Both were evaluated.

**Kept, because it is right:**

- A radial that exists whenever an FFT bin is non-zero is the defect this project
  is trying to remove. The knee makes *presence* require evidence above a floor,
  which is the minimum precondition for "no lines without cause".
- Publishing post-gate provenance is strictly better than publishing pre-gate
  values: it keeps `final` reconstructible from the tuple (I-RM-2), so the render
  boundary can still fail closed on a forged plan. An ungated publication with a
  gated render would have broken that.
- The optical authority (§4.1) removes a real, observed artifact — the static
  crown — without inventing motion.

**Known limitation, recorded rather than hidden:**

A knee is *memoryless*. Evidence oscillating across `lo` still produces
appear/disappear flicker at the knee instead of at zero; the defect is moved, not
removed. The attack knee at 0.08 also suppresses genuinely soft onsets (bowed
strings, brushed percussion), which the engine is otherwise meant to distinguish.

Neither is repaired here. Repairing them means giving each visual target a
response kernel in the sample domain (attack / hold / release / refractory) so
that persistence comes from *memory of a past event* rather than from a threshold
on the present frame. That is Visual Dynamics, tracked separately. Freezing v9/v11
first is what makes that change measurable: the diff will be against a tree whose
identity is true.

## 6. Reconciliation record

| Item | v8 / v10 | v9 / v11 |
|---|---|---|
| body publication | `mul(fast, 0.45)` | knee-gated |
| release publication | `slow − fast` raw | knee-gated |
| attack publication | `attack_in` raw | knee-gated |
| body raster opacity | `field_op` | `field_op × authority(attack, release)` |
| tip raster opacity | `field_op × attack` | `field_op × attack²` |
| policy asserts tip is exact attack | yes (false) | no — declares quadratic |

Independent reconstruction lives in `tools/odm_radial_spec.py` and is consumed by
`check_radial_provenance_oracle`, `check_causal_trace_oracle`,
`check_radial_timescale_geometry_oracle`, `check_multi_axis_visual_oracle` and
`check_multiscale_radial_raster_oracle`. Negative controls for the knees and for
lane isolation live in `check_radial_provenance_oracle`.

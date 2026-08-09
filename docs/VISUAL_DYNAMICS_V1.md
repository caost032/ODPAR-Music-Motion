# Visual Dynamics v1 — normative specification

Status: NORMATIVE. Frozen by `ODM_VISUAL_DYNAMICS_POLICY_VERSION 1`.

Layer position: between MUSIC EVIDENCE and VISUAL COMPOSITION.

```
MUSIC EVIDENCE  →  VISUAL DYNAMICS  →  VISUAL COMPOSITION / DIRECTOR  →  FRAME PLAN
```

---

## 0. The problem this layer exists to solve

The defect is not "not enough effects". It is that a visual quantity currently
exists exactly while an evidence value is high:

```
evidence rises  → line appears
evidence falls  → line disappears
```

Two consequences, both visible in slow motion:

1. **One-frame flicker.** Evidence oscillating around any threshold produces
   appear/disappear at frame rate.
2. **No consequence.** A hit inflates and deflates instantly. Nothing "receives
   the blow and then relaxes", because the image has no memory of the past.

Radial Morphology v2 (`docs/RADIAL_MORPHOLOGY_V2.md`) put a perceptual knee in
front of publication. That removes sub-threshold noise, but a knee is
**memoryless**: it moves the oscillation from zero to the knee. It cannot fix (2)
at all.

The fix is to give each visual target its own **response kernel in the sample
domain**, so that persistence is a *consequence of a past event* rather than a
threshold on the present frame.

## 1. Authority and non-goals

Visual Dynamics **consumes** evidence and **publishes** response levels. It:

- never creates evidence, and never modifies Music Evidence;
- never invents motion in the absence of an event (silence in, silence out);
- never reads project time as an animation clock — only as the coordinate at
  which a past event's consequence is evaluated.

It is not a beat tracker, not a smoother of musical truth, and not a place to hide
aesthetic constants that belong to a style.

## 2. Time domain — why samples, and why closed form

All kernel parameters are **int64 sample counts** at the canonical 48 kHz
timeline. Not frames, not 100 Hz map ticks.

Consequences, all required:

- **FPS independence.** `level(s)` is a function of the sample coordinate. Changing
  24 → 60 fps changes *where* the envelope is sampled, never the envelope. This is
  the metamorphic property `FPS CHANGE` in §7.
- **Sub-tick resolution.** An event published at an exact sample starts its
  response at that sample, not at the enclosing 10 ms tick boundary.
- **Exact seek.** The response is evaluated in **closed form** from the event
  coordinate, never integrated frame to frame. Jumping to sample `s` yields the
  same value as playing to `s`, bit for bit, with no warm-up and no replay of
  historical attacks.

The engine constitution already requires this preference ("prefer closed-form /
time-evaluable motion over arbitrary frame-to-frame integration"); Visual Dynamics
makes it structural rather than optional.

## 3. State

A response is fully described by four numbers:

```c
typedef struct {
    uint64_t event_sample;   /* where the current response began   */
    uint32_t peak_q31;       /* level it is heading to             */
    uint32_t base_q31;       /* level it departed from (continuity)*/
    uint64_t trigger_sample; /* last accepted trigger, for refractory */
} odm_vd_state;
```

`base_q31` is what makes retriggering continuous: a new event starts from the level
the target currently has, so a response can never produce a visual discontinuity
even when it arrives mid-release.

State is a POD of fixed-width integers: serializable, checkpointable, and
comparable. Preview and master carry identical state at identical samples.

## 4. Kernel

### 4.1 Admission (evidence → peak)

```
if evidence <= dead_zone:              peak = 0
else:
    gated = evidence * knee(evidence, dead_zone, knee_hi)
    peak  = min(max_q31, gated * gain)
```

`knee` is the same smoothstep used by Radial Morphology v2 (§1 of that document),
reused deliberately so the two layers cannot drift into different curve families.

### 4.2 Trigger and refractory

A trigger at sample `s` with admitted level `p` is **accepted** iff

```
p > level(s)                                    /* it must actually add something */
AND (s - trigger_sample >= refractory_samples  OR  p > peak_q31)
```

On acceptance: `base = level(s)`, `peak = p`, `event_sample = s`,
`trigger_sample = s`.

Refractory prevents an evidence stream from re-arming the same response every
frame. The `p > peak_q31` escape hatch means a genuinely stronger event is never
suppressed by a weaker recent one — refractory bounds *repetition*, not *dynamics*.

A rejected trigger changes nothing. This is what makes "coarse authority cannot
retrigger a sample-domain event" hold at the dynamics layer too.

### 4.3 Evaluation — piecewise closed form

With `d = s − event_sample`:

| phase | condition | level |
|---|---|---|
| attack | `d < A` | `base + (peak − base) · smoothstep(d/A)` |
| overshoot | `A ≤ d < A + O` | `peak + (peak · overshoot) · hump((d−A)/O)` |
| hold | `A + O ≤ d < A + O + H` | `peak` |
| release | `d ≥ A + O + H` | `floor + (peak − floor) · 2^(−(d − A − O − H)/T)` |

- `A` attack samples, `O` overshoot samples, `H` hold samples, `T` release
  half-life samples, `floor` the resting level.
- `A = 0` means instant attack (`level` jumps to `peak` at the event sample).
- `T = 0` means instant release.
- `hump(x) = 4x(1−x)` — one smooth excursion above `peak`, returning exactly to
  `peak` at `x = 1`, so overshoot can never leave a permanent offset.

**Exponential release without floats.** With `d' = d − A − O − H`, write
`n = d' / T` and `f = d' mod T`. Then

```
2^(−d'/T) = (1/2)^n · 2^(−f/T)
```

The integer part is an exact right shift; the fractional part is a frozen
257-entry Q1.31 table of `2^(−i/256)` with linear interpolation. Error versus the
true exponential is below 1e-5 of full scale, it is bit-identical on every
platform, and it is monotone decreasing — which is the property tests actually
depend on.

### 4.4 Guarantees

- **G-VD-1 — no spontaneous motion.** With no accepted trigger ever, `level(s) =
  floor` for all `s`. Silence produces stillness.
- **G-VD-2 — monotone release.** After `A + O + H`, `level` is non-increasing
  until the next accepted trigger.
- **G-VD-3 — continuity.** `level` at an accepted trigger equals `base`, which was
  read at that same sample. No visual jump on retrigger.
- **G-VD-4 — bounded.** `floor ≤ level ≤ peak · (1 + overshoot) ≤ Q`.
- **G-VD-5 — seek exactness.** `eval(state, s)` depends only on `(state, s)`. Any
  two paths that arrive at the same `(state, s)` agree bit for bit.
- **G-VD-6 — FPS invariance.** The set `{(s, level(s))}` is independent of frame
  rate. Rendering at a different FPS samples the same function.

## 5. Target classes

A *class* is a named response shape. Styles pick parameters; the class fixes the
character, so that "core" cannot accidentally behave like "particles".

| class | attack | hold | release ½-life | refractory | intent |
|---|---|---|---|---|---|
| `CORE_IMPACT` | 4 ms | 12 ms | 90 ms | 60 ms | receives the blow, relaxes visibly |
| `RADIAL_TIP` | 1 ms | 6 ms | 55 ms | 20 ms | fast, but never gone in one frame |
| `HALO_BODY` | 25 ms | 40 ms | 180 ms | 0 | breathing, not snapping |
| `GRID_FIELD` | 180 ms | 0 | 700 ms | 0 | meso/macro only; cannot whip on an onset |
| `BACKGROUND` | 600 ms | 0 | 2500 ms | 0 | macro scale |
| `PARTICLE_SPAWN` | 0 | 0 | 300 ms | 40 ms | the event ends; the consequence continues |
| `MEMORY_FIELD` | 0 | 0 | 1200 ms | 0 | literal consequence of past events |

Sample counts are derived from these millisecond intents at 48 kHz and frozen in
the policy bytes. The millisecond column is documentation; the samples are truth.

The ordering `RADIAL_TIP < CORE_IMPACT < HALO_BODY < GRID_FIELD < BACKGROUND` is
itself an invariant (§7): it is what enforces micro/meso/macro separation, so a
10 ms transient cannot restructure the scene and a 20 s structural change cannot
be drawn as a one-frame flash.

## 6. Relationship to Radial Morphology v2

They compose, and the order matters:

```
lane evidence → [v2 knee: is this real?] → [dynamics: how does it persist?] → radial
```

The knee still answers *admission*. Dynamics answers *persistence*. Keeping them
separate means the knee can later be relaxed — letting soft bowed onsets through —
without reintroducing flicker, because flicker is no longer the knee's job to
prevent. That is the intended migration path, and it is why v9 was frozen before
this layer was written rather than being "fixed" in place.

## 7. Independent verification

`tools/check_visual_dynamics_oracle.py` reconstructs the whole kernel from this
document and compares against the C engine at many sample coordinates.

Metamorphic properties, none of which depend on a golden constant:

| property | statement |
|---|---|
| SILENCE | no trigger ⇒ level ≡ floor |
| FPS CHANGE | levels at shared sample coordinates identical for 24/25/30/50/60/120 fps sampling |
| SEEK | direct evaluation at `s` equals sequential evaluation to `s` |
| TIME SHIFT | shifting every event by `k` shifts the whole response by exactly `k` |
| MONOTONE RELEASE | non-increasing after hold, verified across the full envelope |
| CONTINUITY | retrigger mid-release introduces no discontinuity |
| NO DOUBLE FIRE | a repeated equal-strength trigger inside refractory is rejected |
| STRONGER WINS | a stronger trigger inside refractory is accepted |
| BOUNDED | level never exceeds `peak·(1+overshoot)` nor drops below `floor` |
| TIMESCALE ORDER | class half-lives are strictly ordered micro < meso < macro |

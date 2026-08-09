# Strict Causal Director v1 — 0.16 WIP

## Purpose

`STRICT_CAUSAL` is a semantic render authority. It is deliberately separate
from `RADIAL_HIRES`: resolution answers *how many independently addressable
spectral lanes survive to the raster*; strict causal mode answers *which
classes of authority are allowed to move reactive geometry*.

The rule is narrow and auditable:

> If a visible change is presented as music-reactive in the strict path, seed,
> project time, procedural phase and autonomous orbit are forbidden causes.

This does **not** claim semantic instrument recognition, beat-grid truth, or
human musical understanding. It constrains causality of the signals the engine
has actually resolved.

## Public authority

`ODM_COMPOSITION_FLAG_STRICT_CAUSAL` is explicit in Composition and propagated
into `odm_layered_frame_plan.flags`. It can be used with either 48 or 96 radial
segments. `ODM_COMPOSITION_FLAG_RADIAL_HIRES` remains an independent flag.

Music-Reaction Spine v1 emits both flags because its native visual path is
96-lane and strict-causal.

## Director rules

When `STRICT_CAUSAL` is present:

- music-derived layout candidates, dwell, recovery and impact transitions are
  still permitted;
- the seed-selected stagnation alternate is forbidden;
- autonomous Director phase is pinned to zero;
- output asymmetry X/Y is zero unless a future typed music-derived authority is
  explicitly introduced and policy-bound;
- output orbit phase is zero.

A 5000-tick metamorphic oracle runs identical musical evidence through two
unrelated Director seeds while poisoning Composition phase/ring phase. Director
frames must remain byte-identical and no stagnation layout shift may occur.

## Layered compositor rules

When `STRICT_CAUSAL` is present:

- grid X/Y offsets are zero; project phase cannot scroll the grid;
- radial ring phase is zero; project time or Director orbit cannot rotate the
  reactive radial field;
- strict particles use fixed index-derived spatial identity, not configuration
  seed, sample time, tick index or flicker time;
- particle visibility is multiplied by the corresponding music-derived radial
  amplitude;
- the 96->96 path preserves each radial one-to-one; 96->48 remains a frozen
  adjacent-pair reduction for legacy compositor configurations.

## Negative control

`tools/check_strict_causal_oracle.py` deliberately repeats the same perturbation
without `STRICT_CAUSAL`. The non-strict Background and Field must differ. This
proves strict equality is caused by the policy rather than by an inert test
fixture.

## Policy identity

Strict-causal semantics are bound into both policy records:

- Visual Policy v4
- Layered Policy v5

Changing these semantics changes their SHA-256 identities and therefore cannot
silently masquerade as the previous policy.

## Compatibility

Legacy procedural rendering is preserved. Existing non-strict projects retain
sample-authoritative particle motion, procedural grid phase and the historical
Director alternate/orbit behavior. Strict causal mode is opt-in at the
Composition semantic boundary; Music-Reaction v1 opts in automatically.

## Verification

Primary evidence:

- `tools/check_director_oracle.py`
- `tools/check_strict_causal_oracle.py`
- `tools/check_music_reaction_oracle.py`
- `tools/check_visual_policy_oracle.py`
- `tools/check_layered_oracle.py`
- `tools/check_radial_hires_geometry_oracle.py`
- unit tests in `tests/unit/test_visual.c`, `test_compositor.c`, and
  `test_music_reaction.c`

These are regression and independent-oracle evidence, not a claim of formal
proof or external certification.

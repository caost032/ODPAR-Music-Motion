# ODPAR Music — Advanced Composition Layer v1

Status: post-constitutional additive extension over the Gate 11 engine.
It does not change Music Map truth, Render IR truth, the reference renderer,
Gate 11 commercial evidence semantics, or Music Policy v1.

## Problem

Gate 7 exposes deterministic pixel systems (Grid, Halo, Residual Event Field,
Particles) but a renderer still needs a frame-level composition decision: how
large/stable the visual core is, how the environment opens, how strong memory
and fracture should be, and how the six exact music bands drive geometry.
Doing that per pixel is wasteful and putting it in an external editor creates a
second, unverifiable interpretation of the Music Map.

## Contract

`odm_composition_profile_build()` performs a deterministic O(N) first pass over
consecutive canonical Music Map ticks. It records per-feature references for
short/medium envelopes, spectral flux, positive energy delta and all six music
bands. Zero-energy features remain zero; no energy is invented.

`odm_composition_resolve_tick_profiled()` then normalizes each tick using only
integer arithmetic and advances a 72-byte sequential resolver state. The output
is a fixed 360-byte `odm_composition_frame_state` containing:

- stable centered core scale and bounded internal drift;
- breathing/border/halo strengths;
- 48 exact spectral geometry segments plus the six normalized bands;
- radial aperture and lateral stereo field;
- Grid, Particles, Memory Echo and Void strengths;
- impact/fracture/chroma/scan strengths;
- a continuous integer phase and a deterministic impact gate.

The profiled transient path uses an integer cubic transfer. Mid-strength flux
therefore does not produce a high-rate glitch stream, while near-maximum
transients preserve their authority. Energy and band normalization remain
linear.

`odm_composition_resolve_batch[_profiled]()` is fail-closed and transactional:
it preflights the entire tick sequence before caller output is touched. A gap,
non-canonical silence flag or tick-index overflow publishes neither state nor
frames.

## Numeric/ownership rules

- no `float`/`double` in composition authority;
- no allocation;
- no hidden pointer ownership;
- all Q1.31 outputs are saturated/bounded;
- tick order is strict and `UINT64_MAX` is rejected before next-index wrap;
- first-pass profile and second-pass resolver are deterministic;
- composition is O(1) work per tick after the O(N) profile scan.

## Independent oracle

`tools/check_composition_oracle.py` independently reimplements the fixed-point
normalization, smoothing, transient cubic, refractory gate, phase progression,
48 spectral segments and all exported fields in Python. It checks both raw and
profiled streams field-by-field.

## Observed performance — Quiet, Not Empty

Host observation only, not an SLA:

- 30,116 canonical ticks / 301.16 s song;
- profile pass: about 0.20 ms;
- profiled resolve pass: about 10.4 ms median;
- about 2.9 million ticks/s on this container;
- no allocation in the C resolver.

Observed profiled state distribution for this song:

- VOID: 114 ticks
- CALM: 4,545 ticks
- FLOW: 24,911 ticks
- IMPACT: 546 ticks
- discrete glitch gates: 146

These counts are evidence for this soundtrack/profile only, not universal
threshold claims.


## Visual Director v3

Composition v1 resolves fast per-tick visual energy; Director v3 adds a slower
long-form macro grammar without re-reading audio. It consumes only
`odm_composition_frame_state` and emits a fixed 128-byte
`odm_director_frame_state`. The seven layouts are MONOLITH, ORBIT, WINGS,
MEMORY, EXPAND, VOID and FRACTURE.

Normal macro changes require 1.8 s of candidate stability, at least 12 s of
current-layout dwell and accumulated novelty. VOID and recovery have bounded
fast paths. A one-tick impact remains a micro-event. FRACTURE is special: its
smoothed candidate must remain dominant for 10 ticks, the current architecture
must already have 12 s dwell, and the raw fracture field must cross the exact
40% gate. Once entered, FRACTURE cannot release before a 1.8 s minimum hold and
80 stable recovery-candidate ticks. Anti-stagnation is eligible after 28 s only
when its independent novelty integral crosses the frozen threshold.

Transitions are exact integer tick intervals (1.8 s normal, 0.7 s fast).
`odm_director_resolve_batch()` preflights every input tick before caller output
or state is published. The 88-byte caller-owned state makes candidate
persistence, layout epoch, phase and accumulated novelty checkpointable.

`tools/check_director_oracle.py` independently reimplements Director v3 and
compares every exported state/frame field over a synthetic corpus that covers
all seven layouts and transaction-failure cases.

Observed on `Quiet, Not Empty` (host observation, not an SLA), Director v3
resolved 30,116 ticks in about 1.98 ms (~15.2 million ticks/s), produced 13
macro-layout changes, used all seven layouts, and emitted one bounded 0.98 s
FRACTURE chapter rather than turning hundreds of impact ticks into layout
thrash.

## Visual Policy v1

`odm_visual_policy_bytes()` publishes a fixed 1024-byte canonical little-endian
semantic policy for Advanced Composition v1 + Visual Director v3. It records
algorithm revision IDs, normalization and smoothing rules, Composition
thresholds, refractory/phase rules, Director candidate thresholds,
dwell/stability/transition constants, deterministic alternate-selection
constants and the complete target table for all seven macro layouts. Unused
bytes are canonical zero.

`odm_visual_policy_current_sha256()` hashes exactly those 1024 bytes. The
current Visual Policy v1 SHA-256 is:

`ab589d9a451f548a70443e3d15cd9c0d4aeb7d3ab4918904b9b28bf12eb9f588`

`tools/check_visual_policy_oracle.py` builds the expected policy independently
from literal semantic constants and compares all 1024 bytes plus SHA-256. The
source-id remains the identity of implementation bytes; Visual Policy is the
identity of the visual semantics a Composition/Director stream claims.

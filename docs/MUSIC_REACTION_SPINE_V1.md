# ODPAR Music — Music Reaction Spine v1 (0.15 WIP)

## Status

This document describes the **0.15 Music Reaction Spine v1 WIP checkpoint**. It is
an engineering checkpoint, not a public FINAL release. Its purpose is to close a
specific correctness gap discovered after the 0.14 Style/Reaction checkpoint:
the engine was deterministic and heavily verified, but its visible radial field
could still be perceptually weak because 1025 FFT bins were reduced to six Music
Map bands and then reused across 48 radials.

The design objective of v1 is stricter:

> A visible motion advertised as music-reactive must have an explicit,
> deterministic signal cause that can be traced to the music-derived stream.

This is a causal-engineering claim, not a claim that every aesthetic decision or
semantic musical concept is solved.

## Constitutional boundary

Music Map v1 remains unchanged and authoritative for canonical exact facts:
48 kHz stereo Q1.31, 100 Hz ticks, the fixed 2048-sample transform, six canonical
bands and the existing Music Map policy identity.

Music Reaction Spine v1 is a separately versioned derived/inference stream. It
consumes the deterministic spectrum scratch immediately after a canonical
`odm_music_analyze_tick()` call. Visual/compositor code never reads PCM or FFT
scratch directly. Therefore the higher-resolution visual stream does not rewrite,
masquerade as, or silently expand Music Map v1.

## Frozen high-resolution spectral topology

Reaction Spine v1 exposes **96 contiguous spectral lanes** covering FFT bins
1..768. With the canonical 48 kHz / 2048-point transform this spans approximately
23.4375 Hz through 18 kHz; DC is excluded.

Lane topology is frozen and independently reconstructed by
`tools/check_music_reaction_oracle.py`. The lanes are not 96 interpolated copies
of six bands: every lane is accumulated from its own explicit FFT-bin interval.

The six broader reaction families remain available as derived controls:

- SUB
- BASS
- LOW_MID
- BODY
- PRESENCE
- AIR

Families are useful for macro controls; local radial shape uses the 96-lane
stream so macro aggregation cannot erase local spectral identity.

## Robust track normalization

The 0.14 global-maximum profile could let one extreme peak become normalization
authority for an entire song. Reaction Spine v1 instead uses deterministic exact
order statistics:

- magnitude reference: track P99;
- attack reference: P95 over **positive deltas only**;
- selection: allocation-free four-pass radix selection;
- no random sampling and no floating-point semantic authority.

The normalized response deliberately preserves low/mid-level detail with a
bounded 75% linear + 25% integer-square-root shape. There is no artificial
non-zero floor.

The independent oracle injects an `INT32_MAX` outlier and proves that it does not
become the track authority.

## Temporal reaction and events

Each local lane has an immediate attack and a deterministic release envelope.
Family and broadband controls use separately bounded release rates.

A global reaction event is **not** the maximum of 96 narrow-band attacks. That
prototype was rejected after real-song testing because multiple-comparison
statistics caused excessive macro triggers. The final v1 rule separates local
shape from global evidence:

- 96 narrow lanes control local geometry;
- global event evidence uses broadband attack and coherent family evidence;
- coherent family evidence is based on the strongest two families, not one
  isolated narrow lane;
- the threshold is computed from **prior** history before the current event is
  decided;
- only after the decision is current evidence folded into the adaptive baseline;
- refractory period: 14 ticks (~140 ms);
- event pulse has bounded deterministic decay.

This produces an onset/impact stream. **It is not yet certified as a beat grid,
BPM estimator, phrase detector or semantic section detector.** Those remain
separate future inference contracts rather than being mislabeled as exact facts.

## 96 lanes -> 96 visible radials

0.15 adds a versioned high-resolution radial path:

- Reaction lane `i` -> visible radial `i` one-to-one for 96-radial configs;
- legacy 48-radial configs remain valid and deterministically reduce adjacent
  pairs `(2i, 2i+1)`;
- a legacy 48-radial composition rendered through a 96-radial config duplicates
  each legacy radial into an adjacent pair instead of inventing interpolation;
- the 48-radial legacy raster remains covered by its original independent oracle.

The compositor field renderer now uses the configured radial count, so 96 values
are not merely stored: they are rasterized as 96 independently addressable
geometries.

`tools/check_radial_hires_geometry_oracle.py` independently reconstructs the
96-radial CORDIC/end-point/Q24.8 projection/coverage/premultiplied-blend pixel
stream and compares it byte-for-byte with the C renderer.

## No autonomous phase in the strict reaction path

The 0.14 radial path could mix music-derived amplitude with procedural phase and
Director orbit. That made deterministic motion possible even when the music did
not cause that motion.

For `ODM_COMPOSITION_FLAG_RADIAL_HIRES` / Music Reaction Spine v1:

- `composition.phase = 0`;
- `composition.ring_phase = 0`;
- the grid does not scroll from project time;
- the halo does not rotate from project time;
- Layered Compositor explicitly excludes Director orbit phase from the hi-res
  radial path;
- time changes alone must leave reaction phase zero and radial amplitude
  byte-identical.

This semantic is bound into Visual Policy v2 and Layered Policy v3, so changing it
changes policy identity instead of becoming a silent implementation drift.

Procedural macro composition remains available in legacy/non-strict paths. The
strict path does not claim that deterministic decoration is music causality.

## Provenance

Reaction Spine exposes radial provenance helpers. A consumer can query which
spectral source caused a visible radial:

- high-resolution radial 40 -> lane 40;
- legacy radial 20 -> lanes 40 and 41.

The independent reaction oracle verifies both mappings.

The intended future extension is to propagate this provenance into exported
debug/evidence receipts so a visual event can be traced through:

`tick -> lane/family/event -> reaction frame -> composition target -> radial`

without reading audio again.

## Real-song observation: Hear Myself Breathe

Input used for the 0.15 WIP observation:

- 12,201,600 PCM frames;
- 48 kHz stereo;
- 254.2 seconds;
- 25,420 canonical 100 Hz ticks.

Observed on the local reference environment:

- canonical analysis + reaction extraction: ~8.94 s;
- robust reaction profile: ~0.058 s;
- reaction resolution plus legacy comparison: ~0.384 s;
- total probe wall time: ~10.08 s;
- probe peak RSS: ~144,912 KiB (the probe intentionally retains full-song tick
  arrays; this is not the streaming core working set).

Spatial-detail comparison on the same song:

- new 96-radial mean active radials (>0.15): **34.669 / 96**;
- new 48-equivalent mean active radials: **17.892 / 48**;
- 0.14 legacy mean active radials: **2.088 / 48**;
- new 96-radial mean quantized distinct levels/tick: **50.999**;
- new 48-equivalent mean distinct levels/tick: **32.728**;
- 0.14 legacy mean distinct levels/tick: **5.773**;
- new reduced-48 repeat-by-six mean absolute difference: **243,238,796.88 Q31**;
- legacy repeat-by-six mean absolute difference: **4,372,803.22 Q31**.

The repeat-by-six metric is intentionally diagnostic: a very low value reveals
the old six-band silhouette being repeated around the halo. The much larger v1
value demonstrates that the periodic six-band alias has been removed. This is
not, by itself, a subjective beauty metric.

The observed onset/impact stream produced 908 events (~3.572/s) with mean
strength ~0.469 on this track. These are **impact candidates**, not a certified
beat count.

## Verification lanes added/strengthened

- `tests/unit/test_music_reaction.c`
- `tools/check_music_reaction_oracle.py`
- `tools/check_radial_hires_geometry_oracle.py`
- 96/48 bridge tests in `tests/unit/test_compositor.c`
- Music Spine invariants `MUSIC-REACTION-1..5`
- capabilities for high-resolution spectrum, robust profile, causal events,
  radial provenance, 96-radial raster and stationary strict geometry.

The legacy 48-radial geometry oracle remains active to prevent compatibility
regressions.

## ABI and policy versioning

0.15 changes public native structure layout by extending radial storage from 48
to 96 slots. The engine-level ABI is therefore bumped from **1 to 2**. The FFI
ABI used by the existing flat core interface is a separate contract and remains
unchanged where its golden layouts are unchanged.

Policy versions:

- Visual Policy: v2;
- Layered Policy: v3.

This checkpoint is labeled `0.15.0-music-reaction-spine-v1-wip-local`.

## Explicit non-claims / remaining work

This checkpoint does **not** claim:

- perfect perception for every genre or mastering style;
- exact semantic separation of kick/snare/vocal/violin sources;
- certified beat/BPM/tempo grid;
- certified phrase/section understanding;
- final aesthetic authoring or a public 0.15 FINAL release.

Those are deliberately not fabricated. The completed v1 work is the lower-level
causal substrate they should build on.

The next inference layer should add independently testable onset classes,
beat/tempo hypotheses with confidence and offline structural context, while
keeping Music Map exact facts and Reaction Spine causal geometry separate.

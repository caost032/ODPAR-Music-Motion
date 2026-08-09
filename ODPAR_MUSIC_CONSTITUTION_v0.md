# ODPAR Music — Constitution v0

Status: PRE-IMPLEMENTATION CONTRACT / Gate 0 starting point

## Identity
ODPAR Music is a verifiable audiovisual compiler/renderer, not a generic music visualizer.

Canonical chain:

Immutable assets → Media Facts → Canonical Music Map → Optional Musical Inference → Authored Visual Score → Compiler → Render IR → Exact Frame State → Validated Render DAG → Reference Renderer → Delivery Encoder → Render Receipt / Music Spine.

ODPAR: Compare Documents is the technical constitution: truth before presentation, canonicalization once, exact-vs-derived separation, fail-closed behavior, parity by construction, explicit ownership, hard resource limits, deterministic identity, evidence-earned maturity, hostile-input testing, and a compiled live spine.

## Non-negotiable invariants
1. Wall-clock time never defines render state.
2. Every displayed/master frame is resolved from project time, not runtime delta.
3. No unseeded randomness in render semantics.
4. Visual systems do not decode/analyze audio directly; they consume typed resolved state/modulators.
5. Named presets are never authoritative. They compile to explicit effective parameters.
6. Preview and master consume the same Render IR and Frame State semantics.
7. Failed/cancelled work never becomes a successful master.
8. Unsupported capabilities fail explicitly; no silent semantic fallback.
9. Technical work measurement is separate from business pricing.
10. The CPU reference renderer is the oracle. Later backends prove parity against it.

## Time Contract v0
Canonical project soundtrack/timeline rate: 48,000 samples/second.

Supported master FPS v0:
- 24
- 25
- 30
- 50
- 60

All divide 48,000 exactly.

For FPS F:
- samples_per_frame = 48000 / F
- frame_start_sample(N) = N * samples_per_frame
- project_end_sample is an explicit int64 coordinate
- frame_count = ceil(project_end_sample / samples_per_frame)
- output_end_sample = frame_count * samples_per_frame

Default project_end_sample = canonical soundtrack sample_count + authored post-roll samples.
The soundtrack is never silently truncated. If the visual output ends after source audio, exact digital silence pads the canonical soundtrack to output_end_sample. Explicit trim is an authored operation.

Fractional NTSC rates (24000/1001, 30000/1001, 60000/1001) are intentionally outside v0 and must enter through a later versioned time contract.

## Canonical numeric contract
- Time: int64 sample coordinates and explicit rational structures when required.
- Canonical PCM: signed Q1.31 int32.
- Authored normalized parameters: integer micro-units (1,000,000 = 1.0) where practical.
- Compiled continuous geometry/state: signed Q32.32 int64.
- Angles/phases: uint32 full-turn phase (2^32 = one turn) where applicable.
- Opacity/coverage: normalized integer representation.
- No float/double values in the authoritative serialized Render IR.
- Floating-point/SIMD may be used only behind explicitly measured backend implementations; they do not redefine project semantics.

## Color / compositing contract v0
Working gamut: BT.709 primaries, D65.
Working domain: linear light.
Alpha model: premultiplied.
Reference intermediate: deterministic fixed-point RGB with headroom + normalized alpha.
Blend equations and integer rounding are part of the renderer version and must have golden tests.

Delivery v0 is SDR BT.709. Encode adapters must tag color metadata explicitly. HDR/BT.2020/PQ/HLG are not v0 capabilities.

## Media Truth v0
FFmpeg is an external media adapter, not the semantic engine.

Initial stable input targets:
Audio:
- WAV/PCM
- FLAC
- MP3
- AAC in M4A/MP4
- mono/stereo only in v0

Image:
- PNG
- JPEG
- WebP (static)

Video:
- MP4/MOV + H.264
- WebM + VP9
- timestamp/PTS-driven sampling; VFR is represented by presentation intervals, not assumed CFR

Additional codecs can be added capability-by-capability after evidence. Unsupported/encrypted/corrupt inputs fail closed.

Canonical analysis soundtrack: 48 kHz stereo Q1.31. Mono is represented consistently; multichannel >2 is rejected in v0 rather than silently downmixed.

## Music Map v1 starting contract
Map coordinate rate: 100 Hz = one map tick per 480 canonical samples (10 ms).

STFT proof configuration:
- 2048-sample analysis window
- centered on map tick
- zero padding outside signal bounds
- frozen periodic-Hann coefficient table in fixed point
- canonical transform implementation must be deterministic; floating architecture-specific FFT is not allowed to define canonical map truth until proven

Initial exact/deterministic feature families:
- channel/mid RMS
- multi-band energy
- short/medium/long energy envelopes
- spectral flux
- spectral centroid
- energy delta
- silence state
- stereo balance / width-compatible measures

Onset, beat, BPM, phrase, section and semantic labels are versioned inference, not source facts. They carry method/version/confidence and never overwrite the canonical signal map.

Exact band boundaries/formulas, normalization and fixed-point FFT arithmetic are Gate 3 normative work and must be frozen by vectors before Music Map v1 is declared stable.

## Visual Score
Creative authority contains explicit:
- Acts
- Scenes
- Environment
- exactly one Primary Composition per Scene
- Subjects
- Look
- Effects
- Particle Systems
- Modulators
- Cues
- Automation
- Transitions
- Entry / Exit
- explicit typed State Transfer

Only the Transition Compositor may intentionally mix two scenes. Structural slot conflicts fail validation.

## Render IR v0
Canonical compiled binary representation.
- little-endian
- fixed-width integers only
- no pointers, size_t, native enum layout or floats
- explicit major/minor schema versions
- canonical section ordering
- explicit capability IDs/versions
- Score hash + capability hash included
- whole canonical IR hashed with SHA-256
- unknown required capability/section => fail

Planned required section families:
HEADER, CAPABILITIES, RESOURCES, TIMELINE, NODES, MODULATORS, AUTOMATION, TRANSITIONS, STATE_TRANSFER, OUTPUT.

`.odparms` may contain both the authored Visual Score and compiled IR/hash. Cloud/reference builds may recompile and compare IR hash before master rendering.

## State models
Every visual capability declares one:
- STATELESS
- EVENT_HISTORY (random-access bounded history)
- ACCUMULATOR (checkpoint/prefix capable)
- SIMULATION (sequential/checkpointed)

The planner may frame-parallelize only where the declared state model makes it safe.

Prefer closed-form/time-evaluable particle/event motion over arbitrary frame-to-frame integration.

## Visual Memory
ODPAR Visual Memory / Residual Event Field is a first-class direction, not a generic bass visualizer behavior.
Recent musical events can create deterministic bounded entities/fields with explicit birth coordinate, strength, decay, seed and typed state. Scene inheritance is explicit through State Transfer; no hidden global state.

## Preview contract
Preview uses the same Render IR and Frame State resolver as master.

Allowed silent degradation:
- lower raster resolution
- fewer raster/AA samples
- lower texture filtering quality
- lower post-effect sampling quality, if semantic parameters remain unchanged
- displaying/skipping presentation frames for UI performance

Not allowed silently:
- changed timeline/cue/transition times
- changed parameter values
- changed seeds
- reduced semantic particle/event population
- different Music Map
- different scene selection

Any future semantic approximation must be explicitly labeled APPROX and cannot be represented as exact approval parity.

## Output and identity
Reference render truth ends before delivery codec:
- exact Frame State stream
- canonical reference frames
- canonical soundtrack timeline

`render_id` binds project/assets/Music Map/Visual Score/Render IR/capability set/output timing/profile/seed and excludes wall-clock time and delivery encoder nondeterminism.

Delivery encoding is a separate adapter layer.
`delivery_contract_id` binds render_id + container/codec/encoder/version/settings.
Final file has its own SHA-256.

Initial delivery target:
- MP4
- H.264 video encoder adapter
- AAC audio
- canonical 48 kHz soundtrack
- SDR BT.709
- 24/25/30/50/60 fps
- common 1080p/4K landscape, portrait and square presets; arbitrary even dimensions only after admission checks

Encoder implementation is pluggable and is not renderer truth. OpenH264 is a possible initial H.264 adapter; alternative server-side encoders require separate licensing/build review.

## Music Spine
The spine is compiled code, never a manual README inventory.
It must derive live facts from implementation tables/symbols/contracts and expose:
- modules/dependency DAG
- capabilities and versions
- state models
- renderer backends
- media adapters
- output profiles
- invariants
- ownership pairs
- parity contracts
- known bugs
- security investigations
- maturity/evidence
- impact analysis

A capability earns maturity from tests/evidence; no hard-coded vanity rating.

## First guardians
- NO_WALLCLOCK_IN_RENDER_CORE
- NO_UNSEEDED_RANDOMNESS
- NO_DIRECT_AUDIO_IN_VISUAL_EFFECTS
- NO_UI_SEMANTICS_IN_ENGINE
- NO_PRESET_AS_TRUTH
- NO_PARTIAL_MASTER
- NO_NATIVE_FLOAT_IN_CANONICAL_IR

## Dependency / licensing boundary
FFmpeg adapter begins with libavformat, libavcodec, libavutil; add libswresample/libswscale only where required. Avoid libavfilter defining ODPAR visual semantics.
Build policy begins LGPL-oriented. GPL/nonfree options are never enabled casually; they are a deliberate separate delivery/backend decision.

## Core development rule
Do not modify the mature Compare Documents engine first. Build the new Music foundation from independently proven ODPAR patterns/primitives. Only migrate Compare Documents toward a shared core later, in its own parity-protected gate.

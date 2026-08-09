# ODPAR Music 0.13 — Layered Composition & Native Export v1

Status: `implemented_uncertified` local extension. This document defines the
engineering boundary implemented by version `0.13.0-layered-compositor-export-v1-local`.
It is additive: it does not rewrite Music Policy v1, Music Map truth, Render IR
truth, or the sealed Gate 0–11 constitutional release.

## 1. Authority separation

A final visual frame is not one monolithic effect pass. The compositor owns six
separate authorities with a fixed dependency direction:

1. **Canvas/Layout** — output geometry, supported FPS, aspect family and safe area.
2. **Background** — independent environment layer. It may consume resolved
   Composition/Director state but cannot inspect Core pixels or HUD metadata.
3. **Core Media** — central still/video surface, shape, fit and edge semantics.
4. **Reactive Field** — geometry intentionally anchored to the resolved Core
   contour. It reacts to Composition/Director but never reinterprets audio.
5. **HUD** — optional progress/time/title/artist presentation inside safe area.
6. **Layered Compositor** — the only canonical blend order:
   `Background -> Core -> Reactive Field -> HUD`.

Music/audio is never decoded or interpreted by these six layers. Their reactive
input is versioned Composition/Director state produced upstream from canonical
Music Map truth.

## 2. Canvas and aspect

Canonical aspect families are:

- `ODM_CANVAS_ASPECT_HORIZONTAL_16_9`
- `ODM_CANVAS_ASPECT_SQUARE_1_1`
- `ODM_CANVAS_ASPECT_VERTICAL_9_16`
- `ODM_CANVAS_ASPECT_HORIZONTAL_4_3`
- `ODM_CANVAS_ASPECT_VERTICAL_4_5`
- `ODM_CANVAS_ASPECT_CUSTOM` for explicitly authored dimensions.

Width, height, FPS and Q1.31 safe-area bounds are validated before publication.
Geometry uses integer/fixed-point math. Float/double is not serialized visual
authority. Changing aspect cannot silently alter Music Map, Composition,
Director or source-media identity.

The public convenience constructor
`odm_layered_config_init_default()` produces a normal canonical 1024-byte
`odm_layered_config`. It does not introduce a hidden preset format or parallel
semantic path. It validates the requested aspect/dimensions/FPS and fills a
complete, auditable configuration with resolution-scaled dimensions.

## 3. Background — independent environment

Background v1 supports NONE, SOLID, GRID and PERSPECTIVE_GRID. Perspective Grid
is independently raster-oracled. Spacing, line width, feather, opacity, reactive
zoom/warp/depth and perspective geometry belong only to Background.

A Core shape change (circle -> square -> rounded rectangle) cannot mutate
Background geometry. Background can still react to the same music-derived
Composition/Director state, so independence does not mean static imagery.

## 4. Core Media

Core v1 supports:

- CIRCLE
- SQUARE
- ROUNDED_RECT

with COVER, CONTAIN and STRETCH mapping. Boundaries are resolved with signed-
distance geometry and canonical signed rounding. The source surface is validated
once before the pixel hot loop rather than once per output pixel.

`odm_layered_config_set_core_shape()` changes only the canonical Core shape
configuration and is transactional: invalid input cannot partially mutate the
existing config.

## 5. Reactive Field, radial geometry and particles

The Reactive Field supports radial bars, orbit ring and deterministic particles.
Its perimeter follows the exact Core contour rather than assuming all cores are
circular. Circle, square and rounded-rectangle cores therefore generate
different field geometry by design.

Segment rasterization uses canonical signed nearest rounding, ties away from
zero. This removed a previously observed one-level sRGB asymmetry between
opposite radial pixels.

Particle autonomous phase advances from the exact canonical 48 kHz sample
coordinate, not from output frame index. Therefore one absolute sample maps to
the same physical particle state at 24/30/60 fps. Fixed-point products are
widened before multiplication; extreme sample coordinates are handled without
wraparound.

Two independent Python oracles rebuild these raster paths without asking C for
expected values:

- **Particle Geometry Oracle:** seed/mix64, sample phase, integer CORDIC,
  sub-pixel position, feathered disk and premultiplied blend; includes a vector
  near `INT64_MAX`.
- **Radial Geometry Oracle:** all 48 segments, CORDIC endpoints, Q24.8 segment
  projection, coverage, alternating colors and premultiplied blend.

## 6. HUD, progress and metadata

HUD elements are independently switchable: progress, time code, title and
artist. Geometry stays inside Canvas safe areas. Hiding HUD cannot change Core,
Background, Music Map or Director state.

Progress uses an overflow-safe exact integer mul/div implementation. It matches
an arbitrary-precision oracle through the full signed 64-bit project horizon,
including values adjacent to `INT64_MAX`.

`odm_layered_config_set_metadata()` is transactional. In 0.13 the built-in
fallback text path intentionally accepts bounded ASCII metadata only. UTF-8
shaping is **not claimed** by this extension.

Metadata participates in configuration identity even though it does not alter
music interpretation or Core/Background geometry.

## 7. Transactional frame publication

The Layered Compositor renders into caller-funded staging scratch. It validates
all layers, cancellation and output identity before the final copy. On any error
the caller frame is not partially published.

The canonical compositor order never changes because of aspect, Core shape,
metadata, runtime workers or export codec choices.

## 8. Runtime Plan — performance without semantic drift

The Native Export Session has byte-identical portable and accelerated execution
paths. `odm_export_runtime_plan_recommend()` converts an explicit caller
`worker_budget` (1..4) plus frame work size into a recommendation. It is:

- non-semantic;
- absent from Recipe/Receipt identity;
- explicitly overridable by the caller;
- forced-portable compatible;
- required by tests to produce the same bytes as the portable path.

Current conservative work thresholds are 2 workers from 16,384 pixels and 4
workers from 65,536 pixels when the caller budget allows it. These thresholds
are implementation policy, not a performance guarantee for every CPU.

A local observation on the build host found worker creation harmful at 64/96²,
useful at 128², and clearly beneficial from 256² upward. At 1080² the sampled
hot-loop improved from about 29.4 ms/frame with one worker to about 11.7 ms/frame
with four. These timings are **observations only**, not SLA or semantic truth.

## 9. Export Policy, Profile and Recipe

Export decisions are engine contracts rather than private FFmpeg commands:

- Export Policy: canonical **1024-byte** semantic policy.
- Export Profile: canonical **256-byte** delivery-parameter record.
- Export Recipe: canonical **512-byte** project/export record.

The Recipe binds layered config/policy, Export Policy and Profile identities,
exact frame count and exact `output_end_sample`. `frame_index -> sample` is
calculated by the engine and overflow is rejected before multiplication/cast.

`odm_export_profile_init_h264_aac()` removes boilerplate while preserving caller
choice. The caller supplies only CRF, preset, video thread count, audio bitrate
and explicit flags; the engine fills and validates adapter/container/source
formats, H.264/AAC, yuv420p, BT.709/sRGB/TV-range and rate-control fields. The
returned object is still an ordinary canonical profile; no hidden preset
semantic exists.

## 10. Exact audio and no `-shortest`

Canonical stereo Q1.31 is written as little-endian signed S32LE. The engine
outputs every admitted source sample and then exact digital silence until
`output_end_sample`. FFmpeg is never allowed to choose duration through
`-shortest`.

Native Export performs two source-audio identities:

1. admission hash of the complete canonical source PCM before `sink.begin`;
2. an independent hash of the source portion actually transmitted.

They must match before commit. A callback that mutates even one source sample
during the session causes `INTEGRITY_ERROR`, exactly one abort, no commit and no
published Receipt. The final padded audio stream has a separate identity.

## 11. Native Export Session

For every Recipe frame the runner:

1. derives exact sample and `tick=floor(sample/480)`;
2. obtains Composition and Director from the caller provider;
3. rejects state whose tick is not the exact derived tick;
4. obtains the Core surface;
5. resolves and renders the Layered frame;
6. streams exact RGBA8 bytes to the transactional sink;
7. emits canonical source PCM + exact S32LE silence padding;
8. hashes raw video, source audio and padded audio streams;
9. constructs the canonical 512-byte Export Run Receipt;
10. requests commit only after counts, identities and final cancellation checks.

Before `begin`, the runner takes immutable local snapshots of configuration,
Recipe, sink callback table and job ticket. Callback-side mutation of those
original structs cannot change an admitted session.

Preflight rejects unsafe overlap among frame, scratch, Receipt, PCM, request,
config, Recipe, sink and job-ticket storage. Scratch alignment is checked before
begin.

## 12. Transactional sink and transcript

A conforming sink treats `begin/write_*` as staging operations. Publication has
one terminal event:

`commit XOR abort`.

The tested successful transcript is monotonic:

`begin -> N video writes -> M audio writes -> commit`

Failures/cancellation produce exactly one abort after the failure point and no
published caller Receipt. A failing `commit` is followed by `abort`; therefore a
sink must keep staged state abortable until commit has returned OK. The engine
cannot undo an external irreversible side effect that a non-conforming sink
performs before returning an error.

## 13. Encoder boundary and argv

The core does not spawn a shell. It produces bounded **NUL-separated argv
Tokens** with explicit path placeholders. `odm_export_ffmpeg_argv_bind()` binds
only those paths. The host should execute the resulting argv vector directly.

FFmpeg remains an external adapter, not semantic authority. Current local smoke
coverage observes MP4/H.264/AAC, yuv420p and 48 kHz stereo. Encoder delay, codec
implementation, color conversion and compressed bytes are not claimed
bit-identical across FFmpeg/x264/AAC versions.

The Export Run Receipt binds **pre-encode raw truth**. Gate 11 Delivery
Contract/Artifact records can separately bind final encoded bytes.

## 14. Public integration flow — no private commands required

A host integration can be implemented entirely from public contracts:

1. Call `odm_layered_config_init_default()` with the requested aspect, dimensions
   and FPS.
2. Optionally call `odm_layered_config_set_core_shape()` and
   `odm_layered_config_set_metadata()`.
3. Validate/hash the ordinary Layered Config and create the exact export plan.
4. Call `odm_export_profile_init_h264_aac()` with explicit quality/performance
   choices.
5. Build `odm_export_recipe` using the engine API.
6. Populate the public Export Run request/providers and transactional sink.
7. Query `odm_export_run_requirements()` for caller-owned frame/scratch storage.
8. Optionally call `odm_export_runtime_plan_recommend()` with the host worker
   budget; override it if desired.
9. Run the Native Export Session. The engine produces exact raw video/audio and
   an atomic Receipt through the sink.
10. Ask the engine for/bind the FFmpeg argv vector with
    `odm_export_ffmpeg_argv_bind()`.
11. Execute that argv directly **without a shell**.
12. Treat encoded output as an adapter artifact; if commercial evidence is
    required, bind its bytes separately with the Gate 11 Delivery Artifact.

Callback pointers and frame/audio pointers are borrowed only for the duration of
the callback in which they are supplied. A sink that needs them afterward must
copy them into its own staging storage.

## 15. Evidence required for sealing 0.13

0.13 is not considered sealed until one immutable source identity passes:

- strict GCC and strict Clang matrices;
- all inherited oracles plus Layered, Perspective Grid, HUD geometry, Radial
  Geometry, Particle Geometry, Export Policy/Recipe and Native Export oracles;
- real local FFmpeg export smoke using only the engine-generated argv;
- ASan+UBSan under GCC and Clang;
- TSan;
- GCC `-fanalyzer` over every production unit;
- 3/3 clean-root reproducibility;
- clean parallel build from an empty build directory;
- clean-room extraction whose recomputed source identity matches the sealed tree.

Timing measurements are host observations only. 0.13 remains
`implemented_uncertified` until and unless an external certification process is
performed.

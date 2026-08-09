# ODPAR Music — Gates 0–11 local / Verifiable C11 engine

> **Post-Gate-11 extensions:** 0.12 adds Advanced Composition v1 + Visual Director v3; 0.13 adds independent Layered Composition plus a native exact-timeline Export Session. Both are additive and do not rewrite Music Policy v1, Music Map truth, Render IR truth, or the sealed 0.11 constitutional release.


## Current WIP — 0.24 Sample-Accurate Causal Presentation v1

This working checkpoint keeps the canonical 100 Hz Music Map but projects
Music-Reaction evidence into presentation time without allowing the video FPS
to erase short attacks. The local field uses 96 provenance-carrying spectral
lanes with slow/fast/attack morphology. Macro event authority is published once
at its offline-refined effective sample; immutable contextual salience remains
provenance while `dominant_pulse_q31` carries the strength still alive at the
exact presentation sample. Coarse tick authority cannot fire the same event a
second time. STRICT_CAUSAL forbids seed/time/orbit motion from masquerading as
music-driven reaction.

Current public identities: ABI 6, Music-Reaction Policy 11, Visual Policy 8,
Layered Policy 10. This is deliberately WIP, not a public FINAL release.


ODPAR Music is a deterministic C11 audiovisual engine built as an ordered chain
of evidence: Foundation → Media Truth → Canonical Music Map → Visual Score /
Render IR → signed `.odparms` → CPU reference pixels → deterministic visual
systems → Flutter FFI preview boundary → authoritative headless master → Private Studio workflow → evidence-bound delivery contracts.
Capabilities are never promoted merely because code, a signature, codec
executable, cloud service or visual idea exists.

The normative inputs remain `ODPAR_MUSIC_CONSTITUTION_v0.md` and the engineering
method inherited from ODPAR / Compare Documents: canonicalize once, separate
facts from inference, fail closed, bind identity to content, make ownership and
limits explicit, and earn maturity from executable evidence.

## Implemented local chain

- **Gate 0–1:** exact 48 kHz time, fixed-point/RNG, SHA-256, ODMC, memory budgets,
  ownership, cancellable jobs/executor, FFI-safe ABI and executable Music Spine.
- **Gate 2 Media Truth:** bounded native WAV→stereo Q1.31 and PNG→RGBA8,
  content-addressed Media Facts, exact rational duration and fail-closed media
  dispatcher. Recognized formats without a native adapter are not claimed.
- **Gate 3 Music Map:** deterministic 48 kHz resampler, 100 Hz / 480-sample map,
  2048 periodic-Hann + integer CORDIC/FFT, exact feature vectors, canonical
  192-byte Music Policy and two-pass `analysis.bin`.
- **Gate 4 Score/IR:** validated typed Visual Score, capability registry,
  canonical Render IR, policy/map/resource binding and exact Frame State.
- **Gate 5 `.odparms`:** strict deterministic ZIP32 subset, bounded fixed-Huffman
  DEFLATE, Ed25519 signed manifest, external trust-anchor option and transitive
  Score/IR/Map/Policy/media revalidation.
- **Gate 6 CPU reference renderer:** RGBA16LE linear BT.709 premultiplied pixels,
  fixed-point transform/CORDIC/bilinear sampling, image + exact-PTS video,
  transactional publication, pixel/frame SHA and deterministic frame-root.
- **Gate 7 visual systems:** explicit state slots, Entry/Exit, COPY/RESET Visual
  Memory with latest-32 event history, deterministic Grid/Halo/Residual Event
  Field/Particles and independent pixel/state oracle.
- **Gate 8 local FFI preview:** same canonical CPU renderer followed only by
  deterministic raster degradation; flat shared-library ABI, caller-owned job
  storage and generated Dart binding. Real Flutter runtime remains blocked in
  this build environment because the Flutter/Dart toolchain is absent.
- **Gate 9 local authoritative master:** signed project revalidation, Score→IR
  byte parity, exact PCM/Map binding, canonical frames + soundtrack/silence,
  deterministic work quote and render_id, transactional master sink, 576-byte
  Render Receipt with self-recomputed timeline/work/render_id, and detached
  Ed25519 controller attestation. Cloud deployment itself is **NOT_RUN** and is
  not implied by local master success.
- **Gate 10 local Private Studio workflow:** signer-independent package-content
  identity, immutable 576-byte content-addressed revisions with stable project
  lineage, revision-bound Preview selecting Music Map ticks internally, exact
  320-byte Preview approvals, and revision/package preflight before any master
  sink begins. Full production Studio UI/runtime is **NOT_RUN** in this build.
- **Gate 11 local Commercial Evidence / Delivery:** canonical `render_id` remains
  untouched by codecs; a separate 384-byte Delivery Contract binds the Gate 9
  Receipt to MP4/H.264/AAC/BT.709/yuv420p/48 kHz plus encoder/settings descriptor
  identities, and a 256-byte Artifact record binds the delivered file SHA-256.
  A Python oracle recomputes those identities independently. One local FFmpeg
  7.1.5/libx264/AAC smoke is observed only for this host and is not universal
  certification, licensing advice, a Cloud claim, or a bitrate/quality guarantee.

Music Policy v1 SHA-256:

`cbe9ccd31eeaa27be0113f8c3481f79a48cbd08bf318adefaa665ab8fa8efaec`

Gate 9 canonical master-oracle identities are recorded in
`evidence/gate9_evidence.json` and `tools/check_master_oracle.py`. Gate 10
revision/approval identities are independently recomputed by
`tools/check_studio_oracle.py`.

All implemented capabilities remain `implemented_uncertified`; evidence is not
an external certification.

## Build and verify

```sh
make all
./build/odpar-music --version
./build/odpar-music --spine-summary
./build/odpar-music selftest
make test-gcc
make test-clang
make master-oracle
make studio-oracle
make delivery-oracle
make commercial-claims-check
make ffmpeg-delivery-smoke
make asan-gcc
make asan-clang
make tsan-gcc
make analyze-gcc
make repro
```

## Source map

- `include/`: public fixed-width C11 contracts;
- `src/core/`: hash/wire/limits/ABI/numeric/time/RNG/memory/jobs;
- `src/media/`: Media Truth admission/decoding/canonical facts;
- `src/music_map/`: resampling, exact analysis and `analysis.bin`;
- `src/score/`, `src/ir/`: authored score, compiler, Render IR and Frame State;
- `src/package/`: canonical `.odparms`, DEFLATE and signatures;
- `src/renderer/`: CPU reference color/sampler/compositor/frame-root;
- `src/visual/`: deterministic Visual Memory and procedural systems;
- `src/preview/`, `flutter/`: Gate 8 preview/FFI boundary;
- `src/master/`: Gate 9 master, Render Receipt and controller attestation;
- `src/studio/`: Gate 10 immutable revisions, Preview approvals and workflow preflight;
- `src/delivery/`: Gate 11 delivery-contract and delivered-artifact identity;
- `src/compositor/`: 0.13 independent Canvas/Background/Core/Field/HUD and ordered compositor;
- `src/export/`: canonical export recipe, raw-stream session/receipt and encoder argv boundary;
- `src/spine/`: executable architecture registry and verification;
- `tools/`: independent oracles, guardians and source-bound evidence collection.

Generated `build/` artifacts are never authoritative. Rebuild from source and
verify the content-derived `source_id` plus the gate evidence receipt.


## 0.12 Advanced Composition / Visual Director v3

The post-Gate-11 experimental extension adds a deterministic, fixed-point
Advanced Composition Layer and a long-form Visual Director. Director v3 keeps
macro layouts stable by minimum dwell and candidate-stability rules, treats
one-tick impacts as micro-events, allows sustained fracture to become a bounded
FRACTURE macro layout, and uses exact anti-stagnation novelty accounting. It
consumes Composition state only and never reinterprets or changes Music Map
truth. The layer is allocation-free and O(1) per tick.

Visual Policy v1 binds that semantic grammar to a canonical 1024-byte policy.
Its SHA-256 changes when algorithm revisions, thresholds, dwell/transition rules,
phase/alternate constants or any of the seven layout target tables change. The
policy is reconstructed byte-for-byte by an independent Python oracle.

## 0.13 Layered Composition & Native Export Session

Version 0.13 turns visual composition into explicit independent authorities rather
than a monolithic effect pass:

- **Canvas/Layout:** canonical custom, 16:9, 1:1, 9:16, 4:3 and 4:5 geometry,
  exact supported FPS and Q1.31 safe areas.
- **Background:** independent solid/grid/**perspective-grid** layer. Spacing, zoom,
  warp and depth react to Composition/Director state without depending on Core
  shape or HUD metadata; Perspective Grid has an independent geometry oracle.
- **Core Media:** configurable circle, square or rounded rectangle with exact
  SDF contour, COVER/CONTAIN/STRETCH mapping, feather and border. The source can
  be a still image or a time-addressed decoded video surface.
- **Reactive Field:** radial bars, orbit ring and deterministic particles anchored
  to the exact Core perimeter. Particle motion advances from the canonical
  48 kHz sample coordinate, not export FPS.
- **HUD:** independently switchable progress, time code, title and artist fields,
  positioned inside canonical safe areas. Progress remains exact through the full
  signed-64-bit timeline. Built-in metadata rendering is intentionally bounded
  ASCII in 0.13. HUD metadata changes configuration identity but cannot change
  music interpretation or Core/Background geometry.
- **Layered Compositor:** fixed ordering Background -> Core -> Reactive Field ->
  HUD with caller-owned scratch and all-or-nothing frame publication.
- **Export Engine:** canonical 1024-byte Export Policy + fixed 256-byte Profile
  and 512-byte Recipe, exact frame->sample mapping, padded S32LE Q1.31 audio,
  bounded NUL-separated FFmpeg argv tokens and no shell command or `-shortest`
  duration authority. Public builders remove boilerplate without creating hidden
  semantics.
- **Native Export Session:** calls an exact-time visual-state provider and Core
  frame provider, renders every RGBA8 frame through the native layered
  compositor, emits the complete padded S32LE stream, hashes both raw streams,
  and commits a 512-byte Export Run Receipt only after all counts, hashes and
  cancellation checks succeed. A provider returning a Composition/Director
  tick that differs from `floor(sample/480)` is rejected before frame
  publication.

The full 0.13 contract is documented in `docs/LAYERED_COMPOSITION_EXPORT_V1.md`.
Final 0.13 source-bound evidence is assembled by `tools/collect_extension013_evidence.py`.

0.13 also exposes canonical configuration builders for the supported aspect
families, transactional Core/metadata setters, a non-semantic runtime-plan
recommendation (1/2/4 workers, caller-overridable), and an exact H.264/AAC profile
builder. Independent Python oracles reconstruct extreme HUD progress, Perspective
Grid, radial segments and particle rasterization pixel-for-pixel. Native Export
pre-hashes admitted PCM and verifies the source PCM actually transmitted before
commit, so mid-session audio mutation fails closed.

The external encoder remains an adapter. The session owns the raw frame/audio
truth and exact timeline; FFmpeg only receives the motor-generated argv template
with three path placeholders substituted by the runner. Encoded MP4 bytes are
not claimed to be bit-identical across FFmpeg/x264 versions.

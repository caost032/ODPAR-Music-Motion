# Gate 6 architecture — deterministic CPU reference renderer

Gate 6 freezes the first pixel-authoritative renderer for ODPAR Music. It does
not invent a second timeline or a second visual-state system: validated Render
IR plus the exact Gate 4 Frame State resolver remain the only semantic authority.
The renderer is deliberately CPU/fixed-point and is the oracle against which
later accelerated preview/master backends must prove parity.

## Canonical output pixel contract

Reference frames are tightly packed `RGBA16LE` in linear-light BT.709/D65 with
premultiplied alpha. Each channel is an unsigned 16-bit little-endian value. The
internal color domain is signed Q4.27 RGB plus Q1.31 alpha. No floating-point
operation defines an authoritative output pixel.

Source surfaces admitted by Gate 6 are tightly packed RGBA8 with BT.709
primaries, straight alpha and an explicit transfer function. v1 accepts linear
or IEC 61966-2-1 sRGB transfer only. The sRGB EOTF is a frozen 256-entry Q4.27
table independently regenerated with 80-digit Decimal arithmetic. Unknown color
metadata is rejected rather than guessed.

## Geometry and sampling

The output plane is normalized to `[-1,+1]` in both axes, origin at the frame
centre, Y increasing downward. Node translation/scale use Q32.32; phase uses the
canonical uint32 full-turn representation. Rotation uses deterministic integer
CORDIC with exact cardinal shortcuts. Inverse transform and ratio conversion use
checked integer arithmetic and explicit nearest/ties-away rounding.

RGBA8 resources are sampled with four-tap bilinear interpolation *after* transfer
decode and premultiplication, so interpolation occurs in linear premultiplied
light rather than gamma-coded source values. Samples outside the source extent
are transparent. Node opacity is applied after sampling.

Images expose exactly one zero-timestamp surface. Video resources expose a
strictly contiguous sequence of local 48 kHz sample intervals beginning at zero.
Frame lookup is interval-based and deterministic. A resource that does not cover
the requested local sample fails closed; it never repeats the last frame
silently.

Gate 6 consumes already-admitted canonical video surfaces. It does not claim a
native FFmpeg/libav video decoder in this environment, because the libav
development headers are unavailable.

## Scene composition and transitions

Every scene is composited independently in canonical z order: Environment first,
then z-index/node-id order. Black Void is an actual opaque linear-black source.
Image/video nodes use premultiplied source-over. Identity/empty nodes are exact
no-ops.

When two scenes are active, the renderer first completes both scene buffers and
then crossfades the two final scene pixels using Gate 4 transition progress. It
does **not** pre-weight each node by transition alpha, avoiding node-count and
layer-order dependent crossfade errors.

The Gate 6 "Halo proof" is only a ring-shaped test image used to exercise
sampling/compositing. No Halo semantic capability is smuggled into Gate 6;
actual Halo/Grid/particle systems remain Gate 7.

## Asset identity and publication

An asset set is content-addressed under `ODPAR_RENDER_ASSETS_V1\0`. The digest
binds sorted resource IDs/kinds, source SHA-256, frame counts, dimensions,
pixel/color metadata, exact PTS intervals, byte lengths and SHA-256 of every
surface. Assets must match the validated Render IR resource section exactly.

Rendering is size-first and transactional. Scratch must be at least 8-byte
aligned and large enough for the complete working frame, Frame State, transfer
events and render items. Caller-visible frame bytes and `odm_reference_frame_info`
are published only after complete rendering and hashing. Cancellation, bad
assets, missing video coverage, invalid scratch or arithmetic failure therefore
cannot publish a partial successful frame.

Each frame receives:

- pixel SHA-256 over its exact canonical RGBA16LE bytes;
- frame SHA-256 under `ODPAR_REF_FRAME_V1\0`, binding Render IR identity, asset
  identity, frame index/sample, geometry/pixel format and exact pixels.

`odm_reference_frame_root_*` folds frame hashes in exact index order under
`ODPAR_REF_FRAME_ROOT_V1\0`. It binds Render IR, assets, renderer version,
pixel format, dimensions, FPS, frame count and output end; updates additionally
reject forged frame sample or byte extent.

## Golden and independent evidence

Gate 6 freezes six 8x8 canonical raw golden frames under
`tests/golden/renderer_v1/`. They cover sRGB image decode, transparent edges,
non-cardinal 45-degree CORDIC rotation, anisotropic scale, non-power-of-two
opacity, scene compositing, a two-frame exact-PTS video and a crossfade.

`tools/check_renderer_oracle.py` independently implements the color math,
CORDIC, transform, bilinear sampler, premultiplied composition, transition,
asset identity, frame identity and frame-root in Python. It requires the frozen
goldens to match that mathematical implementation and then requires the C
renderer to match every one of the 384 pixels / 1,536 RGBA channels byte for
byte.

Current independent oracle root for this frozen vector:

`4058cd8285ffc941db87294386d8c4b573ab6d55a5895d1b7016f8184d097ab0`

## Evidence boundary

Gate 6 proves the deterministic CPU reference path on exercised toolchains. It
is `implemented_uncertified`, not externally certified. It does not claim GPU
parity, HDR, wide-gamut output, arbitrary source pixel formats, native video
container decoding, Halo/Grid/particles, or product UI. Those are downstream
contracts and may not redefine these authoritative reference pixels silently.

# Gate 8 — Flutter FFI preview architecture

Gate 8 adds a preview boundary without creating a second rendering semantics.
The authoritative input remains the validated Render IR and the same Frame State
resolver and CPU reference renderer used by Gate 6/7.

## Semantic boundary

`odm_preview_render_frame` always renders the complete canonical frame first as
RGBA16LE, linear BT.709, premultiplied alpha. Only after that frame succeeds may
Preview reduce raster resolution and convert it to RGBA8 sRGB straight alpha.
The permitted raster classes are EXACT, HALF, QUARTER and EIGHTH. They may not
change frame/sample selection, Music Map values, scene resolution, state
transfer, procedural seeds, parameters or capability semantics.

The reduction is deterministic integer box filtering in premultiplied linear
light. Unpremultiplication and linear-to-sRGB conversion are fixed integer
operations. Preview bytes are staged in scratch and published to the caller only
when the complete operation succeeds; cancellation or any validation failure
leaves the caller output unpublished.

## Flat asset ABI

Flutter never passes engine-owned graph pointers. Resources and surface frames
are flat records whose pixel data is addressed by checked offsets into one
caller-owned byte blob. Resource IDs must be increasing, frame ranges contiguous,
and pixel regions canonical without gaps or overlaps. All arithmetic is checked
before pointer construction.

## ABI discovery and lifecycle

The original FFI ABI v1 remains unchanged and continues to advertise feature
bits 31. Preview is a separate ABI schema (`odm_ffi_preview_abi_info`, 128 bytes)
that publishes all dependent layout sizes, including `odm_music_analysis_tick`,
and the exact size/alignment required for caller-owned `odm_job` storage.

The job object never crosses the ABI as an engine-owned lifetime. The caller
allocates aligned bytes, initializes them, begins a generation, can request
cancellation/read coherent progress, finishes that generation and destroys the
storage. Preview reconstructs only a transient internal ticket from
(storage,generation).

## Flutter binding

`flutter/odpar_music_ffi.dart` is generated deterministically from
`tests/golden/preview_ffi_v1.json`. It contains all fixed-layout structures and
lookups for ABI discovery, job lifecycle, preview planning and preview rendering.
The generated file is byte-compared on every verification matrix.

This environment has no Dart or Flutter executable. Therefore C/shared-library
and static Dart ABI parity can be proven here, but a real Flutter runtime lane is
explicitly `BLOCKED_MISSING_FLUTTER_TOOLCHAIN`; Gate 8 must not claim mobile
runtime certification from this environment.

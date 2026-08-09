# Gate 2 execution plan — Media Truth

Status: native WAV/PNG Media Truth implemented and verified on the evidence host.

## Frozen contracts

- Recognition and decode support are separate facts.
- Audio authority after decode is stereo Q1.31; mono duplication is exact.
- Source floating-point audio never makes host floating-point arithmetic
  authoritative.
- Canonical audio duration at 48 kHz uses exact rational ceiling.
- Media Facts are source-addressed and decoded-content-addressed.
- PNG publication is post-integrity, bounded and transactional.
- Unsupported/unknown/encrypted/corrupt inputs fail closed.
- No shell invocation of ffmpeg is silently substituted for a missing direct
  adapter contract.

## Implemented evidence

- Native RIFF PCM/IEEE/WAVE_FORMAT_EXTENSIBLE adapter.
- Native signature dispatcher with recognized-only boundary.
- PNG bounded CRC prevalidation plus RGBA8 decode adapter.
- Media Facts ODMC v1 encode/decode and SHA-256 identity.
- Public exact canonical-48k duration helper.
- Independent Python oracle for arbitrary-precision duration, Q1.31, ODMC and
  RGBA8.
- Cross-compiler, sanitizer, TSan, static analysis and reproducibility lanes.

## Explicitly not claimed in this environment

Direct libavformat/libavcodec/libswresample/libswscale integration is blocked by
missing development packages. FLAC/JPEG/WebP/MP3/ISO-BMFF/WebM can be recognized
but are not promoted to supported decode through a subprocess workaround.

## Exit rule

Gate 3 may consume only canonical facts/bytes emitted by this boundary. It may
not reinterpret containers or rely on decoder-specific timestamps hidden from
Media Truth.

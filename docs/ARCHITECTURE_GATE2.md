# Gate 2 architecture — Media Truth

Gate 2 admits media only when the engine can state exactly what it recognized,
what it can decode in this build, and what canonical bytes were produced. A
signature match is never promoted into decode support.

## Native WAV truth

The WAV adapter is a bounded RIFF/WAVE parser. It requires the RIFF-declared
extent to equal the supplied byte extent, walks every chunk with checked
padding, rejects duplicate `fmt ` and `data` chunks, validates block alignment
and byte rate, and fails closed on contradictory or unsupported formats.

Accepted v0 audio is mono/stereo integer PCM at 8/16/24/32 bits or IEEE float at
32/64 bits, including `WAVE_FORMAT_EXTENSIBLE` only when its subformat GUID is
PCM or IEEE float. Sample rate is explicit and bounded at 384 kHz.

Decoded authority is stereo signed Q1.31. Mono is duplicated exactly. IEEE
floating-point source bits are decoded by integer bit-field logic; host
`float`/`double` arithmetic is not semantic authority. NaN/Inf are rejected;
out-of-range finite samples saturate deterministically.

Canonical 48 kHz duration is `ceil(source_frames * 48000 / source_rate)` using a
portable full-width 64x64->128 product and 128/64 division. Overflow is a
status, never wraparound.

## Native PNG truth

PNG is prevalidated before libpng is allowed to publish pixels. The engine
checks signature, bounded chunk extents, IHDR ordering/uniqueness, image
ceilings, CRC-32 over every chunk, IDAT presence and exact IEND termination.
The decoded canonical pixel form is RGBA8. Decode is transactional: temporary
bytes are verified and hashed before caller output/facts are published.

libpng is an image codec adapter, not semantic authority for ODPAR policy. The
engine owns admission, integrity, canonical output identity and publication.

## Media Facts

`odm_media_facts` records source kind/container/codec/support, exact source
shape, canonical 48 kHz duration for audio, decoded byte count, SHA-256 of the
source and SHA-256 of canonical decoded bytes. The canonical wire form is a
160-byte payload in the generic 64-byte ODMC envelope. Reserved bytes are zero;
unknown schemas and corrupt payload digests fail closed.

## Dispatcher boundary

The probe recognizes WAV, PNG, FLAC, JPEG, WebP, MP3, ISO-BMFF and WebM
signatures. Only adapters present and independently exercised in this build are
marked `implemented_uncertified`. Recognized-only formats return
`ODM_STATUS_UNSUPPORTED`; corrupt bytes in a supported format preserve the
adapter's real parse/integrity error rather than falling through to another
format.

The evidence host has `ffmpeg`/`ffprobe` executables but no `libav*` development
headers/libraries, so FFmpeg direct-library decode is intentionally not claimed.
This is an environment boundary, not a reason to shell out and silently change
the authoritative runtime contract.

## Evidence

Gate 2 requires strict GCC and Clang matrices, GCC/Clang ASan+UBSan, GCC TSan,
GCC `-fanalyzer`, deterministic outputs, clean-root reproducibility, the Gate 1
crypto/ABI/hardening lanes, and a separate Python Media Truth oracle. The oracle
checks exact duration math against Python arbitrary-precision integers, WAV
Q1.31 bytes and hashes over multiple PCM/float widths and rates, independently
constructed Media Facts ODMC bytes, and PNG RGBA8 streams independently built
with zlib/CRC.

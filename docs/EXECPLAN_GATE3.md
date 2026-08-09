# Gate 3 execution plan — Canonical Music Map v1

Status: deterministic resampling, exact analysis, Music Policy v1 and
`analysis.bin` implemented and independently contrasted on the evidence host.

## Frozen contracts

- Canonical analysis input is 48 kHz stereo Q1.31.
- 48 kHz resampling is a bit-exact bypass.
- Non-48 kHz resampling is integer polyphase FIR/Lanczos with canonical table
  identity and exact output length.
- Music Map coordinates are 100 Hz / 480 samples per tick.
- Analysis window is centered 2048-sample periodic Hann with zero extension.
- Transform authority is a fully specified scaled radix-2 integer FFT.
- Exact features and inference are separate namespaces/contracts.
- Sequential analysis state is explicit and caller-owned.
- Music Policy has a canonical byte representation and SHA-256 semantic identity.
- `analysis.bin` binds canonical PCM identity and Music Policy identity and
  publishes transactionally only after full validation.

## Implemented evidence

- Portable checked resampler with 1024 phases and up to 512 taps.
- Independent regeneration/comparison of 720,896 FIR coefficients.
- Fixed Music Map analysis plan/tables and exact feature vector.
- Independent Python reimplementation of transform and feature formulas.
- Canonical 192-byte Music Policy v1.
- Canonical ODMC-wrapped `analysis.bin` with fixed 128-byte map header and
  128-byte tick records.
- Semantic-tamper tests in which a payload is modified and correctly re-hashed;
  the reader still rejects it before publication.
- Strict GCC/Clang, sanitizer, thread sanitizer, static analysis and clean-root
  reproducibility lanes.

## Explicit non-claims

- Beat/onset/BPM/phrase/section/semantic labels are not exact Music Map facts.
- `implemented_uncertified` is not external certification.
- The reconstructed Music Policy hash is not claimed equal to the unfinished
  Work-session hash because that Work never persisted its final policy bytes.
- Gate 3 does not render visuals; it produces the deterministic musical facts
  consumed by Gate 4.

## Exit rule

Gate 4 may consume only canonical Media Truth, canonical PCM identity and a
validated Music Map whose policy hash is accepted. It must not reinterpret the
analysis formulas or smuggle floating/perceptual inference into authoritative
Render IR state.

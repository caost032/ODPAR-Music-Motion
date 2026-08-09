# Claims matrix

What this engine may state publicly, and what it may not. Every SUPPORTED row
names the evidence that earns it. Anything not listed as SUPPORTED is NOT
claimed, whether or not it happens to work.

Regenerate the machine-checked half with `make commercial-claims-check`, which is
fail-closed against sealed `evidence/gate*_evidence.json`.

---

## SUPPORTED — earned by evidence in this tree

| Claim | Evidence |
|---|---|
| Deterministic local C11 pipeline from Media Facts to encoded delivery | 62-lane sweep, `evidence/audit/matrix5.json` |
| The same canonical render repeats byte for byte | `repro`, `determinism` lanes |
| Canonical `render_id` is separate from delivery identity; delivered bytes get their own artifact identity | `delivery-oracle`, Gate 11 receipt |
| Every hashed policy identity covers the semantics it governs | Visual Policy v9, Layered Policy v11, Visual Dynamics Policy v1, each with an independent byte-exact oracle |
| A frame plan whose provenance does not reconstruct its own `final` is rejected | `radial-provenance-oracle` forged-release negative control |
| Sub-threshold spectral evidence publishes exactly zero, not a faint line | `radial-provenance-oracle` engine-side dead-zone controls |
| A visual response is a function of the sample coordinate, so frame rate cannot change the reaction | `visual-dynamics-oracle` (24/25/30/50/60/120 fps), `test_visual_dynamics.c` |
| Seek equals playback: no warm-up, no replay of historical attacks | `visual-dynamics-oracle`, G-VD-5 |
| Native WAV/PNG media truth with hostile-input rejection | `media-oracle`, guardian fixtures |
| Memory-safety and data-race lanes clean under two compilers | `asan-gcc`, `asan-clang`, `tsan-gcc` |
| One observed local FFmpeg build encoded the frozen MP4/H.264/AAC smoke vector | `ffmpeg-delivery-smoke`, host-local observation only |

## NOT CLAIMED — explicitly, including things that partly work

| Not claimed | Why |
|---|---|
| Beat, BPM, tempo or downbeat | No rhythm evidence layer exists yet (roadmap M7) |
| Sections, verse/chorus, or any semantic musical label | No structure layer; detecting a boundary would still not be a label |
| Instrument or voice identification | Not attempted |
| Onset detection accuracy on real music | Only synthetic discrimination is tested, and only partly (M3) |
| That the knees admit soft onsets | They do not; the 0.08 attack knee suppresses bowed/brushed onsets. Recorded in `RADIAL_MORPHOLOGY_V2.md` §5, repaired in M4 |
| That flicker is eliminated | Visual Dynamics makes it *expressible* to eliminate; nothing consumes the kernels yet (M2). Until then the knee only moves flicker to the knee |
| Native decode of MP3/AAC/FLAC/OGG/Opus/JPEG/WebP | Recognised, not decoded (M6) |
| Fractional NTSC frame rates | `samples_per_frame` is integral (M5) |
| Any GPU backend or GPU parity | None exists |
| Production Flutter/Studio runtime | Dart/Flutter toolchains absent; lane reports BLOCKED, never PASS |
| Cloud deployment, SLA, security posture or cost | Out of scope |
| Bit-exact or lossless equivalence after H.264/AAC | Encoding is lossy by construction |
| Byte-identical FFmpeg output across versions, builds, CPUs or hosts | One host observation is not universality |
| Codec, container, security, legal or regulatory certification | None sought, none implied |
| Any conclusion about third-party codec licensing | Not the engine's to make |
| Pricing, revenue, profitability, or wall-clock performance SLA | Non-semantic by constitution |
| Visual quality | Pixel oracles prove reconstructibility, not that anything looks good. That is a separate axis with separate gates (M10) |

## BLOCKED — cannot currently be evidenced here

| Lane | Cause | Status |
|---|---|---|
| Flutter/Dart runtime parity | Toolchain absent from this host | BLOCKED, never PASS |
| LeakSanitizer | Container tracing model blocks thread enumeration; ASan and UBSan remain active | Recorded as a host limitation in gate evidence |

---

## Rules

1. A claim without named evidence is deleted, not softened.
2. "It works on my machine" is an observation, never a claim.
3. A green test lane earns a claim about *behaviour*, never about *quality*.
4. When a limitation is discovered, it is recorded here in the same commit that
   discovers it — not in the one that fixes it.

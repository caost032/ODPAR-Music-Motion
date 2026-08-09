# Gate 11 — Commercial Evidence / Delivery Architecture

Gate 11 does not make marketing a renderer input. It closes the constitutional chain by making the boundary between **canonical render truth**, **delivery choices**, **delivered bytes**, and **commercial claims** executable and auditable.

## Identity chain

1. Gate 9 `render_id` identifies canonical semantic inputs and exact master timeline/output profile.
2. A Gate 11 Delivery Contract validates the complete Render Receipt and binds its `render_id` and Receipt SHA-256 to a frozen delivery profile plus encoder/version/settings descriptor hashes.
3. `delivery_contract_id` is a separate domain-separated SHA-256. Changing encoder/settings changes it without rewriting `render_id`.
4. A delivered file has its own SHA-256. The Artifact record binds that file hash and byte count to `delivery_contract_id` and produces `artifact_id`.

No wall clock, price, revenue, margin, user identity, cloud provider, billing state or marketing label enters any of these identities.

## Delivery Contract v1

The local profile is deliberately narrow and fail-closed. Music Spine registers the canonical master as `implemented_uncertified` and the FFmpeg delivery profile separately as `observed_local_adapter_only`; an observed adapter is never promoted to a canonical renderer backend. The profile is: MP4 container, H.264 video, AAC audio, SDR BT.709, yuv420p, CRF rate-control class, 48 kHz audio, exact constitutional FPS and even geometry bounded to 8192x8192. The ODMC payload is 320 bytes (384-byte record). Reserved bytes must be zero.

Encoder and settings strings are printable-ASCII descriptors hashed under a versioned domain with kind and exact length. The engine stores only their SHA-256 identities in the canonical contract; host discovery text remains outside renderer truth.

## Delivered Artifact v1

The artifact ODMC payload is 192 bytes (256-byte record). It binds exact delivered byte count, delivered-file SHA-256 and delivery contract ID. It does not assert that lossy encoded media is bit-exact with the canonical master.

## Independent evidence

`tools/check_delivery_oracle.py` uses C only to emit legitimate records. Python independently parses ODMC and recomputes descriptor SHA-256, `delivery_contract_id`, `artifact_id`, payload SHA and record SHA.

`tools/check_ffmpeg_delivery.py` is a host observation lane. It generates a tiny deterministic canonical raw vector and asks the installed FFmpeg to produce MP4/H.264/AAC with BT.709/yuv420p/48 kHz settings, then checks the result with ffprobe. That result is not a universal codec certification, cross-host reproducibility claim or licensing opinion.

`tools/check_commercial_claims.py` derives a deliberately conservative public-claim catalog from sealed Gate evidence and retains explicit nonclaims.

## Color / encoded-sample boundary

The canonical Gate 6 master surface is linear BT.709 premultiplied RGBA16LE. The current FFmpeg smoke uses a frozen black vector and therefore demonstrates container/codec/profile wiring on this host, but it **does not independently prove unpremultiplication, transfer-function conversion, chroma conversion, or encoded sample/color fidelity** for arbitrary imagery. `Delivery Contract` and `Artifact Record` prove binding and byte identity, not color certification.

## Measured work versus elapsed time

Deterministic work quantities from Gate 9 remain semantic inputs to quoting/receipts. Gate 11 may additionally record host-local benchmark nanoseconds and throughput, but those measurements are explicitly nonsemantic and cannot define pricing, Cloud cost, SLA, revenue, or profitability.

## Maturity

Canonical renderer and Delivery contract/artifact capabilities remain `implemented_uncertified`. The MP4/H.264/AAC delivery output profile is separately registered as `observed_local_adapter_only`. Local tests and adapter observations are evidence, not external certification.

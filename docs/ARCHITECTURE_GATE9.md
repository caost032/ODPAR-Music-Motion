# Gate 9 — Canonical Cloud Master architecture

Gate 9 defines the headless authoritative master renderer that a cloud worker may execute. The cloud is an execution/transport boundary; it does not redefine Score, Music Map, Render IR, frame state, procedural semantics, PCM, or renderer truth.

## Admission and identity

A master request supplies a signed `.odparms`, the external Score view, immutable admitted render assets, exact 48 kHz stereo Q1.31 PCM, the reference output profile, and optionally a generational job ticket. Before any sink begins, the engine fully verifies the package against an externally expected public key, opens verified borrow-only views of `render.ir` and `analysis.bin`, validates the current Music Policy, revalidates the IR, hashes the exact PCM, and recompiles Score → Render IR byte-for-byte. Any disagreement fails closed.

`analysis.bin` is validated once and exposed as a bounded random-access Music Map view. Master rendering reads only the tick for each frame and never treats inference semantics as canonical signal truth.

## Master profile v1

The authoritative reference profile is `master.reference_rgba16_pcm_q31_v1`:

- video: canonical CPU reference frame, RGBA16LE, linear BT.709, premultiplied alpha;
- audio: stereo signed Q1.31 little-endian at 48 kHz;
- frame timing: the exact integral-FPS/sample contract from Render IR;
- audio beyond canonical PCM through `output_end_sample`: exact digital silence;
- no delivery codec, bitrate, container, wall clock, region, machine identity, or business price participates in semantic identity.

The work schema is deterministic technical work: `width*height*frame_count + output_end_sample`. It is a capacity/accounting quantity, not billing policy. `observed_work_units` must exactly equal the frozen quote before commit.

## Transactional publication

The sink protocol is `begin → write_frame* → write_audio* → commit`, with `abort` mandatory after any post-begin failure. The caller receipt is staged internally and is copied out only after sink commit succeeds. Cancellation before begin creates no active sink; cancellation or any late failure after begin aborts and cannot publish a receipt.

## Render identity and receipt

`render_id` is a domain-separated SHA-256 over package, Score, capability set, Music Map, Music Policy, Render IR, asset-set, canonical PCM, master profile, renderer version, dimensions, FPS, frame/sample extents, and project seed. It deliberately excludes wall clock and delivery encoding.

A fixed 576-byte ODMC Render Receipt binds the same semantic identities plus soundtrack SHA-256, canonical frame-root SHA-256, quoted/observed work, and render_id. Receipt validation does not trust a valid outer ODMC digest by itself: it reconstructs the canonical timeline through `odm_time_*`, recomputes technical work, and recomputes render_id from the embedded identities. A re-hashed but semantically forged receipt therefore fails closed. A separate 160-byte Ed25519 controller envelope may attest the exact receipt. The rendering core never owns the controller private key; verification requires an externally expected public key.

## Performance boundary

Verified `.odparms` core views and the validated Music Map view avoid repeated package extraction/copying and repeated full-map validation. Master planning also calls `odm_reference_render_max_requirements`: the IR is parsed once and total canonical node/transfer counts provide a safe scratch upper bound, eliminating the former O(frame_count) requirements scan. Actual frame rendering still computes exact per-frame requirements and the CPU reference renderer remains the pixel oracle. Any later cache of immutable admitted renderer state must remain byte-identical to this oracle; Gate 9 does not weaken reference validation to claim speed.

## Certification boundary

Local headless master semantics can be fully tested in this environment. Actual Google Cloud Run deployment, service authentication, regional infrastructure, object storage, network retries, delivery encoding and production key custody are outside the local Gate 9 certification and must be recorded as NOT_RUN until exercised on the real cloud environment.

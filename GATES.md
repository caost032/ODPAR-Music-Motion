# ODPAR Music implementation gates

## Gate 0 — Executable constitution
- C11 project/build
- GCC + Clang strict warnings
- ASan/UBSan test modes
- source/module inventory
- dependency DAG
- live `odm_spine`
- typed status/errors/phases
- invariant registry
- guardian scripts
- reproducible-build metadata

Exit: empty renderer is acceptable; spine truth must be green.

## Gate 1 — Foundation / ODPAR core-for-Music
- rational/sample time
- fixed-point primitives
- deterministic RNG
- SHA-256 / proven crypto utilities as needed
- arenas / scratch arenas
- memory budget
- ownership ledger
- cancellable jobs
- progress snapshots
- FFI-safe flat contracts (without Flutter UI yet)

## Gate 2 — Media Truth
- FFmpeg adapter
- safe probe/admit
- canonical Media Facts
- audio decode to canonical PCM contract
- image/video decode adapters
- timestamp/VFR tests
- hostile/corrupt input tests

## Gate 3 — Canonical Music Map v1
- deterministic resampling/canonicalization policy
- fixed-point analysis vectors
- 100 Hz map coordinates
- deterministic transform/window
- exact feature formulas
- exact-vs-inference boundary
- analysis.bin format/hash

## Gate 4 — Visual Score compiler
- Score schema
- capability registry
- validation/ownership slots
- compiler
- canonical Render IR v0
- IR parity/hash tests
- exact Frame State resolver

## Gate 5 — `.odparms`
- signed/versioned package
- manifest/hashes
- STORE compressed media, DEFLATE suitable text/data
- hostile ZIP/path/overlap/bomb tests inherited in spirit from `.odpar`

## Gate 6 — CPU reference renderer
- canonical color pipeline
- black void
- subject/image/video sampling
- first Halo proof composition
- golden frames
- deterministic frame root

## Gate 7 — ODPAR visual systems
- Residual Event Field
- Grid proof environment
- deterministic particles
- modulators
- transitions
- State Transfer
- entry/exit

## Gate 8 — Flutter FFI preview
- same IR/state resolver
- lifecycle/ownership parity
- raster-only degradation classes
- cancellation/progress

## Gate 9 — Cloud master
- headless CLI/job
- project/IR revalidation
- work quote/observed work
- master render
- Render Receipt
- independent signing/controller boundary

## Gate 10 — Private Studio workflow
- implemented local workflow core; production Studio UI/runtime remains NOT_RUN
- signer-independent package-content identity + stable project lineage
- immutable content-addressed revisions with explicit parent chain
- revision-bound Preview with canonical Music Map tick selection
- exact Preview approval records bound to canonical/preview frame hashes
- revision/package master preflight before sink begin; Gate 9 remains authority

## Gate 11 — Commercial evidence
- implemented local Commercial Evidence/Delivery contract; canonical renderer claims remain `implemented_uncertified`, while encoded MP4/H.264/AAC delivery is only `observed_local_adapter_only`
- canonical render truth ends at Gate 9 `render_id`; delivery codecs never rewrite it
- strict v1 delivery contract: MP4 / H.264 / AAC / BT.709 / yuv420p / 48 kHz / supported exact FPS
- encoder/version/settings identities are descriptor-hashed into a separate `delivery_contract_id`
- delivered file bytes receive a separate SHA-256 and `artifact_id`
- independent Python oracle recomputes contract/artifact identities from ODMC bytes
- one local FFmpeg 7.1.5 + libx264 + AAC smoke is observed, not universal certification
- Delivery Contract/Artifact Record is not a color/sample conformance certificate; encoded color conversion remains an adapter observation
- commercial claim catalog is fail-closed against sealed Gate 2–10 evidence
- wall-clock measurements, pricing, revenue and profitability remain nonsemantic
- no licensing conclusion, Cloud SLA, production Flutter/Studio runtime or external certification is implied

## Post-Gate extension 0.13 — Layered Composition & Native Export
- explicit aspect/canvas authority and safe areas
- independent reactive Background
- circle/square/rounded Core Media with exact contour
- Core-contour Reactive Field and sample-time particles
- optional progress/time/title/artist HUD
- transactional ordered Layered Compositor
- canonical Export Profile/Recipe and shell-free adapter argv
- native export session with exact-time state/core providers
- domain-separated raw video/audio stream identity
- Export Run Receipt committed only after complete successful raw export

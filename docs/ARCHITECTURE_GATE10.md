# Gate 10 — Private Studio workflow architecture

Gate 10 is an authoring/workflow layer over the already-authoritative ODPAR Music chain. It does not define a second Score, Music Map, Render IR, Preview renderer or master renderer. Gate 4 remains the Score/IR authority, Gate 8 remains the same-semantics local Preview boundary, and Gate 9 remains the master rendering authority.

## Identity model

A signed `.odparms` has two distinct identities:

1. `package_sha256`: exact package bytes, including signature/compression representation;
2. `package_content_sha256`: canonical signed project content, deliberately excluding signer/signature and ZIP offsets while including manifest schema, semantic root hashes and every signed MEDIA/DATA entry.

This distinction makes re-signing identical content a semantic no-op while making any signed DATA/MEDIA change alter authoring identity. Package signature verification remains mandatory and separate from semantic identity.

A Studio project begins with a genesis revision. Its `project_id` binds project seed, Score SHA-256, package-content SHA-256 and initial signer public key. Studio v1 requires signer continuity inside one lineage; key rotation is not silently inferred.

## Immutable revision record

`odm_studio_revision_info` serializes as an ODMC record:

- 64-byte ODMC envelope;
- 512-byte canonical payload;
- 576 bytes total.

The payload binds revision number, parent revision ID, project geometry/time/seed, package byte identity, Score/capability/Map/policy/IR identities, authored asset index, package-content identity, stable project ID, signer public key and a recomputable `revision_id`.

Genesis requires a zero parent. Every child requires the exact previous revision record, increments the revision number, preserves project ID and signer, and rejects semantic no-ops. Outer ODMC payload SHA and inner revision ID are independently revalidated.

## Revision-bound Preview

Studio Preview never accepts an arbitrary Music Map tick from UI code. The call receives:

- immutable revision record;
- the revision's `render.ir`;
- the revision's `analysis.bin`;
- frame index and raster-only Preview configuration.

Studio revalidates revision → IR → Music Map identity, checks canonical frame counts, derives exact frame sample time and selects the corresponding canonical 100 Hz analysis tick internally. It then delegates to the Gate 8 Preview renderer. Therefore Studio cannot preview one revision while accidentally driving it with analysis from another project.

## Preview approval

An approval is another ODMC record:

- 64-byte envelope;
- 256-byte canonical payload;
- 320 bytes total.

Its `approval_id` binds revision ID/number, Render IR SHA, exact frame/sample, source/output geometry, FPS, raster/pixel/semantics classes, canonical-frame SHA and Preview-pixel SHA. Subjective UI metadata is not part of engine truth.

## Master boundary

`odm_studio_master_plan_build` validates the revision and delegates to Gate 9 planning, then requires the complete Gate 9 plan identity to match the revision. `odm_studio_master_run` performs this Studio preflight **before** calling Gate 9 and therefore before `sink->begin()`.

A stale/wrong package cannot first be discovered after master side effects begin. Once admitted, Gate 9 remains responsible for frame/audio generation, transactional sink semantics, Render Receipt and controller attestation.

## Fail-closed/nonclaims

Gate 10 local evidence proves the workflow core and its canonical records. It does not claim:

- a production Flutter Studio UI runtime;
- multi-user authorization or collaboration;
- production cloud deployment/object storage/network retry behavior;
- billing or commercial policy;
- production private-key custody/rotation;
- external certification.

These omissions are explicit rather than inferred capabilities.

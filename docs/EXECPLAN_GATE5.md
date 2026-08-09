# Gate 5 execution plan — `.odparms`

Status: implementation and hostile-input hardening complete; compiler,
sanitisers, independent oracle and clean-root reproducibility are the exit lanes.

## Frozen contracts

- ZIP32 canonical subset only; ambiguous ZIP features fail closed.
- manifest/signature/render IR/analysis have fixed first-entry order.
- MEDIA uses STORE; DATA uses deterministic fixed-Huffman raw DEFLATE.
- canonical paths reject traversal and alternate spellings.
- manifest is fixed-width little-endian and signed with Ed25519.
- an external expected public key is the trust anchor for authentication.
- all manifest content is SHA-256-addressed and CRC-checked at ZIP level.
- current Music Policy, Render IR and Music Map are revalidated after signature.
- MEDIA IDs/SHA-256 must match the validated Render IR resource section exactly.
- verify/extract/build publish caller-visible state transactionally.

## Hostile cases exercised

- EOCD disk/comment mutations;
- local/central offset disagreement and overlap/gap attempts;
- non-zero extra fields/timestamps;
- local/central method disagreement;
- STORE size disagreement;
- path traversal;
- modified signature, manifest and compressed DATA;
- validly re-signed manifest with a false MEDIA resource ID;
- wrong external trust key;
- wrong media bytes at build time;
- too-small build/extract buffers with sentinel preservation.

## Independent oracle

A C probe builds a real `.odparms` through public APIs. Python then parses the
ZIP structure without the engine verifier, inflates DATA with `zlib`, verifies
CRC/SHA-256, parses the signed binary manifest, verifies Ed25519 with
`cryptography`, checks Music Policy propagation and independently reads the
Render IR resource section. Two independent builds must be byte-identical.

## Exit rule

Gate 6 may consume only a validated Render IR (directly or from a verified
`.odparms`) and must use the exact Gate 4 Frame State resolver. It must not create
a second timeline, music-analysis path or resource-identity system.

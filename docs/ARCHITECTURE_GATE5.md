# Gate 5 architecture — signed `.odparms` canonical package

Gate 5 freezes the first portable project/package boundary for ODPAR Music. The
package is not trusted merely because it is a ZIP file or because it carries a
signature. The reader first proves one canonical ZIP interpretation, then proves
the signed manifest, then revalidates Render IR and Music Map semantics and the
transitive resource identities they bind.

## Canonical ZIP32 profile

`.odparms` v1 is a deliberately narrow ZIP32 language. It permits no Zip64,
encryption, data descriptors, extra fields, comments, non-zero timestamps,
platform attributes or alternate local-header interpretation. Every local
record is contiguous, every central-directory record points to the exact next
local offset, local and central metadata/name fields must match, and the central
directory must end exactly at the EOCD. Paths are lower-case ASCII from the
frozen safe alphabet and reject absolute paths, empty segments, `.` and `..`.

The first four entries are fixed and ordered:

1. `manifest.odm` — STORE
2. `signature.ed25519` — STORE
3. `render.ir` — STORE
4. `analysis.bin` — STORE

User entries follow in lexical path order. MEDIA is STORE so already-compressed
media is not re-compressed. DATA uses the engine's deterministic raw RFC-1951
fixed-Huffman encoder. The decoder accepts only that frozen subset and is bounded
by exact output length, the 32 KiB DEFLATE window, per-entry/package limits and
an explicit expansion-ratio ceiling.

## Signed manifest

The manifest is a fixed little-endian binary contract: 256-byte header plus
160-byte records. It binds Score SHA-256, capability SHA-256, Music Map SHA-256,
Music Policy SHA-256, Render IR SHA-256, signer public key, entry kind/method,
resource ID, uncompressed byte length, content SHA-256 and canonical path.
Reserved bytes/flags are zero and unknown schema versions fail closed.

The signature is Ed25519 over the exact manifest bytes. The current C build uses
OpenSSL's Ed25519 provider; independent evidence verifies the signature with
Python `cryptography` and RFC 8032 vectors. The embedded public key proves only
self-consistency. Authenticity requires the caller to supply the expected public
key (or establish equivalent trust outside the package); the verifier compares
that trust anchor before accepting the package.

## Transitive semantic binding

A valid signature is not enough. Verification revalidates the canonical current
Music Policy, the Render IR against the signed Score/Policy identity, and the
whole `analysis.bin`. It requires:

- Render IR record hash == manifest Render IR hash;
- Render IR capability hash == signed capability hash;
- Render IR Music Map hash == signed Music Map hash;
- Music Map record hash == signed Music Map hash;
- Music Map frame extent == Render IR canonical music extent;
- every MEDIA manifest entry has exactly one Render IR resource with the same
  resource ID and SHA-256, and every Render IR resource has exactly one MEDIA
  entry.

That final resource check is intentionally performed during package verification,
not only during construction. A correctly re-signed manifest that lies about a
MEDIA resource ID is rejected.

## Publication and extraction

Build is size-first. Compression scratch, manifest and signature are completed
before caller-visible package bytes are published. Too-small output buffers do
not receive partial package data.

Verify constructs all state locally and publishes `odm_odparms_info` only on
complete success. Extract first verifies the entire package and its semantic
bindings, then reports exact required size; a too-small extraction buffer remains
untouched. STORE payloads copy exactly; DATA is decoded by the bounded canonical
DEFLATE reader.

## Evidence boundary

Gate 5 proves deterministic package construction, strict single-interpretation
ZIP parsing, standard Ed25519 interoperability, bounded DEFLATE interoperability,
content-addressed manifest integrity, hostile structural rejection and transitive
Score/IR/Map/Policy/resource binding on the exercised toolchains. It does not
claim external certification or key-custody security, and it does not render
pixels; CPU reference rendering remains Gate 6.

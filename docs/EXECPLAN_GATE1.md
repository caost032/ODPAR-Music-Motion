# Gate 1 execution plan — ODPAR core-for-Music

Status: implementation complete; verification in progress.  This is a living
plan; passing source-level tests alone does not close Gate 1.

## Purpose

Complete the portable C11 foundation required by every later Music gate without
claiming media, Music Map, score, IR, renderer, effect, package, preview or cloud
capabilities.  Compare Documents remains the engineering constitution: canonical
bytes before presentation, one authoritative representation, exact/derived
separation, fail-closed parsing, bounded work, explicit ownership and maturity
earned from independent executable evidence.

## Frozen contracts

- Canonical project time remains signed 48 kHz sample coordinates.
- Canonical numeric state remains fixed-width integer/fixed-point; no native
  floating point becomes semantic authority.
- Canonical binary records are little-endian, fixed-width, versioned and
  content-addressed.  They contain no pointer, `size_t`, native enum layout or
  float representation.
- Every buffer API is size-first and transactional: failure reports the exact
  requirement and does not publish a partial semantic object.
- Unknown major versions, non-zero reserved fields, trailing bytes and digest
  mismatches fail closed.
- ABI-visible structures use fixed-width fields and have compile-time layout
  assertions.  ABI discovery is explicit, never inferred from a library name.
- Resource limits are explicit non-zero ceilings.  Arithmetic overflow is a
  rejection, never a route to admission.
- No Halo, Grid, renderer or visual effect work belongs to this gate.

## Deliverables and exit evidence

1. A self-contained SHA-256 implementation with checked streaming lifecycle,
   FIPS 180-4 vectors, boundary/chunk parity, canonical hex and constant-time
   digest comparison.
2. Sticky checked little-endian readers/writers plus a canonical record envelope
   whose payload and complete bytes have reproducible SHA-256 identities.
3. Versioned resource-limit/admission structures and overflow-safe work quotes.
4. ABI v1 discovery and size-first FFI adapters for status, hashing and live
   Music Spine reports; no borrowed output lifetimes cross that boundary.
5. A bounded executor only after the data/ABI contracts are green.  Queue
   capacity, worker count, task lifetime, cancellation, shutdown and accounting
   must be executable contracts rather than comments.
6. Unit, property, hostile/truncation, concurrency, negative guardian,
   determinism, reproducible-build, sanitizer and static-analysis evidence.
7. Music Spine modules, DAG, invariants, ownership, capabilities,
   investigations and known limitations reconciled independently against files
   and archive symbols.

## Gate decision

Gate 1 can be reported locally complete only when every available required lane
passes from a clean tree.  Toolchains unavailable in the environment remain
explicit blockers for the full cross-toolchain decision; results are never
inferred from another compiler.

## Decision log

- SHA-256 is embedded and endian-neutral.  External executables are not part of
  runtime identity.
- The generic record envelope is infrastructure, not Render IR v0.  Render IR
  sections and capability semantics remain Gate 4 work.
- ABI v1 exposes copied bytes/fixed structs, not internal ownership pointers.
- Limits have deterministic first-failure dimensions so receipts and clients
  agree on the same rejection.

## Risks still open

- Clang is unavailable in the current environment and its mandatory lanes
  cannot be certified here.
- LeakSanitizer cannot enumerate traced threads in this container; allocator
  accounting, ASan/UBSan and static analysis remain active, but this does not
  substitute for a runnable LeakSanitizer lane elsewhere.
- No external crypto certification is claimed.  The implementation is checked
  against published algorithm vectors and differential test tooling, not sold
  as a certified cryptographic module.

## Verification log

- Strict C11 compilation and the unsanitized suite pass after coherent job
  snapshot hardening and every-prefix record truncation checks.
- The suite currently contains more than 7.6 million deterministic checks.
- SHA-256 is differentially checked against Python `hashlib`; ABI layout is
  probed by an independently compiled native program; ELF properties are read
  from the produced binary rather than inferred from flags.
- The final local decision remains pending until GCC ASan/UBSan, TSan,
  `-fanalyzer`, clean-root reproducibility and the release performance floors
  all pass over this exact source identity.

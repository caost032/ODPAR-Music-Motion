# Gate 0 architecture and frozen foundation contracts

## Layering

`src/spine/modules.def` is the single production module inventory and dependency
DAG. It is compiled into the binary and independently parsed by
`tools/check_spine.py`. A source file missing from either side fails the check.
Layers are derived from dependencies: a module's layer must be exactly one more
than its deepest direct dependency.

The current dependency direction is:

1. leaf contracts: status, version, registry;
2. exact primitives: time, fixed point, RNG, budget, progress;
3. bounded state: arena, ownership, jobs, Spine report/impact;
4. executable evidence: self-test;
5. CLI surface.

Visual, media, Music Map, IR, renderer, and delivery directories remain empty.
The Spine serializes their registries as empty arrays and their capabilities as
`not_implemented` or `contract_only`; absence is never interpreted as support.

## Exact time and numeric authority

- The project coordinate is signed 64-bit samples at exactly 48,000 Hz.
- Master fps v0 is limited to 24, 25, 30, 50, and 60.
- `frame_start = frame_index * samples_per_frame` uses checked multiplication.
- `frame_count` is an integer ceiling and `output_end` is checked before any
  result is published.
- Default end is soundtrack length plus authored post-roll. Alignment silence
  is explicit and the soundtrack is not silently truncated.
- PCM is Q1.31, authored scalar values use integer micro-units, compiled
  geometry uses Q32.32, and phase is a wrapping full-turn `uint32_t`.
- Canonical rounding is nearest, with exact ties away from zero.
- No native floating type, compiler-specific 128-bit integer, pointer, `size_t`,
  or native enum layout defines a canonical serialized value.

## Deterministic RNG

PCG32 XSH-RR and its two-step seed procedure are frozen as
`pcg32-xsh-rr-v1`. Domain/entity seeds are derived through fixed SplitMix64
mixing. Public generation calls return typed status and reject an uninitialized
or structurally invalid stream; a zeroed object cannot masquerade as a seeded
generator. Bounded sampling uses rejection rather than modulo-biased admission.

## Memory and ownership

Memory budgets use C11 atomics. Reservation checks `UINT64_MAX - current` before
publishing and refuses both configured-limit excess and reservation-counter
wrap. Arenas make capacity, alignment, rewind lifetime, and clearing explicit.

Owner ledgers are deliberately single-controller structures. Each allocation
has an intrusive owner tag, physical and payload accounting, and one allocator
identity. Release and transfer validate the complete ledger first. Cross-budget
transfers reserve the destination and release the source transactionally before
changing ownership; failed admission leaves both ledgers unchanged. Arena and
owner objects must begin with their public zero initializer macros.

## Jobs, cancellation, and progress

One atomic 64-bit control word binds a visible state, cancellation bit, and
48-bit generation. A worker ticket is valid only for that generation. Reusing a
job invalidates stale workers. Finish first claims a private finishing state;
an accepted cancellation bit always selects the cancelled terminal state even
when the worker tries to finish with success.

Progress is integer millionths. Fraction calculation uses portable full-width
limb multiplication and an exact binary search, so `done * 1,000,000` cannot
overflow. Publication uses atomic maxima for work and progress, and phase can
only advance.

Job and memory-budget operations documented as concurrent are tested with real
pthreads and ThreadSanitizer. Arenas, owner ledgers, and RNG streams are not
internally synchronized and require one controller or external synchronization.

## Identity and publication

`tools/gen_build_meta.py` hashes canonical relative paths and bytes of source,
tests, tools, and normative documentation. The binary carries that SHA-256 and
compiler identity, never a timestamp. Compiler path-prefix mapping and a
suppressed linker build ID make builds in distinct absolute roots byte-identical.

The full Spine JSON uses a size-first API. A short destination is cleared and
returns `buffer_too_small`; it never emits a silently truncated report.

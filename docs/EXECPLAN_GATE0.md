# ExecPlan: executable constitution and C11 Music foundation

This is a living implementation plan. It records what was actually built and
what evidence exists; it is not a retrospective claim that future gates work.

## Purpose

Establish ODPAR Music's executable constitution before any Halo, Grid, media,
or effect work. The deliverable is a portable C11 foundation whose exact time,
fixed-point math, deterministic RNG, memory/ownership, jobs, progress, live
Spine, guardians, and tests can be independently verified.

## Progress

- [x] Verify both input ZIPs, read the starter contracts, and audit the relevant
  Compare Documents architecture and fast-gate behavior.
- [x] Create a strict C11 static library and CLI with hardened release linking.
- [x] Implement fixed-width statuses and exact sample/rational time.
- [x] Implement portable checked arithmetic, Q1.31, Q32.32, micro-units, and phase.
- [x] Implement checked seeded PCG32 and deterministic domain/entity derivation.
- [x] Implement atomic budgets, bounded arenas, owner ledgers, and transactional
  transfers with OOM rollback.
- [x] Implement generational jobs, cancellation, exact monotonic progress, and
  terminal publication.
- [x] Implement the compiled module DAG, invariant/ownership/capability registries,
  explicit future-subsystem registries, JSON reports, and impact traversal.
- [x] Implement independent guardians, negative controls, header checks, Spine
  reconciliation, static analysis, determinism, and reproducible-build checks.
- [x] Run strict GCC, ASan/UBSan, ThreadSanitizer, GCC analyzer, and repeated-output
  evidence; fix defects found during those runs.
- [ ] Run strict Clang and Clang ASan/UBSan. Blocked only because Clang is absent
  from the current runtime; the Make targets fail closed rather than skip.

## Discoveries and corrections

1. The first external test compile exposed an incomplete test include path and a
   non-portable `UINTPTR_C` use. Both were corrected before accepting the suite.
2. Owner bulk transfer originally reserved the destination budget before fully
   validating the source list, which could strand accounting on invariant
   failure. Ledger validation and budget migration are now transactional.
3. The reservation counter could theoretically wrap after a successful byte
   reservation. It now has its own checked CAS and rolls back bytes on overflow.
4. The original RNG value-returning API allowed a zeroed, never-seeded object to
   emit values. Public generation is now status-returning and rejects invalid
   initialization state.
5. A cancel/finish race test initially executed a different number of assertions
   depending on the winning thread. Semantics were correct, but observable test
   output was nondeterministic. The implications are now checked with a constant
   assertion count, and repeated-output evidence passes.
6. LeakSanitizer cannot run under the container's tracing/thread inspection
   restrictions. The limitation is explicit; ASan/UBSan run with leak detection
   disabled, while allocator counters and static analysis cover lifetime errors.

## Decision log

- Use public integer constants rather than native enum ABI layouts.
- Keep all canonical numeric primitives portable C11; do not use `__int128` as
  hidden semantic authority.
- Make source identity content-derived and wall-clock free.
- Treat the module inventory as code, then audit it from a separate Python parser
  and real archive symbols.
- Keep future subsystem registries explicitly empty. Do not create placeholder
  backends/profiles that could look supported.
- Mark foundation capabilities `implemented_uncertified`; full Gate 0 remains
  blocked until the required Clang lanes execute.
- Add ThreadSanitizer even though Gate 0 only requires ASan/UBSan, because jobs
  and budgets make concurrency a material claim.

## Acceptance

Local acceptance requires `make gate0-local` to exit zero, the full and summary
Spine JSON to parse and reconcile, all negative controls to trigger, and two
clean-root builds to match byte for byte. Full Gate 0 acceptance additionally
requires `make test-clang` and `make asan-clang` to exit zero.

The empty renderer is intentional and acceptable at this gate. Media decoding,
Music Map extraction, visual compilation, rendering, and delivery remain future
work and must continue to fail explicitly.

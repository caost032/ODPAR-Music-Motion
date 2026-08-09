# Reference provenance

The implementation was derived from two read-only input archives. Neither
reference tree was modified.

| Input | SHA-256 | Verified scope |
|---|---|---|
| `ODPAR_MUSIC_GATE0_STARTER.zip` | `9df2228907d839fcf321e386b5463d5827bac19e5b6731c51b104c2dd786b574` | 37 ZIP entries; CRC and safe-path checks passed; all four non-empty normative files read |
| `MOTOR1.zip` | `66014dae3bfb3ff87187f1ce3075a745af37a8223de3efe0edbbb6fd76fe98f2` | 2,232 ZIP entries / 1,666 files; CRC, safe-path, duplicate, encryption, and symlink checks passed; every file traversed |

The canonical path-and-content manifest digest computed while traversing
`MOTOR1.zip` was:

`181e784789fae6663f130948953b9ac9ce53f6d3ec80bbeb87a2a6343a0090d0`

The mandatory Compare Documents reading order was honored: repository agent
instructions, improved master prompt, product README, and documentation rules.
The relevant live Spine, status, allocation, ownership, jobs, FFI, build,
guardian, and test implementations were then inspected as engineering
precedent.

The reference engine's fast gate was also executed in a temporary copy. It
returned success for the available hardened GCC, ASan/UBSan, live-Spine, format,
guardian, and invariant lanes. Its Clang-specific lane explicitly reported a
skip because this runtime has no Clang. This observation is baseline evidence
about the reference copy; it is not evidence that ODPAR Music itself passes
Clang.

Patterns adopted rather than copied blindly:

- a compiled live Spine plus an independent filesystem/symbol reconciler;
- resource budgets and fail-closed publication;
- explicit acquire/release/transfer ownership tables;
- atomic job cancellation/progress with stale-work rejection;
- deterministic identity without wall-clock data;
- positive tests paired with deliberately broken guardian fixtures;
- maturity states that cannot become certified merely by appearing in a table.

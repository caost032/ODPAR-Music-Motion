# Decision log

Newest first. Each entry records what was decided, what it cost, and what would
overturn it. A decision with no stated way to overturn it is dogma, not a
decision.

---

## D-006 — Sealed gate evidence checks containment, not equality

**Context.** Every `collect_gateN_evidence.py` compared aggregate Spine counters
with `==` against the values that existed when the gate closed (e.g. Gate 4
expected exactly 35 modules). Gate 11 additionally pinned the exact historical
version string `0.11.0-gate11-...` and an exact check count of 8,047,810.

**Problem.** Those counters only ever grow. `==` therefore turns *"the engine
grew"* into *"past evidence is now false"*, and makes every sealed receipt
permanently unregenerable. That is why no `evidence/` directory shipped with the
0.33 package and why `commercial-claims-check` had never run green.

**Decision.** Compare with `>=`. Rename the receipt fields to `minimum_spine` and
`spine_contains_gate_contract` so the receipt does not claim a match it no longer
performs. For Gate 11, verify that Spine and CLI report the *same* version — the
property that was actually load-bearing — instead of a historical constant.

**Cost, stated honestly.** `==` would have caught a module being deleted and
another added in the same commit (net counter unchanged). `>=` will not. That
loss is real. It is bounded by `spine-check`, which independently reconciles the
module table against the linked symbols on every lane.

**Overturned by.** A per-gate manifest of *named* contracts, checked for presence
rather than counted. That is strictly better than either `==` or `>=` and is the
right long-term answer; it is not done here.

---

## D-005 — Visual Dynamics is a layer, not a smoothing parameter

**Context.** The user-visible defect ("lines appear and vanish", "the core does
not relax after a hit") is usually addressed by smoothing the evidence.

**Decision.** Do not smooth evidence. Add a layer between evidence and geometry
where each target has its own response kernel, evaluated in closed form from the
sample coordinate of the causing event.

**Why not smoothing.** Smoothing the evidence would corrupt Music Evidence, which
the authority chain forbids, and would make every consumer inherit one temporal
character. Response kernels let the Grid be slow while the radial tip is fast,
*from the same evidence*, without either one lying about the music.

**Why closed form rather than per-frame integration.** Integration makes seek
approximate, makes the result depend on the frame grid, and makes preview and
master diverge. Closed form makes FPS invariance and seek exactness structural
properties rather than things to test for and hope.

**Overturned by.** A response that genuinely cannot be expressed in closed form
(a true physical simulation with coupling). Such a target would need its own
state model declaration and checkpoint strategy, per the constitution.

---

## D-004 — `commercial-claims-check` is a release gate, not a unit lane

It was listed inside `test-gcc`/`test-clang` while being fail-closed against
evidence that those very targets produce. It could only pass once it had already
passed. Moved to `gate11-local`, after the lanes that seal what it consumes.

**Overturned by.** Nothing. A gate that cannot be bootstrapped is not strict, it
is broken.

---

## D-003 — Debug output removed from the engine rather than silenced

Six `fprintf(stderr, "DBG export …")` calls sat in production export failure
paths. The determinism lane treats any stderr from the test runner as
nondeterminism, correctly, so the whole lane failed even with 8.7M checks green.

Removed rather than gated behind a flag: typed status already carries the same
information, and a debug flag in engine code is a second, untested output path.

**Overturned by.** A real need for structured tracing — which the Reaction
Inspector should provide as a typed, testable API, not as printf.

---

## D-002 — The 0.33 radial semantics were kept and versioned, not reverted

**The question.** Five oracles and one unit check disagreed with the tree. Was
the implementation wrong, or were the checks stale?

**Evidence that settled it.** Layered Policy v10 encoded, as a hashed policy
fact, `transient tip opacity is exact same-lane attack` while the tree computed
`attack²`. Visual Policy v8 encoded the ungated morphology. The policies were
self-consistent and describing a tree that no longer existed.

**Decision.** Keep the behaviour, freeze it as Visual Policy v9 / Layered Policy
v11, write the specification first, derive the independent checker from that
text, and only then touch the test.

**Why keep it.** Knee-gating is the minimum precondition for "no lines without
cause". Publishing *post*-gate provenance is strictly better than publishing
pre-gate values, because it keeps `final` reconstructible from the published
tuple, so a forged plan still fails closed at the render boundary.

**What was deliberately not fixed.** The knees are memoryless and the 0.08 attack
knee suppresses genuinely soft onsets. Recorded in
`docs/RADIAL_MORPHOLOGY_V2.md` §5 rather than patched, because changing numbers
and reconciling identities in the same commit makes it impossible to attribute
either outcome. D-005 is the real repair; relaxing the knee becomes safe *after*
it, since flicker prevention will no longer be the knee's job.

---

## D-001 — Signal Studio is vocabulary, never authority

`references/signal_studio_concept/` is kept verbatim and marked REFERENCE ONLY
with an explicit do-not-migrate table: `AnalyserNode`, four-band split,
bass-threshold "beat", `Math.random()`, `MediaRecorder` export.

Harvested instead: timeline, halo, grid, scenes, presets, autosave, portable
config, 1:1 / 9:16 / 16:9 canvas authority.

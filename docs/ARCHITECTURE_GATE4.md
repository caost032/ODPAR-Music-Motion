# Gate 4 architecture — Visual Score, Render IR and Exact Frame State

Gate 4 is the semantic compiler boundary between authored audiovisual intent and
render execution. It does not render pixels and it does not reinterpret audio.
It consumes an authored Visual Score plus the immutable Music Map binding and
produces one canonical Render IR whose exact Frame State can be resolved from a
frame index without consulting wall-clock time.

## Visual Score v1

The authored score is a typed C11 view composed of a fixed header and bounded
arrays of resources, acts, scenes, nodes, cues, modulators, automation,
transitions and typed state transfers. All coordinates are canonical 48 kHz
sample coordinates. Authored normalized values are integer micro-units; no
floating-point value enters the authoritative score/IR path.

Validation is fail-closed and size/count bounded. The score requires:

- acts to partition the project timeline exactly;
- scenes to belong to an act and to be ordered by time;
- at most two adjacent scenes to overlap at any sample;
- every overlap to have one exact transition;
- exactly one black-void Environment and exactly one Primary Composition per
  scene, each covering its complete scene interval;
- resources to be immutable SHA-256-addressed objects of an admitted type;
- nodes to use a registered capability/version/role/state-model combination;
- automation for a node/parameter to be ordered and non-overlapping;
- modulators to target supported parameters and exact Music Map facts only;
- state transfer to occur explicitly at a destination scene boundary.

The Score hash is field-by-field canonical little-endian serialization under the
`ODPAR_SCORE_V1\0` domain. Pointer values, struct padding and host ABI are never
hashed. The capability-set hash is likewise canonical under
`ODPAR_CAPS_V1\0` and contains exactly the used capability/version set.

## Capability registry

Gate 4 freezes the first semantic capabilities:

- `visual.black_void`;
- `visual.primary_empty`;
- `visual.primary_image`;
- `visual.subject_image`;
- `visual.look_identity`;
- score-level automation, Music Map modulation, crossfade, state transfer and
  timeline-cue capabilities.

The five visual capabilities currently declare `STATELESS`. Capability records
bind numeric ID, major/minor version, allowed role mask, required resource kind
and state model. Unknown or version-incompatible required capabilities fail; no
silent semantic substitute is permitted.

All Gate 4 capabilities remain `implemented_uncertified` until external evidence
exists.

## Canonical Render IR v0

Render IR is an ODMC-wrapped, content-addressed, little-endian binary record. Its
payload starts with a fixed 256-byte header and exactly nine required sections
in canonical order:

1. CAPABILITIES
2. RESOURCES
3. TIMELINE
4. NODES
5. MODULATORS
6. AUTOMATION
7. TRANSITIONS
8. STATE_TRANSFER
9. OUTPUT

The header binds schema, output dimensions/FPS, exact samples-per-frame,
project/output extent, project seed, Score SHA-256, capability SHA-256, Music
Map SHA-256 and Music Policy SHA-256. Section records are fixed-width integers
only; pointers, native enums, `size_t`, `float` and `double` are absent.

Compilation is size-first and transactional. The compiler validates the Score,
computes the exact record extent with checked arithmetic, writes all sections in
the frozen order, and finally wraps the payload in ODMC with its SHA-256. A
buffer that is too small is never partially advertised as a valid IR.

The semantic IR validator does not trust the ODMC digest alone. After integrity
validation it independently re-checks header geometry, section order/version and
extent, capability identity and exact usage, resources, acts/scenes/cues, node
roles/resource/state-model/ranges, modulators, automation, transitions, state
transfer and output timing. A hostile payload that is modified and then
correctly re-hashed still fails if its semantics are outside the compiler
language.

Transition validation is positional: every actual adjacent scene overlap must
consume exactly one transition whose from/to scene IDs and interval equal that
specific overlap. Declared-but-unused capabilities and used-but-undeclared
capabilities both fail.

## Exact Frame State

Frame State is a pure function of validated Render IR, `frame_index` and, only
when a visible modulator requires it, the exact Music Map tick for that frame.
The resolver computes:

`sample = frame_index * samples_per_frame`

with checked integer arithmetic. Runtime delta, presentation cadence and wall
clock never influence the result.

At a sample it resolves the active scene or the exact two-scene transition,
transition progress, visible nodes, authored automation, Music Map modulation,
scene weights, and state-transfer events. Post-roll beyond canonical music uses
synthetic exact digital silence rather than reading nonexistent Music Map data.

Resolution is transactional. A preflight pass performs every range/reference and
fixed-point arithmetic operation needed for the frame before caller-visible
node/transfer buffers are written. Cancellation or invalid input therefore
cannot publish a partially resolved frame as successful state.

## Numeric contract

Compiled geometry uses signed Q32.32. Opacity and normalized Music facts use
bounded Q1.31. Phase uses an unsigned 32-bit full turn. Automation interpolation,
modulation and transition weighting use explicit checked integer arithmetic and
frozen rounding/saturation rules. The IR validator rejects values outside the
range the Score compiler can produce, including correctly re-signed hostile IR.

## Evidence boundary

Gate 4 evidence proves deterministic semantic compilation and Frame State on the
exercised hosts. It does not claim visual/pixel parity because the CPU reference
renderer is Gate 6. It does not add Halo, Grid, particles or Residual Event Field;
those remain downstream visual-system capabilities and may not bypass this
compiler/state boundary.

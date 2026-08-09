# Gate 1 architecture — portable core-for-Music

Gate 1 extends the executable constitution without entering Media Truth, Music
Map, Visual Score, Render IR or rendering.  Its purpose is to make later gates
depend on one bounded, versioned and independently checkable substrate.

## Canonical byte boundary

`odm_wire_writer` and `odm_wire_reader` are sticky bounded cursors.  Once a
cursor fails, later operations preserve that failure.  Signed values are
reconstructed portably rather than relying on implementation-defined casts.

The ODMC v1 record has one 64-byte little-endian header and an exact payload
length.  It carries explicit wire and payload-schema versions, zero flags and
reserved fields, and the SHA-256 of its payload.  Readers reject unknown wire
versions, corrupt digests, truncation and trailing bytes before publishing any
semantic output.  The generic record is infrastructure; it is not Render IR.

## Identity and ABI boundary

SHA-256 has a checked streaming lifecycle and a maximum representable message
length.  Finalization clears internal material, hex is lowercase canonical, hex
decoding is strict, and digest equality does not exit early.  Published vectors
and a separately compiled Python-`hashlib` oracle cover one-shot and arbitrary
chunk boundaries.

ABI v1 is queried explicitly through a fixed 64-byte discovery structure.
ABI-visible structures use fixed-width fields and compile-time size/offset
assertions.  Text and byte results are copied with size-first APIs; no internal
pointer lifetime crosses FFI.

## Admission, memory and ownership

Resource requests are evaluated against versioned non-zero ceilings in a fixed
dimension order.  The first rejected dimension is deterministic.  Work quotes
use checked addition and multiplication; overflow is a rejection.

The executor charges one exact allocation to the optional memory budget.  That
allocation contains the executor, pthread handles, task slots and FIFO indices.
There is no per-submit allocation.  Slot acquire/release uses an O(1) freelist.

Task functions, contexts and job storage remain caller-owned from successful
submit until collection or executor destruction.  The executor owns terminal
job publication; a callback cannot legitimately finish or destroy its job.

## Concurrency and cancellation

Each job control word binds state, cancellation and generation.  A revision
sequence serializes multi-field publications.  Snapshot readers retry across an
active or changed revision, so state, phase, error, progress and work counters
come from one coherent publication.

The executor admits no more than its fixed task capacity.  Workers consume a
mutex-protected FIFO.  Queued cancellation prevents callback execution; running
tasks observe cooperative cancellation through their ticket.  Destroy stops
admission, requests cancellation, drains queued work, joins every worker,
publishes terminal job states and releases the complete allocation.  No public
executor operation may race destruction.

## Evidence boundary

Capabilities remain `implemented_uncertified`.  Local Gate 1 requires strict
GCC, ASan/UBSan, TSan, GCC `-fanalyzer`, deterministic outputs, clean-root
byte-for-byte reproducibility, crypto/ABI oracles, ELF hardening inspection and
explicit performance floors.  Strict Clang and Clang ASan/UBSan are mandatory
for the full gate and are never inferred from GCC.

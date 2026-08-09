# First implementation target

Do NOT implement Halo first.

First source files should establish the engine's constitutional skeleton, approximately:

include/
  odm_status.h
  odm_time.h
  odm_fixed.h
  odm_rng.h
  odm_memory.h
  odm_job.h
  odm_progress.h
  odm_spine.h

src/core/
  status.c
  time.c
  fixed.c
  rng.c
  arena.c
  memory_budget.c
  job.c
  progress.c

src/spine/
  spine.c
  spine_registry.c
  spine_impact.c

Tests first:
- time arithmetic / overflow
- frame sample mapping for 24/25/30/50/60
- frame_count/end padding contract
- fixed-point conversion/rounding
- deterministic RNG vectors
- memory budget refusal
- cancellation state transitions
- progress monotonicity
- spine dependency acyclicity
- guardian negative fixtures

Initial CLI targets:
- `odpar-music --version`
- `odpar-music --spine`
- `odpar-music --spine-summary`
- `odpar-music --spine-impact=<module>`
- `odpar-music selftest`

No media, no renderer, no visual effect is required to pass Gate 0.

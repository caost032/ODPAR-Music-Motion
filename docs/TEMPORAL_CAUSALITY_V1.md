# Temporal Causality v1 — ODPAR Music 0.17 WIP

This extension closes a presentation-rate correctness gap in Music-Reaction.
The canonical reaction timeline remains 100 Hz / 480 samples at 48 kHz. Video
presentation rate is not allowed to become musical truth.

## Causal Presentation Projection

For presentation sample `P` after previous presentation sample `Q`:

- Sustained lane/family/broadband levels are held from the latest canonical
  reaction tick whose center is `<= P`.
- `event_pulse` and `event_threshold` are also held state, not retrospectively
  maximized.
- Instantaneous lane/family/broadband attacks and `event_strength` are the exact
  maxima over canonical source ticks whose centers are in `(Q, P]`.
- Event flags are the OR over the same interval.
- No source tick with center `> P` may influence the frame.
- Batch export consumes each source tick at most once across a strictly
  increasing presentation schedule. A seek starts a fresh causal view and does
  not replay historical attacks.
- A single projection gap is bounded to one second to keep malformed callers
  from creating an unbounded per-frame scan.

The independent `check_reaction_projection_oracle.py` reconstructs these rules
without using the C implementation for expected values and exercises
24/25/30/50/60/100/120 fps.

## Offline macro-event refinement

This remains derived inference, not canonical Music Map truth and not a
beat/BPM claim. Candidate events must first be local evidence maxima in a
+/-2-tick window and exceed the causal per-tick threshold. The global offline
gate is an exact policy-bound nearest-rank median over those candidates only;
silence and ordinary sustain do not vote in that statistic. A single narrow
lane cannot trigger a global event: global evidence comes from broadband attack
or at least two frequency families.

## Exactness boundary

“Exact” here means byte-/integer-exact deterministic mapping from the frozen
signal-analysis/reaction policy to presentation state. It does not mean semantic
knowledge of musical intention, beat, instrument identity, vocal identity or
phrase boundaries unless those are separately specified and independently
certified.

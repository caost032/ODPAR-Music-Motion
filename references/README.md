# references/ — REFERENCE ONLY

Nothing in this directory is authority.

## signal_studio_concept/

`KAOST032_SIGNAL_STUDIO_DEFINITIVE` — a browser prototype that communicates
product intent (central media, halo, grid, timeline, scenes, presets, export
aspect ratios). It is **not** the target architecture and **not** a musical
authority.

Explicitly forbidden to migrate into the engine:

| Concept artifact | Why it must not enter the engine |
|---|---|
| `AnalyserNode` + FFT 2048 + 4 bands | Not deterministic, not fixed-point, no provenance, no policy identity |
| bass-threshold "beat" detection | Fabricates rhythmic truth the engine deliberately does not claim |
| `Math.random()` in visual paths | Violates `NO_UNSEEDED_RANDOMNESS` |
| `MediaRecorder` @ 30 FPS export | Not a master; FPS-as-clock violates the time contract |
| visual thresholds as musical policy | Visual constants are not evidence |

What may be harvested (as *vocabulary*, redesigned around the real engine):
timeline, halo, grid, scenes, presets, autosave, portable config,
1:1 / 9:16 / 16:9 canvas authority, keyboard control, fullscreen preview.

Provenance of `assets/`: user-supplied media used by the concept page. Treated as
regression *observation* material only — never as a correctness oracle.

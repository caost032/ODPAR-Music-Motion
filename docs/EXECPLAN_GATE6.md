# Gate 6 execution plan — CPU reference renderer

Status: implementation and hostile-input hardening complete; final compiler,
sanitisers, analyzer and clean-root reproducibility are the exit lanes.

## Frozen contracts

- validated Render IR + Gate 4 Frame State are the only semantic/timing input;
- canonical output is RGBA16LE, linear BT.709/D65, premultiplied alpha;
- source RGBA8 transfer metadata is explicit (`linear` or `sRGB`), never guessed;
- fixed-point transform and bilinear sampling only;
- image and exact-PTS video sampling fail closed outside admitted coverage;
- scenes composite independently before transition crossfade;
- output is transactional and scratch has an explicit 8-byte alignment contract;
- asset identity binds source/content/color/PTS metadata;
- each frame has pixel SHA + semantic frame SHA;
- frame-root consumes every frame exactly once in order and binds renderer/output identity.

## Hostile cases exercised

- wrong source SHA against Render IR;
- unsorted assets;
- unknown transfer function;
- discontinuous video PTS;
- wrong source byte length;
- canonical video that does not cover the requested frame;
- too-small and deliberately misaligned scratch;
- cancellation before publication;
- forged frame-root order, sample and pixel-byte extent.

## Independent oracle

The Python oracle regenerates sRGB transfer values independently, reproduces
integer CORDIC and all renderer fixed-point rounding, renders six 8x8 frames,
checks frozen golden bytes and independently computes asset SHA, every pixel/frame
SHA and the final frame-root. The C probe must match all 384 pixels / 1,536
channels exactly.

## Exit rule

Gate 7 may add visual systems only as deterministic authored/compiler/runtime
capabilities that ultimately resolve through the same Render IR / Frame State
and reference-renderer contracts. A visual system may not read wall clock or raw
audio, invent hidden state, or bypass canonical resource identity.

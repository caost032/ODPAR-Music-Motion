# Gate 8 execution plan / acceptance

## Required local evidence

- strict C11 GCC and Clang builds with `-Werror`;
- full inherited test suite plus Preview unit/negative tests;
- Music Spine reconciliation and negative guardians;
- shared `libodpar_music.so` with hardened ELF properties;
- independent dynamic ABI/layout/lifecycle oracle;
- independent raster oracle comparing every RGBA8 byte for all four raster
  classes against separately computed canonical renderer frames;
- generated Dart binding byte parity, including plan/render function types;
- ASan+UBSan under GCC and Clang;
- TSan;
- GCC `-fanalyzer` for every registered production unit;
- clean-root reproducibility 3/3.

## Full Gate 8 runtime acceptance

A separate environment with the supported Dart/Flutter toolchain must load the
same shared ABI from an actual Flutter isolate/process, query ABI sizes, allocate
aligned job storage, plan/render at least one canonical vector, verify progress
and cancellation, and compare the resulting pixels with the frozen Preview
oracle. Until that lane exists, this checkpoint is a **local Gate 8 pass with a
blocked Flutter-runtime lane**, not an external/mobile certification.

# Source layout

What each source file holds, and why the large ones stay whole. Splits follow cpp-libraries'
[`docs/SPLITTING.md`](https://github.com/Kronuz/cpp-libraries/blob/main/docs/SPLITTING.md).

## Library

- `docset.hh` (1,455 lines): the whole library. It stays one file.
  - The contract is a single header: the README promises "One header, no dependencies" and tells
    users without CMake to copy `docset.hh` onto their include path. SPLITTING.md keeps a library
    whose contract is a single file single-file unless the owner decides otherwise.
  - Its layers build on each other in order and share the target detection at its top
    (`DOCSET_SSE`, `DOCSET_BMI`, `DOCSET_NEON`, all off under `DOCSET_NO_SIMD`):
    `doc_iterator`, `bitset` and `sorted_set` with their 32-bit merges; the 16-bit sorted-array
    kernels (SSE4.2, SSSE3, SSE4.1, NEON, scalar); the 8 KiB bitmap word loops and the
    bitmap-to-array extraction; the chunk `container` and `set`; and the frozen on-disk form
    (`freeze`, `frozen_view`).
  - If the owner chooses to split it, those layers are the cuts, in that order, each a contiguous
    slice behind `docset.hh` as an umbrella. The token stream of every translation unit would stay
    the same, so the code placement the x86-64 extraction loop is sensitive to (AGENTS.md) would
    not move.

## Tests and benchmarks

No other hand-written file is over 1,000 lines: the largest are `test/test.cc` (329),
`test/diff.cc` (315) and `bench/micro_extract.cc` (211). `test/docset_old.hh` (1,171) is the
frozen previous header kept for the differential test and the A/B benchmarks, and is never edited
by hand.

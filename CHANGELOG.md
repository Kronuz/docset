# Changelog

Notable changes to docset, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

## 2026-10-31

### Changed

- **Array union on arm64** merges 8 lanes at a time with NEON (`unite16_neon`); it was the
  scalar merge. The vector merge uses a perfect-shuffle network on both ISAs: reverse one
  input, then four rounds of min/max and interleaving the minima with the maxima
  (`punpck` on SSE, `zip` on NEON). It returns the eight smallest and eight largest
  values in 17 instructions where min/max followed by a half-cleaner sort of each half took
  about 28 (SSE 1.04-1.2x). The tail of the union merges on a 16-element stack buffer
  (`unite16_finish`); the SSE version allocated a `std::vector` per call. Under clang the
  SSE union picks the next block with selects instead of a branch (+20% on LLVM 18 on a
  Zen 3 server, +2% on Apple clang on the i9, -4% under GCC, so GCC keeps the branch).
- **`set::test()` keeps its conditional move on x86-64 LLVM.** The branch-free lower
  bound compiled to a compare and branch under Apple clang and LLVM 23 on x86-64
  (point `test()` read 0.41-0.71x of the start there; arm64 and GCC were 1.5-3x).
  `__builtin_unpredictable` on the comparison restores `cmov`. `docset_codegen` compiles
  the function to assembly and looks for the move (it fails with the hint removed on
  x86-64 clang; GCC emits `cmov` either way).
- **SSE intersection and difference** handle a zero element before the loop (a zero can
  only be an array's first element, and `pcmpistrm` reads a zero lane as the end of the
  string) and always use `pcmpistrm`; the difference keeps its match mask in a vector
  register. Before, every step tested both heads for zero and a block starting an array
  took `pcmpestrm`. Apple clang on the i9: ANDNOT 1.18x and 1.24x over the previous head
  at densities 0.01 and 0.031, and AND 1.05x over the start.
- **NEON difference** compares each block of `a` with every `b` value up to the block's
  last lane in one loop (the few values below the block's first lane match nothing). It
  skipped them in a second loop first, so each block mispredicted two loop exits.
  Array ANDNOT on the M4: 1.14x (density 1e-3), 1.50x (1e-2), 1.57x (0.031).
- The CRoaring reference is pinned to 5.4.0 (`db0d1e9`), the version the README numbers
  use; the CMake default fetched 5.2.3.

Against the start (`test/docset_old.hh`, one binary per version, interleaved rounds, best
time; below 1 means slower; `bench/ab_compare.py`, 2^22 ordinals, each operation over a
pool of 16 random inputs below density 0.07 and 4 above). A/A is 0.97-1.03 except where
noted:

| Row | arm64 M4 Pro | x86-64 i9-9980HK, Apple clang | x86-64 i9, gcc 16 |
|---|---:|---:|---:|
| AND arrays, density 0.01 | 0.99 | 1.05 | 1.20 |
| AND arrays, density 0.031 | 0.99 | 1.06 | 1.23 |
| OR arrays, density 1e-3 | 2.22 | 1.20 | 1.16 |
| OR arrays, density 1e-2 | 2.78 | 1.20 | 1.11 |
| OR arrays, density 0.031 | 1.91 | 1.18 | 1.09 |
| ANDNOT arrays, density 1e-3 | 1.14 | 1.06 | 1.01 |
| ANDNOT arrays, density 1e-2 | 1.50 | 1.28 | 1.02 |
| ANDNOT arrays, density 0.031 | 1.57 | 1.36 | 1.02 |
| AND bitmaps, density 0.1 | 3.63 | 2.30 | 2.39 |
| AND bitmaps, density 0.2 | 3.33 | 1.85 | 2.16 |
| ANDNOT 0.1 x 0.9 | 3.27 | 2.26 | 2.37 |
| `test()`, density 0.01 | 2.13 | 1.61 | 2.85 |
| `test()`, density 0.5 | 1.65 | 1.15 | 2.77 |
| `test()`, 1 chunk, 8 members | 1.30 | 1.47 | 2.69 |
| `test()`, 1 chunk, 4096 members | 1.74 | 1.51 | 3.79 |
| `test()`, 65,536 chunks | 1.47 | 1.22 | 1.93 |

Rows between 0.96 and 1.05 are the dense bitmap OR, ANDNOT, and AND at densities
0.07-0.9 and iteration, whose code did not change. Two rows read lower and carry a noisy
A/A: OR and ANDNOT at density 0.07 on the M4 (0.87 and 0.91, A/A 0.85 and 0.89: the two
runs of the same binary differ by that much there) and `iterate` at density 0.5 under
GCC (0.92, A/A 0.96).

### Fixed

- The array ANDNOT read 0.58-0.86x of the start on arm64 in the previous benchmark and
  1.4x in another build of the same code. The benchmark timed one input repeatedly, so the
  branch predictor memorized the outcomes of the kernel's data-dependent branches, and the
  row measured code layout. `bench/ab_bench.cc` and `bench/bench.cc` now take the next of
  several random inputs for each timed operation.

### Added

- `docset_codegen`: compiles `lower_bound_index` to assembly and requires a conditional
  move (x86-64 and arm64).
- `test/test.cc`: every pair of sizes 0-40 for the intersection, union, and difference
  over runs that start at 0, end at 65535, interleave, nest, and are disjoint either way
  round; `lower_bound_index` against `std::lower_bound` for every probe value at array
  sizes around each halving boundary; point tests on a set with 65,536 chunks and at
  ordinal `0xffffffff`.
- `bench/ab_bench.cc` and `bench/ab_compare.py` (`docset_ab_new`, `docset_ab_old`): one
  docset version per binary, alternated, with an A/A column.

### Documentation

- The README comparison with CRoaring is measured on an Apple M4 Pro and an Intel
  i9-9980HK (macOS 15, Apple clang 17) against CRoaring 5.4.0 (`db0d1e9`), over several
  random inputs per operation. It replaces two tables, below: the i9 at `cd38597` against
  CRoaring 5.2.3, before the `test()` and bitmap AND/ANDNOT changes, and the M4 at
  `2bc55cc` against 5.4.0. Ratio of docset to CRoaring time, below 1 means docset is
  faster. On the Linux gate VM (Clang 18, shared with another VM) the worst rows were at
  density 0.01: OR 1.15 and ANDNOT 1.07 to 1.17.

- The i9 at `cd38597`, against CRoaring 5.2.3 (the M4 at `2bc55cc` follows):

  | Density | AND | OR | ANDNOT | Iterate | Count |
  |---:|---:|---:|---:|---:|---:|
  | 1e-6 | 0.72 | 0.61 | 0.64 | 0.55 | 0.71 |
  | 1e-5 | 0.20 | 0.63 | 0.66 | 0.30 | 0.26 |
  | 1e-4 | 0.29 | 0.77 | 0.77 | 0.22 | 0.15 |
  | 1e-3 | 0.62 | 0.96 | 0.92 | 0.18 | 0.15 |
  | 1e-2 | 0.98 | 1.08 | 1.14 | 0.18 | 0.15 |
  | 0.1 | 1.03 | 0.89 | 0.87 | 0.72 | 0.15 |
  | 0.5 | 0.77 | 0.85 | 0.93 | 0.62 | 0.15 |

- The M4 at `2bc55cc`, against CRoaring 5.4.0 (OR at density 1e-3 read 1.07):

  | Density | AND | OR | ANDNOT | Iterate | Count |
  |---:|---:|---:|---:|---:|---:|
  | 1e-6 | 0.61 | 0.65 | 0.71 | 0.57 | 0.81 |
  | 1e-5 | 0.25 | 0.73 | 0.76 | 0.41 | 0.24 |
  | 1e-4 | 0.27 | 0.72 | 0.66 | 0.59 | 0.12 |
  | 1e-3 | 0.29 | 1.07 | 0.23 | 0.19 | 0.12 |
  | 1e-2 | 0.29 | 0.98 | 0.38 | 0.09 | 0.12 |
  | 0.1 | 0.52 | 0.71 | 0.70 | 0.86 | 0.13 |
  | 0.5 | 0.66 | 0.72 | 0.71 | 0.82 | 0.12 |

- The M4 against CRoaring 5.2.3 (the CMake default fetched it then), the mean of two
  runs:

  | Density | AND | OR | ANDNOT | Iterate | Count |
  |---:|---:|---:|---:|---:|---:|
  | 1e-6 | 0.65 | 0.64 | 0.70 | 0.56 | 0.81 |
  | 1e-5 | 0.29 | 0.74 | 0.77 | 0.41 | 0.24 |
  | 1e-4 | 0.40 | 0.73 | 0.67 | 0.62 | 0.13 |
  | 1e-3 | 0.53 | 1.87 | 0.43 | 0.19 | 0.12 |
  | 1e-2 | 0.21 | 0.76 | 0.24 | 0.09 | 0.12 |
  | 0.1 | 0.21 | 0.73 | 0.72 | 0.85 | 0.13 |
  | 0.5 | 0.69 | 0.74 | 0.73 | 0.82 | 0.13 |

### Changed (`511c575`)

- A bitmap AND or ANDNOT result at or below 4096 members converts to an array with a
  routine chosen by cardinality and CPU. arm64 and x86-64 with BMI1 (`tzcnt`, `blsr`,
  selected at run time) take two bits per word without a branch up to 1024 members and
  four per word above; x86 without BMI1 keeps the per-bit loop. The 2026-10-30 form took
  two bits per word everywhere: on x86 that was slower than the loop (`bsf` and a
  conditional move for a zero word), and on arm64 it was 10 to 22% slower than the loop
  above 2.5 bits per word. Bitmap AND against the reference in `test/docset_old.hh`,
  interleaved, A/A within 3% (before this change in brackets):

  | density (members per chunk) | arm64 M4 Pro | x86-64 i9-9980HK, Apple clang | x86-64, gcc 16 |
  |---|---:|---:|---:|
  | 0.1 (about 650) | 3.52x (3.42x) | 2.27x (0.91x) | 2.47x (1.01x) |
  | 0.2 (about 2600) | 2.81x (0.90x) | 1.86x (0.97x) | 2.21x (0.97x) |
  | 0.25 (about 4100) | 1.27x (0.78x) | 1.17x (0.96x) | 1.22x (0.97x) |

  Results are unchanged.
- The conversion is out of line (`container::fill_array`), so the dense path of
  `from_bitmap`, a move, stays small in `intersect` and `subtract`. With the conversion
  inlined, ANDNOT of two array containers at density 0.03 was 7% slower than the
  reference on arm64 (0.935x, now 1.007x). ANDNOT and AND of dense bitmaps were within
  noise of the reference on x86-64 and arm64 before and after.

### Added (`511c575`)

- `test/diff.cc` checks the conversion on bitmap pairs whose AND and ANDNOT are known
  patterns (random, packed, one to four bits per word, alternating words, the chunk
  edge) at cardinalities from 1 to 4200, around each threshold.

## 2026-10-30

### Changed

- `set::test()` searches the chunk keys and array containers with a branchless lower
  bound. Before, it used `std::binary_search` and `std::lower_bound`. Point `test()`:
  1.6-2.1x faster at densities 0.01-0.5 (arm64, `build/docset_cmp_bench`). (`d1aa406`)
- A bitmap AND or ANDNOT result at or below 4096 members converts to an array by
  extracting two bits per word without a branch. Before, a per-word `while (w)` loop
  mispredicted on its trip count at mid density. Bitmap AND at density 0.1:
  830 -> 253 us, 3.3x (arm64). Results are unchanged. (`d1aa406`)

### Added

- `test/diff.cc` and `test/docset_old.hh`: a differential test of AND, OR, ANDNOT,
  `test`, `count`, and iteration against the reference implementation.
- `bench/cmp_bench.cc` (`docset_cmp_bench`): AND, ANDNOT, and `test()` timings against
  the reference, with an A/A column for the noise floor.

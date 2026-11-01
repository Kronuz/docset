# Changelog

Notable changes to docset, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

## 2026-10-31

### Documentation

- The README comparison with CRoaring is measured on an Apple M4 Pro against CRoaring
  5.4.0 (`db0d1e9`, the copy in `third_party`) at `2bc55cc`. It replaces a table
  measured on 2026-10-22 at `cd38597` on an Intel i9-9980HK (macOS, Apple clang `-O3`)
  against CRoaring 5.2.3, before the `test()` and bitmap AND/ANDNOT changes below. Ratio
  of docset to CRoaring time, below 1 means docset is faster:

  | Density | AND | OR | ANDNOT | Iterate | Count |
  |---:|---:|---:|---:|---:|---:|
  | 1e-6 | 0.72 | 0.61 | 0.64 | 0.55 | 0.71 |
  | 1e-5 | 0.20 | 0.63 | 0.66 | 0.30 | 0.26 |
  | 1e-4 | 0.29 | 0.77 | 0.77 | 0.22 | 0.15 |
  | 1e-3 | 0.62 | 0.96 | 0.92 | 0.18 | 0.15 |
  | 1e-2 | 0.98 | 1.08 | 1.14 | 0.18 | 0.15 |
  | 0.1 | 1.03 | 0.89 | 0.87 | 0.72 | 0.15 |
  | 0.5 | 0.77 | 0.85 | 0.93 | 0.62 | 0.15 |

  On the Linux gate VM (Clang 18, shared with another VM) the worst rows were at density
  0.01: OR 1.15 and ANDNOT 1.07 to 1.17.

  The CMake default for `DOCSET_BENCH_REFERENCE` still fetches CRoaring 5.2.3. On the M4
  against 5.2.3 (the mean of two runs) the ratios are:

  | Density | AND | OR | ANDNOT | Iterate | Count |
  |---:|---:|---:|---:|---:|---:|
  | 1e-6 | 0.65 | 0.64 | 0.70 | 0.56 | 0.81 |
  | 1e-5 | 0.29 | 0.74 | 0.77 | 0.41 | 0.24 |
  | 1e-4 | 0.40 | 0.73 | 0.67 | 0.62 | 0.13 |
  | 1e-3 | 0.53 | 1.87 | 0.43 | 0.19 | 0.12 |
  | 1e-2 | 0.21 | 0.76 | 0.24 | 0.09 | 0.12 |
  | 0.1 | 0.21 | 0.73 | 0.72 | 0.85 | 0.13 |
  | 0.5 | 0.69 | 0.74 | 0.73 | 0.82 | 0.13 |

### Changed

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

### Added

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

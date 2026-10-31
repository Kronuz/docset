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

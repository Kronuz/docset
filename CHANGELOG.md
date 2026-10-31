# Changelog

Notable changes to docset, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

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

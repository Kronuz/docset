# Changelog

Notable changes to docset, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

## 2026-11-01

### Fixed

- **Bitmap AND and ANDNOT results of 20 to 160 members on arm64 are as fast as before the
  extraction work.** The final gate read `and bitmaps` at 20, 40 and 80 members 0.955, 0.84
  and 0.92 of START (`cd38597`) on the M4, and `andnot` at 40 members 0.90. Two causes. The
  loop limit of 32 members came from `micro_extract`'s isolated crossover (39 to 43 members);
  end to end, with the 4 inputs of `ab_bench`, the loop wins up to about 90 members, so 33 to 80
  members took the branch-free form, which costs the same at every size. The limit is now 80.
  And every conversion allocated `card + 2` entries and shrank them to `card`, which cost 4% of
  a 20-member row (START's own loop with that resize: 41.6 us, without it 39.5); a result the
  loop converts is now allocated at exactly its members (`extract_room`), and on arm64 each
  extraction form is an aligned, non-inlined function. Apple M4 Pro, `bench/ab_bench.cc`
  (us per 64 chunks, medians of 3 runs, A/A 0.97 to 1.03), START / before / after:

  | bitmaps, members | and | andnot |
  |---:|---|---|
  | 20 | 40.5 / 41.0 / 40.0 | 42.4 / 41.1 / 40.0 |
  | 40 | 43.6 / 51.3 / 43.4 | 46.9 / 51.1 / 43.6 |
  | 80 | 48.8 / 52.1 / 48.9 | 53.2 / 51.6 / 48.3 |
  | 160 | 66.1 / 54.2 / 54.0 | 67.6 / 52.7 / 53.8 |
  | 400 | 130.7 / 57.1 / 57.9 | 135.3 / 56.8 / 57.5 |

  No other row of `ab_bench` is more than 3% slower than the code before (the worst are `andnot`
  at 160 members, 0.965, the same code in both, and `and d=0.25 x 0.25`, 0.972 with a spread of
  0.95 to 1.00). The assembly of the committed header is identical to the measured one (arm64,
  checked with one compiler for both).
  The new `ab_bench` rows of 240 and 320 members and the optional third argument (the number of
  inputs the bitmap rows cycle) are in the same table of that run: START 87.0 and 108.5 us,
  after 54.7 and 56.2.

  The same rows with 24 inputs instead of 4 (`ab_bench 22 "bitmaps, 40 " 24`) showed that a
  faster loop is not always a faster loop. Skipping groups of 8 empty words in the loop read
  1.9x faster than START at 20 members and 1.8x at 40 with 4 inputs, and 1.43x at 80; with 24
  inputs it read 1.46x at 20, 1.0x at 40 and 0.77x at 80 (START 41.4, 47.1 and 59.7 us; grouped
  28.3, 47.1 and 77.7): the M4's predictor memorizes the 256 chunks of the 4-input rows, and the
  grouped loop has fewer branches to memorize. It is not in the code. The per-word loop is also
  slower with 24 inputs (START 59.7 us at 80 members, where the branch-free form takes 52), so
  the limit of 80 is the largest at which the loop is no slower than START on the benchmark's
  rows, not the size at which it is fastest.

- **x86-64 is unchanged, and the i9 rows of the gate are a code-layout effect.** The gate read
  `and bitmaps` at 20 members 0.82 and at 40 members 0.93 of START on the i9, with the same
  loop. Moving the whole benchmark by 0 to 60 bytes of padding (16 builds of START) moves the
  `and` row between 60.4 and 73.0 us and the `andnot` row likewise at other offsets: a row is
  slow exactly when the outer loop of the extraction (a 22-byte body that runs 1024 times per
  chunk) starts at 48 modulo 64 and so straddles a 64-byte line (32 of 32 observations: `and`
  and `andnot` at 16 positions each). With this commit's benchmark the unchanged x86-64 code
  sits at 16 or 0 modulo 64 and reads START 1.02 (`and`) and 1.00 (`andnot`) at 20 members, 1.00
  and 0.99 at 40, 1.03 and 1.01 at 80 (`and`: 62.3 us START, 61.5 now at 20; 74.7 and 74.4 at
  40; 101.4 and 98.6 at 80). A function aligned to 32 bytes for the loop put it at 48 modulo 64
  and read 0.82 to 0.84 at 20 members, 0.86 to 0.88 at 40 and 0.95 at 80, and `iterate d=0.1`,
  which no code of this change touches, 0.92. So the x86-64 code keeps what was measured (its
  assembly is identical to the code before), and a build of the repository can still land in the
  slow position. The prototype that places the loop on purpose (functions aligned to 64 bytes)
  is on branch `perf/docset-x86-align64`; it is not timed.

### Changed

- **Small bitmap results take the plain loop.** A bitmap AND or ANDNOT whose result has
  at most `extract_loop_max` members (96 on x86-64, 32 on arm64) converts to an array
  with the per-bit loop instead of the branch-free form of `511c575`. The branch-free
  forms take the same time for any result (a store per slot of every word: about 1450 ns
  on the i9, 560 on the M4); the loop takes 5 to 9 ns per member over a floor of 270 to
  290 ns (clang), so it is faster below 130 members on the i9 (Apple clang), 40 on the M4
  and 195 on an AMD EPYC 7763 (gcc 11). Each limit is the lowest crossover over the
  compilers measured (`bench/micro_extract.cc`: Apple clang 17, gcc 16 and LLVM 23 on the
  i9 and the M4; gcc 11 on the EPYC; the lowest are 99 members for gcc 16 on the i9 and
  39 for Apple clang on the M4), rounded down to a multiple of 16. Results above the
  limit and every other path are unchanged, and the extracted positions are identical at
  every size (`test/diff.cc`). Extraction of one bitmap, M4 / i9: an empty result 563 ->
  274 ns / 1447 -> 289, 13 members 571 -> 391 / 1447 -> 394, 33 members 569 -> 516 /
  1468 -> 590.
  End to end (`bench/ab_bench.cc`, the previous header against this one, best of five
  runs): AND and ANDNOT of two bitmaps with 20 members per chunk 2.08x and 2.11x on the
  i9 and 1.23x and 1.24x on the M4; 40 members 1.76x and 1.75x on the i9; 80 members 1.30x
  and 1.31x on the i9; 160 and 400 members 1.00x on the i9 (the same code runs). No row on
  the i9 is more than 2% slower. The array ANDNOT rows at densities 1e-3, 1e-2 and 0.031,
  which never reach the extraction, are 1.11x, 1.35x and 1.53x faster in all three runs.
  On the M4 the rows `or` and `andnot` at density 0.07 and `test` of an 8-member chunk
  swing by 10 to 17% from one run to the next of the same two binaries. Outside
  `extract_bits` the two binaries hold the same instructions at shifted addresses.

### Added

- `bench/bench.cc` checks every operation's members against CRoaring's before it times
  anything, and prints the CRoaring version.
- `docset::detail::pick_extract_form`, the choice of extraction form by member count and
  loop limit, with `static_assert`s at both sides of each boundary in `test/diff.cc`, and
  extraction cases at the loop limit of each CPU path (31 to 33, 95 to 97) and of the
  build, one on each side. The `<` mutant of the comparison fails to compile, and a loop
  that emits the wrong position fails `docset_diff` at small results only.
- `bench/ab_bench.cc` rows `and` and `andnot bitmaps, N members per chunk` (N = 20, 40,
  80, 160, 400): bitmap operands whose result has few members, which the density rows
  (321 members at the least) do not reach.
- Boundary tests. `test/diff.cc` takes bitmap AND and ANDNOT results of exactly 0, 4094 to
  4098 and 65536 members, in the first, second and last chunk, and checks contents, count,
  `chunks()` (an empty result leaves no chunk) and `test()` against the reference; its
  extraction cases start at cardinality 0. `test/test.cc` freezes chunks of 1 to 65536
  members in the same chunks and checks the container kind in the directory (array at 4096,
  bitset at 4097 and 65535, full at 65536), then rank and membership at every ordinal in
  and around the chunk. Each fails with the matching edit of `docset.hh`: an empty result
  kept as a chunk, the array limit moved to 4095 or 4097, the full chunk moved to 65537, or
  `open` refusing a 4096-member array.
- `docset_diff_portable` runs `test/diff.cc` with `DOCSET_NO_SIMD`.
- `bench/micro_extract.cc` (`docset_micro_extract`) times the bitmap-to-array extraction
  against the variants it is chosen from, over 256 bitmaps per density, after checking each
  for identical output.

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
pool of 16 random inputs below density 0.07 and 4 above). The runs on the M4 and the i9
took the better of two old-binary runs per round against one new run, which understates
the new code by the run-to-run noise; the M1 column and later runs use one of each. A/A
is 0.97-1.03 except where noted:

| Row | arm64 M4 Pro | arm64 M1 Pro | x86-64 i9-9980HK, Apple clang | x86-64 i9, gcc 16 |
|---|---:|---:|---:|---:|
| AND arrays, density 0.01 | 0.99 | 1.00 | 1.05 | 1.20 |
| AND arrays, density 0.031 | 0.99 | 1.00 | 1.06 | 1.23 |
| OR arrays, density 1e-3 | 2.22 | 1.69 | 1.20 | 1.16 |
| OR arrays, density 1e-2 | 2.78 | 2.38 | 1.20 | 1.11 |
| OR arrays, density 0.031 | 1.91 | 1.65 | 1.18 | 1.09 |
| ANDNOT arrays, density 1e-3 | 1.14 | 1.31 | 1.06 | 1.01 |
| ANDNOT arrays, density 1e-2 | 1.50 | 1.55 | 1.28 | 1.02 |
| ANDNOT arrays, density 0.031 | 1.57 | 1.57 | 1.36 | 1.02 |
| AND bitmaps, density 0.1 | 3.63 | 2.98 | 2.30 | 2.39 |
| AND bitmaps, density 0.2 | 3.33 | 2.21 | 1.85 | 2.16 |
| ANDNOT 0.1 x 0.9 | 3.27 | 2.96 | 2.26 | 2.37 |
| `test()`, density 0.01 | 2.13 | 2.00 | 1.61 | 2.85 |
| `test()`, density 0.5 | 1.65 | 1.60 | 1.15 | 2.77 |
| `test()`, 1 chunk, 8 members | 1.30 | 1.67 | 1.47 | 2.69 |
| `test()`, 1 chunk, 4096 members | 1.74 | 1.56 | 1.51 | 3.79 |
| `test()`, 65,536 chunks | 1.47 | 1.39 | 1.22 | 1.93 |

Rows between 0.96 and 1.05 are the dense bitmap OR, ANDNOT, and AND at densities
0.07-0.9 and iteration, whose code did not change; on the M1 the dense OR and ANDNOT rows
read 0.92-0.94 with an A/A of 0.92-0.95, the same-binary run-to-run difference. Two rows
read lower on the M4 and carry a noisy A/A: OR and ANDNOT at density 0.07 (0.87 and 0.91,
A/A 0.85 and 0.89) and `iterate` at density 0.5 under GCC on the i9 (0.92, A/A 0.96).

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

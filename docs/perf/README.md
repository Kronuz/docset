# docset performance

## Against CRoaring

`bench/bench.cc` times `docset::set` against CRoaring 5.4.0 (`db0d1e9`) on the same members:
random sets per density over 2^24 ordinals, each operation producing a new set. Each timed
operation takes the next of several random input pairs (eight below density 0.07, two above), so
a branch predictor cannot memorize one input. Trials alternate between the two libraries, and the
ratio is the median of per-trial ratios: docset time over CRoaring time, so below 1 means docset
is faster. Apple clang 17 with `-O3 -DNDEBUG -std=gnu++20` (CRoaring with `-O3 -DNDEBUG`), on a
machine kept idle for the run. Each cell is the mean of the runs named under the machine; runs
agree within 0.06.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOCSET_BENCH_REFERENCE=ON
cmake --build build --target docset_bench
build/docset_bench
```

`DOCSET_BENCH_REFERENCE` fetches CRoaring at `db0d1e9` and builds it for the benchmark only;
the library never links it. `-DFETCHCONTENT_SOURCE_DIR_CROARING=<checkout>` uses a local
checkout instead.

Apple M4 Pro (arm64), mean of two runs:

| Density | AND | OR | ANDNOT | Iterate | Count |
|---:|---:|---:|---:|---:|---:|
| 1e-6 | 0.89 | 0.67 | 0.70 | 0.55 | 0.82 |
| 1e-5 | 0.23 | 0.73 | 0.73 | 0.41 | 0.29 |
| 1e-4 | 0.29 | 0.70 | 0.59 | 0.60 | 0.15 |
| 1e-3 | 0.30 | 0.50 | 0.28 | 0.20 | 0.14 |
| 1e-2 | 0.29 | 0.35 | 0.29 | 0.09 | 0.14 |
| 0.1 | 0.45 | 0.67 | 0.70 | 0.87 | 0.14 |
| 0.5 | 0.63 | 0.70 | 0.71 | 0.81 | 0.14 |

Intel Core i9-9980HK (x86-64, macOS 15), mean of three runs:

| Density | AND | OR | ANDNOT | Iterate | Count |
|---:|---:|---:|---:|---:|---:|
| 1e-6 | 0.98 | 0.60 | 0.65 | 0.56 | 0.71 |
| 1e-5 | 0.28 | 0.65 | 0.67 | 0.32 | 0.27 |
| 1e-4 | 0.37 | 0.68 | 0.72 | 0.22 | 0.15 |
| 1e-3 | 0.57 | 0.82 | 0.86 | 0.17 | 0.15 |
| 1e-2 | 0.96 | 1.03 | 1.25 | 0.18 | 0.15 |
| 0.1 | 0.50 | 0.85 | 0.85 | 0.72 | 0.15 |
| 0.5 | 0.75 | 0.84 | 0.87 | 0.61 | 0.15 |

docset is faster in every cell on the M4. On the i9 it is faster in every cell but OR and ANDNOT
at density 1e-2 (arrays of about 650 members per chunk), where CRoaring is 3% and 20% faster. On
x86-64 CPUs with AVX-512, CRoaring also has AVX-512 array kernels that docset does not.

## Bitmap-to-array extraction

A bitmap AND or ANDNOT whose result has at most 4096 members converts to an array (an AND of two
density-0.1 sets produces about 650 members per chunk). Up to 96 members on x86-64 and 80 on
arm64 the conversion is a plain loop over the set bits; above that it takes two or four bits per
word without a branch, chosen by the result size. On x86-64 the branch-free forms use BMI1, and a
CPU without BMI1 keeps the loop.

`build/docset_micro_extract [ROUNDS [DENSITIES]]` times that routine against the variants it is
chosen from: the plain loop (one iteration per set bit), two or four bits per word without a
branch, a byte table, and on x86-64 the BMI1 forms. Each timed pass cycles through 256 different
bitmaps, so a branch predictor cannot memorize the data, and every variant is checked for
identical output first. Median nanoseconds per 64Ki-bit bitmap over 15 rounds, mean of two runs,
Apple clang 17 `-O3 -DNDEBUG`; `landed` is `docset::detail::extract_bits`, the routine the set
operations call.

Apple M4 Pro (arm64), the runs agree within 5%:

| Members | landed | loop | two bits | four bits |
|---:|---:|---:|---:|---:|
| 13 | 391 | 407 | 560 | 1076 |
| 33 | 516 | 516 | 556 | 1070 |
| 131 | 568 | 980 | 564 | 1070 |
| 655 | 770 | 3516 | 768 | 1082 |
| 1311 | 1166 | 4552 | 1717 | 1171 |
| 2621 | 2229 | 6068 | 6686 | 2230 |
| 3932 | 5565 | 6972 | 9158 | 5558 |

Intel Core i9-9980HK (x86-64, macOS 15), the runs agree within 10%:

| Members | landed | loop | BMI1 loop | BMI1 two bits | BMI1 four bits |
|---:|---:|---:|---:|---:|---:|
| 13 | 394 | 392 | 414 | 1472 | 2502 |
| 33 | 590 | 572 | 569 | 1456 | 2532 |
| 131 | 1452 | 1488 | 1469 | 1456 | 2506 |
| 655 | 1644 | 4634 | 4642 | 1674 | 2586 |
| 1311 | 2614 | 5905 | 5932 | 2681 | 2652 |
| 2621 | 3691 | 7490 | 7228 | 7046 | 3728 |
| 3932 | 6720 | 8587 | 7928 | 8588 | 6764 |

`landed` is the fastest form or within 5% of it in every row. The branch-free forms cost the same
for any result (1450 ns on the i9, 560 on the M4 for two bits per word), while the loop costs 5
to 9 ns per member below 200 members, so the loop wins below about 130 members on the i9 and 40
on the M4 under Apple clang; `landed` takes it up to 96 and 80: the end-to-end rows of
`bench/ab_bench.cc` cycle 4 inputs, which the M4's branch predictor partly memorizes, and the loop wins
up to about 90 members there (about 55 with 24 inputs). Other compilers move the
crossover: gcc 16 on the i9 to 99 members (its loop is 2.5x slower on an empty bitmap), gcc 16
and LLVM 23 on the M4 to 43, LLVM 23 on the i9 to 135. On a shared AMD EPYC 7763 VM under gcc 11
the loop wins up to about 195 members (131 members: 1520 ns against 1960), so results of 97 to
195 members take a branch-free form that is slower there by up to 45%. The limits are the lowest
crossover rounded down to a multiple of 16. With gcc 16 on the i9 and gcc 11 on the EPYC the BMI1
branch-free forms lead from about 650 members up, as under Apple clang.

## Against the reference implementation

`build/docset_cmp_bench` times AND, ANDNOT and `test()` against `test/docset_old.hh`, with an A/A
column. `bench/ab_bench.cc` builds the same rows and more once per docset version
(`docset_ab_new`, `docset_ab_old`), and `bench/ab_compare.py` alternates the two binaries:

```sh
python3 bench/ab_compare.py build/docset_ab_old build/docset_ab_new
```

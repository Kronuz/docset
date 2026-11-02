# Bitmap AND/ANDNOT at 20 and 40 members: code placement, not a regression (issue 1)

Status: closed, not a regression. 2026-11-02, Intel i9-9980HK, Apple clang 17, `-O3 -DNDEBUG -std=gnu++20`,
`bench/ab_compare.py` (4 rounds, `quiet strict`), us/ns as printed; ratio is START over new, so below 1 means new is slower.
START is `cd38597` (`test/docset_old.hh`), HEAD is `dcbd528`, patch is HEAD plus `[[clang::code_align(32)]]` on the
outer `extract_loop` `for` (branch `explore/bitmap-and`).

| row (ns per 64 chunks) | START | HEAD | START / HEAD (range, A/A) | patch | START / patch (range, A/A) |
|---|---:|---:|---|---:|---|
| and 20 | 57041 | 68032 | 0.84 (0.81-0.86, 1.00) | 58514 | 0.99 (0.82-1.08, 1.00) |
| andnot 20 | 68031 | 68399 | 0.99 (0.97-1.01, 1.00) | 57655 | 1.19 (1.16-1.29, 1.00) |
| and 40 | 69110 | 75734 | 0.91 (0.90-0.94, 1.00) | 69782 | 0.98 (0.93-1.00, 0.99) |
| andnot 40 | 75571 | 76079 | 0.99 (0.98-1.02, 1.01) | 68432 | 1.10 (1.07-1.12, 1.01) |
| and 80 | 91694 | 92282 | 0.99 | 91434 | 1.00 |
| iterate d=0.1 | 635728 | 625697 | 1.02 | 671868 | 0.94 |

Reading:

1. The slowdown reproduces with this build of HEAD (`and` 20 and 40: 0.84, 0.91; A/A 1.00), and is absent from `andnot`.
2. Against START the same two pairs read the other way in the patch run: START's own `andnot` rows are the slow ones
   (68 us against 57 us), so the patch reads 1.19 and 1.10 faster there. Which row is slow depends on where the loop copy
   for that row landed, the cause the issue's padding experiment already measured.
3. The patch does not remove the lottery: loop positions in the three binaries span 0 to 51 modulo 64
   (`otool -tvV`, `incq` before `cmpq $0x400`), `iterate d=0.1` (no docset code) moves 6% (0.94), and `and` 20 swings 0.82
   to 1.08 between rounds.

No source change lands. A fix that places the loop on purpose needs the offset read per compiler and still moves other
loops; the arm64 aligned functions stay.

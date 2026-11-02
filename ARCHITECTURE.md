# docset architecture

## Chunks

`set` splits the 32-bit id space by its high 16 bits. Each non-empty chunk is one
container, stored in two parallel vectors (keys and containers) sorted by key, with
the set's total cardinality cached.

A container is a sorted array of 16-bit low halves while it has at most 4096
members, and an 8 KiB bitmap above that: 4096 two-byte values take the same 8 KiB as
the bitmap, so the switch point is where the array stops being smaller. Results
convert in both directions as operations cross the threshold.

## Operations

Set operations walk both key lists and combine matching containers:

| Pair | AND | OR | ANDNOT |
|---|---|---|---|
| bitmap, bitmap | fused word loop with popcount | same | same |
| array, bitmap | filter the array by bit tests | set bits into a copy | filter or clear |
| array, array | `pcmpistrm` block walk (NEON: compare rotations) | vector merge | `pcmpistrm` block walk (NEON: compare per `b` value) |

- **Kernels** are compiled for their instruction set plus POPCNT through target
  attributes and selected at runtime; without POPCNT, popcounts become bit tricks that
  cost a third of the union's time.
- **Fused bitmap loops** compute the result word and its popcount in one pass,
  compiled for AVX2 and POPCNT behind a runtime check.
- **Array intersection** compares 8-lane blocks of both arrays with `pcmpistrm`
  ("which lanes of a equal any lane of b") and compacts matches with a 256-entry
  shuffle table, advancing whichever block ends first. `pcmpistrm` treats a zero lane
  as a terminator; a zero can only be an array's first element, so the kernels emit or
  skip it before the loop. Skewed sizes (more than 32:1) gallop instead. Without
  SSE4.2 it falls back to an SSSE3 rotation compare or NEON.
- **Array union** is the Inoue–Taura vectorized merge. Two sorted 8-vectors go
  through a merge network that returns the eight smallest and the eight largest values
  (reverse one input, then four rounds of min/max and interleaving the minima with the
  maxima: `punpck` on SSE, `zip` on NEON). The smallest eight are emitted with
  duplicates removed by comparing them to themselves shifted by one lane, and the next
  block comes from the input whose head is smaller (a branch on SSE, a select on NEON).
  The last values merge on the stack (`unite16_finish`).
- **Array difference** runs the intersection walk while accumulating which lanes of
  the current block of `a` matched, emitting the unmatched lanes when the walk moves
  past the block. The NEON version compares each block of `a` with every `b` value up
  to the block's last lane, one loop exit per block.

## Point tests and conversion

`set::test(d)` finds the chunk key, then the low half in an array container, each with
a branchless lower bound: the comparison feeds a conditional select instead of a jump,
which suits the small, unpredictable searches here. LLVM turns a select on a loaded
value into a branch on x86-64 unless the condition is marked unpredictable
(`__builtin_unpredictable`), so the bound carries that hint, and `docset_codegen`
compiles it to assembly and looks for the conditional move.

A bitmap result at or below 4096 members converts to an array, by one of two forms picked
by the result's member count (`pick_extract_form`).

- **The per-bit loop** (`ctz`, clear the lowest bit) for up to `extract_loop_max` members: 96 on
  x86-64, 80 on arm64. Its exit branch mispredicts about once per word at 0.5 to 3 bits per
  word, so past the limit it loses to the branch-free form. It writes exactly the members.
- **Two or four bits per word without a branch** (the output index advances only where a bit
  was found) up to 1024 members, four per word above that, and a loop over any rest. These
  write two entries past the last member, so the array is allocated with room for them
  (`extract_room`). They need a count-trailing-zeros that is cheap for a zero word: arm64 has
  one, and x86-64 gets it from BMI1 (`tzcnt`, `blsr`) in a function compiled with a target
  attribute and selected at run time. An x86 CPU without BMI1 keeps the loop, because `bsf`
  plus a conditional move makes the branch-free form slower than the loop.

The limits are the sizes where the two forms meet end to end (`bench/ab_bench.cc`, AND and
ANDNOT of two bitmaps with 20 to 400 members in the result). The isolated routine
(`bench/micro_extract.cc`) finds a smaller crossover on arm64, and so does the end-to-end row
with more distinct inputs than the benchmark's four (`ab_bench.cc` takes the count as its
third argument): a predictor memorizes four inputs and makes the loop look cheaper. On arm64
each form is its own function, aligned to 32 bytes, and a loop result is allocated at exactly
its members. x86-64 keeps the forms inline and the array padded: where the hot loop starts
relative to a 64-byte boundary moved a bitmap AND by 20% on an Intel i9 (a result of 20
members, 61 or 73 us) with the loop unchanged, so any edit of the code ahead of it is a
change of that row. The conversion is out of line (`container::fill_array`), so the dense path
of `from_bitmap`, a move, stays small enough to inline into `intersect` and `subtract`.

## Iterators

`advance(target)` skips whole chunks by key, then scans a bitmap word by word or
gallops in an array from the last position. `doc()` is `no_more_docs` once
exhausted.

## Memory

Result buffers use an allocator that leaves elements uninitialized, so a result
about to be overwritten is never zeroed first. Intersections, whose results are
small, compute into a stack buffer and copy out at the exact size; differences,
whose results are close to the input size, write into their final buffer.

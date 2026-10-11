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
| array, array | `pcmpestrm` block walk | bitonic merge | `pcmpestrm` block walk |

- **Kernels** are compiled for their instruction set plus POPCNT through target
  attributes and selected at runtime; without POPCNT, popcounts become bit tricks that
  cost a third of the union's time.
- **Fused bitmap loops** compute the result word and its popcount in one pass,
  compiled for AVX2 and POPCNT behind a runtime check.
- **Array intersection** compares 8-lane blocks of both arrays with `pcmpistrm`
  ("which lanes of a equal any lane of b") and compacts matches with a 256-entry
  shuffle table, advancing whichever block ends first. `pcmpistrm` treats a zero lane
  as a terminator; a zero can only be an array's first element, so blocks that start
  an array use the explicit-length `pcmpestrm`. Skewed sizes (more than 32:1)
  gallop instead. Without SSE4.2 it falls back to an SSSE3 rotation compare or NEON.
- **Array union** is the Inoue–Taura vectorized merge: two sorted 8-vectors go
  through a bitonic merge network (SSE4.1 min/max and blends), the lower half is
  emitted with duplicates removed by comparing it to itself shifted by one lane, and
  the next block comes from the list whose head is smaller.
- **Array difference** runs the intersection walk while accumulating which lanes of
  the current block of `a` matched, emitting the unmatched lanes when the walk moves
  past the block.

## Iterators

`advance(target)` skips whole chunks by key, then scans a bitmap word by word or
gallops in an array from the last position. `doc()` is `no_more_docs` once
exhausted.

## Memory

Result buffers use an allocator that leaves elements uninitialized, so a result
about to be overwritten is never zeroed first. Intersections, whose results are
small, compute into a stack buffer and copy out at the exact size; differences,
whose results are close to the input size, write into their final buffer.

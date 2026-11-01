# AGENTS.md — docset

## Repo map

```
docset.hh        bitset, sorted_set, chunked set, doc_iterator, SIMD kernels (SSE4.2/SSE4.1/SSSE3/AVX2/NEON).
test/test.cc     Operations vs std::set_*, iterators, 16-bit kernels, frozen sets at the container boundaries. Built twice: SIMD and DOCSET_NO_SIMD.
test/diff.cc     Every operation against the reference test/docset_old.hh, with results of exactly 0 and around 4096 and 65536 members. Built twice, like test.cc.
bench/bench.cc   Interleaved comparison against CRoaring (DOCSET_BENCH_REFERENCE).
bench/cmp_bench.cc  AND, ANDNOT, and test() timings against test/docset_old.hh, with an A/A column.
bench/ab_bench.cc   The same rows with one docset version per binary (docset_ab_new, docset_ab_old).
bench/ab_compare.py Alternates two ab_bench binaries and prints old/new per row with an A/A column.
bench/micro_extract.cc  Bitmap-to-array extraction: docset's routine against the loop, branch-free, byte-table and BMI1 variants it is chosen from.
test/codegen_probe.cc, test/check_codegen.cmake  docset_codegen: lower_bound_index must compile to a conditional move.
docs/perf/README.md  The CRoaring comparison and the extraction variants on the M4 Pro and the i9.
```

## Invariants

- Containers hold at most 4096 array members; bitmaps hold more. Empty containers are never stored.
- `count()` is maintained by every operation; never recompute it lazily.
- SIMD kernels need 8 elements of slack in their output buffers.
- The branch-free bitmap-to-array extraction (`extract_bits`) stores blindly past the last
  position, so `container::fill_array` sizes the array to card + 2 before it runs. Results
  of up to `extract_loop_max` members (96 on x86-64, 32 on arm64) take the plain loop,
  which costs less than the branch-free form's fixed cost per word. x86 uses BMI1 behind a
  run-time check; without it the loop is faster than the branch-free form.
- Never pass an empty vector's `data()` to `memcpy`; use `detail::copy_bytes`.
- `lower_bound_index` marks its select unpredictable (`DOCSET_UNPREDICTABLE`): LLVM compiles a select on a loaded
  value to a branch on x86-64 otherwise. `docset_codegen` checks the assembly.
- The vector union finishes through `unite16_finish`, whose scratch is 16 stack elements: the caller passes the
  unread rest with fewer than 8 values as `few`.
- Benchmarks cycle each timed operation through several inputs. One repeated input lets the branch predictor
  memorize the outcomes of the data-dependent branches, and the row then measures predictor aliasing, which moves
  with code layout, not the kernel.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake -S . -B asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" && cmake --build asan && ctest --test-dir asan
```

Run the NEON path with an AArch64 cross compiler and `qemu-aarch64 -cpu max`. Compare
performance with `-DDOCSET_BENCH_REFERENCE=ON` on a quiet machine.

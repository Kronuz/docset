# AGENTS.md — docset

## Repo map

```
docset.hh        bitset, sorted_set, chunked set, doc_iterator, SIMD kernels (SSE4.2/SSE4.1/SSSE3/AVX2/NEON).
test/test.cc     Operations vs std::set_*, iterators, 16-bit kernels. Built twice: SIMD and DOCSET_NO_SIMD.
bench/bench.cc   Interleaved comparison against CRoaring (DOCSET_BENCH_REFERENCE).
docs/docset.d2     README diagram; render: d2 --layout elk --sketch --theme 302 docs/docset.d2 docs/docset.svg
```

## Invariants

- Containers hold at most 4096 array members; bitmaps hold more. Empty containers are never stored.
- `count()` is maintained by every operation; never recompute it lazily.
- SIMD kernels need 8 elements of slack in their output buffers.
- Never pass an empty vector's `data()` to `memcpy`; use `detail::copy_bytes`.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake -S . -B asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" && cmake --build asan && ctest --test-dir asan
```

Run the NEON path with an AArch64 cross compiler and `qemu-aarch64 -cpu max`. Compare
performance with `-DDOCSET_BENCH_REFERENCE=ON` on a quiet machine.

# docset

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/docset/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/docset/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only sets of document ids for C++20: bitset, sorted array and Roaring-style chunks with SIMD AND, OR and ANDNOT.
The chunked set splits the 32-bit id space into chunks of 65,536 ids and keeps each
one in its cheaper form: a sorted array of 16-bit low halves up to 4,096 members,
an 8 KiB bitmap above that. All three sets share `doc_iterator`, the iteration
contract of a search engine's posting lists, scorers and filters. Reach for it for
filter caches, live-docs masks and set algebra over document ids.

## ✨ Highlights

- **`docset::set`.** The Roaring layout without run containers: AND, OR and ANDNOT
  return new sets, and `count()` is kept by every operation, so it is O(1).
- **Vector kernels picked at run time.** Array intersection and difference use
  SSE4.2 (SSSE3 without it) or NEON, union a vectorized merge (SSE4.1 or NEON), and
  bitmap operations fuse with popcount, on AVX2 where the CPU has it.
- **Extraction by CPU and result size.** A bitmap result with at most 4,096 members
  becomes an array: a plain loop up to 96 members on x86-64 and 32 on arm64, and
  above that two or four bits per word without a branch (on x86-64 with BMI1, and
  the loop when the CPU lacks it).
- **`docset::sorted_set`.** Strictly increasing 32-bit ids, with SIMD intersection
  and galloping search for lopsided sizes.
- **`docset::bitset`.** One bit per id, for live-docs masks and dense filters, with
  in-place `&=`, `|=` and `-=`.
- **`doc_iterator`.** `doc()`, `next()`, `advance(target)` and `cost()`, ending at
  `docset::no_more_docs`; all three sets provide one.
- **One header, no dependencies.**

## 🚀 Quick start

```cpp
#include "docset.hh"

#include <cstdio>

int main() {
	const docset::doc_t universe = 1u << 24;            // ordinals 0 .. 2^24 - 1
	auto a = docset::set::from_sorted(universe, {3, 70000, 70001, 5000000});
	auto b = docset::set::from_sorted(universe, {70001, 5000000});

	docset::set both = a & b;                            // {70001, 5000000}
	docset::set either = a | b;                          // {3, 70000, 70001, 5000000}
	docset::set only_a = a - b;                          // {3, 70000}
	std::printf("%llu %llu %llu\n", (unsigned long long)both.count(),
		(unsigned long long)either.count(), (unsigned long long)only_a.count());   // 2 4 2

	auto it = a.begin_iteration();                       // a doc_iterator
	for (auto d = it.advance(60000); d != docset::no_more_docs; d = it.next()) {
		std::printf("%u ", d);                           // 70000 70001 5000000
	}
	std::printf("\n%d\n", a.test(70000));                // 1
}
```

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer; no dependencies. CI builds it
on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan,
and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(docset
  GIT_REPOSITORY https://github.com/Kronuz/docset.git
  GIT_TAG        0e39cc3335c9ea0e62e1ce29c34af458b72e297c)  # or main for the tip
FetchContent_MakeAvailable(docset)

target_link_libraries(my_app PRIVATE docset::docset)
```

```cpp
#include "docset.hh"
```

Without CMake, copy `docset.hh` onto your include path.

## 🎯 When to use it

Use it for posting lists, filters and deleted-document sets over 32-bit ids, where
set algebra and iteration with `advance()` are the hot operations.

Not for:

- **Long runs of consecutive ids.** There are no run-length containers, so a dense
  range costs a bitmap per chunk.
- **AVX-512.** CRoaring has AVX-512 array kernels; docset stops at SSE4.2 and AVX2
  on x86-64.
- **Other targets at full speed.** Without SSE4.1 or NEON the union is the scalar
  merge, and `DOCSET_NO_SIMD` turns every kernel scalar.

## ⚡ Performance

`bench/bench.cc` times `docset::set` against [CRoaring](https://github.com/RoaringBitmap/CRoaring)
[5.4.0](https://github.com/RoaringBitmap/CRoaring/releases/tag/v5.4.0) (`db0d1e9`, fetched by the
benchmark only) on random sets over 2^24 ids. The rows are the density of each set (members per
id); the cells are the cost of building a new set with AND (intersection), OR (union) and ANDNOT
(`a` without `b`), as docset time over CRoaring time, the median of per-trial ratios with the two
libraries alternating, so a cell below 1 means docset is faster. Before timing, the benchmark
checks that every result has CRoaring's members, in the same order. Apple clang 17, `-O3
-DNDEBUG`, mean of two runs on the M4 Pro (macOS 26, arm64) and three on the i9-9980HK (macOS 15,
x86-64):

| Density | Apple M4 Pro: AND / OR / ANDNOT | Intel Core i9-9980HK: AND / OR / ANDNOT |
|---:|---:|---:|
| 1e-6 | 0.89 / 0.67 / 0.70 | 0.98 / 0.60 / 0.65 |
| 1e-4 | 0.29 / 0.70 / 0.59 | 0.37 / 0.68 / 0.72 |
| 1e-2 | 0.29 / 0.35 / 0.29 | 0.96 / 1.03 / 1.25 |
| 0.1 | 0.45 / 0.67 / 0.70 | 0.50 / 0.85 / 0.85 |
| 0.5 | 0.63 / 0.70 / 0.71 | 0.75 / 0.84 / 0.87 |

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOCSET_BENCH_REFERENCE=ON
cmake --build build --target docset_bench
build/docset_bench
```

docset is faster in every cell on the M4 Pro. On the i9 CRoaring is 3% faster on
OR and 20% on ANDNOT at density 1e-2 (arrays of about 650 members per chunk).
[docs/perf/README.md](docs/perf/README.md) has every density, iteration and
`count()`, and the bitmap-to-array extraction variants on both machines.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): chunks, the kernel for each container pair,
  point tests, iterators and memory.
- [docs/perf/README.md](docs/perf/README.md): the benchmark tables and how to run
  them.
- [CHANGELOG.md](CHANGELOG.md): what changed, with numbers.
- [AGENTS.md](AGENTS.md): repository map, invariants, and traps.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#docset): the catalog entry.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

The tests compare every operation with `std::set_*` and with the reference
implementation in `test/docset_old.hh`, across densities, chunk boundaries and
mixed array and bitmap chunks, and again with `DOCSET_NO_SIMD`. `docset_codegen`
checks that the point-test lower bound compiles to a conditional move.
`-DDOCSET_BENCH_REFERENCE=ON` builds `docset_bench` against CRoaring, and
`-DDOCSET_FUZZ=ON` the libFuzzer target (clang).

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the
catalog's [rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo. See [LICENSE](LICENSE).

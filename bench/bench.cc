// AND, OR, ANDNOT (new result), iteration, and cardinality across densities,
// docset::set versus CRoaring on the same members (DOCSET_BENCH_REFERENCE).
#include "docset.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <random>

#if DOCSET_BENCH_REFERENCE
#include "roaring/roaring.h"
#endif

using docset::doc_t;

// Nanoseconds per call over a 20 ms window.
template <typename F>
static double ns_per_op(F&& f) {
	std::size_t ops = 0;
	auto start = std::chrono::steady_clock::now();
	double seconds = 0;
	do {
		f();
		++ops;
		seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	} while (seconds < 0.02);
	return seconds * 1e9 / double(ops);
}

// Interleaves trials of two candidates (A, B, A, B, ...) so host noise hits both;
// returns the best time of each and the median of the per-trial ratios.
template <typename A, typename B>
static std::array<double, 3> compare(A&& a, B&& b, int trials = 11) {
	std::vector<double> ratios;
	double best_a = 1e300, best_b = 1e300;
	for (int t = 0; t < trials; ++t) {
		double x = ns_per_op(a), y = ns_per_op(b);
		best_a = std::min(best_a, x);
		best_b = std::min(best_b, y);
		ratios.push_back(x / y);
	}
	std::nth_element(ratios.begin(), ratios.begin() + trials / 2, ratios.end());
	return {best_a, best_b, ratios[trials / 2]};
}

int main() {
	constexpr doc_t universe = 1u << 24;
	std::mt19937_64 rng(11);
	volatile std::uint64_t sink = 0;
	std::printf("%9s %8s %12s %12s %7s\n", "density", "op", "docset ns", "roaring ns", "ratio");
	for (double density : {1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 0.1, 0.5}) {
		auto make = [&] {
			std::vector<doc_t> v;
			std::geometric_distribution<doc_t> gap(density);
			for (std::uint64_t d = gap(rng); d < universe; d += 1 + gap(rng)) { v.push_back(doc_t(d)); }
			return v;
		};
		auto va = make(), vb = make();
		auto a = docset::set::from_sorted(universe, va), b = docset::set::from_sorted(universe, vb);
		auto ours_and = [&] { sink = sink + (a & b).count(); };
		auto ours_or = [&] { sink = sink + (a | b).count(); };
		auto ours_andnot = [&] { sink = sink + (a - b).count(); };
		auto ours_iterate = [&] { std::uint64_t s = 0; a.for_each([&](doc_t d) { s += d; }); sink = sink + s; };
		auto ours_count = [&] { sink = sink + a.count(); };
#if DOCSET_BENCH_REFERENCE
		roaring_bitmap_t* ra = roaring_bitmap_of_ptr(va.size(), va.data());
		roaring_bitmap_t* rb = roaring_bitmap_of_ptr(vb.size(), vb.data());
		auto row = [&](const char* name, auto&& ours, auto&& theirs) {
			auto [x, y, ratio] = compare(ours, theirs);
			std::printf("%9g %8s %12.0f %12.0f %7.2f\n", density, name, x, y, ratio);
		};
		row("and", ours_and, [&] { auto* r = roaring_bitmap_and(ra, rb); sink = sink + roaring_bitmap_get_cardinality(r); roaring_bitmap_free(r); });
		row("or", ours_or, [&] { auto* r = roaring_bitmap_or(ra, rb); sink = sink + roaring_bitmap_get_cardinality(r); roaring_bitmap_free(r); });
		row("andnot", ours_andnot, [&] { auto* r = roaring_bitmap_andnot(ra, rb); sink = sink + roaring_bitmap_get_cardinality(r); roaring_bitmap_free(r); });
		row("iterate", ours_iterate, [&] {
			std::uint64_t s = 0;
			roaring_iterate(ra, [](std::uint32_t v, void* p) { *static_cast<std::uint64_t*>(p) += v; return true; }, &s);
			sink = sink + s;
		});
		row("count", ours_count, [&] { sink = sink + roaring_bitmap_get_cardinality(ra); });
		roaring_bitmap_free(ra);
		roaring_bitmap_free(rb);
#else
		std::printf("%9g %8s %12.0f\n", density, "and", ns_per_op(ours_and));
		std::printf("%9g %8s %12.0f\n", density, "or", ns_per_op(ours_or));
		std::printf("%9g %8s %12.0f\n", density, "andnot", ns_per_op(ours_andnot));
		std::printf("%9g %8s %12.0f\n", density, "iterate", ns_per_op(ours_iterate));
		std::printf("%9g %8s %12.0f\n", density, "count", ns_per_op(ours_count));
#endif
	}
}

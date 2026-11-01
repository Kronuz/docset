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
#if DOCSET_BENCH_REFERENCE
	std::printf("CRoaring %s; every operation's members are checked against it before timing\n", ROARING_VERSION);
#endif
	std::printf("%9s %8s %12s %12s %7s\n", "density", "op", "docset ns", "roaring ns", "ratio");
	for (double density : {1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 0.1, 0.5}) {
		// Each timed operation takes the next of several random input pairs: repeating one input
		// lets a branch predictor memorize the outcomes of the data-dependent branches in the
		// sparse kernels, which the same sets would not allow in a real index.
		const std::size_t copies = density < 0.07 ? 8 : 2;
		auto make = [&] {
			std::vector<doc_t> v;
			std::geometric_distribution<doc_t> gap(density);
			for (std::uint64_t d = gap(rng); d < universe; d += 1 + gap(rng)) { v.push_back(doc_t(d)); }
			return v;
		};
		std::vector<std::vector<doc_t>> va(copies), vb(copies);
		std::vector<docset::set> a(copies), b(copies);
		for (std::size_t c = 0; c < copies; ++c) {
			va[c] = make();
			vb[c] = make();
			a[c] = docset::set::from_sorted(universe, va[c]);
			b[c] = docset::set::from_sorted(universe, vb[c]);
		}
		std::size_t turn = 0;
		auto ours_and = [&] { const std::size_t t = turn++ % copies; sink = sink + (a[t] & b[t]).count(); };
		auto ours_or = [&] { const std::size_t t = turn++ % copies; sink = sink + (a[t] | b[t]).count(); };
		auto ours_andnot = [&] { const std::size_t t = turn++ % copies; sink = sink + (a[t] - b[t]).count(); };
		auto ours_iterate = [&] { std::uint64_t s = 0; a[0].for_each([&](doc_t d) { s += d; }); sink = sink + s; };
		auto ours_count = [&] { sink = sink + a[0].count(); };
#if DOCSET_BENCH_REFERENCE
		std::vector<roaring_bitmap_t*> ra(copies), rb(copies);
		for (std::size_t c = 0; c < copies; ++c) {
			ra[c] = roaring_bitmap_of_ptr(va[c].size(), va[c].data());
			rb[c] = roaring_bitmap_of_ptr(vb[c].size(), vb[c].data());
		}
		// Before any timing: every operation gives CRoaring's members, in the same order.
		auto same = [&](const docset::set& d, roaring_bitmap_t* r) {
			std::vector<doc_t> x, y(roaring_bitmap_get_cardinality(r));
			x.reserve(y.size());
			d.for_each([&](doc_t v) { x.push_back(v); });
			roaring_bitmap_to_uint32_array(r, y.data());
			return d.count() == y.size() && x == y;
		};
		for (std::size_t c = 0; c < copies; ++c) {
			roaring_bitmap_t* rand_ = roaring_bitmap_and(ra[c], rb[c]);
			roaring_bitmap_t* ror = roaring_bitmap_or(ra[c], rb[c]);
			roaring_bitmap_t* rnot = roaring_bitmap_andnot(ra[c], rb[c]);
			const bool ok = same(a[c], ra[c]) && same(a[c] & b[c], rand_) && same(a[c] | b[c], ror) && same(a[c] - b[c], rnot);
			roaring_bitmap_free(rand_);
			roaring_bitmap_free(ror);
			roaring_bitmap_free(rnot);
			if (!ok) {
				std::printf("MISMATCH at density %g, copy %zu\n", density, c);
				return 1;
			}
		}
		std::size_t their_turn = 0;
		auto row = [&](const char* name, auto&& ours, auto&& theirs) {
			auto [x, y, ratio] = compare(ours, theirs);
			std::printf("%9g %8s %12.0f %12.0f %7.2f\n", density, name, x, y, ratio);
		};
		row("and", ours_and, [&] { const std::size_t t = their_turn++ % copies; auto* r = roaring_bitmap_and(ra[t], rb[t]); sink = sink + roaring_bitmap_get_cardinality(r); roaring_bitmap_free(r); });
		row("or", ours_or, [&] { const std::size_t t = their_turn++ % copies; auto* r = roaring_bitmap_or(ra[t], rb[t]); sink = sink + roaring_bitmap_get_cardinality(r); roaring_bitmap_free(r); });
		row("andnot", ours_andnot, [&] { const std::size_t t = their_turn++ % copies; auto* r = roaring_bitmap_andnot(ra[t], rb[t]); sink = sink + roaring_bitmap_get_cardinality(r); roaring_bitmap_free(r); });
		row("iterate", ours_iterate, [&] {
			std::uint64_t s = 0;
			roaring_iterate(ra[0], [](std::uint32_t v, void* p) { *static_cast<std::uint64_t*>(p) += v; return true; }, &s);
			sink = sink + s;
		});
		row("count", ours_count, [&] { sink = sink + roaring_bitmap_get_cardinality(ra[0]); });
		for (std::size_t c = 0; c < copies; ++c) {
			roaring_bitmap_free(ra[c]);
			roaring_bitmap_free(rb[c]);
		}
#else
		std::printf("%9g %8s %12.0f\n", density, "and", ns_per_op(ours_and));
		std::printf("%9g %8s %12.0f\n", density, "or", ns_per_op(ours_or));
		std::printf("%9g %8s %12.0f\n", density, "andnot", ns_per_op(ours_andnot));
		std::printf("%9g %8s %12.0f\n", density, "iterate", ns_per_op(ours_iterate));
		std::printf("%9g %8s %12.0f\n", density, "count", ns_per_op(ours_count));
#endif
	}
}

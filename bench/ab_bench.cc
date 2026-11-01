// One docset version per binary: docset_ab_new is docset.hh, docset_ab_old is the frozen
// reference test/docset_old.hh. Code layout and inlining decisions differ between a shared
// binary and a separate one, so rows that move only with layout show up here and not in
// docset_cmp_bench. Prints "row ns" lines (best of 15 windows of 20 ms); tools compare the
// two binaries by alternating runs. Usage: docset_ab_new [universe_bits (default 22)]
#ifdef DOCSET_AB_OLD
#include "docset_old.hh"
namespace ds = docset_old;
#else
#include "docset.hh"
namespace ds = docset;
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static volatile std::uint64_t sink;

template <typename F>
static double best_ns(F&& f) {
	double best = 1e300;
	for (int t = 0; t < 15; ++t) {
		std::size_t ops = 0;
		const auto start = std::chrono::steady_clock::now();
		double seconds = 0;
		do {
			f();
			++ops;
			seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		} while (seconds < 0.02);
		best = std::min(best, seconds * 1e9 / double(ops));
	}
	return best;
}

static std::vector<std::uint32_t> bernoulli(std::mt19937_64& rng, std::uint32_t universe, double density) {
	std::vector<std::uint32_t> v;
	std::geometric_distribution<std::uint32_t> gap(density);  // gaps of a Bernoulli process
	for (std::uint64_t d = gap(rng); d < universe; d += 1 + gap(rng)) { v.push_back(std::uint32_t(d)); }
	return v;
}

static void report(const char* name, double ns) { std::printf("%-44s %12.1f\n", name, ns); }

int main(int argc, char** argv) {
	const unsigned bits = argc > 1 ? unsigned(std::atoi(argv[1])) : 22;
	const std::uint32_t universe = 1u << bits;
	std::mt19937_64 rng(11);
	char name[96];

	// Set operations over density pairs (members per 64Ki chunk: 0.001 -> 65, 0.031 -> 2030). Each
	// timed operation takes the next of several random pairs: repeating one input lets a branch
	// predictor memorize its outcomes, which hides the cost of the data-dependent branches in the
	// array kernels (and favors whichever build happens to alias less).
	struct input { ds::set a, b; };
	const double pairs[][2] = {{1e-3, 1e-3}, {1e-2, 1e-2}, {0.031, 0.031}, {0.07, 0.07}, {0.1, 0.1},
	                           {0.2, 0.2}, {0.25, 0.25}, {0.1, 0.9}, {0.5, 0.5}, {0.9, 0.9}};
	for (const auto& p : pairs) {
		std::vector<input> inputs;
		const std::size_t copies = p[0] < 0.07 && p[1] < 0.07 ? 16 : 4;
		for (std::size_t c = 0; c < copies; ++c) {
			inputs.push_back({ds::set::from_sorted(universe, bernoulli(rng, universe, p[0])), ds::set::from_sorted(universe, bernoulli(rng, universe, p[1]))});
		}
		std::size_t turn = 0;
		auto next = [&]() -> const input& { return inputs[turn++ % inputs.size()]; };
		std::snprintf(name, sizeof name, "and    d=%g x %g", p[0], p[1]);
		report(name, best_ns([&] { const input& in = next(); sink = sink + (in.a & in.b).count(); }));
		std::snprintf(name, sizeof name, "or     d=%g x %g", p[0], p[1]);
		report(name, best_ns([&] { const input& in = next(); sink = sink + (in.a | in.b).count(); }));
		std::snprintf(name, sizeof name, "andnot d=%g x %g", p[0], p[1]);
		report(name, best_ns([&] { const input& in = next(); sink = sink + (in.a - in.b).count(); }));
	}

	// Iteration over one set.
	for (double density : {1e-3, 1e-2, 0.1, 0.5}) {
		const auto v = bernoulli(rng, universe, density);
		const auto a = ds::set::from_sorted(universe, v);
		std::snprintf(name, sizeof name, "iterate d=%g", density);
		report(name, best_ns([&] { std::uint64_t s = 0; a.for_each([&](std::uint32_t d) { s += d; }); sink = sink + s; }));
	}

	// Point test(): ns per probe over 4096 random probes, half of them members when there are any.
	auto test_row = [&](const char* label, std::uint32_t u, const std::vector<std::uint32_t>& v) {
		const auto a = ds::set::from_sorted(u, v);
		std::vector<std::uint32_t> probes(4096);
		for (std::size_t i = 0; i < probes.size(); ++i) {
			probes[i] = (i % 2 && !v.empty()) ? v[rng() % v.size()] : std::uint32_t(rng() % (std::uint64_t(u) + 1));
		}
		report(label, best_ns([&] { std::uint64_t s = 0; for (auto p : probes) { s += a.test(p); } sink = sink + s; }) / 4096);
	};
	for (double density : {1e-3, 1e-2, 0.03, 0.1, 0.5}) {
		std::snprintf(name, sizeof name, "test   d=%g (ns/probe)", density);
		test_row(name, universe, bernoulli(rng, universe, density));
	}
	test_row("test   1 chunk, 8 members (ns/probe)", 65536, {3, 100, 2000, 9000, 30000, 40000, 50000, 65535});
	{
		std::vector<std::uint32_t> v;
		for (std::uint32_t i = 0; i < 4096; ++i) { v.push_back(i * 16); }
		test_row("test   1 chunk, 4096 array (ns/probe)", 65536, v);
	}
	{
		std::vector<std::uint32_t> v;
		for (std::uint32_t k = 0; k < 65536; ++k) { v.push_back((k << 16) | std::uint32_t(rng() % 65536)); }
		test_row("test   65536 chunks x 1 (ns/probe)", 0xffffffffu, v);
	}
}

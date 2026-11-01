// docset.hh against the reference test/docset_old.hh: materialized AND / ANDNOT per density and
// point test() probes, with an A/A column (reference against itself) for the noise floor.
#include "docset.hh"
#include "docset_old.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

template <typename F>
static double ns_per_op(F&& f) {
	std::size_t ops = 0;
	auto start = std::chrono::steady_clock::now();
	double seconds = 0;
	do { f(); ++ops; seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); } while (seconds < 0.03);
	return seconds * 1e9 / double(ops);
}

template <typename F>
static double best(F&& f) {
	double b = 1e300;
	for (int i = 0; i < 9; ++i) { b = std::min(b, ns_per_op(f)); }
	return b;
}

static volatile std::uint64_t sink;

int main() {
	constexpr std::uint32_t universe = 1u << 24;
	std::mt19937_64 rng(11);
	std::printf("%9s %8s %10s %10s %10s %8s %6s\n", "density", "op", "old ns", "old A/A", "new ns", "speedup", "A/A");
	for (double density : {1e-3, 1e-2, 0.03, 0.1, 0.2, 0.3, 0.5}) {
		auto make = [&] {
			std::vector<std::uint32_t> v;
			std::bernoulli_distribution bern(density);
			for (std::uint32_t d = 0; d < universe; ++d) { if (bern(rng)) { v.push_back(d); } }
			return v;
		};
		auto va = make(), vb = make();
		auto a = docset::set::from_sorted(universe, va), b = docset::set::from_sorted(universe, vb);
		auto oa = docset_old::set::from_sorted(universe, va), ob = docset_old::set::from_sorted(universe, vb);
		auto row = [&](const char* name, auto&& o, auto&& n) {
			double x = best(o), y = best(o), z = best(n);
			std::printf("%9g %8s %10.0f %10.0f %10.0f %7.2fx %6.2f\n", density, name, x, y, z, x / z, x / y);
		};
		row("and", [&] { sink = sink + (oa & ob).count(); }, [&] { sink = sink + (a & b).count(); });
		row("andnot", [&] { sink = sink + (oa - ob).count(); }, [&] { sink = sink + (a - b).count(); });
		std::vector<std::uint32_t> probes(4096);
		for (auto& p : probes) { p = std::uint32_t(rng() % universe); }
		row("test", [&] { std::uint64_t s = 0; for (auto p : probes) { s += oa.test(p); } sink = sink + s; },
		    [&] { std::uint64_t s = 0; for (auto p : probes) { s += a.test(p); } sink = sink + s; });
	}
}

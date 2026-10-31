// Differential test against test/docset_old.hh, a frozen copy of the original implementation:
// and / or / andnot / test / count / iteration must agree on random sets across densities,
// including chunks that straddle the array/bitmap threshold and empty chunks.
#include "docset.hh"
#include "docset_old.hh"

#include <cstdio>
#include <cstdlib>
#include <random>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); if (++failures > 20) std::exit(1); } \
	} while (0)

using docs = std::vector<std::uint32_t>;

template <typename S>
static docs members(const S& s) {
	docs v;
	s.for_each([&](std::uint32_t d) { v.push_back(d); });
	return v;
}

// Per-chunk density chosen from a spread so that chunks land on both sides of 4096.
static docs make(std::mt19937_64& rng, std::uint32_t universe) {
	static const double dens[] = {0, 1e-4, 1e-3, 0.02, 0.0625, 0.0625 + 1e-3, 0.07, 0.1, 0.3, 0.5, 0.9, 1.0};
	docs v;
	for (std::uint32_t base = 0; base < universe; base += 65536) {
		double d = dens[rng() % std::size(dens)];
		std::uint32_t end = std::min<std::uint64_t>(universe, std::uint64_t(base) + 65536);
		if (d >= 1.0) { for (std::uint32_t x = base; x < end; ++x) { v.push_back(x); } continue; }
		if (d == 0) { continue; }
		std::bernoulli_distribution bern(d);
		for (std::uint32_t x = base; x < end; ++x) { if (bern(rng)) { v.push_back(x); } }
	}
	return v;
}

int main() {
	std::mt19937_64 rng(2024);
	for (int iter = 0; iter < 60; ++iter) {
		std::uint32_t universe = 65536 * (1 + rng() % 6) - (iter % 3 == 0 ? rng() % 60000 : 0);
		docs va = make(rng, universe), vb = make(rng, universe);
		auto a = docset::set::from_sorted(universe, va), b = docset::set::from_sorted(universe, vb);
		auto oa = docset_old::set::from_sorted(universe, va), ob = docset_old::set::from_sorted(universe, vb);
		auto same = [&](const docset::set& n, const docset_old::set& o) {
			CHECK(n.count() == o.count());
			CHECK(members(n) == members(o));
		};
		same(a, oa);
		same(a & b, oa & ob);
		same(a | b, oa | ob);
		same(a - b, oa - ob);
		same(b - a, ob - oa);
		same((a & b) | a, (oa & ob) | oa);
		for (int p = 0; p < 4000; ++p) {
			std::uint32_t d = universe ? std::uint32_t(rng() % universe) : 0;
			CHECK(a.test(d) == oa.test(d));
			CHECK((a & b).test(d) == (oa & ob).test(d));
		}
		// Probes beyond the universe's last chunk and on every member boundary.
		for (std::size_t i = 0; i < va.size(); i += 97) { CHECK(a.test(va[i]) && !a.test(va[i] ^ 0x80000000u)); }
		CHECK(a.test(universe + 70000) == oa.test(universe + 70000));
	}
	if (failures) { return 1; }
	std::puts("all docset differential tests passed");
}

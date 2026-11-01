// Differential test against the reference implementation in test/docset_old.hh:
// and / or / andnot / test / count / iteration must agree on random sets across densities,
// including chunks that straddle the array/bitmap threshold and empty chunks.
#include "docset.hh"
#include "docset_old.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>

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

// Bitmap results of at most array_max members are converted to arrays (from_bitmap), by a different
// extraction routine per CPU feature and cardinality. Build two bitmap containers A = S + R1 and
// B = S + R2 (R1, R2, S pairwise disjoint, both above array_max members) so that A & B is exactly S
// and A - B is exactly R1, for S shaped to hit every extraction path: empty words, one to many bits
// per word, full words, the chunk edges, and cardinalities from 0 through array_max + 1. An empty
// result leaves no chunk.
static docs shaped(std::mt19937_64& rng, int shape, std::uint32_t card) {
	std::set<std::uint32_t> s;
	switch (shape) {
		case 0:  // uniformly random positions
			while (s.size() < card) { s.insert(std::uint32_t(rng() % 65536)); }
			break;
		case 1:  // packed: consecutive positions from a random start (full words, then a partial one)
		{
			std::uint32_t start = std::uint32_t(rng() % (65536 - card + 1));
			for (std::uint32_t i = 0; i < card; ++i) { s.insert(start + i); }
			break;
		}
		case 2:  // k bits in each of the first card/k words (k = 1..4), one run of k in the last word's place
		{
			std::uint32_t k = 1 + std::uint32_t(rng() % 4);
			for (std::uint32_t w = 0; s.size() < card && w < 1024; ++w) {
				for (std::uint32_t j = 0; j < k && s.size() < card; ++j) { s.insert(w * 64 + std::uint32_t(rng() % 64)); }
			}
			while (s.size() < card) { s.insert(std::uint32_t(rng() % 65536)); }
			break;
		}
		case 3:  // every other word full, the rest empty
			for (std::uint32_t w = 0; s.size() < card && w < 1024; w += 2) {
				for (std::uint32_t j = 0; j < 64 && s.size() < card; ++j) { s.insert(w * 64 + j); }
			}
			break;
		default:  // the last words of the chunk
			for (std::uint32_t i = 0; i < card; ++i) { s.insert(65535 - i); }
			break;
	}
	return docs(s.begin(), s.end());
}

// Which extraction form a result of each size takes: the plain loop up to the limit, two bits per word
// from there to extract_two_max, four above. The first argument is the member count, the second the loop
// limit, so the boundary is checked for the limits of every CPU path and not only the one built.
namespace dd = docset::detail;
static_assert(dd::pick_extract_form(0, 0) == dd::extract_form::loop);
static_assert(dd::pick_extract_form(1, 0) == dd::extract_form::two);
static_assert(dd::pick_extract_form(32, 32) == dd::extract_form::loop);
static_assert(dd::pick_extract_form(33, 32) == dd::extract_form::two);
static_assert(dd::pick_extract_form(96, 96) == dd::extract_form::loop);
static_assert(dd::pick_extract_form(97, 96) == dd::extract_form::two);
static_assert(dd::pick_extract_form(dd::extract_two_max, 96) == dd::extract_form::two);
static_assert(dd::pick_extract_form(dd::extract_two_max + 1, 96) == dd::extract_form::four);
static_assert(dd::pick_extract_form(dd::array_max, 96) == dd::extract_form::four);
static_assert(dd::extract_loop_max < dd::extract_two_max);

static void extraction(std::mt19937_64& rng) {
	// Sizes on both sides of each limit of extract_bits: the loop limit of every CPU path (32 on arm64,
	// 96 on x86-64) and of the build, the end of the two-bit form, and the largest array.
	std::vector<std::uint32_t> cards = {0, 1, 2, 3, 5, 8, 31, 32, 33, 63, 64, 65, 95, 96, 97, 127, 128, 129, 500, 1000, 1023, 1024, 1025, 1536, 2047, 2048, 2049, 3000, 3071, 3072, 3073, 4000, 4094, 4095, 4096, 4097, 4098, 4200};
	for (std::uint32_t edge : {dd::extract_loop_max, dd::extract_two_max}) {
		for (std::uint32_t card : {edge ? edge - 1 : 0, edge, edge + 1}) { cards.push_back(card); }
	}
	const std::uint32_t universe = 65536;
	for (int rep = 0; rep < 3; ++rep) {
		for (std::uint32_t card : cards) {
			for (int shape = 0; shape < 5; ++shape) {
				docs s = shaped(rng, shape, card);
				std::set<std::uint32_t> in_s(s.begin(), s.end());
				docs rest;
				for (std::uint32_t x = 0; x < universe; ++x) { if (!in_s.count(x)) { rest.push_back(x); } }
				std::shuffle(rest.begin(), rest.end(), rng);
				const std::size_t need = std::max<std::size_t>(1, 4097 - std::min<std::size_t>(s.size(), 4096)) + rng() % 2000;
				docs r1(rest.begin(), rest.begin() + long(need)), r2(rest.begin() + long(need), rest.begin() + long(2 * need));
				std::sort(r1.begin(), r1.end());
				std::sort(r2.begin(), r2.end());
				auto joined = [&](const docs& r) { docs v(s); v.insert(v.end(), r.begin(), r.end()); std::sort(v.begin(), v.end()); return v; };
				docs va = joined(r1), vb = joined(r2);
				auto a = docset::set::from_sorted(universe, va), b = docset::set::from_sorted(universe, vb);
				auto oa = docset_old::set::from_sorted(universe, va), ob = docset_old::set::from_sorted(universe, vb);
				auto both = a & b;
				CHECK(both.count() == s.size());
				CHECK(both.chunks() == (s.empty() ? 0u : 1u));
				CHECK(members(both) == s);
				CHECK(members(oa & ob) == s);
				auto diff = a - b;
				CHECK(diff.count() == r1.size());
				CHECK(diff.chunks() == 1);
				CHECK(members(diff) == r1);
				CHECK(members(oa - ob) == r1);
				for (std::uint32_t p : {0u, 1u, 63u, 64u, 65535u}) { CHECK(both.test(p) == in_s.count(p)); }
			}
		}
	}
}

// Intersections and differences whose result has exactly r members, r on both sides of array_max
// (as the result, and as the operand B = full chunk - R) and at the empty and full chunk, in chunks
// 0, 1 and 65535. A result above array_max members stays a bitmap and one at or below it is an
// array; an empty result leaves no chunk. Both the bitmap-bitmap and the array-bitmap paths run.
static void result_boundaries(std::mt19937_64& rng) {
	static const std::uint32_t sizes[] = {0, 1, 2, 3, 4094, 4095, 4096, 4097, 4098, 61438, 61439, 61440, 61441, 61442, 65533, 65534, 65535, 65536};
	const std::uint32_t universe = 0xffffffffu;
	for (std::uint32_t key : {0u, 1u, 65535u}) {
		const std::uint32_t base = key << 16;
		docs all(65536);
		for (std::uint32_t i = 0; i < 65536; ++i) { all[i] = base + i; }
		auto full = docset::set::from_sorted(universe, all);
		auto ofull = docset_old::set::from_sorted(universe, all);
		for (std::uint32_t r : sizes) {
			docs shuffled(all);
			std::shuffle(shuffled.begin(), shuffled.end(), rng);
			docs taken(shuffled.begin(), shuffled.begin() + r), rest(shuffled.begin() + r, shuffled.end());
			std::sort(taken.begin(), taken.end());
			std::sort(rest.begin(), rest.end());
			auto t = docset::set::from_sorted(universe, taken), u = docset::set::from_sorted(universe, rest);
			auto ot = docset_old::set::from_sorted(universe, taken), ou = docset_old::set::from_sorted(universe, rest);
			auto expect = [&](const docset::set& got, const docset_old::set& old, const docs& want) {
				CHECK(got.count() == want.size());
				CHECK(got.chunks() == (want.empty() ? 0u : 1u));
				CHECK(members(got) == want);
				CHECK(members(old) == want);
				for (std::uint32_t p : {base, base + 1, base + 65535, base - 1}) { CHECK(got.test(p) == std::binary_search(want.begin(), want.end(), p)); }
				for (std::size_t k = 0; k < want.size(); k += 1 + want.size() / 16) { CHECK(got.test(want[k])); }
			};
			expect(full - u, ofull - ou, taken);   // R = taken, B = rest: bitmap or array
			expect(full - t, ofull - ot, rest);    // R = rest, B = taken
			expect(full & t, ofull & ot, taken);
			expect(full & u, ofull & ou, rest);
			expect(t - full, ot - ofull, {});
			expect(u - u, ou - ou, {});
			expect(t & u, ot & ou, {});
			expect(t | u, ot | ou, all);
		}
	}
}

int main() {
	std::mt19937_64 rng(2024);
	extraction(rng);
	result_boundaries(rng);
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

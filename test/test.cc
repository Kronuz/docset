// docset: every operation against std::set_* references across densities, mixed
// representations, iterators (next and advance), and the representation choice.
#include "docset.hh"

#include <cstdio>
#include <cstdlib>
#include <random>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); if (++failures > 20) std::exit(1); } \
	} while (0)

using docset::doc_t;

static_assert(docset::doc_iterator<docset::bitset::iterator>);
static_assert(docset::doc_iterator<docset::sorted_set::iterator>);
static_assert(docset::doc_iterator<docset::set::iterator>);

static std::vector<doc_t> random_docs(std::mt19937_64& rng, doc_t universe, double density) {
	std::vector<doc_t> v;
	std::bernoulli_distribution keep(density);
	for (doc_t d = 0; d < universe; ++d) { if (keep(rng)) { v.push_back(d); } }
	return v;
}

static std::vector<doc_t> members(const docset::set& s) {
	std::vector<doc_t> v;
	s.for_each([&](doc_t d) { v.push_back(d); });
	return v;
}

template <typename It>
static void check_iterator(It it, const std::vector<doc_t>& expected, std::mt19937_64&) {
	std::vector<doc_t> seen;
	for (doc_t d = it.next(); d != docset::no_more_docs; d = it.next()) { seen.push_back(d); }
	CHECK(seen == expected);
	CHECK(it.doc() == docset::no_more_docs);
}

template <typename Make>
static void check_advance(Make make, const std::vector<doc_t>& expected, doc_t universe, std::mt19937_64& rng) {
	for (int round = 0; round < 20; ++round) {
		auto it = make();
		doc_t target = 0;
		for (;;) {
			target += doc_t(rng() % 300);
			doc_t got = it.advance(target);
			auto want = std::lower_bound(expected.begin(), expected.end(), target);
			CHECK(got == (want == expected.end() ? docset::no_more_docs : *want));
			if (got == docset::no_more_docs || target >= universe) { break; }
			target = got + 1;
			if (rng() % 3 == 0) {  // interleave next()
				auto next_want = std::upper_bound(expected.begin(), expected.end(), got);
				CHECK(it.next() == (next_want == expected.end() ? docset::no_more_docs : *next_want));
				if (it.doc() == docset::no_more_docs) { break; }
				target = it.doc() + 1;
			}
		}
	}
}

int main() {
	std::mt19937_64 rng(20261011);
	for (doc_t universe : {1u, 63u, 64u, 65u, 1000u, 65536u, 65537u, 100003u, 300007u}) {
		for (double da : {0.0, 1e-4, 1e-2, 0.03, 0.1, 0.5, 1.0}) {
			for (double db : {0.0, 1e-3, 0.03, 0.2, 1.0}) {
				auto va = random_docs(rng, universe, da), vb = random_docs(rng, universe, db);
				auto a = docset::set::from_sorted(universe, va), b = docset::set::from_sorted(universe, vb);
				CHECK(a.count() == va.size());
				CHECK(members(a) == va);

				std::vector<doc_t> want;
				std::set_intersection(va.begin(), va.end(), vb.begin(), vb.end(), std::back_inserter(want));
				CHECK(members(a & b) == want);
				CHECK((a & b).count() == want.size());
				want.clear();
				std::set_union(va.begin(), va.end(), vb.begin(), vb.end(), std::back_inserter(want));
				CHECK(members(a | b) == want);
				want.clear();
				std::set_difference(va.begin(), va.end(), vb.begin(), vb.end(), std::back_inserter(want));
				CHECK(members(a - b) == want);

				for (doc_t probe = 0; probe < universe; probe += 1 + universe / 50) {
					CHECK(a.test(probe) == std::binary_search(va.begin(), va.end(), probe));
				}

				auto dense = a.to_bitset();
				docset::sorted_set sparse(universe, va);
				check_iterator(dense.begin_iteration(), va, rng);
				check_iterator(sparse.begin_iteration(), va, rng);
				check_advance([&] { return dense.begin_iteration(); }, va, universe, rng);
				check_advance([&] { return sparse.begin_iteration(); }, va, universe, rng);
				check_iterator(a.begin_iteration(), va, rng);
				check_advance([&] { return a.begin_iteration(); }, va, universe, rng);
				auto in_place = a;
				in_place &= b;
				CHECK(members(in_place) == members(a & b));
			}
		}
	}

	// Skewed sizes take the galloping path.
	for (int round = 0; round < 50; ++round) {
		auto va = random_docs(rng, 1u << 20, 1e-5), vb = random_docs(rng, 1u << 20, 0.02);
		docset::sorted_set a(1u << 20, va), b(1u << 20, vb);
		std::vector<doc_t> want;
		std::set_intersection(va.begin(), va.end(), vb.begin(), vb.end(), std::back_inserter(want));
		CHECK((a & b).docs() == want);
		CHECK((b & a).docs() == want);
	}

	// 16-bit array kernels against std:: algorithms, with heavy overlap and every small size.
	for (int round = 0; round < 20000; ++round) {
		auto make16 = [&](std::size_t n, unsigned span) {
			std::vector<std::uint16_t> v;
			for (std::size_t i = 0; i < n; ++i) { v.push_back(std::uint16_t(rng() % span)); }
			std::sort(v.begin(), v.end());
			v.erase(std::unique(v.begin(), v.end()), v.end());
			return v;
		};
		unsigned span = round % 3 == 0 ? 65536 : 64 + unsigned(rng() % 600);
		auto x = make16(rng() % 300, span), y = make16(rng() % 300, span);
		std::vector<std::uint16_t> out(x.size() + y.size() + 16), want;
		std::set_intersection(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(want));
		CHECK(std::vector<std::uint16_t>(out.begin(), out.begin() + docset::detail::intersect16(x.data(), x.size(), y.data(), y.size(), out.data())) == want);
		want.clear();
		std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(want));
		CHECK(std::vector<std::uint16_t>(out.begin(), out.begin() + docset::detail::unite16(x.data(), x.size(), y.data(), y.size(), out.data())) == want);
		want.clear();
		std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(want));
		CHECK(std::vector<std::uint16_t>(out.begin(), out.begin() + docset::detail::subtract16(x.data(), x.size(), y.data(), y.size(), out.data())) == want);
	}

	// Dense chunks: arrays become bitmaps past 4096 members per 64Ki chunk and back.
	{
		auto va = random_docs(rng, 1u << 18, 0.3), vb = random_docs(rng, 1u << 18, 0.3);
		auto a = docset::set::from_sorted(1u << 18, va), b = docset::set::from_sorted(1u << 18, vb);
		std::vector<doc_t> want;
		std::set_intersection(va.begin(), va.end(), vb.begin(), vb.end(), std::back_inserter(want));
		CHECK(members(a & b) == want);
		auto sparse_b = docset::set::from_sorted(1u << 18, random_docs(rng, 1u << 18, 0.01));
		CHECK(members(a & sparse_b).size() == (a & sparse_b).count());
		CHECK(members(a - a).empty());
		CHECK((a | b).count() == a.count() + b.count() - (a & b).count());
	}

	std::printf("docset: %s\n", failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

// docset: every operation against std::set_* references across densities, mixed
// representations, iterators (next and advance), and the representation choice.
#include "docset.hh"

#include <algorithm>
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
	// Frozen sets: rank and membership against brute force at every density, chunk kinds
	// (array, bitset, full) side by side, and every single-byte corruption either refused
	// at open or harmless.
	{
		std::mt19937_64 rng(7);
		for (int round = 0; round < 60; ++round) {
			std::vector<docset::doc_t> docs;
			const int chunks = 1 + int(rng() % 5);
			for (int c = 0; c < chunks; ++c) {
				const docset::doc_t base = docset::doc_t((rng() % 4 + c * 5) << 16);
				const int mode = int(rng() % 4);
				for (docset::doc_t low = 0; low < 65536; ++low) {
					bool take = mode == 0 ? rng() % 1000 == 0 : mode == 1 ? rng() % 3 != 0 : mode == 2 ? true : rng() % 16 == 0;
					if (take) { docs.push_back(base + low); }
				}
			}
			std::sort(docs.begin(), docs.end()); docs.erase(std::unique(docs.begin(), docs.end()), docs.end());
			auto bytes = docset::freeze(docs.data(), docs.size());
			auto view = docset::frozen_view::open(bytes.data(), bytes.size());
			CHECK(view && view->cardinality() == docs.size());
			if (!view) { continue; }
			for (int probe = 0; probe < 2000; ++probe) {
				const docset::doc_t doc = probe % 2 && !docs.empty() ? docs[rng() % docs.size()] + docset::doc_t(rng() % 3) - 1 : docset::doc_t(rng() % (26u << 16));
				const auto below = std::uint64_t(std::lower_bound(docs.begin(), docs.end(), doc) - docs.begin());
				CHECK(view->rank(doc) == below);
				CHECK(view->contains(doc) == std::binary_search(docs.begin(), docs.end(), doc));
			}
			for (std::size_t k = 0; k < docs.size(); k += 1 + docs.size() / 64) { CHECK(view->rank(docs[k]) == k && view->contains(docs[k])); }
			if (round % 6 == 0) {
				for (std::size_t at = 0; at < bytes.size(); at += 1 + rng() % 97) {
					auto bad = bytes; bad[at] ^= std::uint8_t(1 + rng() % 255);
					if (auto v = docset::frozen_view::open(bad.data(), bad.size())) { (void)v->rank(docset::doc_t(rng())); (void)v->contains(docset::doc_t(rng())); }
				}
				CHECK(!docset::frozen_view::open(bytes.data(), bytes.size() - 1));
			}
		}
		auto empty = docset::freeze(nullptr, 0);
		auto view = docset::frozen_view::open(empty.data(), empty.size());
		CHECK(view && view->cardinality() == 0 && view->rank(123) == 0 && !view->contains(0));
	}

	// Frozen chunks at the container boundaries: 4096 members is an array, 4097 a bitset, 65535 a
	// bitset and 65536 a full chunk. The kind is the third byte of the directory entry. Rank and
	// membership are checked at every doc in and around the chunk, in chunks 0, 1 and 65535.
	{
		std::mt19937_64 rng(31);
		const auto expected_kind = [](std::uint32_t n) { return n == 65536 ? 0 : n <= 4096 ? 1 : 2; };
		for (docset::doc_t key : {0u, 1u, 65535u}) {
			const docset::doc_t base = key << 16;
			for (std::uint32_t n : {1u, 2u, 4095u, 4096u, 4097u, 4098u, 65534u, 65535u, 65536u}) {
				std::vector<std::uint32_t> lows(65536);
				for (std::uint32_t i = 0; i < 65536; ++i) { lows[i] = i; }
				std::shuffle(lows.begin(), lows.end(), rng);
				lows.resize(n);
				std::sort(lows.begin(), lows.end());
				std::vector<docset::doc_t> docs;
				std::vector<bool> present(65536, false);
				for (auto low : lows) { docs.push_back(base + low); present[low] = true; }
				auto bytes = docset::freeze(docs.data(), docs.size());
				const int kind = expected_kind(n);
				CHECK(bytes.size() > 18 && bytes[18] == kind);
				CHECK(bytes.size() == 16 + 24 + (kind == 0 ? 0 : kind == 1 ? 2 * n : 8 * 1024 + 2 * 128));
				auto view = docset::frozen_view::open(bytes.data(), bytes.size());
				CHECK(view && view->cardinality() == n);
				if (!view) { continue; }
				std::uint64_t below = 0;
				for (std::int64_t k = -3; k < 65536 + 3; ++k) {
					const std::int64_t doc = std::int64_t(base) + k;
					if (doc < 0 || doc > 0xffffffffll) { continue; }
					const bool in_chunk = k >= 0 && k < 65536;
					CHECK(view->contains(docset::doc_t(doc)) == (in_chunk && present[std::size_t(k)]));
					CHECK(view->rank(docset::doc_t(doc)) == (k < 0 ? 0 : below));
					if (in_chunk && present[std::size_t(k)]) { ++below; }
				}
				CHECK(below == n);
			}
		}
		// Chunks of each kind side by side: the ranks carry across them.
		std::vector<docset::doc_t> docs;
		const std::pair<docset::doc_t, std::uint32_t> chunks[] = {{0, 4096}, {1, 4097}, {2, 65536}, {3, 1}, {4, 65535}, {9, 4096}};
		for (auto [key, n] : chunks) {
			std::vector<std::uint32_t> lows(65536);
			for (std::uint32_t i = 0; i < 65536; ++i) { lows[i] = i; }
			std::shuffle(lows.begin(), lows.end(), rng);
			lows.resize(n);
			std::sort(lows.begin(), lows.end());
			for (auto low : lows) { docs.push_back((key << 16) + low); }
		}
		auto bytes = docset::freeze(docs.data(), docs.size());
		auto view = docset::frozen_view::open(bytes.data(), bytes.size());
		CHECK(view && view->cardinality() == docs.size());
		for (std::size_t c = 0; c < std::size(chunks) && view; ++c) {
			CHECK(bytes[16 + 24 * c + 2] == expected_kind(chunks[c].second));
		}
		if (view) {
			for (auto [key, n] : chunks) {
				for (docset::doc_t doc : {(key << 16) - 2, (key << 16) - 1, key << 16, (key << 16) + 1, (key << 16) + 65534, (key << 16) + 65535, (key << 16) + 65536, (key << 16) + 65537}) {
					const auto below = std::uint64_t(std::lower_bound(docs.begin(), docs.end(), doc) - docs.begin());
					CHECK(view->rank(doc) == below);
					CHECK(view->contains(doc) == std::binary_search(docs.begin(), docs.end(), doc));
				}
			}
			CHECK(view->rank(0xffffffffu) == docs.size());
		}
	}

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
				CHECK(dense.begin_iteration().cost() == va.size());
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

	// Union, intersection and difference at every pair of sizes 0..40 over shaped inputs: runs that
	// touch 0 and 65535, equal, disjoint either way round, nested, and interleaved, so block
	// boundaries, repeated values across them, and the scalar tails are all hit.
	{
		auto run = [](std::size_t n, std::size_t first, std::size_t step) {
			std::vector<std::uint16_t> v;
			for (std::size_t i = 0; i < n; ++i) { v.push_back(std::uint16_t(first + i * step)); }
			return v;
		};
		std::vector<std::uint16_t> out(100);
		auto check = [&](const std::vector<std::uint16_t>& x, const std::vector<std::uint16_t>& y) {
			std::vector<std::uint16_t> want;
			std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(want));
			out.assign(x.size() + y.size() + 16, 0xaaaa);
			CHECK(std::vector<std::uint16_t>(out.begin(), out.begin() + docset::detail::unite16(x.data(), x.size(), y.data(), y.size(), out.data())) == want);
			want.clear();
			std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(want));
			CHECK(std::vector<std::uint16_t>(out.begin(), out.begin() + docset::detail::subtract16(x.data(), x.size(), y.data(), y.size(), out.data())) == want);
			want.clear();
			std::set_intersection(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(want));
			CHECK(std::vector<std::uint16_t>(out.begin(), out.begin() + docset::detail::intersect16(x.data(), x.size(), y.data(), y.size(), out.data())) == want);
		};
		for (std::size_t na = 0; na <= 40; ++na) {
			for (std::size_t nb = 0; nb <= 40; ++nb) {
				check(run(na, 0, 1), run(nb, 0, 1));                        // nested prefixes, start at 0
				check(run(na, 0, 2), run(nb, 1, 2));                        // interleaved, no common value
				check(run(na, 0, 3), run(nb, 0, 2));                        // some common values
				check(run(na, 0, 1), run(nb, 1000, 1));                     // a entirely below b
				check(run(na, 1000, 1), run(nb, 0, 1));                     // b entirely below a
				check(run(na, 65535 - na + 1, 1), run(nb, 65535 - nb + 1, 1));  // ends at 65535
				check(run(na, 65535 - 2 * na + 1, 2), run(nb, 65535 - 2 * nb + 2, 2));  // interleaved at the top
			}
		}
	}

	// The branch-free lower bound against std::lower_bound for every probe value, at array sizes
	// around each halving boundary, with arrays that include 0 and 65535.
	for (std::size_t n : {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 255, 256, 257, 300, 1000, 4095, 4096, 4097}) {
		std::vector<std::uint16_t> v;
		if (n) {
			std::vector<bool> used(65536, false);
			used[0] = true;
			if (n > 1) { used[65535] = true; }
			std::size_t have = std::size_t(used[0]) + std::size_t(n > 1);
			while (have < n) {
				std::size_t x = rng() % 65536;
				if (!used[x]) { used[x] = true; ++have; }
			}
			for (std::size_t x = 0; x < 65536; ++x) { if (used[x]) { v.push_back(std::uint16_t(x)); } }
		}
		for (std::uint32_t x = 0; x < 65536; ++x) {
			CHECK(docset::detail::lower_bound_index(v.data(), v.size(), std::uint16_t(x)) ==
			      std::size_t(std::lower_bound(v.begin(), v.end(), std::uint16_t(x)) - v.begin()));
		}
	}

	// Point test() on sets with many chunks and at the largest ordinal.
	{
		std::vector<doc_t> v;
		for (std::uint32_t k = 0; k < 65536; ++k) { v.push_back((k << 16) | std::uint32_t(rng() % 65536)); }
		auto s = docset::set::from_sorted(0xffffffffu, v);
		for (doc_t d : v) {
			CHECK(s.test(d));
			CHECK(!s.test(d ^ 1u) || std::binary_search(v.begin(), v.end(), d ^ 1u));
		}
		CHECK(s.chunks() == 65536);
		auto top = docset::set::from_sorted(0xffffffffu, {0u, 0xfffffffeu, 0xffffffffu});
		CHECK(top.test(0) && top.test(0xfffffffeu) && top.test(0xffffffffu) && !top.test(1) && !top.test(0xfffffffdu));
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

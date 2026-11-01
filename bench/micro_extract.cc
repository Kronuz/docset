// Bitmap-to-array extraction: the sorted 16-bit positions of the set bits of one 64Ki-bit
// bitmap (1024 words), docset's own routine against the variants it is chosen from. Each timed
// pass cycles through 256 different bitmaps (a 2^24-ordinal set has that many chunks), so the
// branch predictor cannot memorize the data. Every variant is checked against the plain loop
// for identical output before it is timed. The BMI rows exist on x86-64 only and run when the
// CPU has BMI1.
//
// usage: docset_micro_extract [ROUNDS [DENSITIES]]   (default 15 and 0.002,0.005,0.01,0.02,0.04,0.06)
// Densities are per-bit probabilities; a bitmap holds at most 4096 members when it becomes an array.
// Output: the median ns per bitmap of each variant, one line per density.
#include "docset.hh"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

constexpr std::size_t W = 1024;  // words per bitmap
constexpr std::size_t NB = 256;  // bitmaps per pass
static std::uint32_t g_card;     // members of the bitmap being extracted

using Fn = std::size_t (*)(const std::uint64_t*, std::uint16_t*);

// docset's routine: the form it picks by CPU and cardinality.
static std::size_t ext_landed(const std::uint64_t* words, std::uint16_t* out) {
	docset::detail::extract_bits(words, out, g_card);
	return g_card;
}

// One loop iteration per set bit.
static std::size_t ext_loop(const std::uint64_t* words, std::uint16_t* out) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < W; ++i) {
		for (std::uint64_t w = words[i]; w; w &= w - 1) { out[k++] = std::uint16_t(i * 64 + unsigned(std::countr_zero(w))); }
	}
	return k;
}

// Two bits per word without a branch (the output index advances only on a set bit), then a loop.
static std::size_t ext_two(const std::uint64_t* words, std::uint16_t* out) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < W; ++i) {
		std::uint64_t w = words[i];
		const std::uint16_t base = std::uint16_t(i * 64);
		out[k] = std::uint16_t(base + unsigned(std::countr_zero(w)));
		k += w != 0;
		w &= w - 1;
		out[k] = std::uint16_t(base + unsigned(std::countr_zero(w)));
		k += w != 0;
		w &= w - 1;
		for (; w; w &= w - 1) { out[k++] = std::uint16_t(base + unsigned(std::countr_zero(w))); }
	}
	return k;
}

// Four bits per word without a branch, then a loop.
static std::size_t ext_four(const std::uint64_t* words, std::uint16_t* out) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < W; ++i) {
		std::uint64_t w = words[i];
		const std::uint16_t base = std::uint16_t(i * 64);
#define STEP out[k] = std::uint16_t(base + unsigned(std::countr_zero(w))); k += w != 0; w &= w - 1;
		STEP STEP STEP STEP
#undef STEP
		for (; w; w &= w - 1) { out[k++] = std::uint16_t(base + unsigned(std::countr_zero(w))); }
	}
	return k;
}

// Byte table: per byte, eight 8-bit positions from a table, widened and offset, always stored whole.
struct Table {
	std::uint8_t pos[256][8];
	std::uint8_t cnt[256];
	Table() {
		for (int b = 0; b < 256; ++b) {
			int n = 0;
			for (int j = 0; j < 8; ++j) { if (b >> j & 1) { pos[b][n++] = std::uint8_t(j); } }
			cnt[b] = std::uint8_t(n);
			for (int j = n; j < 8; ++j) { pos[b][j] = 0; }
		}
	}
};
static const Table table;
static std::size_t ext_table(const std::uint64_t* words, std::uint16_t* out) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < W; ++i) {
		std::uint64_t w = words[i];
		for (unsigned b = 0; b < 8; ++b) {
			const unsigned byte = unsigned(w >> (8 * b)) & 255;
			const std::uint16_t base = std::uint16_t(i * 64 + b * 8);
			std::uint16_t* o = out + k;
			for (int j = 0; j < 8; ++j) { o[j] = std::uint16_t(base + table.pos[byte][j]); }
			k += table.cnt[byte];
		}
	}
	return k;
}

#if defined(__x86_64__)
// BMI1: tzcnt gives 64 for a zero word and blsr clears the lowest bit in one instruction.
__attribute__((target("bmi"))) static std::size_t ext_loop_bmi(const std::uint64_t* words, std::uint16_t* out) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < W; ++i) {
		for (std::uint64_t w = words[i]; w; w = _blsr_u64(w)) { out[k++] = std::uint16_t(i * 64 + unsigned(_tzcnt_u64(w))); }
	}
	return k;
}

#define STEP_BMI out[k] = std::uint16_t(base + unsigned(_tzcnt_u64(w))); k += w != 0; w = _blsr_u64(w);
#define EXTRACT_BMI(NAME, STEPS) \
	__attribute__((target("bmi"))) static std::size_t NAME(const std::uint64_t* words, std::uint16_t* out) { \
		std::size_t k = 0; \
		for (std::size_t i = 0; i < W; ++i) { \
			std::uint64_t w = words[i]; \
			const std::uint16_t base = std::uint16_t(i * 64); \
			STEPS \
			for (; w; w = _blsr_u64(w)) { out[k++] = std::uint16_t(base + unsigned(_tzcnt_u64(w))); } \
		} \
		return k; \
	}
// Two, three and four bits per word without a branch, then the loop.
EXTRACT_BMI(ext_two_bmi, STEP_BMI STEP_BMI)
EXTRACT_BMI(ext_three_bmi, STEP_BMI STEP_BMI STEP_BMI)
EXTRACT_BMI(ext_four_bmi, STEP_BMI STEP_BMI STEP_BMI STEP_BMI)
#undef EXTRACT_BMI
#undef STEP_BMI
#endif

struct Variant { const char* name; Fn f; bool needs_bmi; };

static volatile std::size_t sink;

int main(int argc, char** argv) {
	const int rounds = argc > 1 ? std::atoi(argv[1]) : 15;
	const std::string densities = argc > 2 ? argv[2] : "0.002,0.005,0.01,0.02,0.04,0.06";
	std::vector<Variant> variants = {{"landed", ext_landed, false}, {"loop", ext_loop, false}, {"two", ext_two, false},
	                                 {"four", ext_four, false}, {"table", ext_table, false}
#if defined(__x86_64__)
	                                 , {"loop_bmi", ext_loop_bmi, true}, {"two_bmi", ext_two_bmi, true},
	                                 {"three_bmi", ext_three_bmi, true}, {"four_bmi", ext_four_bmi, true}
#endif
	};
#if defined(__x86_64__)
	const bool bmi = __builtin_cpu_supports("bmi");
#else
	const bool bmi = false;
#endif
	std::mt19937_64 rng(5);
	std::vector<std::uint64_t> data(NB * W);
	std::vector<std::uint16_t> out(W * 64 + 64), want(W * 64 + 64);
	for (std::size_t pos = 0; pos < densities.size();) {
		std::size_t end = densities.find(',', pos);
		if (end == std::string::npos) { end = densities.size(); }
		const double p = std::atof(densities.substr(pos, end - pos).c_str());
		pos = end + 1;
		std::bernoulli_distribution bern(p);
		for (auto& w : data) {
			w = 0;
			for (int b = 0; b < 64; ++b) { if (bern(rng)) { w |= std::uint64_t(1) << b; } }
		}
		std::size_t total = 0;
		std::vector<std::uint32_t> cards(NB);
		for (std::size_t c = 0; c < NB; ++c) {
			const std::size_t n0 = ext_loop(&data[c * W], want.data());
			cards[c] = std::uint32_t(n0);
			total += n0;
			g_card = cards[c];
			for (auto& v : variants) {
				if (v.needs_bmi && !bmi) { continue; }
				const std::size_t n = v.f(&data[c * W], out.data());
				if (n != n0 || std::memcmp(want.data(), out.data(), n0 * 2) != 0) {
					std::printf("MISMATCH %s, bitmap %zu\n", v.name, c);
					return 1;
				}
			}
		}
		// Rounds rotate the order in which the variants run, so host noise does not favor one position.
		const int count = int(variants.size());
		std::vector<std::vector<double>> ns(count);
		for (int round = 0; round < rounds; ++round) {
			for (int j = 0; j < count; ++j) {
				const int i = (j + round) % count;
				if (variants[i].needs_bmi && !bmi) { continue; }
				const auto start = std::chrono::steady_clock::now();
				std::size_t s = 0;
				for (int rep = 0; rep < 4; ++rep) {
					for (std::size_t c = 0; c < NB; ++c) { g_card = cards[c]; s += variants[i].f(&data[c * W], out.data()); }
				}
				sink = s;
				ns[i].push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 1e9 / (4 * NB));
			}
		}
		std::printf("p=%-6g bits/word %.2f  ns per bitmap (median):", p, double(total) / (NB * W));
		for (int i = 0; i < count; ++i) {
			if (ns[i].empty()) { continue; }
			std::sort(ns[i].begin(), ns[i].end());
			std::printf("  %s %.0f", variants[i].name, ns[i][ns[i].size() / 2]);
		}
		std::printf("\n");
		std::fflush(stdout);
	}
}

/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Sets of document ordinals in [0, universe): a dense bitset, a sparse sorted
// array, and `docset`, which picks between them by density. Plus the iterator
// concept every posting list, scorer, and filter in a search engine implements.

#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#if defined(DOCSET_NO_SIMD)
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#include <tmmintrin.h>
#include <smmintrin.h>
#include <nmmintrin.h>
#define DOCSET_SSE 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define DOCSET_NEON 1
#endif

namespace docset {

using doc_t = std::uint32_t;

// Returned by doc(), next(), and advance() once an iterator is exhausted.
inline constexpr doc_t no_more_docs = std::numeric_limits<doc_t>::max();

// An ascending iterator over doc ordinals. doc() is the current doc (no_more_docs
// when exhausted; unspecified before the first next()), next() moves to the
// following doc, advance(t) moves to the first doc >= t (t greater than doc()),
// and cost() estimates how many docs it yields in total.
template <typename T>
concept doc_iterator = requires(T it, doc_t target) {
	{ it.doc() } -> std::same_as<doc_t>;
	{ it.next() } -> std::same_as<doc_t>;
	{ it.advance(target) } -> std::same_as<doc_t>;
	{ it.cost() } -> std::convertible_to<std::uint64_t>;
};

class bitset;
class sorted_set;

// Dense set: one bit per ordinal.
class bitset {
public:
	bitset() = default;
	explicit bitset(doc_t universe) : words_((std::size_t(universe) + 63) / 64), universe_(universe) {}

	// result = op(a, b) word by word in one pass; returns the result's cardinality.
	template <typename Op>
	static std::pair<bitset, std::uint64_t> combine(const bitset& a, const bitset& b, Op op) {
		bitset r;
		r.universe_ = a.universe_;
		r.words_.resize(a.words_.size());
		const std::uint64_t* x = a.words_.data();
		const std::uint64_t* y = b.words_.data();
		std::uint64_t* z = r.words_.data();
		std::uint64_t n = 0;
		for (std::size_t i = 0, e = a.words_.size(); i < e; ++i) {
			z[i] = op(x[i], y[i]);
			n += unsigned(std::popcount(z[i]));
		}
		return {std::move(r), n};
	}

	doc_t universe() const noexcept { return universe_; }
	void set(doc_t d) noexcept { words_[d >> 6] |= std::uint64_t(1) << (d & 63); }
	void reset(doc_t d) noexcept { words_[d >> 6] &= ~(std::uint64_t(1) << (d & 63)); }
	bool test(doc_t d) const noexcept { return (words_[d >> 6] >> (d & 63)) & 1; }

	std::uint64_t count() const noexcept {
		std::uint64_t n = 0;
		for (auto w : words_) { n += unsigned(std::popcount(w)); }
		return n;
	}

	bitset& operator&=(const bitset& o) noexcept {
		for (std::size_t i = 0; i < words_.size(); ++i) { words_[i] &= o.words_[i]; }
		return *this;
	}
	bitset& operator|=(const bitset& o) noexcept {
		for (std::size_t i = 0; i < words_.size(); ++i) { words_[i] |= o.words_[i]; }
		return *this;
	}
	bitset& operator-=(const bitset& o) noexcept {
		for (std::size_t i = 0; i < words_.size(); ++i) { words_[i] &= ~o.words_[i]; }
		return *this;
	}

	// Calls f(doc) for every member in ascending order.
	template <typename F>
	void for_each(F&& f) const {
		for (std::size_t i = 0; i < words_.size(); ++i) {
			for (std::uint64_t w = words_[i]; w; w &= w - 1) { f(doc_t(i * 64 + unsigned(std::countr_zero(w)))); }
		}
	}

	const std::vector<std::uint64_t>& words() const noexcept { return words_; }
	std::vector<std::uint64_t>& words() noexcept { return words_; }

	class iterator {
	public:
		explicit iterator(const bitset& s) noexcept : s_(&s) {}
		doc_t doc() const noexcept { return doc_; }
		doc_t next() noexcept { return seek(started_ ? (doc_ == no_more_docs ? doc_ : doc_ + 1) : 0); }
		doc_t advance(doc_t target) noexcept { return seek(target); }
		std::uint64_t cost() const noexcept { return s_->universe_; }

	private:
		doc_t seek(doc_t from) noexcept {
			started_ = true;
			if (from >= s_->universe_) { return doc_ = no_more_docs; }
			std::size_t i = from >> 6;
			std::uint64_t w = s_->words_[i] & (~std::uint64_t(0) << (from & 63));
			while (!w) {
				if (++i == s_->words_.size()) { return doc_ = no_more_docs; }
				w = s_->words_[i];
			}
			return doc_ = doc_t(i * 64 + unsigned(std::countr_zero(w)));
		}
		const bitset* s_;
		doc_t doc_ = no_more_docs;
		bool started_ = false;
	};
	iterator begin_iteration() const noexcept { return iterator(*this); }

private:
	std::vector<std::uint64_t> words_;
	doc_t universe_ = 0;
};

namespace detail {

// memcpy that accepts empty ranges with null pointers (empty vectors have no storage).
inline void copy_bytes(void* dst, const void* src, std::size_t bytes) noexcept {
	if (bytes) { std::memcpy(dst, src, bytes); }
}

// Appends a[i] for each set bit i of mask (4 lanes), without branches.
inline std::size_t emit4(doc_t* out, const doc_t* a, unsigned mask) noexcept {
	std::size_t k = 0;
	out[k] = a[0]; k += mask & 1;
	out[k] = a[1]; k += (mask >> 1) & 1;
	out[k] = a[2]; k += (mask >> 2) & 1;
	out[k] = a[3]; k += (mask >> 3) & 1;
	return k;
}

// Merge intersection of two strictly increasing arrays into out (room for min(na, nb) + 3).
inline std::size_t intersect_merge(const doc_t* a, std::size_t na, const doc_t* b, std::size_t nb, doc_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
#if DOCSET_SSE || DOCSET_NEON
	while (i + 4 <= na && j + 4 <= nb) {
#if DOCSET_SSE
		__m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
		__m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + j));
		__m128i eq = _mm_or_si128(
			_mm_or_si128(_mm_cmpeq_epi32(va, vb), _mm_cmpeq_epi32(va, _mm_shuffle_epi32(vb, 0x39))),
			_mm_or_si128(_mm_cmpeq_epi32(va, _mm_shuffle_epi32(vb, 0x4e)), _mm_cmpeq_epi32(va, _mm_shuffle_epi32(vb, 0x93))));
		unsigned mask = unsigned(_mm_movemask_ps(_mm_castsi128_ps(eq)));
#else
		uint32x4_t va = vld1q_u32(a + i), vb = vld1q_u32(b + j);
		uint32x4_t eq = vorrq_u32(vorrq_u32(vceqq_u32(va, vb), vceqq_u32(va, vextq_u32(vb, vb, 1))),
			vorrq_u32(vceqq_u32(va, vextq_u32(vb, vb, 2)), vceqq_u32(va, vextq_u32(vb, vb, 3))));
		static constexpr std::uint32_t bits[4] = {1, 2, 4, 8};
		unsigned mask = vaddvq_u32(vandq_u32(eq, vld1q_u32(bits)));
#endif
		k += emit4(out + k, a + i, mask);
		doc_t amax = a[i + 3], bmax = b[j + 3];
		i += amax <= bmax ? 4 : 0;
		j += bmax <= amax ? 4 : 0;
	}
#endif
	while (i < na && j < nb) {
		doc_t x = a[i], y = b[j];
		out[k] = x;
		k += x == y;
		i += x <= y;
		j += y <= x;
	}
	return k;
}

// First index in [lo, n) with a[index] >= target, by exponential then binary search.
inline std::size_t gallop(const doc_t* a, std::size_t lo, std::size_t n, doc_t target) noexcept {
	std::size_t step = 1, hi = lo;
	while (hi < n && a[hi] < target) { lo = hi + 1; hi += step; step <<= 1; }
	return std::size_t(std::lower_bound(a + lo, a + std::min(hi, n), target) - a);
}

inline std::size_t intersect_gallop(const doc_t* small, std::size_t ns, const doc_t* large, std::size_t nl, doc_t* out) noexcept {
	std::size_t k = 0, j = 0;
	for (std::size_t i = 0; i < ns && j < nl; ++i) {
		j = gallop(large, j, nl, small[i]);
		if (j < nl && large[j] == small[i]) { out[k++] = small[i]; }
	}
	return k;
}

}  // namespace detail

// Sparse set: strictly increasing ordinals.
class sorted_set {
public:
	sorted_set() = default;
	explicit sorted_set(doc_t universe) : universe_(universe) {}
	sorted_set(doc_t universe, std::vector<doc_t> docs) : docs_(std::move(docs)), universe_(universe) {}

	doc_t universe() const noexcept { return universe_; }
	std::uint64_t count() const noexcept { return docs_.size(); }
	bool test(doc_t d) const noexcept { return std::binary_search(docs_.begin(), docs_.end(), d); }
	// Appends a doc greater than every current member.
	void push_back(doc_t d) { docs_.push_back(d); }
	const std::vector<doc_t>& docs() const noexcept { return docs_; }
	std::vector<doc_t>& docs() noexcept { return docs_; }

	template <typename F>
	void for_each(F&& f) const {
		for (auto d : docs_) { f(d); }
	}

	class iterator {
	public:
		explicit iterator(const sorted_set& s) noexcept : s_(&s) {}
		doc_t doc() const noexcept { return i_ < s_->docs_.size() && started_ ? s_->docs_[i_] : no_more_docs; }
		doc_t next() noexcept {
			if (!started_) { started_ = true; } else { ++i_; }
			return doc();
		}
		doc_t advance(doc_t target) noexcept {
			started_ = true;
			i_ = detail::gallop(s_->docs_.data(), i_, s_->docs_.size(), target);
			return doc();
		}
		std::uint64_t cost() const noexcept { return s_->docs_.size(); }

	private:
		const sorted_set* s_;
		std::size_t i_ = 0;
		bool started_ = false;
	};
	iterator begin_iteration() const noexcept { return iterator(*this); }

private:
	std::vector<doc_t> docs_;
	doc_t universe_ = 0;
};

// Intersection of two sorted sets: merge with SIMD block compares, or galloping when sizes are skewed.
inline sorted_set operator&(const sorted_set& a, const sorted_set& b) {
	const auto& x = a.count() <= b.count() ? a.docs() : b.docs();
	const auto& y = a.count() <= b.count() ? b.docs() : a.docs();
	std::vector<doc_t> out(x.size() + 3);
	std::size_t n = x.size() * 32 < y.size() ? detail::intersect_gallop(x.data(), x.size(), y.data(), y.size(), out.data())
	                                         : detail::intersect_merge(x.data(), x.size(), y.data(), y.size(), out.data());
	out.resize(n);
	return sorted_set(a.universe(), std::move(out));
}

namespace detail {

// Branchless merges: the comparisons feed index increments instead of jumps.
inline std::size_t unite(const doc_t* a, std::size_t na, const doc_t* b, std::size_t nb, doc_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	while (i < na && j < nb) {
		doc_t x = a[i], y = b[j];
		out[k++] = x < y ? x : y;
		i += x <= y;
		j += y <= x;
	}
	copy_bytes(out + k, a + i, (na - i) * sizeof(doc_t));
	k += na - i;
	copy_bytes(out + k, b + j, (nb - j) * sizeof(doc_t));
	return k + (nb - j);
}

inline std::size_t subtract(const doc_t* a, std::size_t na, const doc_t* b, std::size_t nb, doc_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	while (i < na && j < nb) {
		doc_t x = a[i], y = b[j];
		out[k] = x;
		k += x < y;
		i += x <= y;
		j += y <= x;
	}
	copy_bytes(out + k, a + i, (na - i) * sizeof(doc_t));
	return k + (na - i);
}

}  // namespace detail

inline sorted_set operator|(const sorted_set& a, const sorted_set& b) {
	std::vector<doc_t> out(a.count() + b.count());
	out.resize(detail::unite(a.docs().data(), a.docs().size(), b.docs().data(), b.docs().size(), out.data()));
	return sorted_set(a.universe(), std::move(out));
}

inline sorted_set operator-(const sorted_set& a, const sorted_set& b) {
	std::vector<doc_t> out(a.count());
	out.resize(detail::subtract(a.docs().data(), a.docs().size(), b.docs().data(), b.docs().size(), out.data()));
	return sorted_set(a.universe(), std::move(out));
}

namespace detail {

// Allocator that leaves trivially constructible elements uninitialized, so result
// buffers about to be overwritten are not zeroed first.
template <typename T>
struct uninitialized_allocator : std::allocator<T> {
	template <typename U>
	struct rebind { using other = uninitialized_allocator<U>; };
	uninitialized_allocator() = default;
	template <typename U>
	uninitialized_allocator(const uninitialized_allocator<U>&) noexcept {}
	template <typename U, typename... Args>
	void construct(U* p, Args&&... args) {
		if constexpr (sizeof...(Args) == 0) { ::new (static_cast<void*>(p)) U; } else { ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...); }
	}
};

template <typename T>
using buffer = std::vector<T, uninitialized_allocator<T>>;

// ---- 16-bit sorted arrays --------------------------------------------------------------

// Shuffle masks that compact the lanes selected by an 8-bit mask to the front.
inline constexpr auto compact16 = [] {
	std::array<std::array<std::uint8_t, 16>, 256> t{};
	for (unsigned m = 0; m < 256; ++m) {
		unsigned k = 0;
		for (unsigned lane = 0; lane < 8; ++lane) {
			if (m >> lane & 1) {
				t[m][2 * k] = std::uint8_t(2 * lane);
				t[m][2 * k + 1] = std::uint8_t(2 * lane + 1);
				++k;
			}
		}
		for (unsigned b = 2 * k; b < 16; ++b) { t[m][b] = 0x80; }
	}
	return t;
}();

inline std::size_t intersect16_scalar(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	while (i < na && j < nb) {
		std::uint16_t x = a[i], y = b[j];
		out[k] = x;
		k += x == y;
		i += x <= y;
		j += y <= x;
	}
	return k;
}

#if DOCSET_SSE
// 8×8 all-pairs compare per step: b rotated through its eight positions against a.
__attribute__((target("ssse3"))) inline std::size_t intersect16_ssse3(
	const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	const __m128i zero = _mm_setzero_si128();
	while (i + 8 <= na && j + 8 <= nb) {
		__m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
		__m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + j));
		__m128i eq = _mm_or_si128(
			_mm_or_si128(_mm_or_si128(_mm_cmpeq_epi16(va, vb), _mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 2))),
				_mm_or_si128(_mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 4)), _mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 6)))),
			_mm_or_si128(_mm_or_si128(_mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 8)), _mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 10))),
				_mm_or_si128(_mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 12)), _mm_cmpeq_epi16(va, _mm_alignr_epi8(vb, vb, 14)))));
		unsigned mask = unsigned(_mm_movemask_epi8(_mm_packs_epi16(eq, zero)));
		__m128i shuffle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(compact16[mask].data()));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(out + k), _mm_shuffle_epi8(va, shuffle));
		k += unsigned(std::popcount(mask));
		std::uint16_t amax = a[i + 7], bmax = b[j + 7];
		i += amax <= bmax ? 8 : 0;
		j += bmax <= amax ? 8 : 0;
	}
	return k + intersect16_scalar(a + i, na - i, b + j, nb - j, out + k);
}
// pcmpestrm answers "which lanes of a equal any lane of b" for 8×8 16-bit values in one instruction.
__attribute__((target("sse4.2"))) inline unsigned equal_any16(__m128i va, __m128i vb) noexcept {
	return unsigned(_mm_cvtsi128_si32(_mm_cmpestrm(vb, 8, va, 8, _SIDD_UWORD_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK)));
}

__attribute__((target("sse4.2"))) inline std::size_t intersect16_sse42(
	const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	while (i + 8 <= na && j + 8 <= nb) {
		__m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
		__m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + j));
		unsigned mask = equal_any16(va, vb);
		__m128i shuffle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(compact16[mask].data()));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(out + k), _mm_shuffle_epi8(va, shuffle));
		k += unsigned(std::popcount(mask));
		std::uint16_t amax = a[i + 7], bmax = b[j + 7];
		i += amax <= bmax ? 8 : 0;
		j += bmax <= amax ? 8 : 0;
	}
	return k + intersect16_scalar(a + i, na - i, b + j, nb - j, out + k);
}
#elif DOCSET_NEON
inline std::size_t intersect16_neon(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	static constexpr std::uint16_t lane_bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};
	const uint16x8_t bits = vld1q_u16(lane_bits);
	while (i + 8 <= na && j + 8 <= nb) {
		uint16x8_t va = vld1q_u16(a + i), vb = vld1q_u16(b + j);
		uint16x8_t eq = vorrq_u16(
			vorrq_u16(vorrq_u16(vceqq_u16(va, vb), vceqq_u16(va, vextq_u16(vb, vb, 1))),
				vorrq_u16(vceqq_u16(va, vextq_u16(vb, vb, 2)), vceqq_u16(va, vextq_u16(vb, vb, 3)))),
			vorrq_u16(vorrq_u16(vceqq_u16(va, vextq_u16(vb, vb, 4)), vceqq_u16(va, vextq_u16(vb, vb, 5))),
				vorrq_u16(vceqq_u16(va, vextq_u16(vb, vb, 6)), vceqq_u16(va, vextq_u16(vb, vb, 7)))));
		unsigned mask = vaddvq_u16(vandq_u16(eq, bits));
		uint8x16_t shuffle = vld1q_u8(compact16[mask].data());
		vst1q_u8(reinterpret_cast<std::uint8_t*>(out + k), vqtbl1q_u8(vreinterpretq_u8_u16(va), shuffle));
		k += unsigned(std::popcount(mask));
		std::uint16_t amax = a[i + 7], bmax = b[j + 7];
		i += amax <= bmax ? 8 : 0;
		j += bmax <= amax ? 8 : 0;
	}
	return k + intersect16_scalar(a + i, na - i, b + j, nb - j, out + k);
}
#endif

inline std::size_t gallop16(const std::uint16_t* a, std::size_t lo, std::size_t n, std::uint16_t target) noexcept {
	std::size_t step = 1, hi = lo;
	while (hi < n && a[hi] < target) { lo = hi + 1; hi += step; step <<= 1; }
	return std::size_t(std::lower_bound(a + lo, a + std::min(hi, n), target) - a);
}

// Intersection into out (room for min(na, nb) + 8).
inline std::size_t intersect16(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	if (na > nb) { std::swap(a, b); std::swap(na, nb); }
	if (na * 32 < nb) {
		std::size_t k = 0, j = 0;
		for (std::size_t i = 0; i < na && j < nb; ++i) {
			j = gallop16(b, j, nb, a[i]);
			if (j < nb && b[j] == a[i]) { out[k++] = a[i]; }
		}
		return k;
	}
#if DOCSET_SSE
	static const bool sse42 = __builtin_cpu_supports("sse4.2");
	static const bool ssse3 = __builtin_cpu_supports("ssse3");
	if (sse42) { return intersect16_sse42(a, na, b, nb, out); }
	if (ssse3) { return intersect16_ssse3(a, na, b, nb, out); }
#elif DOCSET_NEON
	return intersect16_neon(a, na, b, nb, out);
#endif
	return intersect16_scalar(a, na, b, nb, out);
}

inline std::size_t unite16_scalar(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	while (i < na && j < nb) {
		std::uint16_t x = a[i], y = b[j];
		out[k++] = x < y ? x : y;
		i += x <= y;
		j += y <= x;
	}
	copy_bytes(out + k, a + i, (na - i) * 2);
	k += na - i;
	copy_bytes(out + k, b + j, (nb - j) * 2);
	return k + (nb - j);
}

inline std::size_t subtract16_scalar(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	while (i < na && j < nb) {
		std::uint16_t x = a[i], y = b[j];
		out[k] = x;
		k += x < y;
		i += x <= y;
		j += y <= x;
	}
	copy_bytes(out + k, a + i, (na - i) * 2);
	return k + (na - i);
}

#if DOCSET_SSE
// Sorts a bitonic 8-lane vector: half-cleaners at distances 4, 2, 1.
__attribute__((target("sse4.1"))) inline __m128i bitonic_sort8(__m128i x) noexcept {
	__m128i y = _mm_shuffle_epi32(x, 0x4e);
	x = _mm_blend_epi16(_mm_min_epu16(x, y), _mm_max_epu16(x, y), 0xf0);
	y = _mm_shuffle_epi32(x, 0xb1);
	x = _mm_blend_epi16(_mm_min_epu16(x, y), _mm_max_epu16(x, y), 0xcc);
	y = _mm_shufflehi_epi16(_mm_shufflelo_epi16(x, 0xb1), 0xb1);
	return _mm_blend_epi16(_mm_min_epu16(x, y), _mm_max_epu16(x, y), 0xaa);
}

// Vectorized merge (Inoue and Taura): merge two sorted 8-vectors, emit the lower 8 with
// duplicates removed, and refill from the list whose head is smaller.
__attribute__((target("sse4.1"))) inline std::size_t unite16_sse41(
	const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	if (na < 8 || nb < 8) { return unite16_scalar(a, na, b, nb, out); }
	const __m128i reverse = _mm_setr_epi8(14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1);
	const __m128i zero = _mm_setzero_si128();
	__m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
	__m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
	std::size_t i = 8, j = 8, k = 0;
	__m128i last = zero;
	bool first = true;
	for (;;) {
		__m128i r = _mm_shuffle_epi8(vb, reverse);
		__m128i lo = bitonic_sort8(_mm_min_epu16(va, r));
		__m128i hi = bitonic_sort8(_mm_max_epu16(va, r));
		__m128i prev = _mm_alignr_epi8(lo, last, 14);  // [last[7], lo[0..6]]
		unsigned dup = unsigned(_mm_movemask_epi8(_mm_packs_epi16(_mm_cmpeq_epi16(lo, prev), zero)));
		if (first) { dup &= ~1u; first = false; }
		unsigned keep = ~dup & 0xff;
		__m128i shuffle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(compact16[keep].data()));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(out + k), _mm_shuffle_epi8(lo, shuffle));
		k += unsigned(std::popcount(keep));
		last = lo;
		vb = hi;
		bool take_a = i < na && (j >= nb || a[i] <= b[j]);
		if (take_a ? i + 8 > na : j + 8 > nb) { break; }
		if (take_a) {
			va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
			i += 8;
		} else {
			va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + j));
			j += 8;
		}
	}
	// Tail: the pending upper vector plus whatever remains of each list, all >= the last emission.
	alignas(16) std::uint16_t pending[8];
	_mm_store_si128(reinterpret_cast<__m128i*>(pending), vb);
	std::size_t np = 0;
	for (std::size_t p = 0; p < 8; ++p) {
		if (np == 0 || pending[p] != pending[np - 1]) { pending[np++] = pending[p]; }
	}
	std::uint16_t emitted = std::uint16_t(_mm_extract_epi16(last, 7));
	std::vector<std::uint16_t> scratch(np + (na - i));
	std::size_t ns = unite16_scalar(pending, np, a + i, na - i, scratch.data());
	std::size_t start = k;
	k += unite16_scalar(scratch.data(), ns, b + j, nb - j, out + k);
	if (k > start && out[start] == emitted) {
		std::memmove(out + start, out + start + 1, (k - start - 1) * 2);
		--k;
	}
	return k;
}
#endif

#if DOCSET_SSE
// Walks 8-lane blocks of a and b like the intersection (advance whichever block ends
// first), accumulating which lanes of the current a block matched; an a block is emitted,
// minus its matched lanes, when the walk moves past it.
__attribute__((target("sse4.2"))) inline std::size_t subtract16_sse42(
	const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	auto emit = [&](__m128i va, unsigned matched) __attribute__((target("sse4.2"))) {
		unsigned keep = ~matched & 0xff;
		__m128i shuffle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(compact16[keep].data()));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(out + k), _mm_shuffle_epi8(va, shuffle));
		k += unsigned(std::popcount(keep));
	};
	if (na >= 8 && nb >= 8) {
		__m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a));
		__m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
		unsigned matched = 0;
		for (;;) {
			matched |= equal_any16(va, vb);
			const std::uint16_t amax = a[i + 7], bmax = b[j + 7];
			if (amax <= bmax) {
				emit(va, matched);
				matched = 0;
				i += 8;
				if (i + 8 > na) { break; }
				va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
			}
			if (bmax <= amax) {
				j += 8;
				if (j + 8 > nb) {
					// b has fewer than 8 left: finish the current a block against them one by one.
					// a may have moved to its next block in this same step, so bound by the current block.
					for (const std::uint16_t top = a[i + 7]; j < nb && b[j] <= top; ++j) { matched |= equal_any16(va, _mm_set1_epi16(short(b[j]))); }
					emit(va, matched);
					i += 8;
					break;
				}
				vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + j));
			}
		}
	}
	return k + subtract16_scalar(a + i, na - i, b + j, nb - j, out + k);
}
#elif DOCSET_NEON
inline std::size_t subtract16_neon(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	std::size_t i = 0, j = 0, k = 0;
	static constexpr std::uint16_t lane_bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};
	const uint16x8_t bits = vld1q_u16(lane_bits);
	for (; i + 8 <= na; i += 8) {
		uint16x8_t va = vld1q_u16(a + i);
		std::uint16_t lo = a[i], hi = a[i + 7];
		while (j < nb && b[j] < lo) { ++j; }
		uint16x8_t hit = vdupq_n_u16(0);
		for (; j < nb && b[j] <= hi; ++j) { hit = vorrq_u16(hit, vceqq_u16(va, vdupq_n_u16(b[j]))); }
		unsigned keep = ~unsigned(vaddvq_u16(vandq_u16(hit, bits))) & 0xff;
		vst1q_u8(reinterpret_cast<std::uint8_t*>(out + k), vqtbl1q_u8(vreinterpretq_u8_u16(va), vld1q_u8(compact16[keep].data())));
		k += unsigned(std::popcount(keep));
	}
	return k + subtract16_scalar(a + i, na - i, b + j, nb - j, out + k);
}
#endif

// Union into out (room for na + nb + 8).
inline std::size_t unite16(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
#if DOCSET_SSE
	static const bool sse41 = __builtin_cpu_supports("sse4.1");
	if (sse41) { return unite16_sse41(a, na, b, nb, out); }
#endif
	return unite16_scalar(a, na, b, nb, out);
}

// Difference a - b into out (room for na + 8).
inline std::size_t subtract16(const std::uint16_t* a, std::size_t na, const std::uint16_t* b, std::size_t nb, std::uint16_t* out) noexcept {
	if (nb * 32 < na) {  // few removals: copy runs between galloped hits
		std::size_t k = 0, i = 0;
		for (std::size_t j = 0; j < nb && i < na; ++j) {
			std::size_t at = gallop16(a, i, na, b[j]);
			copy_bytes(out + k, a + i, (at - i) * 2);
			k += at - i;
			i = at + (at < na && a[at] == b[j]);
		}
		copy_bytes(out + k, a + i, (na - i) * 2);
		return k + (na - i);
	}
#if DOCSET_SSE
	static const bool sse42 = __builtin_cpu_supports("sse4.2");
	if (sse42) { return subtract16_sse42(a, na, b, nb, out); }
#elif DOCSET_NEON
	return subtract16_neon(a, na, b, nb, out);
#endif
	return subtract16_scalar(a, na, b, nb, out);
}

// ---- 8 KiB bitmaps ---------------------------------------------------------------------

inline constexpr std::size_t chunk_bits = 1u << 16;
inline constexpr std::size_t bitmap_words = chunk_bits / 64;
inline constexpr std::uint32_t array_max = 4096;  // 4096 × 2 bytes = one 8 KiB bitmap

template <typename Op>
inline std::uint32_t combine_words_generic(const std::uint64_t* x, const std::uint64_t* y, std::uint64_t* z, Op op) noexcept {
	std::uint32_t n = 0;
	for (std::size_t i = 0; i < bitmap_words; ++i) {
		z[i] = op(x[i], y[i]);
		n += unsigned(std::popcount(z[i]));
	}
	return n;
}

#if DOCSET_SSE
template <typename Op>
__attribute__((target("avx2,popcnt"))) inline std::uint32_t combine_words_avx2(const std::uint64_t* x, const std::uint64_t* y, std::uint64_t* z, Op op) noexcept {
	std::uint32_t n = 0;
	for (std::size_t i = 0; i < bitmap_words; ++i) {
		z[i] = op(x[i], y[i]);
		n += unsigned(__builtin_popcountll(z[i]));
	}
	return n;
}
#endif

// z = op(x, y) over one bitmap; returns popcount(z).
template <typename Op>
inline std::uint32_t combine_words(const std::uint64_t* x, const std::uint64_t* y, std::uint64_t* z, Op op) noexcept {
#if DOCSET_SSE
	static const bool fast = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("popcnt");
	if (fast) { return combine_words_avx2(x, y, z, op); }
#endif
	return combine_words_generic(x, y, z, op);
}

// One 64Ki-ordinal chunk: a sorted array of low halves, or a bitmap past array_max members.
struct container {
	buffer<std::uint16_t> array;
	buffer<std::uint64_t> bitmap;
	std::uint32_t card = 0;

	bool is_bitmap() const noexcept { return !bitmap.empty(); }

	bool test(std::uint16_t low) const noexcept {
		if (is_bitmap()) { return (bitmap[low >> 6] >> (low & 63)) & 1; }
		return std::binary_search(array.begin(), array.end(), low);
	}

	template <typename F>
	void for_each(std::uint32_t high, F&& f) const {
		if (is_bitmap()) {
			for (std::size_t i = 0; i < bitmap_words; ++i) {
				for (std::uint64_t w = bitmap[i]; w; w &= w - 1) { f(doc_t(high | (i * 64 + unsigned(std::countr_zero(w))))); }
			}
		} else {
			for (auto low : array) { f(doc_t(high | low)); }
		}
	}

	static container from_array(buffer<std::uint16_t> values) {
		container c;
		c.card = std::uint32_t(values.size());
		c.array = std::move(values);
		return c;
	}

	// Keeps a bitmap result above array_max members, converts it to an array otherwise.
	static container from_bitmap(buffer<std::uint64_t> words, std::uint32_t card) {
		container c;
		c.card = card;
		if (card > array_max) {
			c.bitmap = std::move(words);
		} else {
			c.array.resize(card);
			std::size_t k = 0;
			for (std::size_t i = 0; i < bitmap_words; ++i) {
				for (std::uint64_t w = words[i]; w; w &= w - 1) { c.array[k++] = std::uint16_t(i * 64 + unsigned(std::countr_zero(w))); }
			}
		}
		return c;
	}

	buffer<std::uint64_t> words() const {
		if (is_bitmap()) { return bitmap; }
		buffer<std::uint64_t> w(bitmap_words);
		std::memset(w.data(), 0, bitmap_words * 8);
		for (auto low : array) { w[low >> 6] |= std::uint64_t(1) << (low & 63); }
		return w;
	}
};

inline container intersect(const container& a, const container& b) {
	if (a.is_bitmap() && b.is_bitmap()) {
		buffer<std::uint64_t> w(bitmap_words);
		auto n = combine_words(a.bitmap.data(), b.bitmap.data(), w.data(), [](std::uint64_t x, std::uint64_t y) { return x & y; });
		return container::from_bitmap(std::move(w), n);
	}
	if (!a.is_bitmap() && !b.is_bitmap()) {
		std::uint16_t scratch[array_max + 8];
		std::size_t n = intersect16(a.array.data(), a.card, b.array.data(), b.card, scratch);
		return container::from_array(buffer<std::uint16_t>(scratch, scratch + n));
	}
	const container& arr = a.is_bitmap() ? b : a;
	const container& bmp = a.is_bitmap() ? a : b;
	buffer<std::uint16_t> out(arr.card);
	std::size_t k = 0;
	for (auto low : arr.array) {
		out[k] = low;
		k += (bmp.bitmap[low >> 6] >> (low & 63)) & 1;
	}
	out.resize(k);
	return container::from_array(std::move(out));
}

inline container unite(const container& a, const container& b) {
	if (a.is_bitmap() && b.is_bitmap()) {
		buffer<std::uint64_t> w(bitmap_words);
		auto n = combine_words(a.bitmap.data(), b.bitmap.data(), w.data(), [](std::uint64_t x, std::uint64_t y) { return x | y; });
		return container::from_bitmap(std::move(w), n);
	}
	if (!a.is_bitmap() && !b.is_bitmap() && a.card + b.card <= array_max) {
		buffer<std::uint16_t> out(a.card + b.card + 8);
		out.resize(unite16(a.array.data(), a.card, b.array.data(), b.card, out.data()));
		return container::from_array(std::move(out));
	}
	const container& base = a.is_bitmap() ? a : b;
	const container& other = a.is_bitmap() ? b : a;
	buffer<std::uint64_t> w = base.words();
	std::uint32_t n = base.is_bitmap() ? base.card : 0;
	if (!base.is_bitmap()) { n = base.card; }
	for (auto low : other.array) {
		std::uint64_t bit = std::uint64_t(1) << (low & 63);
		n += !(w[low >> 6] & bit);
		w[low >> 6] |= bit;
	}
	return container::from_bitmap(std::move(w), n);
}

inline container subtract(const container& a, const container& b) {
	if (a.is_bitmap() && b.is_bitmap()) {
		buffer<std::uint64_t> w(bitmap_words);
		auto n = combine_words(a.bitmap.data(), b.bitmap.data(), w.data(), [](std::uint64_t x, std::uint64_t y) { return x & ~y; });
		return container::from_bitmap(std::move(w), n);
	}
	if (!a.is_bitmap()) {
		// Differences keep most of a, so write straight into a result sized for all of it.
		buffer<std::uint16_t> out(a.card + 8);
		std::size_t k = 0;
		if (b.is_bitmap()) {
			for (auto low : a.array) {
				out[k] = low;
				k += !((b.bitmap[low >> 6] >> (low & 63)) & 1);
			}
		} else {
			k = subtract16(a.array.data(), a.card, b.array.data(), b.card, out.data());
		}
		out.resize(k);
		return container::from_array(std::move(out));
	}
	buffer<std::uint64_t> w = a.bitmap;
	std::uint32_t n = a.card;
	for (auto low : b.array) {
		std::uint64_t bit = std::uint64_t(1) << (low & 63);
		n -= (w[low >> 6] & bit) != 0;
		w[low >> 6] &= ~bit;
	}
	return container::from_bitmap(std::move(w), n);
}

}  // namespace detail

// A set of ordinals split into 64Ki chunks; each chunk is a sorted array of 16-bit
// low halves or, past 4096 members, an 8 KiB bitmap (the Roaring layout, without run
// containers). Sparse and dense regions each take their cheaper form, and both
// shapes have SIMD-friendly operations.
class set {
public:
	set() = default;
	explicit set(doc_t universe) : universe_(universe) {}

	// Builds from strictly increasing ordinals.
	static set from_sorted(doc_t universe, const std::vector<doc_t>& docs) {
		set s(universe);
		std::size_t i = 0;
		while (i < docs.size()) {
			std::uint16_t high = std::uint16_t(docs[i] >> 16);
			std::size_t j = i;
			while (j < docs.size() && (docs[j] >> 16) == high) { ++j; }
			std::uint32_t n = std::uint32_t(j - i);
			if (n > detail::array_max) {
				detail::buffer<std::uint64_t> w(detail::bitmap_words);
				std::memset(w.data(), 0, detail::bitmap_words * 8);
				for (std::size_t k = i; k < j; ++k) { w[(docs[k] & 0xffff) >> 6] |= std::uint64_t(1) << (docs[k] & 63); }
				s.push(high, detail::container::from_bitmap(std::move(w), n));
			} else {
				detail::buffer<std::uint16_t> a(n);
				for (std::size_t k = i; k < j; ++k) { a[k - i] = std::uint16_t(docs[k]); }
				s.push(high, detail::container::from_array(std::move(a)));
			}
			i = j;
		}
		return s;
	}

	doc_t universe() const noexcept { return universe_; }
	std::uint64_t count() const noexcept { return count_; }
	std::size_t chunks() const noexcept { return keys_.size(); }

	bool test(doc_t d) const noexcept {
		auto it = std::lower_bound(keys_.begin(), keys_.end(), std::uint16_t(d >> 16));
		return it != keys_.end() && *it == (d >> 16) && containers_[std::size_t(it - keys_.begin())].test(std::uint16_t(d));
	}

	template <typename F>
	void for_each(F&& f) const {
		for (std::size_t c = 0; c < keys_.size(); ++c) { containers_[c].for_each(std::uint32_t(keys_[c]) << 16, f); }
	}

	bitset to_bitset() const {
		bitset r(universe_);
		for_each([&](doc_t d) { r.set(d); });
		return r;
	}

	friend set operator&(const set& a, const set& b) {
		set r(a.universe_);
		r.reserve(std::min(a.keys_.size(), b.keys_.size()));
		std::size_t i = 0, j = 0;
		while (i < a.keys_.size() && j < b.keys_.size()) {
			if (a.keys_[i] < b.keys_[j]) { ++i; continue; }
			if (b.keys_[j] < a.keys_[i]) { ++j; continue; }
			r.push(a.keys_[i], detail::intersect(a.containers_[i], b.containers_[j]));
			++i, ++j;
		}
		return r;
	}

	friend set operator|(const set& a, const set& b) {
		set r(a.universe_);
		r.reserve(a.keys_.size() + b.keys_.size());
		std::size_t i = 0, j = 0;
		while (i < a.keys_.size() || j < b.keys_.size()) {
			if (j == b.keys_.size() || (i < a.keys_.size() && a.keys_[i] < b.keys_[j])) {
				r.push(a.keys_[i], a.containers_[i]);
				++i;
			} else if (i == a.keys_.size() || b.keys_[j] < a.keys_[i]) {
				r.push(b.keys_[j], b.containers_[j]);
				++j;
			} else {
				r.push(a.keys_[i], detail::unite(a.containers_[i], b.containers_[j]));
				++i, ++j;
			}
		}
		return r;
	}

	friend set operator-(const set& a, const set& b) {
		set r(a.universe_);
		r.reserve(a.keys_.size());
		std::size_t j = 0;
		for (std::size_t i = 0; i < a.keys_.size(); ++i) {
			while (j < b.keys_.size() && b.keys_[j] < a.keys_[i]) { ++j; }
			if (j < b.keys_.size() && b.keys_[j] == a.keys_[i]) {
				r.push(a.keys_[i], detail::subtract(a.containers_[i], b.containers_[j]));
			} else {
				r.push(a.keys_[i], a.containers_[i]);
			}
		}
		return r;
	}

	set& operator&=(const set& o) { return *this = *this & o; }
	set& operator|=(const set& o) { return *this = *this | o; }
	set& operator-=(const set& o) { return *this = *this - o; }

	class iterator {
	public:
		explicit iterator(const set& s) noexcept : s_(&s) {}
		doc_t doc() const noexcept { return doc_; }
		doc_t next() noexcept {
			if (!started_) { started_ = true; return seek(0, 0); }
			if (doc_ == no_more_docs) { return doc_; }
			return doc_ == no_more_docs - 1 ? (doc_ = no_more_docs) : seek(c_, doc_ + 1);
		}
		doc_t advance(doc_t target) noexcept {
			started_ = true;
			return seek(c_, target);
		}
		std::uint64_t cost() const noexcept { return s_->count_; }

	private:
		// First member >= target, searching chunks from index c.
		doc_t seek(std::size_t c, doc_t target) noexcept {
			const auto& keys = s_->keys_;
			std::uint16_t high = std::uint16_t(target >> 16);
			while (c < keys.size() && keys[c] < high) { ++c; }
			for (; c < keys.size(); ++c) {
				std::uint32_t low = keys[c] == high ? (target & 0xffff) : 0;
				const auto& box = s_->containers_[c];
				if (box.is_bitmap()) {
					std::size_t w = low >> 6;
					std::uint64_t bits = box.bitmap[w] & (~std::uint64_t(0) << (low & 63));
					while (!bits && ++w < detail::bitmap_words) { bits = box.bitmap[w]; }
					if (bits) { c_ = c; return doc_ = doc_t(std::uint32_t(keys[c]) << 16 | (w * 64 + unsigned(std::countr_zero(bits)))); }
				} else {
					std::size_t from = c == c_ ? pos_ : 0;
					std::size_t at = detail::gallop16(box.array.data(), from, box.card, std::uint16_t(low));
					if (low > 0xffff) { at = box.card; }
					if (at < box.card) { c_ = c; pos_ = at; return doc_ = doc_t(std::uint32_t(keys[c]) << 16 | box.array[at]); }
				}
			}
			c_ = keys.size();
			return doc_ = no_more_docs;
		}
		const set* s_;
		std::size_t c_ = 0, pos_ = 0;
		doc_t doc_ = no_more_docs;
		bool started_ = false;
	};
	iterator begin_iteration() const noexcept { return iterator(*this); }

private:
	void reserve(std::size_t n) {
		keys_.reserve(n);
		containers_.reserve(n);
	}

	void push(std::uint16_t key, detail::container c) {
		if (c.card == 0) { return; }
		count_ += c.card;
		keys_.push_back(key);
		containers_.push_back(std::move(c));
	}

	std::vector<std::uint16_t> keys_;
	std::vector<detail::container> containers_;
	std::uint64_t count_ = 0;
	doc_t universe_ = 0;
};

}  // namespace docset

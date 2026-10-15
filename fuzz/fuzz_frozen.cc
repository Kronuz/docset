// libFuzzer target: arbitrary bytes either fail frozen_view::open or open a set whose rank
// and membership agree (rank(d + 1) - rank(d) is contains(d)) and never pass its
// cardinality; and any increasing docs the input names round-trip through freeze.
#include "docset.hh"

#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	if (auto view = docset::frozen_view::open(data, size)) {
		for (std::size_t i = 0; i + 4 <= std::min<std::size_t>(size, 256); i += 4) {
			docset::doc_t d; std::memcpy(&d, data + i, 4);
			if (d == docset::no_more_docs) { continue; }
			const auto r = view->rank(d), next = view->rank(d + 1);
			if (next - r != (view->contains(d) ? 1u : 0u) || next > view->cardinality()) { std::abort(); }
		}
	}
	std::vector<docset::doc_t> docs;
	docset::doc_t at = 0;
	for (std::size_t i = 0; i + 2 <= size; i += 2) {
		std::uint16_t step; std::memcpy(&step, data + i, 2);
		if (docset::no_more_docs - at <= docset::doc_t(step) + 1u) { break; }
		at += docset::doc_t(step) + 1u; docs.push_back(at);
	}
	auto bytes = docset::freeze(docs.data(), docs.size());
	auto view = docset::frozen_view::open(bytes.data(), bytes.size());
	if (!view || view->cardinality() != docs.size()) { std::abort(); }
	for (std::size_t k = 0; k < docs.size(); ++k) { if (view->rank(docs[k]) != k || !view->contains(docs[k])) { std::abort(); } }
	return 0;
}

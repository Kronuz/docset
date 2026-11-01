// Compiled to assembly by test/check_codegen.cmake: lower_bound_index must select with a
// conditional move (cmov, csel) instead of branching on the comparison.
#include "docset.hh"

extern "C" std::size_t docset_probe_lower_bound(const std::uint16_t* a, std::size_t n, std::uint16_t x) {
	return docset::detail::lower_bound_index(a, n, x);
}

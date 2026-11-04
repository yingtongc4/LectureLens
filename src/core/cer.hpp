#pragma once

#include <cstddef>
#include <string_view>

namespace ll {

// Levenshtein distance over code points, O(|a|*|b|) time, O(|b|) memory.
std::size_t edit_distance(std::u32string_view a, std::u32string_view b);

// Character error rate = edits / |reference|. Defined as 0 when both are
// empty and 1 when only the reference is empty.
double character_error_rate(std::u32string_view hypothesis, std::u32string_view reference);

// Length of the longest common subsequence, O(|a|*|b|) time, O(|b|) memory.
std::size_t lcs_length(std::u32string_view a, std::u32string_view b);

}  // namespace ll

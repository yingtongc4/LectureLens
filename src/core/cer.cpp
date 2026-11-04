#include "core/cer.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

namespace ll {

std::size_t edit_distance(std::u32string_view a, std::u32string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    std::iota(row.begin(), row.end(), std::size_t{0});
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

std::size_t lcs_length(std::u32string_view a, std::u32string_view b) {
    std::vector<std::size_t> row(b.size() + 1, 0);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = 0;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = a[i - 1] == b[j - 1] ? diag + 1 : std::max(row[j], row[j - 1]);
            diag = up;
        }
    }
    return row[b.size()];
}

double character_error_rate(std::u32string_view hypothesis, std::u32string_view reference) {
    if (reference.empty()) return hypothesis.empty() ? 0.0 : 1.0;
    return static_cast<double>(edit_distance(hypothesis, reference)) /
           static_cast<double>(reference.size());
}

}  // namespace ll

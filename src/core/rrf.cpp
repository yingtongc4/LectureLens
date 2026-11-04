#include "core/rrf.hpp"

#include <algorithm>
#include <unordered_map>

namespace ll {

std::vector<Fused> reciprocal_rank_fusion(const std::vector<std::int64_t>& a, const std::vector<std::int64_t>& b,
                                          int k) {
    std::unordered_map<std::int64_t, Fused> by_id;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto& f = by_id[a[i]];
        if (f.rank_a) continue;  // duplicate id: keep the best rank
        f.id = a[i];
        f.rank_a = static_cast<int>(i + 1);
        f.score += 1.0 / (k + f.rank_a);
    }
    for (std::size_t i = 0; i < b.size(); ++i) {
        auto& f = by_id[b[i]];
        if (f.rank_b) continue;
        f.id = b[i];
        f.rank_b = static_cast<int>(i + 1);
        f.score += 1.0 / (k + f.rank_b);
    }
    std::vector<Fused> out;
    out.reserve(by_id.size());
    for (auto& [id, f] : by_id) out.push_back(f);
    std::sort(out.begin(), out.end(),
              [](const Fused& x, const Fused& y) { return x.score != y.score ? x.score > y.score : x.id < y.id; });
    return out;
}

}  // namespace ll

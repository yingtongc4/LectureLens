#pragma once

#include <cstdint>
#include <vector>

namespace ll {

struct Fused {
    std::int64_t id = 0;
    double score = 0.0;
    int rank_a = 0;  // 1-based rank in list A, 0 if absent
    int rank_b = 0;  // 1-based rank in list B, 0 if absent
};

// Reciprocal Rank Fusion of two ranked id lists: score = sum 1 / (k + rank).
// Rank-based, so the lists' scores (cosine vs ts_rank) need no calibration.
// Ties break on the smaller id for a deterministic order.
std::vector<Fused> reciprocal_rank_fusion(const std::vector<std::int64_t>& a, const std::vector<std::int64_t>& b,
                                          int k = 60);

}  // namespace ll

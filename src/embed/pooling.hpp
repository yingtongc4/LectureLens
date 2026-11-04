#pragma once

#include <cstdint>
#include <vector>

namespace ll::embed {

// Mean-pools token states over the attention mask and L2-normalizes, giving
// one unit vector per sequence (E5 / sentence-transformers pooling).
// hidden: [batch][tokens][dim] row-major; mask: [batch][tokens] of 0/1.
std::vector<std::vector<float>> mean_pool_normalize(const float* hidden, int batch, int tokens, int dim,
                                                    const std::vector<std::int64_t>& mask);

// Dot product of two equal-length vectors (cosine for unit vectors).
float dot(const std::vector<float>& a, const std::vector<float>& b);

}  // namespace ll::embed

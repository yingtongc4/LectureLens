#include "embed/pooling.hpp"

#include <cmath>

namespace ll::embed {

std::vector<std::vector<float>> mean_pool_normalize(const float* hidden, int batch, int tokens, int dim,
                                                    const std::vector<std::int64_t>& mask) {
    std::vector<std::vector<float>> out(batch, std::vector<float>(dim, 0.0f));
    for (int b = 0; b < batch; ++b) {
        auto& v = out[b];
        int count = 0;
        for (int t = 0; t < tokens; ++t) {
            if (!mask[static_cast<std::size_t>(b) * tokens + t]) continue;
            const float* h = hidden + (static_cast<std::size_t>(b) * tokens + t) * dim;
            for (int d = 0; d < dim; ++d) v[d] += h[d];
            ++count;
        }
        double norm = 0.0;
        for (auto& x : v) {
            if (count) x /= count;
            norm += static_cast<double>(x) * x;
        }
        if (norm > 0) {
            const auto inv = static_cast<float>(1.0 / std::sqrt(norm));
            for (auto& x : v) x *= inv;
        }
    }
    return out;
}

float dot(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0.0f;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) s += a[i] * b[i];
    return s;
}

}  // namespace ll::embed

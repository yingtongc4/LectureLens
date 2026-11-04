#include "ocr/ctc.hpp"

#include <sstream>

namespace ll::ocr {

Decoded ctc_greedy_decode(const float* probs, int steps, int classes,
                          const std::vector<std::string>& labels) {
    Decoded out;
    int prev = -1;
    float conf_sum = 0.0f;
    int emitted = 0;
    for (int t = 0; t < steps; ++t) {
        const float* row = probs + static_cast<std::size_t>(t) * classes;
        int best = 0;
        for (int c = 1; c < classes; ++c) {
            if (row[c] > row[best]) best = c;
        }
        if (best != 0 && best != prev && best < static_cast<int>(labels.size())) {
            out.text += labels[best];
            conf_sum += row[best];
            ++emitted;
        }
        prev = best;
    }
    out.confidence = emitted ? conf_sum / emitted : 0.0f;
    return out;
}

std::vector<std::string> make_ctc_labels(const std::string& charset) {
    std::vector<std::string> labels{"<blank>"};
    std::istringstream in(charset);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        labels.push_back(line);
    }
    labels.emplace_back(" ");
    return labels;
}

}  // namespace ll::ocr

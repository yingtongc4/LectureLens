#pragma once

#include <string>
#include <vector>

namespace ll::ocr {

struct Decoded {
    std::string text;
    float confidence = 0.0f;  // mean max-probability of the emitted characters
};

// Greedy CTC decoding of one sequence: argmax per time step, merge repeats,
// drop blanks. `probs` is row-major [steps][classes]; labels[0] is the blank.
Decoded ctc_greedy_decode(const float* probs, int steps, int classes,
                          const std::vector<std::string>& labels);

// PaddleOCR label table: blank, one entry per line of `charset`, then " ".
std::vector<std::string> make_ctc_labels(const std::string& charset);

}  // namespace ll::ocr

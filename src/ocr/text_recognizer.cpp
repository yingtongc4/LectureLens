#include "ocr/text_recognizer.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace ll::ocr {

TextRecognizer::TextRecognizer(const Model& model, RecognizerOptions opts)
    : model_(model), opts_(opts) {
    const auto charset = model_.metadata("character");
    if (!charset) throw std::runtime_error("recognition model has no 'character' metadata");
    labels_ = make_ctc_labels(*charset);
}

std::vector<Decoded> TextRecognizer::recognize(const std::vector<cv::Mat>& crops) const {
    std::vector<Decoded> results(crops.size());

    // Batch crops of similar aspect ratio together to limit padding.
    std::vector<int> order(crops.size());
    std::iota(order.begin(), order.end(), 0);
    auto ratio = [&](int i) { return static_cast<double>(crops[i].cols) / std::max(1, crops[i].rows); };
    std::sort(order.begin(), order.end(), [&](int a, int b) { return ratio(a) < ratio(b); });

    const int H = opts_.height;
    for (std::size_t start = 0; start < order.size(); start += opts_.batch_size) {
        const auto end = std::min(order.size(), start + opts_.batch_size);
        double max_ratio = static_cast<double>(opts_.base_width) / H;
        for (auto k = start; k < end; ++k) max_ratio = std::max(max_ratio, ratio(order[k]));
        const int W = static_cast<int>(std::ceil(H * max_ratio));

        TensorF in;
        const auto n = static_cast<std::int64_t>(end - start);
        in.shape = {n, 3, H, W};
        in.data.assign(static_cast<std::size_t>(n) * 3 * H * W, 0.0f);  // zero = padding
        const std::size_t plane = static_cast<std::size_t>(H) * W;

        for (auto k = start; k < end; ++k) {
            const auto& crop = crops[order[k]];
            if (crop.empty()) continue;
            const int w = std::min(W, static_cast<int>(std::ceil(H * ratio(order[k]))));
            cv::Mat resized;
            cv::resize(crop, resized, cv::Size(std::max(1, w), H));
            float* dst = in.data.data() + (k - start) * 3 * plane;
            for (int y = 0; y < H; ++y) {
                const auto* row = resized.ptr<cv::Vec3b>(y);
                for (int x = 0; x < resized.cols; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        dst[c * plane + static_cast<std::size_t>(y) * W + x] = row[x][c] / 127.5f - 1.0f;
                    }
                }
            }
        }

        const auto out = model_.run(in);  // [n, steps, classes]
        const int steps = static_cast<int>(out.shape[1]);
        const int classes = static_cast<int>(out.shape[2]);
        for (auto k = start; k < end; ++k) {
            const float* probs = out.data.data() + (k - start) * static_cast<std::size_t>(steps) * classes;
            results[order[k]] = ctc_greedy_decode(probs, steps, classes, labels_);
        }
    }
    return results;
}

}  // namespace ll::ocr

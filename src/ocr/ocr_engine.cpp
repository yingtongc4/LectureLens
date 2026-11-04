#include "ocr/ocr_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ll::ocr {
namespace {

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

Box bounding_box(const TextBox& b) {
    float x0 = b.pts[0].x, y0 = b.pts[0].y, x1 = x0, y1 = y0;
    for (const auto& p : b.pts) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    return {static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(std::ceil(x1 - x0)),
            static_cast<int>(std::ceil(y1 - y0))};
}

}  // namespace

OcrEngine::OcrEngine(const std::filesystem::path& models_dir, Backend backend, int threads, OcrOptions opts)
    : opts_(opts),
      det_model_(std::make_unique<OrtModel>(models_dir / "ch_PP-OCRv4_det_infer.onnx", backend, threads)),
      rec_model_(std::make_unique<OrtModel>(models_dir / "ch_PP-OCRv4_rec_infer.onnx", backend, threads)),
      detector_(std::make_unique<TextDetector>(*det_model_, opts_.det)),
      recognizer_(std::make_unique<TextRecognizer>(*rec_model_, opts_.rec)) {}

OcrResult OcrEngine::run(const cv::Mat& bgr) const {
    OcrResult result;

    auto t0 = std::chrono::steady_clock::now();
    const auto boxes = detector_->detect(bgr);
    result.det_ms = ms_since(t0);

    t0 = std::chrono::steady_clock::now();
    std::vector<cv::Mat> crops;
    crops.reserve(boxes.size());
    for (const auto& b : boxes) crops.push_back(crop_box(bgr, b));
    const auto texts = recognizer_->recognize(crops);
    result.rec_ms = ms_since(t0);

    std::vector<OcrLine> lines;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        if (texts[i].text.empty() || texts[i].confidence < opts_.drop_score) continue;
        lines.push_back({texts[i].text, texts[i].confidence, bounding_box(boxes[i])});
    }

    std::vector<Box> line_boxes;
    for (const auto& l : lines) line_boxes.push_back(l.box);
    for (const auto& row : reading_order(line_boxes)) {
        for (const int i : row) result.lines.push_back(std::move(lines[i]));
    }
    return result;
}

}  // namespace ll::ocr

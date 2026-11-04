#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <opencv2/core.hpp>

#include "inference/model.hpp"
#include "ocr/layout.hpp"
#include "ocr/text_detector.hpp"
#include "ocr/text_recognizer.hpp"

namespace ll::ocr {

struct OcrResult {
    std::vector<OcrLine> lines;  // in reading order
    double det_ms = 0.0;
    double rec_ms = 0.0;
};

struct OcrOptions {
    DetectorOptions det;
    RecognizerOptions rec;
    float drop_score = 0.5f;  // discard recognized lines below this confidence
};

// Detection + recognition over one image.
class OcrEngine {
public:
    OcrEngine(const std::filesystem::path& models_dir, Backend backend, int threads = 0, OcrOptions opts = {});
    OcrResult run(const cv::Mat& bgr) const;

private:
    OcrOptions opts_;
    std::unique_ptr<Model> det_model_;
    std::unique_ptr<Model> rec_model_;
    std::unique_ptr<TextDetector> detector_;
    std::unique_ptr<TextRecognizer> recognizer_;
};

}  // namespace ll::ocr

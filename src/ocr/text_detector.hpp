#pragma once

#include <array>
#include <vector>

#include <opencv2/core.hpp>

#include "inference/ort_model.hpp"

namespace ll::ocr {

struct TextBox {
    std::array<cv::Point2f, 4> pts;  // top-left, top-right, bottom-right, bottom-left
    float score = 0.0f;
};

struct DetectorOptions {
    int limit_side = 960;       // longest side fed to the model
    float bin_thresh = 0.3f;    // pixel probability -> text mask
    float box_thresh = 0.6f;    // mean probability inside a box to keep it
    float unclip_ratio = 1.5f;  // DBNet shrinks text regions; grow them back
    float min_size = 3.0f;
    int max_candidates = 1000;
};

// PP-OCR DBNet text detector: image -> probability map -> rotated boxes.
class TextDetector {
public:
    TextDetector(const OrtModel& model, DetectorOptions opts = {});
    std::vector<TextBox> detect(const cv::Mat& bgr) const;

private:
    const OrtModel& model_;
    DetectorOptions opts_;
};

// Perspective-crops one box out of the image, upright.
cv::Mat crop_box(const cv::Mat& bgr, const TextBox& box);

}  // namespace ll::ocr

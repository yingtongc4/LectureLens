#include "ocr/text_detector.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace ll::ocr {
namespace {

constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};

int round_to_32(double v) { return std::max(32, static_cast<int>(std::lround(v / 32.0)) * 32); }

// HWC uint8 BGR -> NCHW float, ImageNet-normalized (PaddleOCR keeps BGR order).
TensorF to_tensor(const cv::Mat& img) {
    TensorF t;
    t.shape = {1, 3, img.rows, img.cols};
    const std::size_t plane = static_cast<std::size_t>(img.rows) * img.cols;
    t.data.resize(3 * plane);
    for (int y = 0; y < img.rows; ++y) {
        const auto* row = img.ptr<cv::Vec3b>(y);
        for (int x = 0; x < img.cols; ++x) {
            const std::size_t o = static_cast<std::size_t>(y) * img.cols + x;
            for (int c = 0; c < 3; ++c) {
                t.data[c * plane + o] = (row[x][c] / 255.0f - kMean[c]) / kStd[c];
            }
        }
    }
    return t;
}

std::array<cv::Point2f, 4> order_points(const cv::RotatedRect& r) {
    cv::Point2f p[4];
    r.points(p);
    std::sort(p, p + 4, [](const auto& a, const auto& b) { return a.x < b.x; });
    // Two leftmost points: upper is top-left. Two rightmost: upper is top-right.
    const auto [tl, bl] = p[0].y <= p[1].y ? std::pair{p[0], p[1]} : std::pair{p[1], p[0]};
    const auto [tr, br] = p[2].y <= p[3].y ? std::pair{p[2], p[3]} : std::pair{p[3], p[2]};
    return {tl, tr, br, bl};
}

// Mean probability inside the box polygon.
float box_score(const cv::Mat& prob, const std::array<cv::Point2f, 4>& pts) {
    cv::Rect bound = cv::boundingRect(std::vector<cv::Point2f>(pts.begin(), pts.end()));
    bound &= cv::Rect(0, 0, prob.cols, prob.rows);
    if (bound.empty()) return 0.0f;
    cv::Mat mask = cv::Mat::zeros(bound.size(), CV_8UC1);
    std::vector<cv::Point> poly;
    for (const auto& p : pts) poly.emplace_back(cvRound(p.x) - bound.x, cvRound(p.y) - bound.y);
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{poly}, cv::Scalar(1));
    return static_cast<float>(cv::mean(prob(bound), mask)[0]);
}

}  // namespace

TextDetector::TextDetector(const Model& model, DetectorOptions opts) : model_(model), opts_(opts) {}

std::vector<TextBox> TextDetector::detect(const cv::Mat& bgr) const {
    const double scale = std::min(1.0, static_cast<double>(opts_.limit_side) / std::max(bgr.cols, bgr.rows));
    const int w = round_to_32(bgr.cols * scale);
    const int h = round_to_32(bgr.rows * scale);
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(w, h));

    const auto out = model_.run(to_tensor(resized));  // [1, 1, h, w]
    const cv::Mat prob(h, w, CV_32FC1, const_cast<float*>(out.data.data()));
    cv::Mat bitmap = prob > opts_.bin_thresh;

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    const float sx = static_cast<float>(bgr.cols) / w;
    const float sy = static_cast<float>(bgr.rows) / h;
    std::vector<TextBox> boxes;
    const auto n = std::min<std::size_t>(contours.size(), opts_.max_candidates);
    for (std::size_t i = 0; i < n; ++i) {
        const auto rect = cv::minAreaRect(contours[i]);
        if (std::min(rect.size.width, rect.size.height) < opts_.min_size) continue;
        const float score = box_score(prob, order_points(rect));
        if (score < opts_.box_thresh) continue;

        // Unclip: offset the rectangle outward by area * ratio / perimeter.
        const float area = rect.size.area();
        const float perimeter = 2.0f * (rect.size.width + rect.size.height);
        const float d = area * opts_.unclip_ratio / perimeter;
        const cv::RotatedRect grown(rect.center, cv::Size2f(rect.size.width + 2 * d, rect.size.height + 2 * d),
                                    rect.angle);
        if (std::min(grown.size.width, grown.size.height) < opts_.min_size + 2) continue;

        TextBox box;
        box.score = score;
        box.pts = order_points(grown);
        for (auto& p : box.pts) {
            p.x = std::clamp(p.x * sx, 0.0f, static_cast<float>(bgr.cols - 1));
            p.y = std::clamp(p.y * sy, 0.0f, static_cast<float>(bgr.rows - 1));
        }
        boxes.push_back(box);
    }
    return boxes;
}

cv::Mat crop_box(const cv::Mat& bgr, const TextBox& box) {
    const auto& [tl, tr, br, bl] = box.pts;
    const int w = static_cast<int>(std::max(cv::norm(tl - tr), cv::norm(bl - br)));
    const int h = static_cast<int>(std::max(cv::norm(tl - bl), cv::norm(tr - br)));
    if (w < 1 || h < 1) return {};

    const cv::Point2f src[4] = {tl, tr, br, bl};
    const cv::Point2f dst[4] = {{0, 0}, {static_cast<float>(w), 0},
                                {static_cast<float>(w), static_cast<float>(h)}, {0, static_cast<float>(h)}};
    cv::Mat crop;
    cv::warpPerspective(bgr, crop, cv::getPerspectiveTransform(src, dst), cv::Size(w, h), cv::INTER_CUBIC,
                        cv::BORDER_REPLICATE);
    if (crop.rows >= crop.cols * 1.5) cv::rotate(crop, crop, cv::ROTATE_90_COUNTERCLOCKWISE);
    return crop;
}

}  // namespace ll::ocr

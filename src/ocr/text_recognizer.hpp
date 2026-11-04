#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include "inference/model.hpp"
#include "ocr/ctc.hpp"

namespace ll::ocr {

struct RecognizerOptions {
    int height = 48;       // PP-OCRv4 rec input height
    int base_width = 320;  // minimum batch width, as in PaddleOCR
    int batch_size = 6;
};

// PP-OCR SVTR/CRNN text-line recognizer with CTC decoding.
class TextRecognizer {
public:
    // Reads the character set from the model's "character" metadata.
    TextRecognizer(const Model& model, RecognizerOptions opts = {});

    // Recognizes each crop; output order matches input order.
    std::vector<Decoded> recognize(const std::vector<cv::Mat>& crops) const;

    std::size_t label_count() const { return labels_.size(); }

private:
    const Model& model_;
    RecognizerOptions opts_;
    std::vector<std::string> labels_;
};

}  // namespace ll::ocr

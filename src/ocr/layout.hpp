#pragma once

#include <string>
#include <vector>

namespace ll::ocr {

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

struct OcrLine {
    std::string text;
    float confidence = 0.0f;
    Box box;
};

// Orders detected text boxes for reading: boxes whose vertical extents
// overlap by at least half the shorter height form one row; rows go top to
// bottom and boxes within a row left to right. Returns indices into `boxes`,
// one vector per row.
std::vector<std::vector<int>> reading_order(const std::vector<Box>& boxes);

// Joins lines in reading order: spaces within a row, newlines between rows.
std::string join_reading_order(const std::vector<OcrLine>& lines);

}  // namespace ll::ocr

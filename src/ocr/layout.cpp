#include "ocr/layout.hpp"

#include <algorithm>
#include <numeric>

namespace ll::ocr {

std::vector<std::vector<int>> reading_order(const std::vector<Box>& boxes) {
    std::vector<int> idx(boxes.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        const int ca = boxes[a].y + boxes[a].h / 2, cb = boxes[b].y + boxes[b].h / 2;
        return ca != cb ? ca < cb : boxes[a].x < boxes[b].x;
    });

    std::vector<std::vector<int>> rows;
    int row_top = 0, row_bottom = 0;
    for (const int i : idx) {
        const auto& b = boxes[i];
        bool same_row = false;
        if (!rows.empty()) {
            const int overlap = std::min(row_bottom, b.y + b.h) - std::max(row_top, b.y);
            const int shorter = std::min(row_bottom - row_top, b.h);
            same_row = shorter > 0 && overlap * 2 >= shorter;
        }
        if (same_row) {
            rows.back().push_back(i);
            row_top = std::min(row_top, b.y);
            row_bottom = std::max(row_bottom, b.y + b.h);
        } else {
            rows.push_back({i});
            row_top = b.y;
            row_bottom = b.y + b.h;
        }
    }
    for (auto& row : rows) {
        std::sort(row.begin(), row.end(), [&](int a, int b) { return boxes[a].x < boxes[b].x; });
    }
    return rows;
}

std::string join_reading_order(const std::vector<OcrLine>& lines) {
    std::vector<Box> boxes;
    boxes.reserve(lines.size());
    for (const auto& l : lines) boxes.push_back(l.box);

    std::string out;
    for (const auto& row : reading_order(boxes)) {
        if (!out.empty()) out += '\n';
        for (std::size_t k = 0; k < row.size(); ++k) {
            if (k) out += ' ';
            out += lines[row[k]].text;
        }
    }
    return out;
}

}  // namespace ll::ocr

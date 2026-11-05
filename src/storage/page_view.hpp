#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <pqxx/pqxx>

namespace ll::repo {

struct OcrLineView {
    std::string text;
    int x = 0, y = 0, w = 0, h = 0;
};

// One slide as a reader sees it: the PDF text layer plus OCR lines.
struct PageView {
    std::int64_t document_id = 0;
    std::string document;
    std::string lecture;
    int page_no = 0;
    std::string text_layer;
    bool needs_ocr = false;
    std::vector<OcrLineView> ocr_lines;  // reading order, one OCR backend
};

std::optional<PageView> get_page(pqxx::transaction_base& tx, std::int64_t document_id, int page_no);

struct LectureView {
    std::int64_t document_id = 0;
    std::string document;
    int seq = 0;
    std::string title;
    int first_page = 0;
    int last_page = 0;
};

// All lectures, ordered by deck and position; optionally one deck only.
std::vector<LectureView> list_lectures(pqxx::transaction_base& tx, std::optional<std::int64_t> document_id);

}  // namespace ll::repo

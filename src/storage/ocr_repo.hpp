#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <pqxx/pqxx>

#include "ocr/layout.hpp"

namespace ll::repo {

struct PageRef {
    std::int64_t page_id = 0;
    std::int64_t document_id = 0;
    int page_no = 0;
    bool needs_ocr = false;
    std::string source_path;
    std::string text_layer;
};

enum class PageFilter { NeedsOcr, HasTextLayer, All };

// Pages ordered by (document, page). `every` keeps one page in N (sampling).
std::vector<PageRef> list_pages(pqxx::work& tx, PageFilter filter, int limit, int every);

std::int64_t start_ocr_run(pqxx::work& tx, const std::string& kind, const std::string& backend,
                           const std::string& params_json);
void finish_ocr_run(pqxx::work& tx, std::int64_t run_id, const std::string& metrics_json);

// Replaces this backend's OCR lines for the page.
void replace_ocr_blocks(pqxx::work& tx, std::int64_t page_id, const std::string& backend,
                        const std::vector<ocr::OcrLine>& lines);

// Replaces the page's 'ocr' chunks (queuing outbox deletes/upserts).
void replace_ocr_chunks(pqxx::work& tx, std::int64_t page_id, const std::vector<std::string>& chunks);

struct PageScore {
    double cer = 0.0;
    double recall = 0.0;
    int extra_chars = 0;
};

void insert_page_result(pqxx::work& tx, std::int64_t run_id, std::int64_t page_id, int lines,
                        double det_ms, double rec_ms, const std::optional<PageScore>& score);

}  // namespace ll::repo

#include "storage/ocr_repo.hpp"

#include <algorithm>
#include <charconv>

#include "storage/document_repo.hpp"

namespace ll::repo {
namespace {

// Ubuntu 24.04's libpqxx 7.8 is built as C++17, but under C++20 its headers
// reference a std::source_location constructor that the library lacks, so
// floating-point parameters fail to link. Pass them as text instead; Postgres
// casts them to REAL.
std::string num(double v) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return {buf, r.ptr};
}

}  // namespace

std::vector<PageRef> list_pages(pqxx::work& tx, PageFilter filter, int limit, int every) {
    const char* where = filter == PageFilter::NeedsOcr       ? "p.needs_ocr"
                        : filter == PageFilter::HasTextLayer ? "NOT p.needs_ocr"
                                                             : "true";
    const auto r = tx.exec_params(
        std::string("SELECT id, document_id, page_no, needs_ocr, source_path, text_layer FROM ("
                    "  SELECT p.*, d.source_path, row_number() OVER (ORDER BY p.document_id, p.page_no) AS rn"
                    "  FROM pages p JOIN documents d ON d.id = p.document_id WHERE ") +
            where +
            ") s WHERE (rn - 1) % $1 = 0 ORDER BY document_id, page_no LIMIT $2",
        std::max(1, every), limit > 0 ? limit : 1'000'000);

    std::vector<PageRef> pages;
    pages.reserve(r.size());
    for (const auto& row : r) {
        pages.push_back({row[0].as<std::int64_t>(), row[1].as<std::int64_t>(), row[2].as<int>(),
                         row[3].as<bool>(), row[4].as<std::string>(), row[5].as<std::string>()});
    }
    return pages;
}

std::int64_t start_ocr_run(pqxx::work& tx, const std::string& kind, const std::string& backend,
                           const std::string& params_json) {
    return tx.exec_params("INSERT INTO ocr_runs (kind, backend, params) VALUES ($1, $2, $3::jsonb) RETURNING id",
                          kind, backend, params_json)
        .at(0)
        .at(0)
        .as<std::int64_t>();
}

void finish_ocr_run(pqxx::work& tx, std::int64_t run_id, const std::string& metrics_json) {
    tx.exec_params("UPDATE ocr_runs SET metrics = $2::jsonb, finished_at = now() WHERE id = $1", run_id,
                   metrics_json);
}

void replace_ocr_blocks(pqxx::work& tx, std::int64_t page_id, const std::string& backend,
                        const std::vector<ocr::OcrLine>& lines) {
    tx.exec_params("DELETE FROM ocr_blocks WHERE page_id = $1 AND backend = $2", page_id, backend);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        tx.exec_params(
            "INSERT INTO ocr_blocks (page_id, backend, line_no, text, confidence, x, y, w, h) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)",
            page_id, backend, static_cast<int>(i), l.text, num(l.confidence), l.box.x, l.box.y, l.box.w, l.box.h);
    }
}

void replace_ocr_chunks(pqxx::work& tx, std::int64_t page_id, const std::vector<std::string>& chunks) {
    tx.exec_params(
        "INSERT INTO outbox_events (aggregate, aggregate_id, version, op) "
        "SELECT 'chunk', id, version + 1, 'delete' FROM chunks WHERE page_id = $1 AND source = 'ocr'",
        page_id);
    tx.exec_params("DELETE FROM chunks WHERE page_id = $1 AND source = 'ocr'", page_id);
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        insert_chunk(tx, page_id, "ocr", static_cast<int>(i), chunks[i]);
    }
}

void insert_page_result(pqxx::work& tx, std::int64_t run_id, std::int64_t page_id, int lines,
                        double det_ms, double rec_ms, const std::optional<PageScore>& score) {
    // std::optional parameters hit the same libpqxx link problem as doubles,
    // so unscored pages use a statement with literal NULLs.
    if (score) {
        tx.exec_params(
            "INSERT INTO ocr_page_results (run_id, page_id, lines, det_ms, rec_ms, cer, recall, extra_chars) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
            run_id, page_id, lines, num(det_ms), num(rec_ms), num(score->cer), num(score->recall),
            score->extra_chars);
    } else {
        tx.exec_params(
            "INSERT INTO ocr_page_results (run_id, page_id, lines, det_ms, rec_ms) VALUES ($1, $2, $3, $4, $5)",
            run_id, page_id, lines, num(det_ms), num(rec_ms));
    }
}

}  // namespace ll::repo

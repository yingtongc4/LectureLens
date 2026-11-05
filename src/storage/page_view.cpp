#include "storage/page_view.hpp"

namespace ll::repo {

std::optional<PageView> get_page(pqxx::transaction_base& tx, std::int64_t document_id, int page_no) {
    const auto rows = tx.exec_params(
        "SELECT p.id, d.title, l.title, p.text_layer, p.needs_ocr FROM pages p "
        "JOIN documents d ON d.id = p.document_id JOIN lectures l ON l.id = p.lecture_id "
        "WHERE p.document_id = $1 AND p.page_no = $2",
        document_id, page_no);
    if (rows.empty()) return std::nullopt;
    const auto& r = rows[0];
    PageView v{document_id,         r[1].as<std::string>(), r[2].as<std::string>(), page_no,
               r[3].as<std::string>(), r[4].as<bool>(),     {}};

    // A page may have been OCR'd by several backends; show one of them.
    const auto blocks = tx.exec_params(
        "SELECT text, x, y, w, h FROM ocr_blocks WHERE page_id = $1 "
        "  AND backend = (SELECT min(backend) FROM ocr_blocks WHERE page_id = $1) "
        "ORDER BY line_no",
        r[0].as<std::int64_t>());
    for (const auto& b : blocks) {
        v.ocr_lines.push_back({b[0].as<std::string>(), b[1].as<int>(), b[2].as<int>(), b[3].as<int>(), b[4].as<int>()});
    }
    return v;
}

std::vector<LectureView> list_lectures(pqxx::transaction_base& tx, std::optional<std::int64_t> document_id) {
    // '' = all decks: optional parameters hit the libpqxx link problem noted
    // in storage/ocr_repo.cpp.
    const auto rows = tx.exec_params(
        "SELECT d.id, d.title, l.seq, l.title, l.first_page, l.last_page FROM lectures l "
        "JOIN documents d ON d.id = l.document_id "
        "WHERE NULLIF($1, '') IS NULL OR d.id = NULLIF($1, '')::bigint "
        "ORDER BY d.id, l.seq",
        document_id ? std::to_string(*document_id) : std::string());
    std::vector<LectureView> out;
    for (const auto& r : rows) {
        out.push_back({r[0].as<std::int64_t>(), r[1].as<std::string>(), r[2].as<int>(), r[3].as<std::string>(),
                       r[4].as<int>(), r[5].as<int>()});
    }
    return out;
}

}  // namespace ll::repo

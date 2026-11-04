#include "storage/document_repo.hpp"

namespace ll::repo {
namespace {

std::int64_t returning_id(const pqxx::result& r) { return r.at(0).at(0).as<std::int64_t>(); }

}  // namespace

std::optional<std::int64_t> find_document_by_sha(pqxx::work& tx, const std::string& sha256) {
    const auto r = tx.exec_params("SELECT id FROM documents WHERE sha256 = $1", sha256);
    if (r.empty()) return std::nullopt;
    return returning_id(r);
}

void delete_document(pqxx::work& tx, std::int64_t document_id) {
    tx.exec_params(
        "INSERT INTO outbox_events (aggregate, aggregate_id, version, op) "
        "SELECT 'chunk', c.id, c.version + 1, 'delete' "
        "FROM chunks c JOIN pages p ON p.id = c.page_id WHERE p.document_id = $1",
        document_id);
    tx.exec_params("DELETE FROM documents WHERE id = $1", document_id);
}

std::int64_t insert_document(pqxx::work& tx, const std::string& source_path,
                             const std::string& sha256, const std::string& title, int page_count) {
    return returning_id(tx.exec_params(
        "INSERT INTO documents (source_path, sha256, title, page_count) "
        "VALUES ($1, $2, $3, $4) RETURNING id",
        source_path, sha256, title, page_count));
}

std::int64_t insert_lecture(pqxx::work& tx, std::int64_t document_id, int seq,
                            const std::string& title, int first_page, int last_page) {
    return returning_id(tx.exec_params(
        "INSERT INTO lectures (document_id, seq, title, first_page, last_page) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id",
        document_id, seq, title, first_page, last_page));
}

std::int64_t insert_page(pqxx::work& tx, std::int64_t document_id, std::int64_t lecture_id,
                         int page_no, const std::string& text_layer, int visible_chars,
                         bool needs_ocr) {
    return returning_id(tx.exec_params(
        "INSERT INTO pages (document_id, lecture_id, page_no, text_layer, visible_chars, needs_ocr) "
        "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id",
        document_id, lecture_id, page_no, text_layer, visible_chars, needs_ocr));
}

std::int64_t insert_chunk(pqxx::work& tx, std::int64_t page_id, const std::string& source,
                          int ordinal, const std::string& content) {
    const auto id = returning_id(tx.exec_params(
        "INSERT INTO chunks (page_id, source, ordinal, content) "
        "VALUES ($1, $2, $3, $4) RETURNING id",
        page_id, source, ordinal, content));
    tx.exec_params(
        "INSERT INTO outbox_events (aggregate, aggregate_id, version, op) "
        "VALUES ('chunk', $1, 1, 'upsert')",
        id);
    return id;
}

}  // namespace ll::repo

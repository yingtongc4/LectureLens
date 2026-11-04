#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <pqxx/pqxx>

namespace ll::repo {

// All functions run inside the caller's transaction so an ingest either fully
// lands (rows + outbox events) or not at all.

std::optional<std::int64_t> find_document_by_sha(pqxx::work& tx, const std::string& sha256);

// Deletes a document and everything under it (ON DELETE CASCADE), and queues
// a Qdrant delete for each of its chunks.
void delete_document(pqxx::work& tx, std::int64_t document_id);

std::int64_t insert_document(pqxx::work& tx, const std::string& source_path,
                             const std::string& sha256, const std::string& title, int page_count);

std::int64_t insert_lecture(pqxx::work& tx, std::int64_t document_id, int seq,
                            const std::string& title, int first_page, int last_page);

std::int64_t insert_page(pqxx::work& tx, std::int64_t document_id, std::int64_t lecture_id,
                         int page_no, const std::string& text_layer, int visible_chars,
                         bool needs_ocr);

// Inserts a chunk and its 'upsert' outbox event.
std::int64_t insert_chunk(pqxx::work& tx, std::int64_t page_id, const std::string& source,
                          int ordinal, const std::string& content);

}  // namespace ll::repo

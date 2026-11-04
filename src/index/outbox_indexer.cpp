#include "index/outbox_indexer.hpp"

#include <chrono>
#include <map>
#include <pqxx/pqxx>

namespace ll::index {
namespace {

std::string pg_array(const std::vector<std::int64_t>& ids) {
    std::string s = "{";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(ids[i]);
    }
    return s + "}";
}

}  // namespace

std::string passage_text(const std::string& lecture_title, const std::string& content) {
    return lecture_title + "\n" + content;
}

OutboxIndexer::OutboxIndexer(IndexerOptions opts, const embed::TextEmbedder& embedder, vec::QdrantClient& qdrant)
    : opts_(std::move(opts)), embedder_(embedder), qdrant_(qdrant) {}

void OutboxIndexer::ensure_collection() {
    qdrant_.ensure_collection(kChunkCollection, embedder_.dim(), {"document_id", "lecture_id", "page_no"});
}

BatchResult OutboxIndexer::run_batch() {
    BatchResult r;
    pqxx::connection conn(opts_.dsn);

    std::vector<std::int64_t> event_ids;
    try {
        pqxx::work tx(conn);
        const auto events = tx.exec_params(
            "SELECT id, aggregate_id, op FROM outbox_events "
            "WHERE processed_at IS NULL AND NOT dead AND aggregate = 'chunk' AND next_attempt_at <= now() "
            "ORDER BY id LIMIT $1 FOR UPDATE SKIP LOCKED",
            opts_.batch_size);
        r.claimed = static_cast<int>(events.size());
        if (events.empty()) return r;

        // Last event per chunk wins: an upsert followed by a delete is a delete.
        std::map<std::int64_t, std::string> final_op;
        for (const auto& e : events) {
            event_ids.push_back(e[0].as<std::int64_t>());
            final_op[e[1].as<std::int64_t>()] = e[2].as<std::string>();
        }
        std::vector<std::int64_t> upsert_ids, delete_ids;
        for (const auto& [chunk_id, op] : final_op) (op == "upsert" ? upsert_ids : delete_ids).push_back(chunk_id);

        std::vector<vec::Point> points;
        if (!upsert_ids.empty()) {
            const auto rows = tx.exec_params(
                "SELECT c.id, c.content, c.source, c.ordinal, p.id, p.page_no, p.document_id, l.id, l.title "
                "FROM chunks c JOIN pages p ON p.id = c.page_id JOIN lectures l ON l.id = p.lecture_id "
                "WHERE c.id = ANY($1::bigint[])",
                pg_array(upsert_ids));
            r.skipped = static_cast<int>(upsert_ids.size() - rows.size());

            std::vector<std::string> texts;
            for (const auto& row : rows) texts.push_back(passage_text(row[8].as<std::string>(), row[1].as<std::string>()));
            const auto t0 = std::chrono::steady_clock::now();
            auto vectors = embedder_.embed_passages(texts);
            r.embed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

            for (int i = 0; i < rows.size(); ++i) {
                const auto& row = rows[i];
                points.push_back({row[0].as<std::int64_t>(),
                                  std::move(vectors[i]),
                                  {{"source", row[2].as<std::string>()},
                                   {"ordinal", row[3].as<int>()},
                                   {"page_id", row[4].as<std::int64_t>()},
                                   {"page_no", row[5].as<int>()},
                                   {"document_id", row[6].as<std::int64_t>()},
                                   {"lecture_id", row[7].as<std::int64_t>()},
                                   {"lecture", row[8].as<std::string>()}}});
            }
        }

        qdrant_.upsert(kChunkCollection, points);
        qdrant_.remove(kChunkCollection, delete_ids);
        r.upserted = static_cast<int>(points.size());
        r.deleted = static_cast<int>(delete_ids.size());

        tx.exec_params("UPDATE outbox_events SET processed_at = now() WHERE id = ANY($1::bigint[])",
                       pg_array(event_ids));
        tx.commit();
    } catch (const std::exception& e) {
        // The claim transaction rolled back; record the failure separately.
        r.failed = true;
        if (event_ids.empty()) throw;
        pqxx::work tx(conn);
        tx.exec_params(
            "UPDATE outbox_events SET attempts = attempts + 1, last_error = $2, "
            "  next_attempt_at = now() + make_interval(secs => power(2, attempts)), "
            "  dead = attempts + 1 >= $3 "
            "WHERE id = ANY($1::bigint[])",
            pg_array(event_ids), std::string(e.what()).substr(0, 500), opts_.max_attempts);
        tx.commit();
    }
    return r;
}

}  // namespace ll::index

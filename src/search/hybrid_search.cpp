#include "search/hybrid_search.hpp"

#include <chrono>
#include <pqxx/pqxx>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "core/rrf.hpp"
#include "index/outbox_indexer.hpp"

namespace ll::search {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string pg_array(const std::vector<std::int64_t>& ids) {
    std::string s = "{";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(ids[i]);
    }
    return s + "}";
}

}  // namespace

Mode parse_mode(const std::string& s) {
    if (s == "hybrid") return Mode::Hybrid;
    if (s == "vector") return Mode::Vector;
    if (s == "keyword") return Mode::Keyword;
    throw std::invalid_argument("unknown search mode: " + s);
}

HybridSearcher::HybridSearcher(std::string dsn, const embed::TextEmbedder& embedder, vec::QdrantClient& qdrant)
    : dsn_(std::move(dsn)), embedder_(embedder), qdrant_(qdrant) {}

std::vector<Result> HybridSearcher::search(const Query& q, Timings* timings) {
    Timings t;
    std::vector<std::int64_t> vector_ids, keyword_ids;

    if (q.mode != Mode::Keyword) {
        try {
            auto t0 = Clock::now();
            const auto v = embedder_.embed_query(q.text);
            t.embed_ms = ms_since(t0);

            t0 = Clock::now();
            nlohmann::json filter = nullptr;
            if (q.document_id) {
                filter = {{"must", {{{"key", "document_id"}, {"match", {{"value", *q.document_id}}}}}}};
            }
            for (const auto& h : qdrant_.search(index::kChunkCollection, v, q.candidates, filter)) {
                vector_ids.push_back(h.id);
            }
            t.vector_ms = ms_since(t0);
        } catch (const std::exception& e) {
            if (q.mode == Mode::Vector) throw;  // nothing to fall back to
            t.vector_error = e.what();
            vector_ids.clear();
        }
    }

    pqxx::connection conn(dsn_);
    pqxx::work tx(conn);

    if (q.mode != Mode::Vector) {
        const auto t0 = Clock::now();
        // A natural-language question rarely has every term on one slide, so
        // the lexemes are OR-ed and ts_rank_cd rewards pages matching more of
        // them. The document filter goes in as text ('' = none): optional
        // parameters hit the libpqxx link problem noted in storage/ocr_repo.cpp.
        const auto rows = tx.exec_params(
            "SELECT c.id FROM chunks c JOIN pages p ON p.id = c.page_id, "
            "     to_tsquery('english', replace(plainto_tsquery('english', $1)::text, ' & ', ' | ')) query "
            "WHERE c.content_tsv @@ query AND (NULLIF($2, '') IS NULL OR p.document_id = NULLIF($2, '')::bigint) "
            "ORDER BY ts_rank_cd(c.content_tsv, query) DESC, c.id LIMIT $3",
            q.text, q.document_id ? std::to_string(*q.document_id) : std::string(), q.candidates);
        for (const auto& r : rows) keyword_ids.push_back(r[0].as<std::int64_t>());
        t.keyword_ms = ms_since(t0);
    }

    const auto fused = reciprocal_rank_fusion(vector_ids, keyword_ids);
    std::vector<std::int64_t> ids;
    for (const auto& f : fused) ids.push_back(f.id);

    // Fetch every candidate (not just top_k): filtering and per-page dedup
    // below can drop some. Lecture cover pages and outline slides match any
    // topic of their lecture but answer nothing, so they are excluded.
    const auto t0 = Clock::now();
    const auto rows = tx.exec_params(
        "SELECT c.id, d.title, l.title, p.page_no, c.source, c.content, p.id, d.id "
        "FROM chunks c JOIN pages p ON p.id = c.page_id JOIN lectures l ON l.id = p.lecture_id "
        "JOIN documents d ON d.id = p.document_id "
        "WHERE c.id = ANY($1::bigint[]) AND p.page_no <> l.first_page "
        "  AND lower(split_part(p.text_layer, E'\\n', 1)) NOT IN ('outline', 'roadmap', 'road map')",
        pg_array(ids));
    std::unordered_map<std::int64_t, pqxx::row> by_id;
    for (const auto& r : rows) by_id.emplace(r[0].as<std::int64_t>(), r);
    t.fetch_ms = ms_since(t0);

    // One result per page: the text-layer and OCR chunks of a slide are
    // alternative views of the same citation; keep the better-ranked one.
    std::vector<Result> results;
    std::unordered_set<std::int64_t> seen_pages;
    for (const auto& f : fused) {
        if (static_cast<int>(results.size()) >= q.top_k) break;
        const auto it = by_id.find(f.id);
        if (it == by_id.end()) continue;  // filtered, or a stale vector point
        const auto& r = it->second;
        if (!seen_pages.insert(r[6].as<std::int64_t>()).second) continue;
        results.push_back({f.id, f.score, r[7].as<std::int64_t>(), f.rank_a, f.rank_b, r[1].as<std::string>(),
                           r[2].as<std::string>(), r[3].as<int>(), r[4].as<std::string>(), r[5].as<std::string>()});
    }
    if (timings) *timings = t;
    return results;
}

nlohmann::json to_json(const Result& r) {
    return {{"chunk_id", r.chunk_id},       {"score", r.score},     {"vector_rank", r.vector_rank},
            {"keyword_rank", r.keyword_rank}, {"document_id", r.document_id}, {"document", r.document},
            {"lecture", r.lecture},
            {"page", r.page_no},            {"source", r.source},   {"content", r.content}};
}

}  // namespace ll::search

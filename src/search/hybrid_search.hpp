#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "embed/text_embedder.hpp"
#include "vector/qdrant_client.hpp"

namespace ll::search {

enum class Mode { Hybrid, Vector, Keyword };
Mode parse_mode(const std::string& s);

struct Query {
    std::string text;
    int top_k = 10;
    Mode mode = Mode::Hybrid;
    std::optional<std::int64_t> document_id;  // restrict to one deck
    int candidates = 50;                      // per retriever, before fusion
};

struct Result {
    std::int64_t chunk_id = 0;
    double score = 0.0;
    std::int64_t document_id = 0;
    int vector_rank = 0;   // 0 = not retrieved by that retriever
    int keyword_rank = 0;
    std::string document;
    std::string lecture;
    int page_no = 0;
    std::string source;    // text_layer | ocr
    std::string content;
};

struct Timings {
    double embed_ms = 0, vector_ms = 0, keyword_ms = 0, fetch_ms = 0;
    // Set when hybrid search fell back to keyword-only because the vector
    // side (embedding or Qdrant) failed.
    std::string vector_error;
};

// Vector search in Qdrant + full-text search in PostgreSQL, fused with RRF,
// then joined back to PostgreSQL for citations (deck, lecture, page).
// PostgreSQL is the source of truth and is required; Qdrant is a derived
// index, so in hybrid mode a Qdrant failure degrades to keyword-only results
// (reported in Timings::vector_error) instead of failing the query.
class HybridSearcher {
public:
    HybridSearcher(std::string dsn, const embed::TextEmbedder& embedder, vec::QdrantClient& qdrant);

    std::vector<Result> search(const Query& q, Timings* timings = nullptr);

private:
    std::string dsn_;
    const embed::TextEmbedder& embedder_;
    vec::QdrantClient& qdrant_;
};

nlohmann::json to_json(const Result& r);

}  // namespace ll::search

#pragma once

#include <string>

#include "embed/text_embedder.hpp"
#include "vector/qdrant_client.hpp"

namespace ll::index {

inline constexpr const char* kChunkCollection = "chunks";

struct IndexerOptions {
    std::string dsn;
    int batch_size = 32;
    int max_attempts = 8;  // then the event is parked as dead
};

struct BatchResult {
    int claimed = 0;
    int upserted = 0;
    int deleted = 0;
    int skipped = 0;  // upsert for a chunk that no longer exists
    bool failed = false;
    double embed_ms = 0.0;
};

// Drains outbox_events into Qdrant. Each batch is claimed with
// FOR UPDATE SKIP LOCKED, so several indexers can run side by side; Qdrant
// writes are idempotent (point id = chunk id), so a batch that fails after a
// partial write can simply be retried with exponential backoff.
class OutboxIndexer {
public:
    OutboxIndexer(IndexerOptions opts, const embed::TextEmbedder& embedder, vec::QdrantClient& qdrant);

    void ensure_collection();

    // Processes one batch of due events.
    BatchResult run_batch();

private:
    IndexerOptions opts_;
    const embed::TextEmbedder& embedder_;
    vec::QdrantClient& qdrant_;
};

// The text that represents a chunk in vector space: lecture title for context,
// then the slide text.
std::string passage_text(const std::string& lecture_title, const std::string& content);

}  // namespace ll::index

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ll {

struct PdfIngestOptions {
    std::string dsn;
    bool force = false;             // re-ingest even if the same file is already stored
    std::size_t ocr_threshold = 80; // pages with fewer visible chars need OCR
    std::size_t max_chunk_chars = 1500;
    // Matches the title-page header of the course decks; override per corpus.
    std::string lecture_header_regex = R"(CS915/(CS)?435 Advanced Computer Security)";
};

struct PdfIngestReport {
    std::int64_t document_id = 0;
    bool skipped = false;           // identical file already ingested
    int pages = 0;
    int lectures = 0;
    int chunks = 0;
    int needs_ocr = 0;
};

// Reads a PDF, normalizes each page's text layer, splits lectures, and writes
// document/lectures/pages/chunks plus outbox events in one transaction.
PdfIngestReport ingest_pdf(const std::filesystem::path& path, const PdfIngestOptions& opts);

}  // namespace ll

// ll-ingest: load PDF slide decks into PostgreSQL.
//
//   ll-ingest [--force] [--dsn DSN] FILE.pdf...

#include <cstring>
#include <exception>
#include <iostream>
#include <vector>

#include "ingest/pdf_ingestor.hpp"
#include "storage/pg.hpp"

namespace {

int usage() {
    std::cerr << "usage: ll-ingest [--force] [--dsn DSN] FILE.pdf...\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    ll::PdfIngestOptions opts;
    opts.dsn = ll::pg_dsn_from_env();
    std::vector<std::filesystem::path> files;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--force") == 0) {
            opts.force = true;
        } else if (std::strcmp(argv[i], "--dsn") == 0 && i + 1 < argc) {
            opts.dsn = argv[++i];
        } else if (argv[i][0] == '-') {
            return usage();
        } else {
            files.emplace_back(argv[i]);
        }
    }
    if (files.empty()) return usage();

    int failures = 0;
    for (const auto& f : files) {
        try {
            const auto r = ll::ingest_pdf(f, opts);
            if (r.skipped) {
                std::cout << f.filename().string() << ": already ingested as document "
                          << r.document_id << " (use --force to replace)\n";
                continue;
            }
            std::cout << f.filename().string() << ": document " << r.document_id << ", "
                      << r.pages << " pages, " << r.lectures << " lectures, " << r.chunks
                      << " chunks, " << r.needs_ocr << " pages need OCR\n";
        } catch (const std::exception& e) {
            std::cerr << f.string() << ": " << e.what() << '\n';
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

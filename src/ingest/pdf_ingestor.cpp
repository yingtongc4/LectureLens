#include "ingest/pdf_ingestor.hpp"

#include <pqxx/pqxx>

#include <vector>

#include "common/sha256.hpp"
#include "core/chunker.hpp"
#include "core/lecture_splitter.hpp"
#include "core/text_normalize.hpp"
#include "pdf/pdf_reader.hpp"
#include "storage/document_repo.hpp"

namespace ll {

PdfIngestReport ingest_pdf(const std::filesystem::path& path, const PdfIngestOptions& opts) {
    PdfIngestReport report;
    const auto sha = sha256_file(path);

    // Parse everything before opening a transaction: PDF work is slow and must
    // not hold database locks.
    const PdfReader pdf(path);
    const int n = pdf.page_count();
    std::vector<std::string> raw(n);
    for (int p = 1; p <= n; ++p) raw[p - 1] = pdf.page_text(p);

    const auto title = path.stem().string();
    const auto lectures = split_lectures(raw, opts.lecture_header_regex, title);

    pqxx::connection conn(opts.dsn);
    pqxx::work tx(conn);

    if (const auto existing = repo::find_document_by_sha(tx, sha)) {
        if (!opts.force) {
            report.document_id = *existing;
            report.skipped = true;
            return report;
        }
        repo::delete_document(tx, *existing);
    }

    report.document_id = repo::insert_document(tx, path.string(), sha, title, n);
    report.pages = n;
    report.lectures = static_cast<int>(lectures.size());

    for (const auto& lec : lectures) {
        const auto lecture_id =
            repo::insert_lecture(tx, report.document_id, lec.seq, lec.title, lec.first_page, lec.last_page);
        for (int p = lec.first_page; p <= lec.last_page; ++p) {
            const auto text = normalize_slide_text(raw[p - 1]);
            const auto visible = visible_char_count(text);
            const bool needs_ocr = visible < opts.ocr_threshold;
            report.needs_ocr += needs_ocr;

            const auto page_id = repo::insert_page(tx, report.document_id, lecture_id, p, text,
                                                   static_cast<int>(visible), needs_ocr);
            const auto chunks = chunk_page_text(text, opts.max_chunk_chars);
            for (std::size_t i = 0; i < chunks.size(); ++i) {
                repo::insert_chunk(tx, page_id, "text_layer", static_cast<int>(i), chunks[i]);
            }
            report.chunks += static_cast<int>(chunks.size());
        }
    }

    tx.commit();
    return report;
}

}  // namespace ll

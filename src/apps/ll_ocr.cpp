// ll-ocr: render PDF pages, run OCR, store results, and measure accuracy.
//
//   ll-ocr [options] page FILE.pdf PAGE   OCR one page and print it (no DB)
//   ll-ocr [options] run                  OCR pages flagged needs_ocr; store
//                                         ocr_blocks + 'ocr' chunks
//   ll-ocr [options] eval                 OCR pages that have a good text layer
//                                         and report CER against it
// options:
//   --models DIR      model directory (default $LL_MODELS_DIR or ~/.local/share/lecturelens/models)
//   --backend NAME    cpu | openvino | cuda | tensorrt (default cpu)
//   --threads N       ORT intra-op threads (default: ORT decides)
//   --dpi N           render resolution (default 144)
//   --limit-side N    detector input size (default 960)
//   --limit N         at most N pages
//   --every N         sample one page in N (eval)
//   --dsn DSN         PostgreSQL connection string

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <pqxx/pqxx>

#include "core/cer.hpp"
#include "core/chunker.hpp"
#include "core/text_normalize.hpp"
#include "ocr/ocr_engine.hpp"
#include "pdf/pdf_reader.hpp"
#include "storage/ocr_repo.hpp"
#include "storage/pg.hpp"

namespace {

constexpr double kExtraTextRatio = 1.2;

struct Args {
    std::string mode;
    std::vector<std::string> positional;
    std::filesystem::path models = ll::default_models_dir();
    ll::Backend backend = ll::Backend::Cpu;
    int threads = 0;
    double dpi = 144;
    int limit_side = 960;
    int limit = 0;
    int every = 1;
    std::string dsn = ll::pg_dsn_from_env();
};

int usage() {
    std::cerr << "usage: ll-ocr [--models DIR] [--backend NAME] [--threads N] [--dpi N] [--limit-side N]\n"
                 "              [--limit N] [--every N] [--dsn DSN] (page FILE.pdf PAGE | run | eval)\n";
    return 2;
}

cv::Mat to_bgr(const ll::RgbImage& img) {
    cv::Mat bgr(img.height, img.width, CV_8UC3);
    for (int y = 0; y < img.height; ++y) {
        const auto* src = img.pixels.data() + static_cast<std::size_t>(y) * img.width * 3;
        auto* dst = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < img.width; ++x) dst[x] = {src[3 * x + 2], src[3 * x + 1], src[3 * x]};
    }
    return bgr;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(p * (v.size() - 1) + 0.5);
    return v[std::min(k, v.size() - 1)];
}

double mean(const std::vector<double>& v) {
    double s = 0;
    for (const double x : v) s += x;
    return v.empty() ? 0.0 : s / v.size();
}

// Opens each source PDF once while walking pages ordered by document.
class PdfCache {
public:
    const ll::PdfReader& get(const std::string& path) {
        if (path != path_) {
            reader_ = std::make_unique<ll::PdfReader>(path);
            path_ = path;
        }
        return *reader_;
    }

private:
    std::string path_;
    std::unique_ptr<ll::PdfReader> reader_;
};

int cmd_page(const Args& a, const ll::ocr::OcrEngine& engine) {
    if (a.positional.size() != 2) return usage();
    const ll::PdfReader pdf(a.positional[0]);
    const auto bgr = to_bgr(pdf.render_page(std::stoi(a.positional[1]), a.dpi));
    const auto r = engine.run(bgr);
    for (const auto& l : r.lines) {
        std::cout << std::fixed << std::setprecision(2) << l.confidence << "  [" << l.box.x << ',' << l.box.y
                  << ' ' << l.box.w << 'x' << l.box.h << "]  " << l.text << '\n';
    }
    std::cout << "image " << bgr.cols << 'x' << bgr.rows << ", " << r.lines.size() << " lines, det "
              << r.det_ms << " ms, rec " << r.rec_ms << " ms\n";
    return 0;
}

int cmd_db(const Args& a, const ll::ocr::OcrEngine& engine, bool eval) {
    pqxx::connection conn(a.dsn);
    const std::string backend = ll::backend_name(a.backend);
    const nlohmann::json params = {{"dpi", a.dpi},       {"limit_side", a.limit_side}, {"threads", a.threads},
                                   {"limit", a.limit},   {"every", a.every},           {"model", "PP-OCRv4"}};

    std::vector<ll::repo::PageRef> pages;
    std::int64_t run_id = 0;
    {
        pqxx::work tx(conn);
        pages = ll::repo::list_pages(tx, eval ? ll::repo::PageFilter::HasTextLayer : ll::repo::PageFilter::NeedsOcr,
                                     a.limit, a.every);
        run_id = ll::repo::start_ocr_run(tx, eval ? "eval" : "run", backend, params.dump());
        tx.commit();
    }

    PdfCache pdfs;
    std::vector<double> det_ms, rec_ms, render_ms, recalls, clean_cers;
    std::size_t ref_chars = 0, matched = 0, clean_ref_chars = 0, clean_edits = 0;
    int extra_text_pages = 0;
    const auto wall0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& p = pages[i];
        const auto t0 = std::chrono::steady_clock::now();
        const auto bgr = to_bgr(pdfs.get(p.source_path).render_page(p.page_no, a.dpi));
        render_ms.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

        const auto r = engine.run(bgr);
        det_ms.push_back(r.det_ms);
        rec_ms.push_back(r.rec_ms);
        const auto text = ll::ocr::join_reading_order(r.lines);

        std::optional<ll::repo::PageScore> score;
        if (eval) {
            const auto hyp = ll::comparable_chars(text);
            const auto ref = ll::comparable_chars(p.text_layer);
            ll::repo::PageScore s;
            s.cer = ll::character_error_rate(hyp, ref);
            const auto lcs = ll::lcs_length(hyp, ref);
            s.recall = ref.empty() ? 1.0 : static_cast<double>(lcs) / ref.size();
            s.extra_chars = static_cast<int>(hyp.size() > ref.size() ? hyp.size() - ref.size() : 0);
            score = s;

            ref_chars += ref.size();
            matched += lcs;
            recalls.push_back(s.recall);
            // OCR read noticeably more than the text layer has: text inside
            // images. CER is not meaningful there, recall still is.
            if (hyp.size() > ref.size() * kExtraTextRatio) {
                ++extra_text_pages;
            } else {
                clean_ref_chars += ref.size();
                clean_edits += ll::edit_distance(hyp, ref);
                clean_cers.push_back(s.cer);
            }
        }

        pqxx::work tx(conn);
        ll::repo::replace_ocr_blocks(tx, p.page_id, backend, r.lines);
        if (!eval) ll::repo::replace_ocr_chunks(tx, p.page_id, ll::chunk_page_text(text, 1500));
        ll::repo::insert_page_result(tx, run_id, p.page_id, static_cast<int>(r.lines.size()), r.det_ms, r.rec_ms,
                                     score);
        tx.commit();

        std::cerr << "\r[" << (i + 1) << '/' << pages.size() << "] doc " << p.document_id << " p." << p.page_no
                  << "  " << r.lines.size() << " lines" << std::flush;
    }
    std::cerr << '\n';

    const double wall_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    nlohmann::json m = {
        {"pages", pages.size()},
        {"wall_s", wall_s},
        {"pages_per_s", wall_s > 0 ? pages.size() / wall_s : 0.0},
        {"render_ms", {{"mean", mean(render_ms)}, {"p50", percentile(render_ms, 0.5)}, {"p95", percentile(render_ms, 0.95)}}},
        {"det_ms", {{"mean", mean(det_ms)}, {"p50", percentile(det_ms, 0.5)}, {"p95", percentile(det_ms, 0.95)}}},
        {"rec_ms", {{"mean", mean(rec_ms)}, {"p50", percentile(rec_ms, 0.5)}, {"p95", percentile(rec_ms, 0.95)}}},
    };
    if (eval) {
        m["recall_micro"] = ref_chars ? static_cast<double>(matched) / ref_chars : 0.0;
        m["recall_page_p10"] = percentile(recalls, 0.1);
        m["extra_text_pages"] = extra_text_pages;
        m["clean_pages"] = clean_cers.size();
        m["cer_clean_micro"] = clean_ref_chars ? static_cast<double>(clean_edits) / clean_ref_chars : 0.0;
        m["cer_clean_p50"] = percentile(clean_cers, 0.5);
        m["cer_clean_p90"] = percentile(clean_cers, 0.9);
    }
    {
        pqxx::work tx(conn);
        ll::repo::finish_ocr_run(tx, run_id, m.dump());
        tx.commit();
    }
    std::cout << "run " << run_id << " (" << (eval ? "eval" : "run") << ", " << backend << ")\n"
              << m.dump(2) << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string s = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument("missing value for " + s);
                return argv[++i];
            };
            if (s == "--models") a.models = value();
            else if (s == "--backend") a.backend = ll::parse_backend(value());
            else if (s == "--threads") a.threads = std::stoi(value());
            else if (s == "--dpi") a.dpi = std::stod(value());
            else if (s == "--limit-side") a.limit_side = std::stoi(value());
            else if (s == "--limit") a.limit = std::stoi(value());
            else if (s == "--every") a.every = std::stoi(value());
            else if (s == "--dsn") a.dsn = value();
            else if (s == "--help" || s == "-h") return usage();
            else if (a.mode.empty()) a.mode = s;
            else a.positional.push_back(s);
        }
        if (a.mode.empty()) return usage();

        ll::ocr::OcrOptions opts;
        opts.det.limit_side = a.limit_side;
        const ll::ocr::OcrEngine engine(a.models, a.backend, a.threads, opts);

        if (a.mode == "page") return cmd_page(a, engine);
        if (a.mode == "run") return cmd_db(a, engine, false);
        if (a.mode == "eval") return cmd_db(a, engine, true);
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "ll-ocr: " << e.what() << '\n';
        return 1;
    }
}

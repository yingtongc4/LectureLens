// ll-bench: compare inference backends and model variants on real data.
//
//   ll-bench embed [--models DIR] [--threads N] [--batch N] [--variant BACKEND:FILE]...
//                  [--queries FILE.jsonl] [--out FILE.json]
//
// Embeds every indexed passage with each variant and reports load time,
// passage throughput, single-query latency, agreement with the first variant
// (cosine between the two embeddings of the same text), and vector-only
// retrieval quality. Retrieval is exact search in memory over the same
// passages, filters and per-page dedup that ll-search uses, so variants are
// compared without re-indexing Qdrant for each one.
//
// Default variants: cpu:model.onnx, openvino:model.onnx, and the same two for
// model_int8.onnx when it exists (scripts/quantize_e5.py).
//
// OCR backends are compared with `ll-ocr --backend NAME eval`, which stores
// accuracy and timings per run.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include "embed/pooling.hpp"
#include "embed/text_embedder.hpp"
#include "index/outbox_indexer.hpp"
#include "storage/pg.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(p * (v.size() - 1) + 0.5);
    return v[std::min(k, v.size() - 1)];
}

struct Passage {
    std::int64_t page_id;
    std::string lecture;
    int page_no;
    std::string text;
};

struct EvalCase {
    std::string query;
    std::string lecture;
    std::set<int> pages;
};

struct Variant {
    ll::Backend backend;
    std::string file;
    std::string label() const { return ll::backend_name(backend) + ":" + file; }
};

Variant parse_variant(const std::string& s) {
    const auto colon = s.find(':');
    if (colon == std::string::npos) throw std::invalid_argument("variant must be BACKEND:FILE, got " + s);
    return {ll::parse_backend(s.substr(0, colon)), s.substr(colon + 1)};
}

std::vector<Passage> load_passages(const std::string& dsn) {
    pqxx::connection conn(dsn);
    pqxx::work tx(conn);
    // Same exclusions as HybridSearcher: lecture covers and outline slides.
    const auto rows = tx.exec(
        "SELECT p.id, l.title, p.page_no, c.content FROM chunks c "
        "JOIN pages p ON p.id = c.page_id JOIN lectures l ON l.id = p.lecture_id "
        "WHERE p.page_no <> l.first_page "
        "  AND lower(split_part(p.text_layer, E'\\n', 1)) NOT IN ('outline', 'roadmap', 'road map') "
        "ORDER BY c.id");
    std::vector<Passage> out;
    for (const auto& r : rows) {
        const auto lecture = r[1].as<std::string>();
        out.push_back({r[0].as<std::int64_t>(), lecture, r[2].as<int>(),
                       ll::index::passage_text(lecture, r[3].as<std::string>())});
    }
    return out;
}

std::vector<EvalCase> load_queries(const std::string& file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot read " + file);
    std::vector<EvalCase> cases;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        const auto j = nlohmann::json::parse(line);
        EvalCase c{j["query"], j["lecture"], {}};
        for (const int p : j.value("pages", std::vector<int>{})) c.pages.insert(p);
        cases.push_back(std::move(c));
    }
    return cases;
}

// Rank of the first relevant page in the top `k` distinct pages (0 = miss).
int hit_rank(const std::vector<float>& q, const std::vector<std::vector<float>>& passages,
             const std::vector<Passage>& meta, const EvalCase& c, int k) {
    std::vector<std::pair<float, std::size_t>> scored;
    scored.reserve(passages.size());
    for (std::size_t i = 0; i < passages.size(); ++i) {
        scored.emplace_back(ll::embed::dot(q, passages[i]), i);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::unordered_set<std::int64_t> seen;
    int rank = 0;
    for (const auto& [score, i] : scored) {
        if (!seen.insert(meta[i].page_id).second) continue;
        if (++rank > k) break;
        if (meta[i].lecture == c.lecture && (c.pages.empty() || c.pages.count(meta[i].page_no))) return rank;
    }
    return 0;
}

struct Agreement {
    double mean = 0, min = 1;
};

Agreement agreement(const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b) {
    Agreement r;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double c = ll::embed::dot(a[i], b[i]);
        r.mean += c;
        r.min = std::min(r.min, c);
    }
    r.mean /= std::max<std::size_t>(1, a.size());
    return r;
}

std::string cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("model name", 0) == 0) return line.substr(line.find(':') + 2);
    }
    return "unknown";
}

int usage() {
    std::cerr << "usage: ll-bench embed [--models DIR] [--threads N] [--batch N] [--variant BACKEND:FILE]...\n"
                 "                      [--queries FILE.jsonl] [--out FILE.json]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "embed") return usage();
    std::filesystem::path models = ll::default_models_dir();
    std::string queries_file = "eval/retrieval_queries.jsonl", out_file;
    int threads = 0, batch = 16;
    std::vector<Variant> variants;
    try {
        for (int i = 2; i < argc; ++i) {
            const std::string s = argv[i];
            if (i + 1 >= argc) return usage();
            if (s == "--models") models = argv[++i];
            else if (s == "--threads") threads = std::stoi(argv[++i]);
            else if (s == "--batch") batch = std::stoi(argv[++i]);
            else if (s == "--variant") variants.push_back(parse_variant(argv[++i]));
            else if (s == "--queries") queries_file = argv[++i];
            else if (s == "--out") out_file = argv[++i];
            else return usage();
        }
        const auto dir = models / "multilingual-e5-small";
        if (variants.empty()) {
            for (const auto* file : {"model.onnx", "model_int8.onnx"}) {
                if (!std::filesystem::exists(dir / file)) continue;
                variants.push_back({ll::Backend::Cpu, file});
                variants.push_back({ll::Backend::OpenVino, file});
            }
        }

        const auto passages = load_passages(ll::pg_dsn_from_env());
        const auto cases = load_queries(queries_file);
        std::vector<std::string> texts;
        for (const auto& p : passages) texts.push_back(p.text);
        std::cerr << passages.size() << " passages, " << cases.size() << " queries, batch " << batch << "\n";

        nlohmann::json report = {{"cpu", cpu_model()},
                                 {"hardware_threads", std::thread::hardware_concurrency()},
                                 {"threads", threads},
                                 {"batch", batch},
                                 {"passages", passages.size()},
                                 {"queries", cases.size()},
                                 {"variants", nlohmann::json::array()}};
        std::vector<std::vector<float>> ref_passages, ref_queries;

        std::cout << std::left << std::setw(26) << "variant" << std::right << std::setw(9) << "load ms"
                  << std::setw(11) << "passage/s" << std::setw(10) << "q p50 ms" << std::setw(10) << "q p95 ms"
                  << std::setw(10) << "cos mean" << std::setw(9) << "cos min" << std::setw(8) << "R@5"
                  << std::setw(8) << "R@10" << std::setw(8) << "MRR@5" << '\n';

        for (const auto& v : variants) {
            nlohmann::json row = {{"variant", v.label()}};
            try {
                auto t0 = Clock::now();
                const ll::embed::TextEmbedder embedder(dir, v.backend, threads, v.file);
                const double load_ms = ms_since(t0);

                // Passage throughput, in index order and batch size like ll-indexer.
                std::vector<std::vector<float>> pv;
                std::vector<double> batch_ms;
                t0 = Clock::now();
                for (std::size_t i = 0; i < texts.size(); i += batch) {
                    const std::vector<std::string> part(texts.begin() + i,
                                                        texts.begin() + std::min(texts.size(), i + batch));
                    const auto t1 = Clock::now();
                    for (auto& e : embedder.embed_passages(part)) pv.push_back(std::move(e));
                    batch_ms.push_back(ms_since(t1));
                }
                const double total_s = ms_since(t0) / 1000.0;

                // Single-query latency: one warm-up, then three timed runs per query.
                std::vector<std::vector<float>> qv;
                std::vector<double> query_ms;
                for (const auto& c : cases) {
                    qv.push_back(embedder.embed_query(c.query));
                    for (int r = 0; r < 3; ++r) {
                        const auto t1 = Clock::now();
                        embedder.embed_query(c.query);
                        query_ms.push_back(ms_since(t1));
                    }
                }

                int hits5 = 0, hits10 = 0;
                double rr5 = 0;
                for (std::size_t i = 0; i < cases.size(); ++i) {
                    const int rank = hit_rank(qv[i], pv, passages, cases[i], 10);
                    if (rank) ++hits10;
                    if (rank && rank <= 5) {
                        ++hits5;
                        rr5 += 1.0 / rank;
                    }
                }
                if (ref_passages.empty()) {
                    ref_passages = pv;
                    ref_queries = qv;
                }
                const auto ap = agreement(ref_passages, pv);
                const auto aq = agreement(ref_queries, qv);
                const double n = std::max<std::size_t>(1, cases.size());

                row.update({{"load_ms", load_ms},
                            {"passages_per_s", texts.size() / total_s},
                            {"batch_ms_p50", percentile(batch_ms, 0.5)},
                            {"batch_ms_p95", percentile(batch_ms, 0.95)},
                            {"query_ms_p50", percentile(query_ms, 0.5)},
                            {"query_ms_p95", percentile(query_ms, 0.95)},
                            {"passage_cos_mean", ap.mean},
                            {"passage_cos_min", ap.min},
                            {"query_cos_mean", aq.mean},
                            {"query_cos_min", aq.min},
                            {"recall_at_5", hits5 / n},
                            {"recall_at_10", hits10 / n},
                            {"mrr_at_5", rr5 / n},
                            {"model_bytes", std::filesystem::file_size(dir / v.file)}});
                std::cout << std::left << std::setw(26) << v.label() << std::right << std::fixed
                          << std::setprecision(0) << std::setw(9) << load_ms << std::setprecision(1) << std::setw(11)
                          << texts.size() / total_s << std::setw(10) << percentile(query_ms, 0.5) << std::setw(10)
                          << percentile(query_ms, 0.95) << std::setprecision(4) << std::setw(10) << ap.mean
                          << std::setw(9) << ap.min << std::setprecision(3) << std::setw(8) << hits5 / n
                          << std::setw(8) << hits10 / n << std::setw(8) << rr5 / n << std::endl;
            } catch (const std::exception& e) {
                row["error"] = e.what();
                std::cout << std::left << std::setw(26) << v.label() << " failed: " << e.what() << std::endl;
            }
            report["variants"].push_back(row);
        }

        if (!out_file.empty()) {
            std::filesystem::create_directories(std::filesystem::path(out_file).parent_path());
            std::ofstream(out_file) << report.dump(2) << '\n';
            std::cerr << "wrote " << out_file << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "ll-bench: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

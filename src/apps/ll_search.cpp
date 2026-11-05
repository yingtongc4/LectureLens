// ll-search: query the knowledge base, or evaluate retrieval quality.
//
//   ll-search [--mode hybrid|vector|keyword] [-k N] "QUERY"
//   ll-search eval FILE.jsonl [-k N]
// common: [--models DIR] [--backend cpu|openvino|...] [--model-file FILE]
//
// Eval file: one JSON object per line,
//   {"query": "...", "lecture": "Web security (II)", "pages": [148, 149, 150]}
// A hit is a result from that lecture whose page is in `pages` (any page of
// the lecture when `pages` is empty). Reports Recall@k and MRR per mode.

#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

#include "index/outbox_indexer.hpp"
#include "search/hybrid_search.hpp"
#include "storage/pg.hpp"

namespace {

struct EvalCase {
    std::string query;
    std::string lecture;
    std::set<int> pages;
};

bool is_hit(const ll::search::Result& r, const EvalCase& c) {
    return r.lecture == c.lecture && (c.pages.empty() || c.pages.count(r.page_no));
}

int run_eval(ll::search::HybridSearcher& searcher, const std::string& file, int k) {
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

    nlohmann::json report = nlohmann::json::object();
    for (const auto* mode : {"keyword", "vector", "hybrid"}) {
        double hits = 0, rr = 0, latency = 0;
        std::vector<std::string> misses;
        for (const auto& c : cases) {
            ll::search::Query q;
            q.text = c.query;
            q.top_k = k;
            q.mode = ll::search::parse_mode(mode);
            ll::search::Timings t;
            const auto results = searcher.search(q, &t);
            latency += t.embed_ms + t.vector_ms + t.keyword_ms + t.fetch_ms;
            int rank = 0;
            for (std::size_t i = 0; i < results.size(); ++i) {
                if (is_hit(results[i], c)) {
                    rank = static_cast<int>(i + 1);
                    break;
                }
            }
            if (rank) {
                hits += 1;
                rr += 1.0 / rank;
            } else {
                misses.push_back(c.query);
            }
        }
        const double n = static_cast<double>(cases.size());
        report[mode] = {{"recall_at_k", hits / n}, {"mrr", rr / n}, {"mean_latency_ms", latency / n},
                        {"misses", misses}};
        std::cout << std::left << std::setw(8) << mode << "  Recall@" << k << " " << std::fixed
                  << std::setprecision(3) << hits / n << "   MRR " << rr / n << "   " << std::setprecision(1)
                  << latency / n << " ms/query\n";
    }
    std::cout << "queries: " << cases.size() << '\n' << report.dump(2) << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode = "hybrid", query, eval_file;
    int k = 5;
    std::filesystem::path models = ll::default_models_dir();
    ll::Backend backend = ll::Backend::Cpu;
    std::string model_file = "model.onnx";  // must match what ll-indexer used
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (s == "-k" && i + 1 < argc) k = std::stoi(argv[++i]);
        else if (s == "--models" && i + 1 < argc) models = argv[++i];
        else if (s == "--backend" && i + 1 < argc) backend = ll::parse_backend(argv[++i]);
        else if (s == "--model-file" && i + 1 < argc) model_file = argv[++i];
        else if (s == "eval" && i + 1 < argc) eval_file = argv[++i];
        else query = s;
    }
    if (query.empty() && eval_file.empty()) {
        std::cerr << "usage: ll-search [--mode hybrid|vector|keyword] [-k N] QUERY | ll-search eval FILE.jsonl\n";
        return 2;
    }
    try {
        const ll::embed::TextEmbedder embedder(models / "multilingual-e5-small", backend, 0, model_file);
        ll::vec::QdrantClient qdrant(ll::vec::qdrant_url_from_env());
        ll::search::HybridSearcher searcher(ll::pg_dsn_from_env(), embedder, qdrant);
        if (!eval_file.empty()) return run_eval(searcher, eval_file, k);

        ll::search::Query q;
        q.text = query;
        q.top_k = k;
        q.mode = ll::search::parse_mode(mode);
        ll::search::Timings t;
        const auto results = searcher.search(q, &t);
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            auto first_line = r.content.substr(0, r.content.find('\n'));
            std::cout << i + 1 << ". [" << r.lecture << " p." << r.page_no << ", " << r.source << "] " << first_line
                      << "   (rrf " << std::setprecision(4) << r.score << ", vec#" << r.vector_rank << ", kw#"
                      << r.keyword_rank << ")\n";
        }
        if (!t.vector_error.empty()) std::cout << "degraded to keyword-only: " << t.vector_error << '\n';
        std::cout << std::setprecision(1) << std::fixed << "embed " << t.embed_ms << " ms, qdrant " << t.vector_ms
                  << " ms, fts " << t.keyword_ms << " ms, fetch " << t.fetch_ms << " ms\n";
    } catch (const std::exception& e) {
        std::cerr << "ll-search: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

// ll-api: HTTP API over the knowledge base.
//
//   ll-api [--port 8080] [--models DIR]
//
//   GET  /health
//   POST /api/search   {"query": "...", "top_k": 5, "mode": "hybrid", "document_id": 1}
//   GET  /api/pages/{document_id}/{page_no}   page text, OCR lines, lecture

#include <httplib.h>

#include <iostream>
#include <pqxx/pqxx>

#include "search/hybrid_search.hpp"
#include "storage/page_view.hpp"
#include "storage/pg.hpp"

namespace {

void reply_json(httplib::Response& res, const nlohmann::json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8080;
    std::filesystem::path models = ll::default_models_dir();
    ll::Backend backend = ll::Backend::Cpu;
    std::string model_file = "model.onnx";  // must match what ll-indexer used
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string s = argv[i];
        if (s == "--port") port = std::stoi(argv[i + 1]);
        else if (s == "--models") models = argv[i + 1];
        else if (s == "--backend") backend = ll::parse_backend(argv[i + 1]);
        else if (s == "--model-file") model_file = argv[i + 1];
    }

    try {
        const auto dsn = ll::pg_dsn_from_env();
        const ll::embed::TextEmbedder embedder(models / "multilingual-e5-small", backend, 0, model_file);
        const auto qdrant_url = ll::vec::qdrant_url_from_env();

        httplib::Server server;
        server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            std::string msg = "internal error";
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                msg = e.what();
            }
            reply_json(res, {{"error", msg}}, 500);
        });

        // PostgreSQL down = cannot serve (503); Qdrant down = degraded
        // (keyword-only search), still 200.
        server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
            bool pg_ok = true;
            try {
                pqxx::connection conn(dsn);
                pqxx::nontransaction(conn).exec("SELECT 1");
            } catch (const std::exception&) {
                pg_ok = false;
            }
            thread_local ll::vec::QdrantClient qdrant(qdrant_url, 2);
            const bool qdrant_ok = qdrant.ready();
            const char* status = !pg_ok ? "down" : qdrant_ok ? "ok" : "degraded";
            reply_json(res, {{"status", status}, {"postgres", pg_ok}, {"qdrant", qdrant_ok}}, pg_ok ? 200 : 503);
        });

        server.Post("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
            const auto body = nlohmann::json::parse(req.body, nullptr, false);
            if (body.is_discarded() || !body.contains("query") || !body["query"].is_string()) {
                return reply_json(res, {{"error", "body must be JSON with a string 'query'"}}, 400);
            }
            ll::search::Query q;
            q.text = body["query"];
            q.top_k = std::clamp(body.value("top_k", 5), 1, 50);
            q.mode = ll::search::parse_mode(body.value("mode", std::string("hybrid")));
            if (body.contains("document_id")) q.document_id = body["document_id"].get<std::int64_t>();

            // httplib serves each request on a pool thread; give each its own
            // HTTP client (the embedder's ORT session is thread-safe).
            thread_local ll::vec::QdrantClient qdrant(qdrant_url, 2);  // fail fast, then degrade
            ll::search::HybridSearcher searcher(dsn, embedder, qdrant);
            ll::search::Timings t;
            const auto results = searcher.search(q, &t);

            nlohmann::json out = {{"query", q.text}, {"results", nlohmann::json::array()}};
            for (const auto& r : results) out["results"].push_back(ll::search::to_json(r));
            out["timings_ms"] = {{"embed", t.embed_ms}, {"vector", t.vector_ms}, {"keyword", t.keyword_ms},
                                 {"fetch", t.fetch_ms}};
            if (!t.vector_error.empty()) out["degraded"] = {{"vector", t.vector_error}};
            reply_json(res, out);
        });

        server.Get(R"(/api/pages/(\d+)/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
            pqxx::connection conn(dsn);
            pqxx::read_transaction tx(conn);
            const auto page = ll::repo::get_page(tx, std::stoll(req.matches[1]), std::stoi(req.matches[2]));
            if (!page) return reply_json(res, {{"error", "page not found"}}, 404);
            nlohmann::json ocr = nlohmann::json::array();
            for (const auto& l : page->ocr_lines) ocr.push_back({{"text", l.text}, {"box", {l.x, l.y, l.w, l.h}}});
            reply_json(res, {{"document", page->document},
                             {"lecture", page->lecture},
                             {"text_layer", page->text_layer},
                             {"needs_ocr", page->needs_ocr},
                             {"ocr_lines", ocr}});
        });

        std::cout << "ll-api listening on http://127.0.0.1:" << port << std::endl;
        if (!server.listen("127.0.0.1", port)) throw std::runtime_error("cannot bind port " + std::to_string(port));
    } catch (const std::exception& e) {
        std::cerr << "ll-api: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

// ll-mcp: MCP server (stdio) exposing the lecture knowledge base as tools.
//
//   ll-mcp [--models DIR] [--backend NAME] [--model-file F] [--modern-only | --legacy-only]
//
// Tools: search_lectures, get_page, list_lectures. Speaks protocol 2026-07-28
// (stateless, per-request _meta) and the initialize-based 2025-03-26 to
// 2025-11-25 revisions. stdout carries JSON-RPC only; logs go to stderr.
//
// Example client config (Claude Code / Claude Desktop, via WSL):
//   {"command": "wsl", "args": ["-d", "Ubuntu-24.04", "--", "/home/USER/build/lecturelens/src/ll-mcp"]}

#include <iostream>
#include <sstream>
#include <string>

#include <pqxx/pqxx>

#include "mcp/server.hpp"
#include "search/hybrid_search.hpp"
#include "storage/page_view.hpp"
#include "storage/pg.hpp"

namespace {

using ll::mcp::json;

// At most `max_bytes`, cut on a UTF-8 character boundary.
std::string snippet(const std::string& s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    std::size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end) + " …";
}

ll::mcp::Tool search_tool(const std::string& dsn, const ll::embed::TextEmbedder& embedder,
                          ll::vec::QdrantClient& qdrant) {
    ll::mcp::Tool t;
    t.name = "search_lectures";
    t.title = "Search lecture slides";
    t.description =
        "Search the course's lecture slides (written in English) by meaning and by keywords; questions in any "
        "language work. Returns the best-matching slides, one per page, with lecture title, page number, "
        "document_id and a text snippet. Use get_page to read a whole slide. Cite results as (lecture, p.page).";
    t.input_schema = {
        {"type", "object"},
        {"properties",
         {{"query", {{"type", "string"}, {"minLength", 1}, {"description", "What to look for"}}},
          {"top_k", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}, {"default", 5},
                     {"description", "Number of slides to return"}}},
          {"mode", {{"type", "string"}, {"enum", {"hybrid", "vector", "keyword"}}, {"default", "hybrid"},
                    {"description", "hybrid (default) fuses semantic and keyword search"}}},
          {"document_id", {{"type", "integer"}, {"description", "Restrict to one slide deck (see list_lectures)"}}}}},
        {"required", {"query"}},
        {"additionalProperties", false}};
    t.run = [dsn, emb = &embedder, qd = &qdrant](const json& a) {
        ll::search::Query q;
        q.text = a["query"];
        q.top_k = a.value("top_k", 5);
        q.mode = ll::search::parse_mode(a.value("mode", std::string("hybrid")));
        if (a.contains("document_id")) q.document_id = a["document_id"].get<std::int64_t>();
        ll::search::HybridSearcher searcher(dsn, *emb, *qd);
        const auto results = searcher.search(q);

        json items = json::array();
        std::ostringstream text;
        if (results.empty()) text << "No matching slides.";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            const auto snip = snippet(r.content, 700);
            items.push_back({{"document_id", r.document_id}, {"lecture", r.lecture}, {"page", r.page_no},
                             {"source", r.source}, {"score", r.score}, {"snippet", snip}});
            text << i + 1 << ". " << r.lecture << ", p." << r.page_no << " (document_id " << r.document_id
                 << (r.source == "ocr" ? ", text read by OCR" : "") << ")\n"
                 << snip << "\n\n";
        }
        return ll::mcp::ToolResult{{{"results", items}}, text.str()};
    };
    return t;
}

ll::mcp::Tool page_tool(const std::string& dsn) {
    ll::mcp::Tool t;
    t.name = "get_page";
    t.title = "Read one slide";
    t.description =
        "Read one slide in full: its text layer and any text found by OCR in images (code screenshots, "
        "diagrams). Identify it by document_id and page number from search_lectures.";
    t.input_schema = {{"type", "object"},
                      {"properties",
                       {{"document_id", {{"type", "integer"}, {"description", "Slide deck id"}}},
                        {"page", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based page number"}}}}},
                      {"required", {"document_id", "page"}},
                      {"additionalProperties", false}};
    t.run = [dsn](const json& a) {
        pqxx::connection conn(dsn);
        pqxx::read_transaction tx(conn);
        const auto page = ll::repo::get_page(tx, a["document_id"].get<std::int64_t>(), a["page"].get<int>());
        if (!page) {
            throw ll::mcp::ToolError("no page " + a["page"].dump() + " in document " + a["document_id"].dump());
        }
        std::string ocr;
        for (const auto& l : page->ocr_lines) ocr += l.text + "\n";
        std::ostringstream text;
        text << page->lecture << ", p." << page->page_no << " (document_id " << page->document_id << ")\n"
             << "--- slide text ---\n" << (page->text_layer.empty() ? "(none)" : page->text_layer) << "\n";
        if (!ocr.empty()) text << "--- text found by OCR in images ---\n" << ocr;
        return ll::mcp::ToolResult{{{"document_id", page->document_id},
                                    {"document", page->document},
                                    {"lecture", page->lecture},
                                    {"page", page->page_no},
                                    {"text_layer", page->text_layer},
                                    {"ocr_text", ocr}},
                                   text.str()};
    };
    return t;
}

ll::mcp::Tool lectures_tool(const std::string& dsn) {
    ll::mcp::Tool t;
    t.name = "list_lectures";
    t.title = "List lectures";
    t.description = "List the course's lectures with their slide deck (document_id) and page ranges.";
    t.input_schema = {{"type", "object"},
                      {"properties", {{"document_id", {{"type", "integer"}, {"description", "Only this deck"}}}}},
                      {"additionalProperties", false}};
    t.run = [dsn](const json& a) {
        pqxx::connection conn(dsn);
        pqxx::read_transaction tx(conn);
        std::optional<std::int64_t> doc;
        if (a.contains("document_id")) doc = a["document_id"].get<std::int64_t>();
        json items = json::array();
        std::ostringstream text;
        for (const auto& l : ll::repo::list_lectures(tx, doc)) {
            items.push_back({{"document_id", l.document_id}, {"seq", l.seq}, {"title", l.title},
                             {"first_page", l.first_page}, {"last_page", l.last_page}});
            text << "document " << l.document_id << ", lecture " << l.seq << ": " << l.title << " (pp. "
                 << l.first_page << "-" << l.last_page << ")\n";
        }
        return ll::mcp::ToolResult{{{"lectures", items}}, text.str()};
    };
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path models = ll::default_models_dir();
    ll::Backend backend = ll::Backend::Cpu;
    std::string model_file = "model.onnx";
    ll::mcp::ServerOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--models" && i + 1 < argc) models = argv[++i];
        else if (s == "--backend" && i + 1 < argc) backend = ll::parse_backend(argv[++i]);
        else if (s == "--model-file" && i + 1 < argc) model_file = argv[++i];
        else if (s == "--modern-only") opts.legacy = false;
        else if (s == "--legacy-only") opts.modern = false;
        else {
            std::cerr << "usage: ll-mcp [--models DIR] [--backend NAME] [--model-file F] [--modern-only | "
                         "--legacy-only]\n";
            return 2;
        }
    }
    try {
        const auto dsn = ll::pg_dsn_from_env();
        const ll::embed::TextEmbedder embedder(models / "multilingual-e5-small", backend, 0, model_file);
        ll::vec::QdrantClient qdrant(ll::vec::qdrant_url_from_env());

        ll::mcp::Server server({"lecturelens", "0.1.0"},
                               "Knowledge base of lecture slides for an advanced computer security course. "
                               "Search with search_lectures, read a slide with get_page, and cite answers as "
                               "(lecture, p.N).",
                               opts);
        server.add_tool(search_tool(dsn, embedder, qdrant));
        server.add_tool(page_tool(dsn));
        server.add_tool(lectures_tool(dsn));
        std::cerr << "ll-mcp ready: 3 tools, backend " << ll::backend_name(backend) << std::endl;
        ll::mcp::serve(server, std::cin, std::cout);
    } catch (const std::exception& e) {
        std::cerr << "ll-mcp: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

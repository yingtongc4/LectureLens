// ll-indexer: sync chunks from PostgreSQL to Qdrant via the transactional outbox.
//
//   ll-indexer [--once] [--batch N] [--threads N] [--models DIR] [--backend NAME] [--model-file F]
//
// --once drains the backlog and exits; otherwise it polls every 2 s.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>

#include "index/outbox_indexer.hpp"
#include "storage/pg.hpp"

namespace {

std::atomic<bool> g_stop{false};

}  // namespace

int main(int argc, char** argv) {
    bool once = false;
    int threads = 0;
    std::filesystem::path models = ll::default_models_dir();
    ll::Backend backend = ll::Backend::Cpu;
    std::string model_file = "model.onnx";
    ll::index::IndexerOptions opts;
    opts.dsn = ll::pg_dsn_from_env();

    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--once") once = true;
        else if (s == "--batch" && i + 1 < argc) opts.batch_size = std::stoi(argv[++i]);
        else if (s == "--threads" && i + 1 < argc) threads = std::stoi(argv[++i]);
        else if (s == "--models" && i + 1 < argc) models = argv[++i];
        else if (s == "--backend" && i + 1 < argc) backend = ll::parse_backend(argv[++i]);
        else if (s == "--model-file" && i + 1 < argc) model_file = argv[++i];
        else {
            std::cerr << "usage: ll-indexer [--once] [--batch N] [--threads N] [--models DIR]\n"
                         "                  [--backend NAME] [--model-file FILE]\n";
            return 2;
        }
    }
    std::signal(SIGINT, [](int) { g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop = true; });

    try {
        const ll::embed::TextEmbedder embedder(models / "multilingual-e5-small", backend, threads, model_file);
        ll::vec::QdrantClient qdrant(ll::vec::qdrant_url_from_env());
        ll::index::OutboxIndexer indexer(opts, embedder, qdrant);

        int total = 0, upserted = 0, deleted = 0, failures = 0;
        double embed_ms = 0;
        bool collection_ready = false;
        int outage_s = 0;  // backoff while PostgreSQL (or Qdrant at startup) is unreachable
        const auto t0 = std::chrono::steady_clock::now();
        while (!g_stop) {
            ll::index::BatchResult r;
            try {
                if (!collection_ready) {
                    indexer.ensure_collection();
                    collection_ready = true;
                }
                r = indexer.run_batch();
                outage_s = 0;
            } catch (const std::exception& e) {
                // Could not claim or reschedule events at all. Nothing is
                // lost: they stay pending. Exit in --once mode, else retry.
                if (once) throw;
                outage_s = std::min(outage_s == 0 ? 1 : outage_s * 2, 30);
                std::cerr << "\nindexer: " << e.what() << "; retrying in " << outage_s << " s\n";
                for (int i = 0; i < outage_s * 10 && !g_stop; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }
            total += r.claimed;
            upserted += r.upserted;
            deleted += r.deleted;
            embed_ms += r.embed_ms;
            if (r.failed) {
                ++failures;
                std::cerr << "\nbatch failed; events rescheduled with backoff\n";
            }
            if (r.claimed > 0) {
                std::cerr << "\revents " << total << "  upserted " << upserted << "  deleted " << deleted
                          << std::flush;
                continue;
            }
            if (once) break;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cerr << '\n';
        std::cout << "events " << total << ", upserted " << upserted << ", deleted " << deleted << ", failed batches "
                  << failures << ", embed " << embed_ms / 1000 << " s, wall " << wall << " s, points in qdrant "
                  << qdrant.count(ll::index::kChunkCollection) << '\n';
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "ll-indexer: " << e.what() << '\n';
        return 1;
    }
}

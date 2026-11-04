#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace httplib {
class Client;
}

namespace ll::vec {

struct Point {
    std::int64_t id = 0;
    std::vector<float> vector;
    nlohmann::json payload;
};

struct Hit {
    std::int64_t id = 0;
    float score = 0.0f;
    nlohmann::json payload;
};

// Minimal Qdrant REST client. Throws std::runtime_error on transport errors
// and non-2xx responses, so callers (the outbox indexer) can retry.
class QdrantClient {
public:
    explicit QdrantClient(const std::string& base_url, int timeout_s = 10);
    ~QdrantClient();

    // Creates the collection (cosine distance) and integer payload indexes if
    // it does not exist; validates the vector size if it does.
    void ensure_collection(const std::string& name, int dim, const std::vector<std::string>& int_index_fields);

    void upsert(const std::string& collection, const std::vector<Point>& points);
    void remove(const std::string& collection, const std::vector<std::int64_t>& ids);

    // filter: optional Qdrant filter object, e.g. {"must": [...]}.
    std::vector<Hit> search(const std::string& collection, const std::vector<float>& vector, int limit,
                            const nlohmann::json& filter = nullptr);

    std::int64_t count(const std::string& collection);

private:
    nlohmann::json request(const std::string& method, const std::string& path, const nlohmann::json* body);

    std::unique_ptr<httplib::Client> http_;
};

// Base URL from LL_QDRANT_URL, defaulting to the docker-compose instance.
std::string qdrant_url_from_env();

}  // namespace ll::vec

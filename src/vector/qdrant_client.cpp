#include "vector/qdrant_client.hpp"

#include <httplib.h>

#include <cstdlib>
#include <stdexcept>

namespace ll::vec {

std::string qdrant_url_from_env() {
    if (const char* url = std::getenv("LL_QDRANT_URL"); url && *url) return url;
    return "http://localhost:6333";
}

QdrantClient::QdrantClient(const std::string& base_url, int timeout_s)
    : http_(std::make_unique<httplib::Client>(base_url)) {
    http_->set_connection_timeout(timeout_s);
    http_->set_read_timeout(timeout_s);
    http_->set_write_timeout(timeout_s);
}

QdrantClient::~QdrantClient() = default;

bool QdrantClient::ready() {
    const auto res = http_->Get("/readyz");
    return res && res->status == 200;
}

nlohmann::json QdrantClient::request(const std::string& method, const std::string& path,
                                     const nlohmann::json* body) {
    const std::string payload = body ? body->dump() : std::string();
    httplib::Result res;
    if (method == "GET") res = http_->Get(path);
    else if (method == "PUT") res = http_->Put(path, payload, "application/json");
    else if (method == "POST") res = http_->Post(path, payload, "application/json");
    else throw std::invalid_argument("unsupported method " + method);

    if (!res) throw std::runtime_error("qdrant " + method + " " + path + ": " + httplib::to_string(res.error()));
    if (res->status / 100 != 2) {
        throw std::runtime_error("qdrant " + method + " " + path + ": HTTP " + std::to_string(res->status) + " " +
                                 res->body.substr(0, 300));
    }
    return nlohmann::json::parse(res->body);
}

void QdrantClient::ensure_collection(const std::string& name, int dim,
                                     const std::vector<std::string>& int_index_fields) {
    const auto res = http_->Get("/collections/" + name);
    if (res && res->status == 200) {
        const auto info = nlohmann::json::parse(res->body);
        const int size = info["result"]["config"]["params"]["vectors"]["size"];
        if (size != dim) {
            throw std::runtime_error("collection " + name + " has vector size " + std::to_string(size) +
                                     ", model produces " + std::to_string(dim));
        }
        return;
    }
    const nlohmann::json create = {{"vectors", {{"size", dim}, {"distance", "Cosine"}}}};
    request("PUT", "/collections/" + name, &create);
    for (const auto& field : int_index_fields) {
        const nlohmann::json index = {{"field_name", field}, {"field_schema", "integer"}};
        request("PUT", "/collections/" + name + "/index?wait=true", &index);
    }
}

void QdrantClient::upsert(const std::string& collection, const std::vector<Point>& points) {
    if (points.empty()) return;
    nlohmann::json body = {{"points", nlohmann::json::array()}};
    for (const auto& p : points) {
        body["points"].push_back({{"id", p.id}, {"vector", p.vector}, {"payload", p.payload}});
    }
    request("PUT", "/collections/" + collection + "/points?wait=true", &body);
}

void QdrantClient::remove(const std::string& collection, const std::vector<std::int64_t>& ids) {
    if (ids.empty()) return;
    const nlohmann::json body = {{"points", ids}};
    request("POST", "/collections/" + collection + "/points/delete?wait=true", &body);
}

std::vector<Hit> QdrantClient::search(const std::string& collection, const std::vector<float>& vector, int limit,
                                      const nlohmann::json& filter) {
    nlohmann::json body = {{"vector", vector}, {"limit", limit}, {"with_payload", true}};
    if (!filter.is_null()) body["filter"] = filter;
    const auto res = request("POST", "/collections/" + collection + "/points/search", &body);
    std::vector<Hit> hits;
    for (const auto& h : res["result"]) {
        hits.push_back({h["id"].get<std::int64_t>(), h["score"].get<float>(), h["payload"]});
    }
    return hits;
}

std::int64_t QdrantClient::count(const std::string& collection) {
    const nlohmann::json body = {{"exact", true}};
    return request("POST", "/collections/" + collection + "/points/count", &body)["result"]["count"];
}

}  // namespace ll::vec

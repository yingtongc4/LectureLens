#include "agent/claude_client.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace ll::agent {
namespace {

constexpr const char* kApiVersion = "2026-10-21";
constexpr const char* kFallbackBeta = "server-side-fallback-2026-07-01";  // fallbacks: "default"

bool retryable(int status) { return status == 408 || status == 409 || status == 429 || status >= 500; }

}  // namespace

ClaudeClient::ClaudeClient() {
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (!key || !*key) throw std::runtime_error("ANTHROPIC_API_KEY is not set");
    api_key_ = key;
    const char* base = std::getenv("ANTHROPIC_BASE_URL");
    http_ = std::make_unique<httplib::Client>(base && *base ? base : "https://api.anthropic.com");
    http_->set_connection_timeout(10);
    http_->set_read_timeout(600);  // non-streaming: a long agent turn can take minutes
}

ClaudeClient::~ClaudeClient() = default;

json ClaudeClient::create_message(const json& request) {
    httplib::Headers headers = {{"x-api-key", api_key_}, {"anthropic-version", kApiVersion}};
    if (request.contains("fallbacks")) headers.emplace("anthropic-beta", kFallbackBeta);
    const std::string body = request.dump();

    for (int attempt = 0;; ++attempt) {
        const auto res = http_->Post("/v1/messages", headers, body, "application/json");
        const bool last = attempt >= max_retries_;
        if (!res) {
            if (last) throw std::runtime_error("Claude API connection failed: " + httplib::to_string(res.error()));
        } else if (res->status == 200) {
            return json::parse(res->body);
        } else if (!retryable(res->status) || last) {
            const auto err = json::parse(res->body, nullptr, false);
            std::string msg = res->body;
            if (!err.is_discarded() && err.contains("error")) {
                msg = err["error"].value("type", std::string()) + ": " + err["error"].value("message", std::string());
            }
            throw std::runtime_error("Claude API HTTP " + std::to_string(res->status) + " " + msg);
        }
        double wait_s = 1 << attempt;
        if (res && res->has_header("retry-after")) wait_s = std::atof(res->get_header_value("retry-after").c_str());
        std::this_thread::sleep_for(std::chrono::duration<double>(std::min(wait_s, 60.0)));
    }
}

}  // namespace ll::agent

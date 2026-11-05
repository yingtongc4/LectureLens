#pragma once

#include <memory>
#include <string>

#include "agent/agent.hpp"

namespace httplib {
class Client;
}

namespace ll::agent {

// Claude Messages API over HTTPS (no official C++ SDK, so raw HTTP).
// Reads ANTHROPIC_API_KEY, and ANTHROPIC_BASE_URL if set. Retries 408, 409,
// 429, 5xx and connection errors with exponential backoff (or retry-after).
class ClaudeClient final : public LlmClient {
public:
    ClaudeClient();
    ~ClaudeClient() override;
    json create_message(const json& request) override;

private:
    std::unique_ptr<httplib::Client> http_;
    std::string api_key_;
    int max_retries_ = 3;
};

}  // namespace ll::agent

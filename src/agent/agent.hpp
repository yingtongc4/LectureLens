#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mcp/client.hpp"

namespace ll::agent {

using json = nlohmann::json;

// One Messages API call: request body in, response body out. Implemented by
// ClaudeClient (HTTPS) and by scripted fakes in tests.
class LlmClient {
public:
    virtual ~LlmClient() = default;
    virtual json create_message(const json& request) = 0;
};

struct AgentOptions {
    std::string model = "claude-opus-5-5";
    int max_tokens = 16000;
    std::string effort = "medium";  // output_config.effort
    int max_turns = 8;              // model calls per question
    bool server_fallbacks = true;   // fallbacks: "default" on a refusal
    std::string system;             // empty = default_system_prompt()
};

struct ToolCall {
    std::string name;
    json input;
    bool is_error = false;
    double ms = 0;
};

struct AgentResult {
    std::string answer;
    std::string stop_reason;  // of the last model call
    int turns = 0;
    std::vector<ToolCall> calls;
    long input_tokens = 0;
    long output_tokens = 0;
    json messages;  // full transcript, for --trace
};

std::string default_system_prompt();

// Answers a question with a Claude tool-use loop over the tools of one MCP
// server: the model asks for tools, the agent calls them through MCP and
// sends the results back, until the model ends its turn.
class Agent {
public:
    Agent(LlmClient& llm, mcp::McpClient& mcp, AgentOptions options = {});

    AgentResult ask(const std::string& question, const std::function<void(const ToolCall&)>& on_call = {});

private:
    json build_request(const json& messages) const;

    LlmClient& llm_;
    mcp::McpClient& mcp_;
    AgentOptions options_;
    json tools_;  // MCP tools in Messages API form
};

}  // namespace ll::agent

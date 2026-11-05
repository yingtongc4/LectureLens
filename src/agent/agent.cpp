#include "agent/agent.hpp"

#include <chrono>
#include <stdexcept>

namespace ll::agent {

std::string default_system_prompt() {
    return "You answer questions about a university course on advanced computer security, using tools that "
           "search and read the course's lecture slides.\n"
           "- Search before answering. When a search snippet is not enough, read the slide with get_page.\n"
           "- Base every statement on slides you retrieved, and cite each as (lecture title, p.N).\n"
           "- If the slides do not cover the question, say so plainly rather than answering from general "
           "knowledge.\n"
           "- Answer in the language the question is written in.";
}

Agent::Agent(LlmClient& llm, mcp::McpClient& mcp, AgentOptions options)
    : llm_(llm), mcp_(mcp), options_(std::move(options)), tools_(json::array()) {
    if (options_.system.empty()) options_.system = default_system_prompt();
    for (const auto& t : mcp_.list_tools()) {
        tools_.push_back({{"name", t.name}, {"description", t.description}, {"input_schema", t.input_schema}});
    }
}

json Agent::build_request(const json& messages) const {
    json req = {{"model", options_.model},
                {"max_tokens", options_.max_tokens},
                {"system", options_.system},
                {"tools", tools_},
                {"messages", messages},
                {"output_config", {{"effort", options_.effort}}},
                // Tools + system are identical on every turn: let the API cache them.
                {"cache_control", {{"type", "ephemeral"}}}};
    if (options_.server_fallbacks) req["fallbacks"] = "default";
    return req;
}

AgentResult Agent::ask(const std::string& question, const std::function<void(const ToolCall&)>& on_call) {
    AgentResult result;
    json messages = json::array({{{"role", "user"}, {"content", question}}});

    while (result.turns < options_.max_turns) {
        const json resp = llm_.create_message(build_request(messages));
        ++result.turns;
        if (resp.contains("usage")) {
            result.input_tokens += resp["usage"].value("input_tokens", 0L) +
                                   resp["usage"].value("cache_read_input_tokens", 0L) +
                                   resp["usage"].value("cache_creation_input_tokens", 0L);
            result.output_tokens += resp["usage"].value("output_tokens", 0L);
        }
        result.stop_reason = resp.value("stop_reason", std::string());
        const json content = resp.value("content", json::array());
        // Append the whole content unchanged: thinking blocks must be sent back as-is.
        messages.push_back({{"role", "assistant"}, {"content", content}});

        if (result.stop_reason == "tool_use") {
            json results = json::array();
            for (const auto& block : content) {
                if (block.value("type", "") != "tool_use") continue;
                ToolCall call{block.at("name"), block.value("input", json::object())};
                const auto t0 = std::chrono::steady_clock::now();
                std::string text;
                try {
                    const auto r = mcp_.call_tool(call.name, call.input);
                    text = r.text;
                    call.is_error = r.is_error;
                } catch (const std::exception& e) {
                    // Protocol-level failure: still answer the tool_use, as an error.
                    text = std::string("tool call failed: ") + e.what();
                    call.is_error = true;
                }
                call.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                json tr = {{"type", "tool_result"}, {"tool_use_id", block.at("id")}, {"content", text}};
                if (call.is_error) tr["is_error"] = true;
                results.push_back(std::move(tr));
                if (on_call) on_call(call);
                result.calls.push_back(std::move(call));
            }
            // All results of one turn go back in a single user message.
            messages.push_back({{"role", "user"}, {"content", results}});
            continue;
        }
        if (result.stop_reason == "pause_turn") continue;  // server paused a long turn: resend to resume

        for (const auto& block : content) {
            if (block.value("type", "") == "text") result.answer += block.value("text", "");
        }
        break;  // end_turn, stop_sequence, max_tokens, refusal
    }
    if (result.stop_reason == "tool_use" || result.stop_reason == "pause_turn") {
        result.stop_reason = "max_turns";
    }
    result.messages = std::move(messages);
    return result;
}

}  // namespace ll::agent

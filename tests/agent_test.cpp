#include <gtest/gtest.h>

#include <deque>

#include "agent/agent.hpp"
#include "mcp/server.hpp"

namespace {

using ll::agent::json;

// Returns canned responses in order and records every request.
class ScriptedLlm final : public ll::agent::LlmClient {
public:
    explicit ScriptedLlm(std::deque<json> replies) : replies_(std::move(replies)) {}
    json create_message(const json& request) override {
        requests.push_back(request);
        if (replies_.empty()) throw std::runtime_error("script exhausted");
        json r = replies_.front();
        replies_.pop_front();
        return r;
    }
    std::vector<json> requests;

private:
    std::deque<json> replies_;
};

json tool_use(const std::string& id, const std::string& name, json input) {
    return {{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", std::move(input)}};
}

json reply(const std::string& stop, json content) {
    return {{"stop_reason", stop}, {"content", std::move(content)}, {"usage", {{"input_tokens", 100}, {"output_tokens", 10}}}};
}

struct Fixture {
    ll::mcp::Server server{{"kb", "1"}};
    std::unique_ptr<ll::mcp::InProcessTransport> transport;
    std::unique_ptr<ll::mcp::McpClient> client;

    Fixture() {
        server.add_tool({"search_lectures", "", "Search slides",
                         {{"type", "object"},
                          {"properties", {{"query", {{"type", "string"}}}}},
                          {"required", {"query"}},
                          {"additionalProperties", false}},
                         [](const json& a) {
                             return ll::mcp::ToolResult{nullptr, "1. Web security (II), p.148: " +
                                                                     a["query"].get<std::string>()};
                         }});
        transport = std::make_unique<ll::mcp::InProcessTransport>(server);
        client = std::make_unique<ll::mcp::McpClient>(*transport, ll::mcp::Implementation{"test", "1"});
        client->connect();
    }
};

}  // namespace

TEST(Agent, RunsToolLoopAndReturnsAnswer) {
    Fixture f;
    const json thinking = {{"type", "thinking"}, {"thinking", ""}, {"signature", "sig"}};
    ScriptedLlm llm({reply("tool_use", {thinking, tool_use("toolu_1", "search_lectures", {{"query", "SQL injection"}})}),
                     reply("end_turn", {{{"type", "text"}, {"text", "Use prepared statements (Web security (II), p.148)."}}})});
    ll::agent::Agent agent(llm, *f.client);
    const auto r = agent.ask("How do I stop SQL injection?");

    EXPECT_EQ(r.answer, "Use prepared statements (Web security (II), p.148).");
    EXPECT_EQ(r.stop_reason, "end_turn");
    EXPECT_EQ(r.turns, 2);
    ASSERT_EQ(r.calls.size(), 1u);
    EXPECT_EQ(r.calls[0].name, "search_lectures");
    EXPECT_EQ(r.input_tokens, 200);

    // First request: MCP tools mapped to Messages API tools.
    const auto& first = llm.requests[0];
    EXPECT_EQ(first["model"], "claude-opus-5-5");
    EXPECT_EQ(first["tools"][0]["name"], "search_lectures");
    EXPECT_EQ(first["tools"][0]["input_schema"]["required"][0], "query");
    EXPECT_EQ(first["output_config"]["effort"], "medium");
    EXPECT_EQ(first["fallbacks"], "default");
    EXPECT_FALSE(first.contains("temperature"));

    // Second request: assistant content echoed unchanged (thinking included),
    // then one user message with the tool result for that tool_use id.
    const auto& msgs = llm.requests[1]["messages"];
    ASSERT_EQ(msgs.size(), 3u);
    EXPECT_EQ(msgs[1]["role"], "assistant");
    EXPECT_EQ(msgs[1]["content"][0], thinking);
    EXPECT_EQ(msgs[2]["role"], "user");
    EXPECT_EQ(msgs[2]["content"][0]["type"], "tool_result");
    EXPECT_EQ(msgs[2]["content"][0]["tool_use_id"], "toolu_1");
    EXPECT_EQ(msgs[2]["content"][0]["content"], "1. Web security (II), p.148: SQL injection");
    EXPECT_FALSE(msgs[2]["content"][0].contains("is_error"));
}

TEST(Agent, ParallelToolCallsShareOneUserMessage) {
    Fixture f;
    ScriptedLlm llm({reply("tool_use", {tool_use("a", "search_lectures", {{"query", "x"}}),
                                        tool_use("b", "search_lectures", {{"query", "y"}})}),
                     reply("end_turn", {{{"type", "text"}, {"text", "done"}}})});
    ll::agent::Agent agent(llm, *f.client);
    const auto r = agent.ask("q");
    EXPECT_EQ(r.calls.size(), 2u);
    const auto& results = llm.requests[1]["messages"][2]["content"];
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0]["tool_use_id"], "a");
    EXPECT_EQ(results[1]["tool_use_id"], "b");
}

TEST(Agent, BadToolInputIsReportedToTheModel) {
    Fixture f;
    ScriptedLlm llm({reply("tool_use", {tool_use("t", "search_lectures", {{"q", "typo"}})}),
                     reply("end_turn", {{{"type", "text"}, {"text", "ok"}}})});
    ll::agent::Agent agent(llm, *f.client);
    const auto r = agent.ask("q");
    EXPECT_TRUE(r.calls[0].is_error);
    const auto& tr = llm.requests[1]["messages"][2]["content"][0];
    EXPECT_TRUE(tr["is_error"].get<bool>());
    EXPECT_NE(tr["content"].get<std::string>().find("missing required argument 'query'"), std::string::npos);
}

TEST(Agent, StopsAtMaxTurns) {
    Fixture f;
    std::deque<json> loop;
    for (int i = 0; i < 5; ++i) loop.push_back(reply("tool_use", {tool_use("t" + std::to_string(i), "search_lectures", {{"query", "x"}})}));
    ScriptedLlm llm(loop);
    ll::agent::AgentOptions opts;
    opts.max_turns = 3;
    ll::agent::Agent agent(llm, *f.client, opts);
    const auto r = agent.ask("q");
    EXPECT_EQ(r.turns, 3);
    EXPECT_EQ(r.stop_reason, "max_turns");
    EXPECT_TRUE(r.answer.empty());
}

TEST(Agent, RefusalEndsWithoutAnswer) {
    Fixture f;
    ScriptedLlm llm({reply("refusal", json::array())});
    ll::agent::Agent agent(llm, *f.client);
    const auto r = agent.ask("q");
    EXPECT_EQ(r.stop_reason, "refusal");
    EXPECT_TRUE(r.answer.empty());
}

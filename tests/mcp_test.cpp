#include <gtest/gtest.h>

#include "mcp/client.hpp"
#include "mcp/schema.hpp"
#include "mcp/server.hpp"

namespace {

using ll::mcp::json;
namespace ec = ll::mcp::error_code;

const json kSchema = {{"type", "object"},
                      {"properties",
                       {{"query", {{"type", "string"}, {"minLength", 1}}},
                        {"top_k", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}}},
                        {"mode", {{"type", "string"}, {"enum", {"hybrid", "vector"}}}}}},
                      {"required", {"query"}},
                      {"additionalProperties", false}};

ll::mcp::Server make_server(ll::mcp::ServerOptions opts = {}) {
    ll::mcp::Server s({"test-server", "1.0"}, "test instructions", opts);
    ll::mcp::Tool echo{"echo", "Echo", "Echoes the query", kSchema, [](const json& a) {
                           if (a["query"] == "fail") throw ll::mcp::ToolError("asked to fail");
                           return ll::mcp::ToolResult{{{"echo", a["query"]}}, "echo: " + a["query"].get<std::string>()};
                       }};
    s.add_tool(echo);
    return s;
}

json modern_meta(const std::string& version = "2026-07-28") {
    return {{ll::mcp::kMetaProtocolVersion, version}, {ll::mcp::kMetaClientCapabilities, json::object()}};
}

json rpc(int id, const std::string& method, json params = json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(params)}};
}

}  // namespace

TEST(McpSchema, AcceptsValidArguments) {
    EXPECT_FALSE(ll::mcp::validate_arguments(kSchema, {{"query", "x"}, {"top_k", 3}, {"mode", "vector"}}));
}

TEST(McpSchema, ReportsTheFirstProblem) {
    EXPECT_EQ(*ll::mcp::validate_arguments(kSchema, json::object()), "missing required argument 'query'");
    EXPECT_NE(ll::mcp::validate_arguments(kSchema, {{"query", 5}})->find("type"), std::string::npos);
    EXPECT_NE(ll::mcp::validate_arguments(kSchema, {{"query", "x"}, {"top_k", 50}})->find("<= 20"), std::string::npos);
    EXPECT_NE(ll::mcp::validate_arguments(kSchema, {{"query", "x"}, {"mode", "fuzzy"}})->find("one of"),
              std::string::npos);
    EXPECT_NE(ll::mcp::validate_arguments(kSchema, {{"query", "x"}, {"extra", 1}})->find("unknown"), std::string::npos);
    EXPECT_TRUE(ll::mcp::validate_arguments(kSchema, {{"query", ""}}).has_value());
}

TEST(McpServer, DiscoverAdvertisesBothEras) {
    auto s = make_server();
    const auto r = (*s.handle(rpc(1, "server/discover", {{"_meta", modern_meta()}})))["result"];
    EXPECT_EQ(r["supportedVersions"], json({"2026-07-28", "2025-11-25", "2025-06-18", "2025-03-26"}));
    EXPECT_EQ(r["resultType"], "complete");
    EXPECT_EQ(r["_meta"][ll::mcp::kMetaServerInfo]["name"], "test-server");
    EXPECT_TRUE(r["capabilities"].contains("tools"));
}

TEST(McpServer, ModernToolsListIsCacheable) {
    auto s = make_server();
    const auto r = (*s.handle(rpc(1, "tools/list", {{"_meta", modern_meta()}})))["result"];
    ASSERT_EQ(r["tools"].size(), 1u);
    EXPECT_EQ(r["tools"][0]["name"], "echo");
    EXPECT_EQ(r["tools"][0]["inputSchema"], kSchema);
    EXPECT_TRUE(r["tools"][0]["annotations"]["readOnlyHint"].get<bool>());
    EXPECT_EQ(r["cacheScope"], "public");
    EXPECT_TRUE(r.contains("ttlMs"));
}

TEST(McpServer, UnsupportedVersionListsSupported) {
    auto s = make_server();
    const auto e = (*s.handle(rpc(7, "tools/list", {{"_meta", modern_meta("1900-01-01")}})))["error"];
    EXPECT_EQ(e["code"], ec::kUnsupportedProtocolVersion);
    EXPECT_EQ(e["data"]["requested"], "1900-01-01");
    EXPECT_EQ(e["data"]["supported"][0], "2026-07-28");
}

TEST(McpServer, ToolCallReturnsTextAndStructuredContent) {
    auto s = make_server();
    const auto r = (*s.handle(rpc(2, "tools/call",
                                  {{"name", "echo"}, {"arguments", {{"query", "hi"}}}, {"_meta", modern_meta()}})))["result"];
    EXPECT_FALSE(r["isError"].get<bool>());
    EXPECT_EQ(r["content"][0]["type"], "text");
    EXPECT_EQ(r["content"][0]["text"], "echo: hi");
    EXPECT_EQ(r["structuredContent"]["echo"], "hi");
}

TEST(McpServer, ToolFailuresAreResultsNotProtocolErrors) {
    auto s = make_server();
    auto r = (*s.handle(rpc(3, "tools/call", {{"name", "echo"}, {"arguments", {{"top_k", 2}}}})))["result"];
    EXPECT_TRUE(r["isError"].get<bool>());
    EXPECT_NE(r["content"][0]["text"].get<std::string>().find("missing required argument"), std::string::npos);

    r = (*s.handle(rpc(4, "tools/call", {{"name", "echo"}, {"arguments", {{"query", "fail"}}}})))["result"];
    EXPECT_TRUE(r["isError"].get<bool>());
    EXPECT_EQ(r["content"][0]["text"], "asked to fail");
}

TEST(McpServer, ProtocolErrors) {
    auto s = make_server();
    EXPECT_EQ((*s.handle(rpc(5, "tools/call", {{"name", "nope"}})))["error"]["code"], ec::kInvalidParams);
    EXPECT_EQ((*s.handle(rpc(6, "resources/list")))["error"]["code"], ec::kMethodNotFound);
    EXPECT_EQ(json::parse(*s.handle_line("{not json"))["error"]["code"], ec::kParseError);
    EXPECT_EQ((*s.handle(json::array()))["error"]["code"], ec::kInvalidRequest);
}

TEST(McpServer, NotificationsGetNoReply) {
    auto s = make_server();
    EXPECT_FALSE(s.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    EXPECT_FALSE(s.handle({{"jsonrpc", "2.0"}, {"method", "tools/list"}}));  // no id
}

TEST(McpServer, LegacyInitializeNegotiatesVersion) {
    auto s = make_server();
    auto r = (*s.handle(rpc(1, "initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", json::object()},
                                              {"clientInfo", {{"name", "c"}, {"version", "1"}}}})))["result"];
    EXPECT_EQ(r["protocolVersion"], "2025-06-18");
    EXPECT_EQ(r["serverInfo"]["name"], "test-server");
    EXPECT_EQ(r["instructions"], "test instructions");

    r = (*s.handle(rpc(2, "initialize", {{"protocolVersion", "2024-01-01"}})))["result"];
    EXPECT_EQ(r["protocolVersion"], "2025-11-25");  // ours, newest legacy

    const auto list = (*s.handle(rpc(3, "tools/list")))["result"];
    EXPECT_EQ(list["tools"].size(), 1u);
    EXPECT_FALSE(list.contains("resultType"));  // legacy results carry no modern fields
}

TEST(McpServer, ModernOnlyRejectsInitializeNamingVersions) {
    auto s = make_server({.modern = true, .legacy = false});
    const auto e = (*s.handle(rpc(1, "initialize", {{"protocolVersion", "2025-11-25"}})))["error"];
    EXPECT_EQ(e["code"], ec::kMethodNotFound);
    EXPECT_NE(e["message"].get<std::string>().find("2026-07-28"), std::string::npos);
}

TEST(McpClient, ConnectsModernToDualEraServer) {
    auto s = make_server();
    ll::mcp::InProcessTransport t(s);
    ll::mcp::McpClient c(t, {"test-client", "1.0"});
    c.connect();
    EXPECT_TRUE(c.modern());
    EXPECT_EQ(c.protocol_version(), "2026-07-28");
    EXPECT_EQ(c.server_info()["name"], "test-server");
    EXPECT_EQ(c.instructions(), "test instructions");
    EXPECT_FALSE(s.legacy_version().has_value());

    const auto tools = c.list_tools();
    ASSERT_EQ(tools.size(), 1u);
    const auto r = c.call_tool("echo", {{"query", "hello"}});
    EXPECT_EQ(r.text, "echo: hello");
    EXPECT_EQ(r.structured["echo"], "hello");
    EXPECT_FALSE(r.is_error);
}

TEST(McpClient, FallsBackToInitializeOnLegacyServer) {
    auto s = make_server({.modern = false, .legacy = true});
    ll::mcp::InProcessTransport t(s);
    ll::mcp::McpClient c(t, {"test-client", "1.0"});
    c.connect();
    EXPECT_FALSE(c.modern());
    EXPECT_EQ(c.protocol_version(), "2025-11-25");
    EXPECT_EQ(s.legacy_version(), "2025-11-25");
    EXPECT_EQ(c.call_tool("echo", {{"query", "x"}}).text, "echo: x");
}

TEST(McpClient, ToolErrorsComeBackAsResults) {
    auto s = make_server();
    ll::mcp::InProcessTransport t(s);
    ll::mcp::McpClient c(t, {"test-client", "1.0"});
    c.connect();
    EXPECT_TRUE(c.call_tool("echo", {{"query", "fail"}}).is_error);
    EXPECT_THROW(c.call_tool("missing", json::object()), ll::mcp::RpcError);
}

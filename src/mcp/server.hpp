#pragma once

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mcp/protocol.hpp"

namespace ll::mcp {

struct ToolResult {
    json structured;       // returned as structuredContent
    std::string text;      // returned as the text content; empty = structured.dump()
    bool is_error = false; // a tool-level failure the model should see and handle
};

// A tool failure to report to the model (isError result), e.g. "page not found".
class ToolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Tool {
    std::string name;
    std::string title;
    std::string description;
    json input_schema;
    std::function<ToolResult(const json& arguments)> run;
    bool read_only = true;
};

struct ServerOptions {
    // Turn off to behave like a server that only knows initialize-based
    // revisions (used to test client fallback).
    bool modern = true;
    bool legacy = true;
};

// Dual-era MCP server: serves modern clients (2026-07-28, per-request _meta,
// server/discover) and legacy clients (initialize handshake, 2025-03-26 to
// 2025-11-25). Transport-independent: feed it parsed messages or raw lines.
class Server {
public:
    Server(Implementation info, std::string instructions = {}, ServerOptions options = {});

    void add_tool(Tool tool);

    // Handles one JSON-RPC message; nullopt for notifications (no reply).
    std::optional<json> handle(const json& message);

    // Same for one line of input, including the parse-error response.
    std::optional<std::string> handle_line(std::string_view line);

    // Version negotiated by `initialize`, if a legacy client connected.
    const std::optional<std::string>& legacy_version() const { return legacy_version_; }

private:
    json dispatch(const std::string& method, const json& params, bool modern);
    json discover() const;
    json initialize(const json& params);
    json list_tools(bool modern) const;
    json call_tool(const json& params);
    void add_modern_fields(json& result) const;

    Implementation info_;
    std::string instructions_;
    ServerOptions options_;
    std::vector<Tool> tools_;  // registration order = deterministic tools/list
    std::optional<std::string> legacy_version_;
};

// Newline-delimited JSON-RPC over a stream pair (the MCP stdio transport).
// Returns when `in` reaches EOF. Logs go to stderr, never to `out`.
void serve(Server& server, std::istream& in, std::ostream& out);

}  // namespace ll::mcp
